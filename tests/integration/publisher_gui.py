"""Publisher orchestration and real GUI subprocess checks. No GitHub writes."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import publisher
from release import verify_release
from test_workspace import temporary_workspace


def expect_error(config, fragment):
    try:
        publisher.run_job(config, report=lambda _: None)
    except (ValueError, FileNotFoundError) as error:
        assert fragment in str(error), str(error)
    else:
        raise AssertionError(f"Expected failure: {fragment}")


def main():
    with temporary_workspace() as root, tempfile.TemporaryDirectory(prefix="openbk2-publisher-key-") as keys:
        root = root / "Publishing space & apostrophe's čć"
        root.mkdir()
        data = {
            "shared/Data/base.pak": b"base data", "shared/splash.bmp": b"splash",
            "windows/bin/Game.exe": b"synthetic Windows game",
            "linux/bin/Game": b"synthetic Linux game", "languages/texts-en.pak": b"English",
            "mods/Example/mod.pak": b"mod data", "shared/Profiles/official.dat": b"official profile",
            "initial/Editor/default.cfg": b"initial editor defaults",
            "windows/bin/debug.log": b"exclude this",
        }
        for name, content in data.items():
            path = root / "inputs" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        config = dict(owner="test", repo="game", manifest_repo="test/manifest", manifest_branch="main",
                      key_id="test-key", key_file=str(Path(keys) / "key.pem"), public_key="",
                      source_dir=str(Path(__file__).resolve().parents[2]), python=sys.executable,
                      output_root=str(root / "releases"), version="0.1.0", sequence="1",
                      shared_root=str(root / "inputs/shared"), windows_root=str(root / "inputs/windows"),
                      linux_root=str(root / "inputs/linux"), languages_dir=str(root / "inputs/languages"),
                      mods_dir=str(root / "inputs/mods"), managed_files="Profiles/official.dat",
                      shared_init_data=str(root / "inputs/initial"),
                      excludes="bin/*.log", first_time=True, publish=False, build_launcher=False, resume=False)
        expect_error(dict(config, languages_dir=str(root / "missing")), "Input folder does not exist")
        expect_error(dict(config, shared_init_data=str(root / "missing")), "Input folder does not exist")
        expect_error(dict(config, output_root=str(root / "inputs/initial/releases")), "outside all input folders")
        assert not Path(config["key_file"]).exists()
        messages = []
        first = publisher.run_job(config, report=messages.append)
        assert not first["published"] and len(first["public_key"]) == 64
        assert first["manifest_url"] == "https://raw.githubusercontent.com/test/manifest/main/manifest.envelope.json"
        assert any("Nothing was uploaded" in message for message in messages)
        key_hash = hashlib.sha256(Path(config["key_file"]).read_bytes()).hexdigest()
        trusted = {config["key_id"]: bytes.fromhex(first["public_key"])}
        manifest = verify_release(first["release_dir"], trusted)
        assert manifest["schema_version"] == 3 and manifest["min_launcher_version"] == "0.3.0"
        assert manifest["initial_data"]["folders"] == ["Editor"]
        assert [f["path"] for f in manifest["initial_data"]["files"]] == ["Editor/default.cfg"]
        assert all(p["platform"] == "any" for p in manifest["packages"] if p["kind"] == "initial")
        assert "Example" in manifest["mods"]
        assert all(f["path"] != "bin/debug.log" for f in manifest["platforms"]["windows-x64"]["files"])
        assert any(f["path"] == "Profiles/official.dat" for f in manifest["platforms"]["linux-x64"]["files"])
        expect_error(config, "output already contains files")
        expect_error(dict(config, resume=True, public_key="00" * 32), "does not match")
        frozen = Path(first["release_dir"], "manifest.envelope.json").read_bytes()
        (root / "inputs/shared/Data/base.pak").write_bytes(b"changed base data")
        new_initial = b"updated editor defaults for new installs"
        (root / "inputs/initial/Editor/default.cfg").write_bytes(new_initial)
        publisher.run_job(dict(config, resume=True), report=lambda _: None)
        assert Path(first["release_dir"], "manifest.envelope.json").read_bytes() == frozen

        update = dict(config, first_time=False, public_key=first["public_key"], previous_release=first["release_dir"],
                      version="0.1.1", sequence="2")
        expect_error(dict(update, sequence="1"), "sequence must exceed")
        expect_error(dict(update, linux_root=""), "every previously supported platform")
        expect_error(dict(update, key_file=str(Path(keys) / "missing.pem")), "Private key not found")
        assert not (Path(keys) / "missing.pem").exists()
        second = publisher.run_job(update, report=lambda _: None)
        manifest = verify_release(second["release_dir"], trusted)
        assert manifest["initial_data"]["files"][0]["sha256"] == hashlib.sha256(new_initial).hexdigest()
        assert all(not f["path"].startswith("Editor/")
                   for platform in manifest["platforms"].values() for f in platform["files"])
        shared_patches = [p for p in manifest["packages"] if p["kind"] == "patch" and p["platform"] == "any"]
        assert len(shared_patches) == 1 and shared_patches[0]["files"][0]["path"] == "Data/base.pak"
        assert hashlib.sha256(Path(config["key_file"]).read_bytes()).hexdigest() == key_hash
        prior_package = next(Path(first["release_dir"]).glob("snapshot-*.zip"))
        original = prior_package.read_bytes()
        prior_package.write_bytes(b"corrupt")
        expect_error(dict(update, version="0.1.2", sequence="3"), "Corrupt package")
        prior_package.write_bytes(original)

        # Omitting the optional initial folder preserves the original publishing workflow.
        legacy = publisher.run_job(dict(config, shared_init_data="", version="0.2.0", sequence="3"), report=lambda _: None)
        assert "initial_data" not in verify_release(legacy["release_dir"], trusted)

        # Publish ordering and CMake environment use doubles, while dry runs still fully verify.
        original_publish = publisher.publish_github
        events = []
        def fake_publish(*args, **kwargs):
            events.append("dry-run" if kwargs.get("dry_run") else "publish")
            kwargs["dry_run"] = True
            return original_publish(*args, **kwargs)
        def fake_cmake(command, **kwargs):
            assert isinstance(command, list) and kwargs["shell"] is False
            assert "GITHUB_TOKEN" not in kwargs["env"] and "OPENBK2_SIGNING_KEY_FILE" not in kwargs["env"]
            events.append("cmake")
        with patch.dict(os.environ, {"GITHUB_TOKEN": "test-token-not-real", "OPENBK2_SIGNING_KEY_FILE": "secret"}), \
             patch.object(publisher, "publish_github", side_effect=fake_publish), \
             patch.object(publisher.subprocess, "run", side_effect=fake_cmake):
            published = publisher.run_job(dict(update, resume=True, publish=True, build_launcher=True), report=lambda _: None)
        assert published["published"] and events == ["dry-run", "cmake", "cmake", "cmake", "publish"]
        commands = publisher.launcher_commands(Path(config["source_dir"]), config, first["manifest_url"], first["public_key"])
        assert "-DOBK_FIXTURE_BUILD=OFF" in commands[0] and "-DOBK_BUILD_PUBLISHER=OFF" in commands[0]
        assert any(first["public_key"] in arg for arg in commands[0])
        events.clear()
        with patch.dict(os.environ, {"GITHUB_TOKEN": "test-token-not-real"}), \
             patch.object(publisher, "publish_github", side_effect=fake_publish), \
             patch.object(publisher.subprocess, "run", side_effect=subprocess.CalledProcessError(1, "cmake")):
            try:
                publisher.run_job(dict(update, resume=True, publish=True, build_launcher=True), report=lambda _: None)
            except subprocess.CalledProcessError:
                pass
            else:
                raise AssertionError("Failed launcher build must stop publication")
        assert events == ["dry-run"]

        if len(sys.argv) > 1:
            binary = str(Path(sys.argv[1]).resolve())
            environment = dict(os.environ, SDL_VIDEO_DRIVER="dummy", SDL_RENDER_DRIVER="software",
                               GITHUB_TOKEN="gui-test-token-not-real")
            gui_config = root / "gui-settings"
            request = root / "gui-request.json"
            request.write_text(json.dumps(dict(update, resume=True, publish=True, build_launcher=True)), encoding="utf-8")
            launched = subprocess.run([binary, "--config-dir", str(gui_config), "--check-job", str(request)],
                                      env=environment, timeout=60)
            if launched.returncode:
                log_file = gui_config / "last-run.log"
                raise AssertionError(log_file.read_text(encoding="utf-8") if log_file.exists() else
                                     f"GUI exited {launched.returncode} before writing a log; files: {list(gui_config.rglob('*'))}")
            saved = json.loads((gui_config / "publisher.json").read_bytes())
            assert saved["public_key"] == first["public_key"] and saved["resume"] is True
            assert saved["shared_init_data"] == config["shared_init_data"]
            results = list(gui_config.glob("jobs/*/result.json"))
            assert len(results) == 1
            result = json.loads(results[0].read_bytes())
            assert result["success"] and result["published"] is False
            assert "Local check complete" in (gui_config / "last-run.log").read_text(encoding="utf-8")
            for file in gui_config.rglob("*"):
                if file.is_file():
                    assert b"gui-test-token-not-real" not in file.read_bytes()
                    assert b"BEGIN PRIVATE KEY" not in file.read_bytes()
            request.write_text(json.dumps(dict(update, resume=True, public_key="00" * 32)), encoding="utf-8")
            failed = subprocess.run([binary, "--config-dir", str(gui_config), "--check-job", str(request)],
                                    env=environment, timeout=60)
            assert failed.returncode == 1
            assert "does not match" in (gui_config / "last-run.log").read_text(encoding="utf-8")
        print("Publisher workflow passed: initial data, first release, update, key reuse, resume, guards, CMake ordering, and GUI subprocess results.")


if __name__ == "__main__":
    main()
