"""Signed initial data, migration, reset recovery, and startup without data scans."""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from test_workspace import temporary_workspace
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from release import build_release, sign_release, verify_release
from schema import validate_payload
from signing import sign_payload, stable_json


def main():
    installer, manifest_check, lifecycle, game = map(lambda p: str(Path(p).resolve(strict=True)), sys.argv[1:5])
    platform = "windows-x64" if os.name == "nt" else "linux-x64"
    keep = json.loads(subprocess.check_output([installer, "--backup-policy"], text=True))
    checks = 0
    skipped = []
    with temporary_workspace() as temp:
        key = Ed25519PrivateKey.generate()
        public = key.public_key().public_bytes_raw().hex()
        shared, initial, languages = [temp / name for name in ("shared", "initial", "languages")]
        roots = {p: temp / p for p in ("windows-x64", "linux-x64")}
        def write(path, data):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        for p, folder in roots.items():
            target = folder / ("bin/Game.exe" if p == "windows-x64" else "bin/Game")
            write(target, Path(game).read_bytes())
            target.chmod(0o755)
        write(shared / "Data/base.pak", b"managed game content")
        write(shared / "Editor/default.cfg", b"old managed editor")
        write(shared / "Profiles/default.cfg", b"old managed profile")
        write(languages / "texts-en.pak", b"English")
        releases = {}
        def build(seq, seed=None, allow=()):
            release = temp / f"release-{seq}"
            manifest = build_release(out=release, version=f"1.0.{seq}", sequence=seq, roots=roots,
                shared_root=shared, shared_init_data=seed, languages_dir=languages, allowlist=allow,
                base_url=f"https://fixtures.invalid/{seq}", previous=releases.get(seq-1))
            sign_release(release, key, "fixture-only")
            verify_release(release, {"fixture-only": bytes.fromhex(public)})
            releases[seq] = release
            return manifest
        def invoke(root, seq=2, action="install", mode="update", fault=0, code=0):
            nonlocal checks
            args = [installer, action, str(root), str(releases[seq]), public, platform, "en", mode, str(fault)]
            result = subprocess.run(args, capture_output=True, text=True, timeout=30)
            assert result.returncode == code, (args, result.stdout, result.stderr)
            checks += 1
            return json.loads(result.stdout) if result.stdout.strip() else None
        build(1, allow=["Profiles/default.cfg"])
        migrated = temp / "migrated"
        invoke(migrated, 1)
        write(migrated / "Editor/default.cfg", b"personal editor")
        write(migrated / "Profiles/default.cfg", b"personal profile")
        (shared / "Editor/default.cfg").unlink()
        (shared / "Profiles/default.cfg").unlink()
        write(initial / "Editor/default.cfg", b"editor default 2")
        write(initial / "Profiles/default.cfg", b"profile default 2")
        write(initial / "Profiles/new.cfg", b"new default")
        second = build(2, initial)
        subprocess.run([manifest_check, str(releases[2] / "manifest.envelope.json"), "fixture-only", public], check=True, capture_output=True)
        assert second["schema_version"] == 3 and second["min_launcher_version"] == "0.3.0"
        assert all(p["platform"] == "any" for p in second["packages"] if p["kind"] == "initial")
        invoke(migrated)
        assert (migrated / "Editor/default.cfg").read_bytes() == b"personal editor"
        assert (migrated / "Profiles/default.cfg").read_bytes() == b"personal profile"
        assert (migrated / "Profiles/new.cfg").read_bytes() == b"new default"
        invoke(migrated, action="verify")
        fresh = temp / "fresh"
        invoke(fresh)
        assert (fresh / "Editor/default.cfg").read_bytes() == b"editor default 2"
        adopted = temp / "adopted"
        write(adopted / "Profiles/default.cfg", b"existing save")
        invoke(adopted)
        assert (adopted / "Profiles/default.cfg").read_bytes() == b"existing save"

        write(initial / "Editor/default.cfg", b"editor default 3")
        write(initial / "Profiles/default.cfg", b"profile default 3")
        write(initial / "Profiles/added-later.cfg", b"new release default")
        write(shared / "Data/base.pak", b"managed game content 3")
        third = build(3, initial)
        cli = temp / "cli-release"
        subprocess.run([sys.executable, str(Path(__file__).resolve().parents[2] / "tools/release.py"), "build",
                        "--version", "2.0.0", "--sequence", "1", "--shared-root", str(shared),
                        "--shared-init-data", str(initial), "--windows-root", str(roots["windows-x64"]),
                        "--linux-root", str(roots["linux-x64"]), "--languages-dir", str(languages),
                        "--base-url", "https://fixtures.invalid/cli", "--out", str(cli)], check=True, capture_output=True)
        assert verify_release(cli, allow_unsigned=True)["initial_data"] == third["initial_data"]
        write(fresh / "Editor/default.cfg", b"edited by game")
        (fresh / "Profiles/default.cfg").unlink()
        write(fresh / "Profiles/user-save.dat", b"user save")
        write(fresh / "Data/user.pak", b"unmanaged data")
        # No initial ZIP is needed after initialization, even for Repair/Reinstall.
        held = []
        for p in third["packages"]:
            if p["kind"] == "initial":
                path = releases[3] / (p["id"] + ".zip")
                path.rename(path.with_suffix(".held"))
                held.append(path)
        for mode in ("update", "repair", "reinstall"):
            invoke(fresh, 3, mode=mode)
            assert (fresh / "Editor/default.cfg").read_bytes() == b"edited by game"
            assert not (fresh / "Profiles/default.cfg").exists()
            assert not (fresh / "Profiles/added-later.cfg").exists()
            assert (fresh / "Profiles/user-save.dat").read_bytes() == b"user save"
        for path in held:
            path.with_suffix(".held").rename(path)
        baseline = temp / "before-reset"
        shutil.copytree(fresh, baseline)
        def old_contents(root):
            assert (root / "Editor/default.cfg").read_bytes() == b"edited by game"
            assert (root / "Profiles/user-save.dat").read_bytes() == b"user save"
            assert not (root / "Profiles/default.cfg").exists()
        def new_contents(root):
            assert (root / "Editor/default.cfg").read_bytes() == b"editor default 3"
            assert (root / "Profiles/default.cfg").read_bytes() == b"profile default 3"
            assert (root / "Profiles/added-later.cfg").exists()
            assert not (root / "Profiles/user-save.dat").exists()
        for mode in ("missing-initial", "cancel"):
            invoke(fresh, 3, action="reset-initial", mode=mode, code=1)
            old_contents(fresh)
            invoke(fresh, 3, action="recover")
        package = releases[3] / (next(p["id"] for p in third["packages"] if p["kind"] == "initial") + ".zip")
        good = package.read_bytes()
        package.write_bytes(good[:-8])
        invoke(fresh, 3, action="reset-initial", code=1)
        old_contents(fresh)
        package.write_bytes(good)
        result = invoke(fresh, 3, action="reset-initial")
        steps = result["steps"]
        new_contents(fresh)
        assert (fresh / "Data/user.pak").read_bytes() == b"unmanaged data"
        assert bool(list((fresh / ".launcher/backups/reset-data").glob("*/Profiles/user-save.dat"))) == keep
        assert not list((fresh / ".launcher/resets").rglob("*.cfg"))
        partial = None
        for fault in range(1, steps + 1):
            case = temp / f"reset-crash-{fault}"
            shutil.copytree(baseline, case)
            invoke(case, 3, action="reset-initial", fault=fault, code=86)
            if partial is None and list((case / ".launcher/resets").glob("*/original/Editor")):
                partial = temp / "lost-reset-stage"
                shutil.copytree(case, partial)
            invoke(case, 3, action="recover")
            if (case / "Profiles/user-save.dat").exists(): old_contents(case)
            else: new_contents(case)
            assert not (case / ".launcher/initial-reset.json").exists()
            assert not list((case / ".launcher/resets").rglob("*.cfg"))
        assert partial
        for path in (partial / ".launcher/resets").glob("*/new/Profiles/default.cfg"):
            path.unlink()
        rollback_seed = temp / "rollback-seed"
        shutil.copytree(partial, rollback_seed)
        rollback = invoke(partial, 3, action="recover")
        assert rollback["rolled_back"]
        old_contents(partial)
        for fault in range(1, rollback["steps"] + 1):
            case = temp / f"rollback-crash-{fault}"
            shutil.copytree(rollback_seed, case)
            invoke(case, 3, action="recover", fault=fault, code=86)
            invoke(case, 3, action="recover")
            old_contents(case)
        # Forged reset journals cannot name unrelated folders.
        forged = temp / "forged-reset"
        shutil.copytree(rollback_seed, forged)
        journal = forged / ".launcher/initial-reset.json"
        data = json.loads(journal.read_text())
        data["folders"]["Data"] = True
        journal.write_bytes(stable_json(data))
        invoke(forged, 3, action="recover", code=1)
        assert (forged / "Data/user.pak").read_bytes() == b"unmanaged data"
        # Initial-data installation itself uses the ordinary crash-safe transaction.
        initial_reference = temp / "initial-reference"
        install_steps = invoke(initial_reference)["steps"]
        for fault in range(1, install_steps + 1):
            case = temp / f"initial-crash-{fault}"
            invoke(case, fault=fault, code=86)
            invoke(case, action="recover")
            if (case / ".launcher/state.json").exists():
                invoke(case, action="verify")
                assert (case / "Profiles/default.cfg").read_bytes() == b"profile default 2"
            else:
                assert not (case / "Profiles/default.cfg").exists()

        # Persist the once-only marker even if a release temporarily omits defaults.
        build(4)
        invoke(baseline, 4)
        build(5, initial)
        invoke(baseline, 5, mode="missing-initial")
        old_contents(baseline)
        # Missing game assets do not cause a startup scan or block the Play button.
        (fresh / "Data/base.pak").unlink()
        result = subprocess.run([lifecycle, "quick-play", str(temp / "config"), str(fresh), str(releases[3]), public],
                                capture_output=True, text=True, timeout=15)
        assert result.returncode == 0, (result.stdout, result.stderr)
        invoke(fresh, 3, action="verify", code=1)
        invoke(fresh, 3, mode="repair")
        invoke(fresh, 3, action="verify")

        # Both independent manifest validators reject unsafe initial-data schemas.
        def reject(mutate):
            nonlocal checks
            candidate = copy.deepcopy(third)
            mutate(candidate)
            try: validate_payload(candidate)
            except (ValueError, KeyError): pass
            else: raise AssertionError("Python accepted invalid initial data")
            envelope = temp / "rejected.envelope.json"
            envelope.write_bytes(sign_payload(stable_json(candidate), key, "fixture-only"))
            result = subprocess.run([manifest_check, str(envelope), "fixture-only", public], capture_output=True)
            assert result.returncode == 1, ("C++ accepted invalid initial data", result.stderr)
            checks += 1
        reject(lambda m: m.update(min_launcher_version="0.2.0"))
        reject(lambda m: m["initial_data"]["folders"].append("Data"))
        reject(lambda m: m["initial_data"]["folders"].append("../outside"))
        reject(lambda m: m["initial_data"]["folders"].append("profiles"))
        reject(lambda m: m["initial_data"]["files"][0].update(executable=True))
        reject(lambda m: m["initial_data"].update(package_ids=[]))
        reject(lambda m: next(p for p in m["packages"] if p["kind"] == "initial").update(platform="windows-x64"))
        # A reset refuses hard links or directory links before changing any data.
        outside = temp / "outside"
        write(outside / "keep", b"outside data")
        (fresh / "Profiles/linked").hardlink_to(outside / "keep")
        invoke(fresh, 3, action="reset-initial", code=1)
        assert (outside / "keep").read_bytes() == b"outside data"
        (fresh / "Profiles/linked").unlink()
        try: (fresh / "Profiles/linked").symlink_to(outside, target_is_directory=True)
        except OSError: skipped.append("directory symlink creation unavailable")
        else:
            invoke(fresh, 3, action="reset-initial", code=1)
            assert (outside / "keep").read_bytes() == b"outside data"
            (fresh / "Profiles/linked").unlink()
    print(f"{checks} initial-data checks; {install_steps} install, {steps} reset and {rollback['steps']} rollback crash boundaries; skipped={skipped}")


if __name__ == "__main__":
    main()
