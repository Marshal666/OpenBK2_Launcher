"""Cross-language acceptance tests with real ZIPs and Ed25519 signatures."""
import base64
import copy
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
from test_workspace import temporary_workspace
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from fixture_builder import build_fixtures
from release import build_release, verify_release
from signing import sign_payload, stable_json, strict_json, verify_envelope


def main():
    executable = str(Path(sys.argv[1]).resolve(strict=True))
    checks = 0
    with temporary_workspace() as temp:
        root = Path(temp)
        key = build_fixtures(root / "fixtures")
        public = key.public_key().public_bytes_raw()
        release = root / "fixtures/release-3"
        manifest = verify_release(release, {"fixture-only": public})

        def cpp(data, accepted, extra=()):
            nonlocal checks
            path = root / "candidate.envelope.json"
            path.write_bytes(data)
            result = subprocess.run([executable, str(path), "fixture-only", public.hex(), *extra], capture_output=True, text=True, timeout=15)
            assert (result.returncode == 0) == accepted, (result.returncode, result.stdout, result.stderr)
            checks += 1
            return result.stdout.strip()

        for seq in (1, 2, 3):
            envelope = (root / f"fixtures/release-{seq}/manifest.envelope.json").read_bytes()
            cpp(envelope, True)
            assert verify_envelope(envelope, {"fixture-only": public}) == (root / f"fixtures/release-{seq}/manifest.payload.json").read_bytes()
        good = (release / "manifest.envelope.json").read_bytes()
        payload_hash = hashlib.sha256((release / "manifest.payload.json").read_bytes()).hexdigest()
        cpp(good, True, ("3", payload_hash))
        cpp(good, False, ("4", payload_hash))
        cpp(good, False, ("3", "0"*64))
        envelope = strict_json(good)
        for field, value in [("key_id", "unknown"), ("envelope_version", 2), ("signature_b64", base64.b64encode(bytes(64)).decode()),
                             ("payload_b64", base64.b64encode(b"{}").decode()), ("signature_b64", "AA=="), ("payload_b64", "Zh==")]:
            altered = dict(envelope)
            altered[field] = value
            cpp(stable_json(altered), False)
        cpp(b'{"envelope_version":1,' + good[1:], False)
        cpp(b"{"*100, False)
        duplicate_payload = b'{"schema_version":1,"schema_version":1}'
        cpp(stable_json({"envelope_version": 1, "key_id": "fixture-only",
                         "payload_b64": base64.b64encode(duplicate_payload).decode(),
                         "signature_b64": base64.b64encode(key.sign(duplicate_payload)).decode()}), False)

        def reject_mutation(change):
            altered = copy.deepcopy(manifest)
            change(altered)
            cpp(sign_payload(stable_json(altered), key, "fixture-only"), False)

        reject_mutation(lambda m: m.update(schema_version=2))
        reject_mutation(lambda m: m.update(critical_new_field=True))
        reject_mutation(lambda m: m["release"].update(sequence=-1))
        reject_mutation(lambda m: m["release"].update(sequence=True))
        reject_mutation(lambda m: m["release"].update(sequence=2**64))
        reject_mutation(lambda m: m["release"].update(version="bogus"))
        reject_mutation(lambda m: m["platforms"]["linux-x64"]["launch"]["environment"].update(LD_PRELOAD="evil.so"))
        reject_mutation(lambda m: m["packages"][0].update(url="http://example.com/a.zip"))
        reject_mutation(lambda m: m["packages"][0].update(size=2**63))
        reject_mutation(lambda m: m["packages"][0].update(unpacked_size=0))
        reject_mutation(lambda m: m["platforms"]["windows-x64"].update(managed_user_dir_files=[]))
        reject_mutation(lambda m: m["platforms"]["windows-x64"]["snapshot_package_ids"].append("missing"))
        reject_mutation(lambda m: m["languages"]["de"].update(path="Data/texts-en.pak"))
        reject_mutation(lambda m: m["packages"].append(m["packages"][0]))

        for path in ("../escape", "/absolute", "C:/drive", "a//b", "a/../b", "a\\b", "bin/NUL.exe", "Data/trailing.",
                     ".launcher/state.json", "OpenBK2Launcher.exe", "launcher_installed.txt", "Profiles/private.dat"):
            def replace(m, path=path):
                platform = m["platforms"]["windows-x64"]
                prior = "Data/revised.pak"
                for f in platform["files"]:
                    if f["path"] == prior:
                        f["path"] = path
                for p in m["packages"]:
                    if p["platform"] == "windows-x64":
                        for f in p["files"]:
                            if f["path"] == prior:
                                f["path"] = path
            reject_mutation(replace)
        reject_mutation(lambda m: m["packages"][0]["files"].append(m["packages"][0]["files"][0]))
        reject_mutation(lambda m: m["platforms"]["windows-x64"]["files"].append(dict(m["platforms"]["windows-x64"]["files"][0], path="data/BASE.pak")))

        # Byte reproducibility: identical source metadata and inputs produce all
        # the same archive and payload bytes, including inherited patch edges.
        rebuilt = root / "rebuilt"
        build_release(out=rebuilt, version="0.3.0", sequence=3,
                      roots={"windows-x64": root/"fixtures/input-windows", "linux-x64": root/"fixtures/input-linux"},
                      languages_dir=root/"fixtures/input-languages", base_url="https://fixtures.invalid/releases/3",
                      previous=root/"fixtures/release-2", allowlist=["MODs/official.pak"], created_utc="2026-10-12T08:00:00Z")
        for item in rebuilt.iterdir():
            assert item.read_bytes() == (release / item.name).read_bytes(), item.name
            checks += 1
        package = next(rebuilt.glob("*.zip"))
        package.write_bytes(package.read_bytes()[:-5])
        try:
            verify_release(rebuilt, allow_unsigned=True)
            raise AssertionError("Truncated archive was accepted")
        except ValueError:
            checks += 1
        assert len(manifest["platforms"]["windows-x64"]["update_edges"]) == 2
        assert all(f["path"] not in {"MODs/user.pak", "Profiles/player.dat"} for p in manifest["packages"] for f in p["files"])
    print(f"{checks} cross-language publisher/manifest checks passed")


if __name__ == "__main__":
    main()
