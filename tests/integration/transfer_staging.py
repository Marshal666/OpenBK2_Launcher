"""Real loopback HTTP/HTTPS transfer and adversarial ZIP tests."""
import copy
from datetime import datetime, timedelta, timezone
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import ipaddress
import os
from pathlib import Path
import socket
import ssl
import stat
import struct
import subprocess
import sys
import threading
import time
import zipfile
from test_workspace import temporary_workspace
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID
from fixture_builder import build_fixtures
from signing import sign_payload, stable_json, strict_json


def certificate(root):
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "OpenBK2 loopback test")])
    now = datetime.now(timezone.utc)
    cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key())
            .serial_number(x509.random_serial_number()).not_valid_before(now-timedelta(days=1)).not_valid_after(now+timedelta(days=1))
            .add_extension(x509.SubjectAlternativeName([x509.IPAddress(ipaddress.ip_address("127.0.0.1"))]), critical=False)
            .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True).sign(key, hashes.SHA256()))
    cert_path, key_path = root / "test-ca.pem", root / "test-tls-key.pem"
    cert_path.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    key_path.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    return cert_path, key_path


def main():
    executable = str(Path(sys.argv[1]).resolve(strict=True))
    checks = 0
    with temporary_workspace() as root:
        key = build_fixtures(root / "fixtures")
        release = root / "fixtures/release-1"
        manifest = strict_json((release / "manifest.payload.json").read_bytes())
        package = next(p for p in manifest["packages"] if p["kind"] == "snapshot" and p["platform"] == "windows-x64")
        original = (release / (package["id"]+".zip")).read_bytes()
        requests = []
        counts = {}

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_):
                pass

            def do_GET(self):
                mode = self.path[1:]
                range_header = self.headers.get("Range")
                requests.append((mode, range_header))
                counts[mode] = counts.get(mode, 0) + 1
                if mode in {"404", "429", "503"}:
                    self.send_response(int(mode)); self.send_header("Content-Length", "0"); self.end_headers(); return
                if mode == "redirect-http":
                    self.send_response(302); self.send_header("Location", http_url+"/ok"); self.end_headers(); return
                if mode == "redirect-file":
                    self.send_response(302); self.send_header("Location", "file:///etc/passwd"); self.end_headers(); return
                if mode == "timeout":
                    # Outlast the native client's three-second overall deadline.
                    # Return without writing after the client has disconnected.
                    time.sleep(4)
                    return
                start = int(range_header.removeprefix("bytes=").removesuffix("-")) if range_header else 0
                if mode == "ignore":
                    start = 0
                self.send_response(206 if start else 200)
                if start:
                    self.send_header("Content-Range", f"bytes {start+1 if mode == 'bad-range' else start}-{len(original)-1}/{len(original)}")
                self.send_header("Content-Length", str(len(original)-start))
                self.end_headers()
                data = original[start:]
                if mode == "corrupt":
                    data = bytes([data[0] ^ 1]) + data[1:]
                if mode == "interrupt" and counts[mode] == 1:
                    data = data[:len(data)//2]
                try:
                    for offset in range(0, len(data), 1024):
                        self.wfile.write(data[offset:offset+1024]); self.wfile.flush()
                        time.sleep(0.001)
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, ssl.SSLError):
                    pass

        http = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        http.daemon_threads = True
        cert, private = certificate(root)
        https = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        https.daemon_threads = True
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert, private)
        https.socket = context.wrap_socket(https.socket, server_side=True)
        http_url, https_url = f"http://127.0.0.1:{http.server_port}", f"https://127.0.0.1:{https.server_port}"
        threads = [threading.Thread(target=server.serve_forever, daemon=True) for server in (http, https)]
        for thread in threads:
            thread.start()
        environment = dict(os.environ, NO_PROXY="127.0.0.1,localhost")

        def run(name, url, success=True, mode="both", cancel=0, ca="-", payload=None, cache=None, error=None):
            nonlocal checks
            env_file = root / "candidate.json"
            env_file.write_bytes(sign_payload(stable_json(payload or manifest), key, "fixture-only"))
            result = subprocess.run([executable, mode, str(env_file), key.public_key().public_bytes_raw().hex(), package["id"],
                                     str(cache or root / (name+"-cache")), str(root / (name+"-stage")), str(url), str(cancel), str(ca)],
                                    capture_output=True, text=True, timeout=20, env=environment)
            assert (result.returncode == 0) == success, (name, result.returncode, result.stdout, result.stderr)
            if error:
                assert error in result.stderr, (name, result.stderr)
            checks += 1
            return result

        try:
            result = run("fresh", http_url+"/ok")
            assert f"observed={package['size']}" in result.stdout
            for f in package["files"]:
                assert hashlib.sha256((root/"fresh-stage"/f["path"]).read_bytes()).hexdigest() == f["sha256"]
            for response in ("ok", "ignore", "bad-range"):
                cache = root / (response+"-resume")
                run(response+"-cancel", http_url+"/"+response, False, "download", 20000, cache=cache, error="cancelled")
                assert 0 < (cache / (package["sha256"]+".part")).stat().st_size < package["size"]
                run(response+"-resume", http_url+"/"+response, cache=cache)
                assert any(mode == response and r for mode, r in requests)
                if response != "ok":
                    assert requests[-1] == (response, None), requests[-3:]
            run("interrupted", http_url+"/interrupt")
            assert counts["interrupt"] == 2
            for status in ("404", "429", "503"):
                run(status, http_url+"/"+status, False, error="HTTP "+status)
                assert counts[status] == (1 if status == "404" else 3)
            run("timeout", http_url+"/timeout", False, error="Timeout")
            assert counts["timeout"] == 3
            # Reserve a loopback port without listening: deterministic refusal,
            # with no chance of connecting to another user's local service.
            with socket.socket() as unavailable:
                unavailable.bind(("127.0.0.1", 0))
                run("outage", f"http://127.0.0.1:{unavailable.getsockname()[1]}/", False)
            run("corrupt-download", http_url+"/corrupt", False, error="SHA-256")
            run("reject-file", http_url+"/redirect-file", False)
            run("untrusted-tls", https_url+"/ok", False)
            run("trusted-tls", https_url+"/ok", ca=cert)
            run("downgrade", https_url+"/redirect-http", False, ca=cert)

            # All malicious archives have a correct archive digest in a newly
            # signed manifest, proving extraction has its own defensive checks.
            with zipfile.ZipFile(io.BytesIO(original)) as z:
                files = [(i.filename, z.read(i.filename)) for i in z.infolist()]

            def bad_zip(name, transform, mutate=None):
                altered = copy.deepcopy(manifest)
                buffer = io.BytesIO()
                with zipfile.ZipFile(buffer, "w", zipfile.ZIP_DEFLATED) as z:
                    for path, data in files:
                        info = zipfile.ZipInfo(path)
                        info.create_system = 3
                        info.external_attr = (stat.S_IFREG | 0o644) << 16
                        transform(z, info, data)
                data = buffer.getvalue()
                if mutate:
                    data = mutate(data)
                bad = root / (name+".zip"); bad.write_bytes(data)
                p = next(p for p in altered["packages"] if p["id"] == package["id"])
                p.update(size=len(data), sha256=hashlib.sha256(data).hexdigest())
                run(name, bad, False, "extract", payload=altered)

            for path in ("../outside", "/absolute", "C:/drive", "Data/NUL", "Data/alias."):
                def rename(z, info, data, path=path):
                    if info.filename == "Data/base.pak":
                        info.filename = path
                    z.writestr(info, data)
                bad_zip("bad-path-"+str(checks), rename)
            for mode in (stat.S_IFLNK, stat.S_IFCHR, stat.S_IFBLK, stat.S_IFIFO, stat.S_IFSOCK):
                def special(z, info, data, mode=mode):
                    if info.filename == "Data/base.pak":
                        info.external_attr = (mode | 0o644) << 16
                    z.writestr(info, data)
                bad_zip("special-"+str(mode), special)
            def duplicate(z, info, data):
                z.writestr(info, data)
                if info.filename == "Data/base.pak":
                    z.writestr(info, data)
            bad_zip("duplicate", duplicate)
            def alias(z, info, data):
                z.writestr(info, data)
                if info.filename == "Data/base.pak":
                    z.writestr("data/BASE.pak", data)
            bad_zip("case-alias", alias)
            def hardlink(z, info, data):
                info.extra = b"\x0d\x00\x04\x00link"
                z.writestr(info, data)
            bad_zip("unix-link-extension", hardlink)
            def bad_digest(z, info, data):
                if info.filename == "Data/base.pak":
                    data = b"x" + data[1:]
                z.writestr(info, data)
            bad_zip("file-digest", bad_digest)
            def bad_size(z, info, data):
                z.writestr(info, data+b"extra")
            bad_zip("file-size", bad_size)
            def bomb_sizes(data):
                data = bytearray(data)
                central = data.index(b"PK\x01\x02")
                local = struct.unpack_from("<I", data, central+42)[0]
                # Claim a 2 GiB entry without allocating/decompressing it. Both
                # headers agree, and the complete malicious ZIP is signed.
                struct.pack_into("<I", data, central+24, 0x7fffffff)
                struct.pack_into("<I", data, local+22, 0x7fffffff)
                return bytes(data)
            bad_zip("expansion-bomb", lambda z, info, data: z.writestr(info, data), bomb_sizes)
            assert not any((root / "expansion-bomb-stage").rglob("*.pak")), "Bomb wrote staged payload"
            truncated = root / "truncated.zip"; truncated.write_bytes(original[:-4])
            run("truncated", truncated, False, "extract", error="length or SHA-256")
            # Existing staged files are never overwritten, even when same-named.
            stage = root / "conflict-stage/Data"; stage.mkdir(parents=True)
            (stage / "base.pak").write_bytes(b"preserve me")
            run("conflict", release/(package["id"]+".zip"), False, "extract")
            assert (stage / "base.pak").read_bytes() == b"preserve me"
            outside = root / "unrelated.pak"; outside.write_bytes(b"unrelated user data")
            hard_stage = root / "hardlink-stage/Data"; hard_stage.mkdir(parents=True)
            os.link(outside, hard_stage / "base.pak")
            run("hardlink", release/(package["id"]+".zip"), False, "extract")
            assert outside.read_bytes() == b"unrelated user data"
        finally:
            for server in (http, https):
                server.shutdown(); server.server_close()
            for thread in threads:
                thread.join(timeout=5)
    print(f"{checks} HTTP/HTTPS/resume/cancellation/ZIP checks passed")


if __name__ == "__main__":
    main()
