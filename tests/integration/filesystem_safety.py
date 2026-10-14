"""Real relocation processes and disposable data: no production game or drive-root writes."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from test_workspace import temporary_workspace


def inventory(folder):
    return {p.relative_to(folder).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in folder.rglob("*") if p.is_file()}


def main():
    driver = Path(sys.argv[1]).resolve(strict=True)
    canonical = "OpenBK2Launcher.exe" if os.name == "nt" else "OpenBK2Launcher"
    checks = []

    def run(*args, expected=0):
        result = subprocess.run(list(map(str, args)), capture_output=True, text=True,
                                encoding="utf-8", timeout=20)
        assert result.returncode == expected, (args, result.stdout, result.stderr)
        return result

    with temporary_workspace() as temp:
        outside = temp / "unrelated-user-data.txt"
        outside.write_bytes(b"outside every installation")

        def populated(name):
            folder = temp / name
            folder.mkdir()
            for path in ("Data/personal.pak", "Profiles/save.dat", "MODs/Local mod/notes.txt"):
                target = folder / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(b"user-owned: " + path.encode())
            return folder

        # A launcher already in the installation is left under its existing name.
        for name in (canonical, "Renamed Launcher.exe" if os.name == "nt" else "Renamed Launcher"):
            folder = populated("same-folder-" + str(len(checks)))
            source = folder / name
            shutil.copy2(driver, source)
            if name != canonical:
                (folder / canonical).write_bytes(b"different existing launcher; preserve it too")
            before = inventory(folder)
            config = temp / ("config-" + str(len(checks)))
            default = Path(run(source, "settings-root", config).stdout.strip())
            assert default.parts[-2:] == ("Games", "OpenBK2") and default != folder
            assert not (config / "settings.json").exists()
            run(source, "relocate", config, folder)
            assert inventory(folder) == before and not list(config.glob("handoff-*"))
            checks.append("already-in-game-folder: " + name)

        # An identical existing target is reused through the authenticated handoff.
        folder = populated("identical-target")
        target = folder / canonical
        shutil.copy2(driver, target)
        source = temp / "identical-source.exe"
        shutil.copy2(driver, source)
        config = temp / "identical-config"
        before = inventory(folder)
        run(source, "relocate", config, folder)
        deadline = time.monotonic() + 10
        while not (config / "handoff-success").exists() and time.monotonic() < deadline:
            time.sleep(.05)
        assert (config / "handoff-success").exists() and not source.exists()
        assert {k: v for k, v in inventory(folder).items() if not k.startswith(".launcher/")} == before
        checks.append("identical-target-reuse-and-source-cleanup")

        source = temp / "source-kept.exe"
        shutil.copy2(driver, source)
        source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
        folder = populated("different-target")
        (folder / canonical).write_bytes(b"an unrelated executable")
        before = inventory(folder)
        failed = run(source, "relocate", temp / "different-config", folder, expected=1)
        assert "different launcher" in failed.stderr
        assert {k: v for k, v in inventory(folder).items() if not k.startswith(".launcher/")} == before
        checks.append("different-target-never-overwritten")

        for mode in ("hold", "hold-lock"):
            folder = populated("busy-" + mode)
            target = folder / canonical
            shutil.copy2(driver, target)
            holder = subprocess.Popen([str(target), mode, str(folder)], stdin=subprocess.PIPE,
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                assert holder.stdout.readline().strip() == "ready"
                before = inventory(folder)
                config = temp / ("busy-config-" + mode)
                failed = run(source, "relocate", config, folder, expected=1)
                if mode == "hold":
                    assert "already running" in failed.stderr
                assert holder.poll() is None
                assert {k: v for k, v in inventory(folder).items() if not k.startswith(".launcher/")} == {
                    k: v for k, v in before.items() if not k.startswith(".launcher/")}
                assert not list(config.glob("handoff-*"))
            finally:
                holder.communicate("\n", timeout=10)
            checks.append("relocation-blocked-by-" + mode)

        folder = populated("failed-handoff")
        before = inventory(folder)
        failed = run(source, "relocate-no-ack", temp / "no-ack-config", folder, expected=1)
        assert "did not acknowledge" in failed.stderr
        assert all(inventory(folder)[path] == digest for path, digest in before.items())
        checks.append("failed-handoff-keeps-source-and-user-files")

        # An editable handoff record cannot authorize deleting an arbitrary file,
        # even when both paths contain the same launcher bytes.
        config = temp / "forged-config"
        config.mkdir()
        token = "a" * 32
        target = folder / canonical
        record = {"source": source.as_posix(), "destination": target.as_posix(),
                  "parent_pid": os.getpid(), "sha256": source_hash}
        (config / f"handoff-{token}.json").write_text(json.dumps(record), encoding="utf-8")
        failed = run(target, "handoff-test", config, "--handoff", token, expected=1)
        assert "parent mismatch" in failed.stderr and not (config / f"handoff-{token}.ack").exists()
        assert hashlib.sha256(source.read_bytes()).hexdigest() == source_hash
        assert outside.read_bytes() == b"outside every installation"
        checks.append("forged-handoff-cannot-delete-source")

        # Validate corrupt saved roots without initializing or writing to those roots.
        for root in (str(Path(temp.anchor)), temp.as_posix() + "\0ignored"):
            settings = {"settings_schema_version": 1, "installation_root": root,
                        "requested_language": "en", "configured": True, "relocate": True, "shortcut": True}
            (config / "settings.json").write_text(json.dumps(settings), encoding="utf-8")
            run(source, "settings-root", config, expected=1)
        checks.append("unsafe-saved-roots-rejected-before-use")

        if os.name != "nt":
            os.mkfifo(temp / "user-pipe")
            failed = run(driver, "safe-read", temp, "user-pipe", expected=1)
            assert "nonregular" in failed.stderr
            assert (temp / "user-pipe").exists()
            checks.append("fifo-rejected-without-blocking-or-deletion")

    result = {"platform": sys.platform, "checks": checks, "result": "pass"}
    artifacts = Path(__file__).resolve().parents[2] / "build/safety-artifacts"
    artifacts.mkdir(exist_ok=True)
    (artifacts / f"filesystem-{sys.platform}.json").write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
