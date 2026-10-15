"""Mixed ownership in Data: collisions, updates, migration and scoped reset recovery."""
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
    installer, manifest_check, game = [str(Path(p).resolve(strict=True)) for p in sys.argv[1:]]
    platform = "windows-x64" if os.name == "nt" else "linux-x64"
    keep = json.loads(subprocess.check_output([installer, "--backup-policy"], text=True))
    checks = 0
    with temporary_workspace() as temp:
        key = Ed25519PrivateKey.generate()
        public = key.public_key().public_bytes_raw().hex()
        shared, initial, languages = [temp / p for p in ("shared", "initial", "languages")]
        roots = {p: temp / p for p in ("windows-x64", "linux-x64")}

        def write(path, data):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)

        for p, folder in roots.items():
            exe = folder / ("bin/Game.exe" if p == "windows-x64" else "bin/Game")
            write(exe, Path(game).read_bytes())
            exe.chmod(0o755)
        write(roots["windows-x64"] / "Data/platform.pak", b"Windows data")
        write(shared / "Data/base.pak", b"base 1")
        write(shared / "Data/Prefs/assets.pak", b"managed file beside a default")
        write(shared / "Data/obsolete.pak", b"old managed asset")
        write(shared / "Data/settings.cfg", b"formerly managed default")
        write(languages / "texts-en.pak", b"English")
        write(languages / "texts-de.pak", b"German")
        releases = {}

        def build(seq, seed=initial):
            output = temp / f"release-{seq}"
            manifest = build_release(out=output, version=f"1.0.{seq}", sequence=seq, roots=roots,
                                     shared_root=shared, shared_init_data=seed, languages_dir=languages,
                                     base_url=f"https://fixtures.invalid/{seq}", previous=releases.get(seq-1))
            sign_release(output, key, "fixture-only")
            verify_release(output, {"fixture-only": bytes.fromhex(public)})
            releases[seq] = output
            subprocess.run([manifest_check, str(output / "manifest.envelope.json"), "fixture-only", public],
                           check=True, capture_output=True)
            return manifest

        def invoke(root, seq=2, action="install", mode="update", fault=0, code=0, language="en"):
            nonlocal checks
            args = [installer, action, str(root), str(releases[seq]), public, platform, language, mode, str(fault)]
            result = subprocess.run(args, capture_output=True, text=True, timeout=30)
            assert result.returncode == code, (args, result.stdout, result.stderr)
            checks += 1
            return json.loads(result.stdout) if result.stdout.strip() else None

        build(1, None)
        migrated = temp / "migrated"
        invoke(migrated, 1)
        write(migrated / "Data/settings.cfg", b"personal settings")
        (shared / "Data/settings.cfg").unlink()
        (shared / "Data/obsolete.pak").unlink()
        write(initial / "Data/settings.cfg", b"settings 2")
        write(initial / "Data/Prefs/default.cfg", b"preferences 2")
        write(initial / "Profiles/default.cfg", b"profile 2")
        second = build(2)
        assert second["min_launcher_version"] == "0.3.1"
        invoke(migrated)
        assert (migrated / "Data/settings.cfg").read_bytes() == b"personal settings"
        assert not (migrated / "Data/obsolete.pak").exists()
        assert (migrated / "Data/Prefs/assets.pak").read_bytes() == b"managed file beside a default"
        state = json.loads((migrated / ".launcher/state.json").read_bytes())
        assert state["state_schema_version"] == 3
        assert "data/settings.cfg" in state["initialized_data_files"]
        assert not any(f["path"] == "Data/settings.cfg" for f in state["managed_files"])

        fresh = temp / "fresh"
        install_steps = invoke(fresh)["steps"]
        assert (fresh / "Data/settings.cfg").read_bytes() == b"settings 2"
        for fault in range(1, install_steps + 1):
            case = temp / f"install-crash-{fault}"
            invoke(case, fault=fault, code=86)
            invoke(case, action="recover")
            if (case / ".launcher/state.json").exists():
                invoke(case, action="verify")
                assert (case / "Data/settings.cfg").read_bytes() == b"settings 2"

        write(fresh / "Data/settings.cfg", b"personal settings")
        (fresh / "Data/Prefs/default.cfg").unlink()
        write(fresh / "Data/Prefs/user-created.cfg", b"keep this neighbor")
        write(fresh / "Profiles/user-save.dat", b"personal save")
        write(shared / "Data/base.pak", b"base 3")
        write(initial / "Data/settings.cfg", b"settings 3")
        write(initial / "Data/Prefs/default.cfg", b"preferences 3")
        write(initial / "Data/new-default.cfg", b"new default 3")
        third = build(3)
        for mode in ("update", "repair", "reinstall"):
            invoke(fresh, 3, mode=mode, language="de")
            assert (fresh / "Data/settings.cfg").read_bytes() == b"personal settings"
            assert not (fresh / "Data/Prefs/default.cfg").exists()
            assert not (fresh / "Data/new-default.cfg").exists()
            assert (fresh / "Data/base.pak").read_bytes() == b"base 3"
            assert (fresh / "Data/texts-de.pak").read_bytes() == b"German"
            assert not (fresh / "Data/texts-en.pak").exists()

        baseline = temp / "before-reset"
        shutil.copytree(fresh, baseline)

        def preserved(root):
            assert (root / "Data/base.pak").read_bytes() == b"base 3"
            assert (root / "Data/Prefs/assets.pak").read_bytes() == b"managed file beside a default"
            assert (root / "Data/Prefs/user-created.cfg").read_bytes() == b"keep this neighbor"
            assert (root / "Data/texts-de.pak").read_bytes() == b"German"

        def old_contents(root):
            preserved(root)
            assert (root / "Data/settings.cfg").read_bytes() == b"personal settings"
            assert not (root / "Data/Prefs/default.cfg").exists()
            assert not (root / "Data/new-default.cfg").exists()
            assert (root / "Profiles/user-save.dat").read_bytes() == b"personal save"

        def new_contents(root):
            preserved(root)
            assert (root / "Data/settings.cfg").read_bytes() == b"settings 3"
            assert (root / "Data/Prefs/default.cfg").read_bytes() == b"preferences 3"
            assert (root / "Data/new-default.cfg").read_bytes() == b"new default 3"
            assert not (root / "Profiles/user-save.dat").exists()

        for mode in ("missing-initial", "cancel"):
            invoke(fresh, 3, action="reset-initial", mode=mode, code=1)
            old_contents(fresh)
            invoke(fresh, 3, action="recover")
        steps = invoke(fresh, 3, action="reset-initial")["steps"]
        new_contents(fresh)
        assert bool(list((fresh / ".launcher/backups/reset-data").glob("*/Data/settings.cfg"))) == keep
        assert not list((fresh / ".launcher/resets").rglob("*.cfg"))
        partial = None
        for fault in range(1, steps + 1):
            case = temp / f"reset-crash-{fault}"
            shutil.copytree(baseline, case)
            invoke(case, 3, action="reset-initial", fault=fault, code=86)
            if partial is None and list((case / ".launcher/resets").glob("*/original/Data/settings.cfg")):
                partial = temp / "rollback-seed"
                shutil.copytree(case, partial)
            invoke(case, 3, action="recover")
            if (case / "Profiles/user-save.dat").exists():
                old_contents(case)
            else:
                new_contents(case)
            assert not (case / ".launcher/initial-reset.json").exists()
        assert partial
        missing_data = temp / "lost-data-stage"
        shutil.copytree(partial, missing_data)
        staged_data = list((missing_data / ".launcher/resets").glob("*/new/Data/settings.cfg"))
        assert staged_data
        staged_data[0].unlink()
        assert invoke(missing_data, 3, action="recover")["rolled_back"]
        old_contents(missing_data)
        # Lose a staged file after some Data replacements: recover all original data.
        for path in (partial / ".launcher/resets").glob("*/new/Profiles/default.cfg"):
            path.unlink()
        rollback = temp / "rollback"
        shutil.copytree(partial, rollback)
        result = invoke(rollback, 3, action="recover")
        assert result["rolled_back"]
        old_contents(rollback)
        for fault in range(1, result["steps"] + 1):
            case = temp / f"rollback-crash-{fault}"
            shutil.copytree(partial, case)
            invoke(case, 3, action="recover", fault=fault, code=86)
            invoke(case, 3, action="recover")
            old_contents(case)
        for unauthorized in ("Data", "Data/base.pak", "Data/Prefs", "Data/texts-de.pak"):
            forged = temp / ("forged-" + str(checks))
            shutil.copytree(partial, forged)
            journal = forged / ".launcher/initial-reset.json"
            data = json.loads(journal.read_bytes())
            data["paths"][unauthorized] = True
            journal.write_bytes(stable_json(data))
            invoke(forged, 3, action="recover", code=1)
            preserved(forged)

        # Both independent validators reject exact, case and ancestor collisions.
        def reject(candidate):
            nonlocal checks
            try:
                validate_payload(candidate)
            except (ValueError, KeyError):
                pass
            else:
                raise AssertionError("Python accepted colliding initial files")
            envelope = temp / "rejected.envelope.json"
            envelope.write_bytes(sign_payload(stable_json(candidate), key, "fixture-only"))
            result = subprocess.run([manifest_check, str(envelope), "fixture-only", public], capture_output=True)
            assert result.returncode == 1, result.stderr
            checks += 1

        for path in ("Data/base.pak", "Data/BASE.pak", "Data/base.pak/child.cfg", "Data/Prefs",
                     "Data/prefs/another.cfg", "Data/platform.pak", "Data/texts-en.pak",
                     "Data/TEXTS-DE.pak", "Data/texts-de.pak/child.cfg", "data/another.cfg", "bin/default.cfg"):
            candidate = copy.deepcopy(third)
            old_path = "Data/settings.cfg"
            for entry in candidate["initial_data"]["files"]:
                if entry["path"] == old_path:
                    entry["path"] = path
            candidate["initial_data"]["folders"] = sorted({f["path"].split('/')[0] for f in candidate["initial_data"]["files"]})
            for package in candidate["packages"]:
                if package["kind"] == "initial":
                    for entry in package["files"]:
                        if entry["path"] == old_path:
                            entry["path"] = path
            reject(candidate)
        candidate = copy.deepcopy(third)
        candidate["min_launcher_version"] = "0.3.0"
        reject(candidate)
        # Actual publisher inputs must also stop before emitting a signable release.
        conflict = temp / "conflict-input"
        write(conflict / "Data/base.pak", b"colliding default")
        try:
            build_release(out=temp / "conflict-output", version="2.0.0", sequence=1, roots=roots,
                          shared_root=shared, shared_init_data=conflict, languages_dir=languages,
                          base_url="https://fixtures.invalid/conflict")
        except ValueError:
            pass
        else:
            raise AssertionError("Publisher accepted an input collision")
        assert not (temp / "conflict-output/manifest.payload.json").exists()

        # Ownership markers survive releases without defaults; base files still update.
        omitted = build(4, None)
        assert omitted["min_launcher_version"] == "0.3.1"
        invoke(baseline, 4, language="de")
        build(5)
        invoke(baseline, 5, mode="missing-initial", language="de")
        old_contents(baseline)
        write(shared / "Data/settings.cfg", b"must not reclaim user-owned settings")
        build(6, None)
        invoke(baseline, 6, code=1)
        old_contents(baseline)
        # A Data-only initial inventory also resets individual files successfully.
        (initial / "Profiles/default.cfg").unlink()
        (initial / "Profiles").rmdir()
        (shared / "Data/settings.cfg").unlink()
        build(7)
        only = temp / "data-only"
        invoke(only, 7)
        write(only / "Data/settings.cfg", b"edited")
        invoke(only, 7, action="reset-initial")
        assert (only / "Data/settings.cfg").read_bytes() == b"settings 3"
        assert (only / "Data/base.pak").read_bytes() == b"base 3"
    print(f"{checks} mixed-Data checks; {install_steps} install, {steps} reset and {result['steps']} rollback crash boundaries passed")


if __name__ == "__main__":
    main()
