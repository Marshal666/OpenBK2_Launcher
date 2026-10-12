"""Exact-byte Ed25519 envelopes. No private key is ever embedded in artifacts."""
import base64
import json
import os
from pathlib import Path
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey

MAX_ENVELOPE = 16 * 1024 * 1024


def stable_json(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False).encode("utf-8")


def strict_json(data):
    if not 0 < len(data) <= MAX_ENVELOPE:
        raise ValueError("JSON size limit exceeded")

    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError(f"Duplicate field: {key}")
            result[key] = value
        return result

    def bad_constant(value):
        raise ValueError(f"Invalid JSON constant: {value}")

    value = json.loads(data.decode("utf-8", errors="strict"), object_pairs_hook=pairs, parse_constant=bad_constant)
    remaining = 1_000_000

    def inspect(item, depth=0):
        nonlocal remaining
        remaining -= 1
        if depth > 48 or remaining < 0:
            raise ValueError("JSON complexity limit exceeded")
        if isinstance(item, dict):
            for key, child in item.items():
                inspect(key, depth + 1)
                inspect(child, depth + 1)
        elif isinstance(item, list):
            if len(item) > 100_000:
                raise ValueError("Array limit exceeded")
            for child in item:
                inspect(child, depth + 1)
        elif isinstance(item, str):
            item.encode("utf-8", errors="strict")
            if "\0" in item:
                raise ValueError("NUL in JSON string")
    inspect(value)
    return value


def sign_payload(payload, private_key, key_id):
    strict_json(payload)
    return stable_json({"envelope_version": 1, "key_id": key_id,
                        "payload_b64": base64.b64encode(payload).decode("ascii"),
                        "signature_b64": base64.b64encode(private_key.sign(payload)).decode("ascii")})


def verify_envelope(data, trusted_keys):
    envelope = strict_json(data)
    if not isinstance(envelope, dict) or set(envelope) != {"envelope_version", "key_id", "payload_b64", "signature_b64"}:
        raise ValueError("Invalid envelope fields")
    if type(envelope["envelope_version"]) is not int or envelope["envelope_version"] != 1:
        raise ValueError("Unsupported envelope version")
    key = trusted_keys.get(envelope["key_id"])
    if key is None:
        raise ValueError("Unknown signing key")

    def decode(text):
        if not isinstance(text, str):
            raise ValueError("Invalid base64 type")
        raw = base64.b64decode(text, validate=True)
        if not raw or base64.b64encode(raw).decode("ascii") != text:
            raise ValueError("Noncanonical base64")
        return raw

    payload, signature = decode(envelope["payload_b64"]), decode(envelope["signature_b64"])
    if len(signature) != 64:
        raise ValueError("Incorrect Ed25519 signature length")
    Ed25519PublicKey.from_public_bytes(key).verify(signature, payload)
    return payload  # Caller parses only after the signature succeeds.


def load_private(path):
    key = serialization.load_pem_private_key(Path(path).read_bytes(), password=None)
    if not isinstance(key, Ed25519PrivateKey):
        raise ValueError("Expected an Ed25519 private key")
    return key


def keygen(path):
    path = Path(path).resolve()
    repo = Path(__file__).resolve().parents[1]
    if path.is_relative_to(repo):
        raise ValueError("Private signing keys must be created outside this repository")
    key = Ed25519PrivateKey.generate()
    data = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption())
    # O_EXCL prevents overwriting existing keys; mode 0600 on POSIX.
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as output:
        output.write(data)
        output.flush()
        os.fsync(output.fileno())
    return key.public_key().public_bytes_raw().hex()
