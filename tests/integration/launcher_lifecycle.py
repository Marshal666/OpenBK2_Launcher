"""Native process, controller, handoff, shortcut and rendered installation tests."""
import json
import http.server
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import threading
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from fixture_builder import build_fixtures
from shared_fixtures import build_shared_fixtures
from test_workspace import temporary_workspace


def main():
    driver, game, gui = map(lambda p: Path(p).resolve(), sys.argv[1:])
    checks = 0
    def run(*args, **kwargs):
        nonlocal checks
        result = subprocess.run(list(map(str, args)), capture_output=True, text=True, encoding="utf-8", timeout=45, **kwargs)
        assert result.returncode == 0, (args, result.stdout, result.stderr)
        checks += 1
        return result
    with temporary_workspace() as temp:
        path_examples = (
            [(r"C:\Users\Example\Games/OpenBK2", "C:/Users/Example/Games/OpenBK2"),
             ("C:\\Games\\Žluťoučký", "C:/Games/Žluťoučký"),
             (r"\\server\share\Game folder", "//server/share/Game folder")]
            if os.name == "nt" else
            [("/home/example/Games/Žluťoučký", "/home/example/Games/Žluťoučký"),
             (r"/home/example/Game\folder", r"/home/example/Game\folder")]
        )
        for supplied, expected in path_examples:
            assert run(driver, "path-text", supplied).stdout.rstrip("\n") == expected
        key = build_fixtures(temp / "fixtures", native_game=game).public_key().public_bytes_raw().hex()
        release = temp / "fixtures/release-1"
        root, config = temp / "Game with spaces Žluťoučký", temp / "config"
        # Existing Windows settings with native separators must still load and
        # be saved back with forward slashes without changing the selected root.
        config.mkdir()
        (config / "settings.json").write_text(json.dumps({
            "settings_schema_version": 1, "installation_root": str(root),
            "requested_language": "en", "configured": False, "relocate": False, "shortcut": False,
        }), encoding="utf-8")
        run(driver, "online", config, root, release, key)
        assert (root / "Data/texts-de.pak").exists() and not (root / "Data/texts-en.pak").exists()
        run(driver, "offline", config, root, release, key)
        record = (root / "bin/fixture-run.txt").read_text(encoding="utf-8").splitlines()
        assert Path(record[0]) == root / "bin"
        if os.name != "nt":
            assert record[1:3] == ["SDL3", str(root / "bin")]
        config_json = json.loads((config / "settings.json").read_text(encoding="utf-8"))
        assert config_json["requested_language"] == "de"
        assert config_json["installation_root"] == root.as_posix()
        # Verify native argument quoting without a shell and run from an unrelated cwd.
        arguments = ["two words", 'a"quoted"value', "ends\\", "Žluťoučký", "$(not-a-shell)"]
        executable = root / "bin" / ("Game.exe" if os.name == "nt" else "Game")
        run(driver, "spawn", executable, root / "bin", *arguments, cwd=temp)
        assert (root / "bin/fixture-run.txt").read_text(encoding="utf-8").splitlines()[3:] == arguments
        desktop = temp / "Desktop"
        shortcut_env = dict(os.environ, XDG_DATA_HOME=str(temp / "shortcut-data"))
        run(driver, "shortcut", gui, desktop, env=shortcut_env)
        shortcut = desktop / ("OpenBK2 Launcher.lnk" if os.name == "nt" else "openbk2-launcher.desktop")
        assert shortcut.stat().st_size > 30
        if os.name != "nt":
            assert "Exec=" in shortcut.read_text() and shortcut.stat().st_mode & 0o100
            # Parse and execute the actual desktop entry with the desktop's native GLib loader.
            tricky = temp / 'space "$%`\\ path' / "FixtureGame"
            tricky.parent.mkdir()
            shutil.copy2(game, tricky)
            escaped_desktop = temp / "escaped-desktop"
            run(driver, "shortcut", tricky, escaped_desktop, env=shortcut_env)
            gio = shutil.which("gio")
            assert gio, "Install libglib2.0-bin for native .desktop parsing tests"
            run(gio, "launch", escaped_desktop / "openbk2-launcher.desktop", cwd=temp)
            deadline = time.monotonic()+5
            while not (temp / "fixture-run.txt").exists() and time.monotonic() < deadline:
                time.sleep(.05)
            assert (temp / "fixture-run.txt").exists(), "Escaped .desktop Exec did not launch"
            time.sleep(1.6)
            executable.chmod(0o644)
            failed = subprocess.run([str(driver), "offline", str(config), str(root), str(release), key], capture_output=True)
            assert failed.returncode != 0, "Missing execute bit was reported Ready"
            run(driver, "online", config, root, release, key)
            assert executable.stat().st_mode & 0o100
        else:
            too_long = temp / ("deep" * 60) / "game"
            failed = subprocess.run([str(driver), "online", str(temp / "long-config"), str(too_long), str(release), key], capture_output=True)
            assert failed.returncode != 0 and b"path limit" in failed.stderr.lower(), failed.stderr
            assert (root / ".launcher/state.json").exists()
        # Only a disposable copied test executable is eligible for relocation cleanup.
        source = temp / ("Downloaded Launcher.exe" if os.name == "nt" else "Downloaded Launcher")
        shutil.copy2(driver, source)
        destination, handoff_config = temp / "relocated game", temp / "handoff-config"
        run(source, "relocate", handoff_config, destination)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline and not (handoff_config / "handoff-success").exists():
            time.sleep(.05)
        assert (handoff_config / "handoff-success").exists(), "Destination handoff failed"
        assert not source.exists(), "Source image was not cleaned after parent exit"
        # A collision must leave both originals unchanged.
        source2 = temp / ("Other Launcher.exe" if os.name == "nt" else "Other Launcher")
        shutil.copy2(driver, source2)
        target = destination / ("OpenBK2Launcher.exe" if os.name == "nt" else "OpenBK2Launcher")
        target.write_bytes(b"unrelated user executable")
        failed = subprocess.run([str(source2), "relocate", str(handoff_config), str(destination)], capture_output=True)
        assert failed.returncode != 0 and source2.exists() and target.read_bytes() == b"unrelated user executable"
        root.rename(temp / "moved-game")
        run(driver, "missing", config, root, release, key)
        # Exercise the real renderer and controller together, with fixture-only CLI automation.
        capture = temp / "installed.bmp"
        env = dict(os.environ, SDL_VIDEO_DRIVER="dummy", SDL_RENDER_DRIVER="software")
        transferred = []
        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *_):
                pass
            def do_GET(self):
                name = self.path.lstrip("/")
                assert "/" not in name and name.endswith(".zip")
                data = (release / name).read_bytes()
                self.send_response(200)
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                for offset in range(0, len(data), 4096):
                    self.wfile.write(data[offset:offset+4096])
                    self.wfile.flush()
                    time.sleep(.006)
                transferred.append(len(data))
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        progress_capture = temp / "progress.bmp"
        try:
            run(gui, "--auto-install", "--fixture-release", release, "--fixture-public-key", key,
                "--config-dir", temp / "gui-config", "--root", temp / "gui-game", "--capture", capture,
                "--fixture-package-base", f"http://127.0.0.1:{server.server_port}", "--capture-progress", progress_capture, env=env)
        finally:
            server.shutdown()
            thread.join()
            server.server_close()
        assert sum(transferred) > 128*1024 and progress_capture.exists()
        assert capture.stat().st_size > 1000
        assert (temp / "gui-game/.launcher/state.json").exists()
        artifacts = Path(__file__).resolve().parents[2] / "build/lifecycle-artifacts"
        artifacts.mkdir(exist_ok=True)
        shutil.copyfile(capture, artifacts / "installed.bmp")
        shutil.copyfile(progress_capture, artifacts / "progress.bmp")
        # Real controller + HTTP transfers + process exclusion for optional mods.
        mod_key, mod_releases = build_shared_fixtures(temp / "shared-fixtures", game=game, count=1, hold_game=True)
        release = mod_releases[0]
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            run(driver, "mods", temp / "mods-config", temp / "mods-game", release,
                mod_key.public_key().public_bytes_raw().hex(), f"http://127.0.0.1:{server.server_port}")
        finally:
            server.shutdown()
            thread.join()
            server.server_close()
        retained = list((temp / "mods-game/.launcher/backups/removed-mods").glob("*/Alpha/user.cfg"))
        assert len(retained) == 1 and retained[0].read_text() == "player edit"
        assert (temp / "mods-game/MODs/Beta/readme.txt").exists()
    print(f"{checks} native command checks; offline Play, language persistence, cwd/argv, process exclusion, shortcut, handoff, collision and GUI install passed")


if __name__ == "__main__":
    main()
