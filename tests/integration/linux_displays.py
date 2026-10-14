"""Native X11/Wayland rendering and X11 input/folder-picker integration."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from shared_fixtures import build_shared_fixtures
from test_workspace import temporary_workspace


def wait_for(predicate, message, seconds=15, process=None):
    until = time.monotonic()+seconds
    while time.monotonic() < until:
        if process is not None and process.poll() is not None:
            raise AssertionError(f"{message}: process exited with status {process.returncode}")
        value = predicate()
        if value:
            return value
        time.sleep(.05)
    raise AssertionError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("game", type=Path)
    parser.add_argument("--backend", choices=["x11", "wayland"], required=True)
    args = parser.parse_args()
    binary, game = args.binary.resolve(), args.game.resolve()
    artifacts = Path(__file__).resolve().parents[2] / "build/display-artifacts"
    artifacts.mkdir(parents=True, exist_ok=True)
    children = []
    # Wayland's Unix socket pathname must fit in 108 bytes including NUL.
    # GitHub's checkout path plus our UUID workspace is already too long.
    # mkdtemp creates a private 0700 directory; explicitly avoid a long TMPDIR.
    with temporary_workspace() as temp, tempfile.TemporaryDirectory(prefix="obk2-display-", dir="/tmp") as runtime_name:
        runtime = Path(runtime_name)
        env = dict(os.environ, XDG_RUNTIME_DIR=str(runtime), HOME=str(temp), SDL_VIDEO_DRIVER=args.backend, SDL_RENDER_DRIVER="software")
        logs = []
        def start(command):
            log = (artifacts / f"{args.backend}-{len(children)}.log").open("w")
            logs.append(log)
            child = subprocess.Popen(list(map(str, command)), env=env, stdout=log, stderr=subprocess.STDOUT)
            children.append(child)
            return child
        def run(command, **kwargs):
            return subprocess.run(list(map(str, command)), env=env, capture_output=True, text=True, timeout=40, **kwargs)
        try:
            if args.backend == "wayland":
                env["WAYLAND_DISPLAY"] = "openbk2-test"
                weston = start(["weston", "--backend=headless-backend.so", "--use-pixman", "--socket=openbk2-test", "--width=1280", "--height=800", "--idle-time=0"])
                wait_for(lambda: (runtime / "openbk2-test").is_socket(), "Weston did not create its socket", process=weston)
            else:
                env["DISPLAY"] = ":88"
                xvfb = start(["Xvfb", ":88", "-screen", "0", "1280x800x24", "-nolisten", "tcp"])
                wait_for(lambda: run(["xdotool", "getdisplaygeometry"]).returncode == 0, "Xvfb unavailable", process=xvfb)
                start(["openbox", "--sm-disable"])
                time.sleep(.5)
            private, _ = build_shared_fixtures(temp / "fixtures", game=game, count=1, initial_data=True)
            public = private.public_key().public_bytes_raw().hex()
            common = [binary, "--fixture-release", temp / "fixtures/release-1", "--fixture-public-key", public]
            result = run(common + ["--auto-install", "--config-dir", temp / "config", "--root", temp / "game",
                                   "--capture", artifacts / (args.backend+"-installed.bmp")])
            assert result.returncode == 0, (result.stdout, result.stderr)
            assert (temp / "game/.launcher/state.json").exists()
            report = {"backend": args.backend, "rendered_fixture_install": "pass"}
            if args.backend == "x11":
                config, proposed, selected = temp / "wizard-config", temp / "proposed", temp / "selected"
                config.mkdir(); proposed.mkdir(); selected.mkdir()
                settings = {"settings_schema_version": 1, "installation_root": str(proposed), "requested_language": "en",
                            "configured": False, "relocate": False, "shortcut": False}
                (config / "settings.json").write_text(json.dumps(settings))
                process = start(common + ["--config-dir", config, "--capture", artifacts / "x11-setup.bmp"])
                def window(pattern):
                    found = run(["xdotool", "search", "--onlyvisible", "--name", pattern])
                    return found.stdout.splitlines()[0] if found.returncode == 0 and found.stdout.strip() else None
                hwnd = wait_for(lambda: window("^OpenBK2 Launcher$"), "Launcher window unavailable")
                def geometry():
                    text = run(["xdotool", "getwindowgeometry", "--shell", hwnd]).stdout
                    return {k: int(v) for k, v in (line.split("=", 1) for line in text.splitlines())}
                def click(x, y):
                    run(["xdotool", "windowactivate", "--sync", hwnd], check=True)
                    g = geometry()
                    result = run(["xdotool", "mousemove", "--sync", str(g["X"]+x), str(g["Y"]+y)])
                    assert result.returncode == 0
                    time.sleep(.15)
                    run(["xdotool", "mousedown", "1"], check=True)
                    time.sleep(.15)
                    run(["xdotool", "mouseup", "1"], check=True)
                    time.sleep(.2)
                time.sleep(.5)
                # 1000x600 logical layout; captured setup frame documents these coordinates.
                click(882, 147)
                picker = wait_for(lambda: window("Select|Choose|Open Directory|File Selection|zenity"), "Native folder picker did not open")
                run(["xdotool", "windowactivate", "--sync", picker], check=True)
                run(["xdotool", "key", "ctrl+l"], check=True)
                run(["xdotool", "type", "--clearmodifiers", str(selected)+"/"], check=True)
                run(["xdotool", "key", "Return"], check=True)
                time.sleep(.4)
                run(["xdotool", "key", "Return"], check=True)
                wait_for(lambda: window("Select|Choose|Open Directory|File Selection|zenity") is None, "Folder picker did not close")
                time.sleep(.3)
                click(60, 393)  # Save the selected folder; check the callback's result on disk.
                wait_for(lambda: json.loads((config / "settings.json").read_text())["installation_root"] == str(selected), "Folder callback did not persist selection")
                report["native_folder_picker_callback"] = "pass"
                # Verify blank-title drag while buttons still receive normal pointer input.
                before = geometry()
                run(["xdotool", "mousemove", str(before["X"]+430), str(before["Y"]+24)], check=True)
                time.sleep(.2)
                run(["xdotool", "mousedown", "1"], check=True)
                time.sleep(.2)
                run(["xdotool", "mousemove", "--sync", str(before["X"]+460), str(before["Y"]+54)], check=True)
                time.sleep(.2)
                run(["xdotool", "mouseup", "1"], check=True)
                time.sleep(.3)
                after = geometry()
                assert (before["X"], before["Y"]) != (after["X"], after["Y"]), (before, after)
                report["title_drag"] = "pass"
                click(after["WIDTH"] - 60, 24)  # Circular Minimize, anchored to the right edge.
                wait_for(lambda: window("^OpenBK2 Launcher$") is None, "Minimize button failed")
                run(["xdotool", "windowmap", hwnd, "windowactivate", "--sync", hwnd], check=True)
                time.sleep(.3)
                click(after["WIDTH"] - 24, 24)  # Circular Close.
                assert process.wait(timeout=10) == 0
                report["minimize_restore_close"] = "pass"
                # Exercise the actual Mods controls against signed optional assets.
                process = start(common + ["--config-dir", temp / "config", "--root", temp / "game"])
                hwnd = wait_for(lambda: window("^OpenBK2 Launcher$"), "Installed launcher unavailable")
                time.sleep(.8)
                click(660, 24)
                click(350, 230)  # Alpha Download; verified by files, not a UI-only state.
                active = temp / "game/MODs/Alpha"
                wait_for(lambda: (active / "assets/2.pak").exists(), "Mod Download control did not install Alpha")
                (active / "user.cfg").write_bytes(b"preserved player edits")
                time.sleep(.5)
                click(285, 172)  # Installed Alpha / Remove.
                time.sleep(.3)
                click(355, 362)  # Confirm permanent removal in the centered warning dialog.
                wait_for(lambda: not active.exists(), "Mod Remove control did not remove Alpha")
                backups = list((temp / "game/.launcher/backups/removed-mods").glob("*/Alpha/user.cfg"))
                if backups:
                    assert len(backups) == 1 and backups[0].read_bytes() == b"preserved player edits"
                wait_for(lambda: not list((temp / "game/.launcher/discarded-mods").glob("*/Alpha/user.cfg")), "Removed mod cleanup did not finish")
                report["mod_download_and_confirmed_removal"] = "pass"
                process.terminate()
                process.wait(timeout=10)
                # The Settings reset requires confirmation and replaces only its signed folders.
                defaults = temp / "game/Profiles/default.cfg"
                personal = temp / "game/Profiles/user.save"
                defaults.write_bytes(b"edited settings")
                personal.write_bytes(b"personal save")
                process = start(common + ["--config-dir", temp / "config", "--root", temp / "game"])
                hwnd = wait_for(lambda: window("^OpenBK2 Launcher$"), "Launcher unavailable for Settings reset")
                time.sleep(.8)
                click(790, 24)  # Settings.
                click(500, 429)  # Reset initial data, beside Repair/Reinstall/Shortcut.
                try:
                    from PIL import ImageGrab
                    ImageGrab.grab(xdisplay=env["DISPLAY"]).save(artifacts / "x11-reset-confirmation.png")
                except ImportError:
                    pass
                click(470, 390)  # Cancel in the reset confirmation.
                time.sleep(.2)
                assert defaults.read_bytes() == b"edited settings" and personal.exists()
                click(500, 429)
                click(350, 390)  # Explicitly confirm replacing the initial folders.
                wait_for(lambda: defaults.exists() and defaults.read_bytes() == b"profile default" and not personal.exists(), "Settings Reset did not restore initial data")
                assert (temp / "game/Data/common.pak").exists()
                report["initial_data_reset_cancel_and_confirm"] = "pass"
                process.terminate()
                process.wait(timeout=10)
            (artifacts / (args.backend+"-results.json")).write_text(json.dumps(report, indent=2)+"\n")
            print(json.dumps(report))
        except Exception:
            for log in logs:
                log.flush()
                # Keep console diagnostics bounded; archive the complete log.
                with Path(log.name).open("rb") as stream:
                    stream.seek(0, os.SEEK_END)
                    stream.seek(max(0, stream.tell()-16384))
                    tail = stream.read().decode("utf-8", errors="replace")
                print(f"--- {Path(log.name).name} ---\n{tail}", file=sys.stderr, flush=True)
            if args.backend == "x11":
                print(run(["xdotool", "search", "--onlyvisible", "--name", ".", "getwindowname", "%@"]).stdout, flush=True)
                try:
                    from PIL import ImageGrab
                    ImageGrab.grab(xdisplay=env["DISPLAY"]).save(artifacts / "x11-failure.png")
                except ImportError:
                    pass
            raise
        finally:
            for child in reversed(children):
                if child.poll() is None:
                    child.terminate()
                    child.wait(timeout=10)
            for log in logs:
                log.close()


if __name__ == "__main__":
    main()
