"""Native updater/mod acceptance, using only synthetic signed releases."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
from shared_fixtures import build_shared_fixtures
from test_workspace import temporary_workspace
from release import build_release, sign_release, verify_release
from schema import validate_payload
from signing import sign_payload, stable_json
from github_publication import FakeGitHub
from github_publish import publish_github


def main():
    installer, mod_driver, manifest_driver = [str(Path(p).resolve(strict=True)) for p in sys.argv[1:4]]
    checks = 0
    skipped = []
    with temporary_workspace() as temp:
        source = temp / "fixtures"
        key, releases = build_shared_fixtures(source)
        public = key.public_key().public_bytes_raw().hex()
        manifests = [verify_release(r, {"fixture-only": bytes.fromhex(public)}) for r in releases]

        def run(args, code=0):
            nonlocal checks
            result = subprocess.run(list(map(str, args)), capture_output=True, text=True, timeout=30)
            assert result.returncode == code, (args, result.returncode, result.stdout, result.stderr)
            checks += 1
            return result.stdout.strip()

        def install(root, release, platform="windows-x64", mode="update", command="install", code=0):
            return run([installer, command, root, release, public, platform, "en", mode], code)

        def mod(root, command="download", mid="Alpha", release=None, mode="normal", fault=0, code=0):
            return run([mod_driver, command, root, release or releases[0], public, mid, mode, fault], code)

        # Both targets reference the same shared ZIP, but their executables differ.
        first = manifests[0]
        shared = [p for p in first["packages"] if p["kind"] == "snapshot" and p["platform"] == "any"]
        assert len(shared) == 1
        for target in first["platforms"].values():
            assert shared[0]["id"] in target["snapshot_package_ids"]
        assert all(p["platform"] == "any" for p in first["packages"] if p["kind"] == "mod")
        for sequence, (release, manifest) in enumerate(zip(releases, manifests), 1):
            run([manifest_driver, release / "manifest.envelope.json", "fixture-only", public])
            if sequence > 1:
                edges = [t["update_edges"][-1]["package_ids"] for t in manifest["platforms"].values()]
                assert edges[0] == edges[1] and len(edges[0]) == 1
        for platform in ("windows-x64", "linux-x64"):
            root = temp / platform
            install(root, releases[0], platform)
            assert not (root / "MODs/Alpha").exists()
            assert (root / "bin/Game.exe").exists() == (platform == "windows-x64")
            assert (root / "bin/Game").exists() == (platform == "linux-x64")
            result = json.loads(install(root, releases[4], platform))
            assert result["patch"]  # Includes repeated shared content in the route.
            assert (root / "Data/revised.pak").read_bytes() == b"3"
            install(root, releases[4], platform, command="verify")
        fallback = temp / "fallback"
        install(fallback, releases[0])
        assert json.loads(install(fallback, releases[2], mode="missing-patch"))["fallback"]

        # Publisher rejects ambiguous shared/platform paths and linked inputs.
        def build(out, **extra):
            return build_release(out=out, version="0.2.6", sequence=6,
                roots={"windows-x64": source / "windows", "linux-x64": source / "linux"},
                shared_root=source / "shared", languages_dir=source / "languages", mods_dir=source / "mods",
                base_url="https://example.com/v6", **extra)

        duplicate = source / "windows/Data/common.pak"
        duplicate.parent.mkdir()
        duplicate.write_bytes(b"collision")
        try:
            build(temp / "overlap")
            raise AssertionError("Shared/platform collision accepted")
        except ValueError:
            checks += 1
        duplicate.unlink()

        # Upgrade an asymmetric legacy v1 release: one platform needs a shared
        # file patch and the other already has its bytes. No duplicate root data
        # is needed in the new publisher inputs.
        legacy_roots = {}
        for platform, name in (("windows-x64", "windows"), ("linux-x64", "linux")):
            destination = temp / ("legacy-" + name)
            shutil.copytree(source / name, destination)
            shutil.copytree(source / "shared", destination, dirs_exist_ok=True)
            legacy_roots[platform] = destination
        (legacy_roots["windows-x64"] / "Data/common.pak").write_bytes(b"old Windows-only bytes")
        legacy = temp / "legacy-release"
        build_release(out=legacy, version="0.1.0", sequence=1, roots=legacy_roots,
                      languages_dir=source / "languages", base_url="https://example.com/legacy")
        sign_release(legacy, key, "fixture-only")
        upgraded = temp / "upgraded"
        upgraded_manifest = build(upgraded, previous=legacy)
        sign_release(upgraded, key, "fixture-only")
        assert len(upgraded_manifest["platforms"]["windows-x64"]["update_edges"][0]["package_ids"]) == 1
        assert upgraded_manifest["platforms"]["linux-x64"]["update_edges"][0]["package_ids"] == []
        for platform in legacy_roots:
            game_root = temp / ("migrate-" + platform)
            install(game_root, legacy, platform)
            install(game_root, upgraded, platform)
            install(game_root, upgraded, platform, command="verify")

        # Publication uploads each shared asset once and verifies every mod
        # asset before making the new signed manifest live (offline transport).
        published = copy.deepcopy(manifests[2])
        for package in published["packages"]:
            package["url"] = package["url"].replace("https://example.com/", "https://github.com/test/game/releases/download/")
        (releases[2] / "manifest.payload.json").write_bytes(stable_json(published))
        sign_release(releases[2], key, "fixture-only")
        fake = FakeGitHub(releases[2])
        publish_github(releases[2], {"fixture-only": bytes.fromhex(public)}, transport=fake,
                       owner="test", repo="game", tag="v3", manifest_repo="test/manifests", manifest_branch="main")
        assert fake.events.count("upload") == sum("/v3/" in p["url"] for p in published["packages"])
        assert fake.events[-2:] == ["update fixed manifest", "verify fixed manifest"]
        checks += 1

        # Python and C++ must reject the same signed malformed v2 payloads.
        def reject(change):
            nonlocal checks
            candidate = copy.deepcopy(first)
            change(candidate)
            try:
                validate_payload(candidate)
                raise AssertionError("Publisher accepted invalid catalog")
            except (ValueError, KeyError):
                checks += 1
            envelope = temp / "invalid.envelope.json"
            envelope.write_bytes(sign_payload(stable_json(candidate), key, "fixture-only"))
            run([manifest_driver, envelope, "fixture-only", public], 1)

        reject(lambda m: m.update(schema_version=3))
        reject(lambda m: m.update(min_launcher_version="0.1.0"))
        reject(lambda m: m["mods"]["Alpha"]["package_ids"].clear())
        reject(lambda m: m["mods"]["Alpha"]["files"][0].update(path="MODs/Beta/escape.pak"))
        reject(lambda m: m["mods"].update(alpha=copy.deepcopy(m["mods"]["Alpha"])))
        reject(lambda m: m["mods"]["Alpha"]["package_ids"].append(m["mods"]["Alpha"]["package_ids"][0]))
        reject(lambda m: next(p for p in m["packages"] if p["kind"] == "mod").update(platform="windows-x64"))
        reject(lambda m: m.update(schema_version=1))

        root = temp / "mods-game"
        # Base installation must not even fetch optional mod packages.
        mod_assets = [releases[0] / (p["id"] + ".zip") for p in first["packages"] if p["kind"] == "mod"]
        for asset in mod_assets:
            asset.rename(asset.with_suffix(".held"))
        install(root, releases[0])
        for asset in mod_assets:
            asset.with_suffix(".held").rename(asset)
        assert not (root / "MODs").exists()
        for mode in ("cancel", "missing", "partial", "cancel-extract"):
            mod(root, mode=mode, code=1)
            assert not (root / "MODs/Alpha").exists()
            install(root, releases[0], command="recover")
        original = mod_assets[0].read_bytes()
        mod_assets[0].write_bytes(original[:-8])
        mod(root, code=1)
        assert not (root / "MODs/Alpha").exists()
        mod_assets[0].write_bytes(original)
        mod(root, mid="unknown", code=1)
        mod(root, release=releases[1], code=1)  # Update game before catalog changes.
        assert mod(root) == "downloaded 2"
        assert mod(root, command="list") == "Alpha removable"
        mod(root, mid="Beta", code=1)
        edited = root / "MODs/Alpha/assets/0.pak"
        edited.write_bytes(b"player edit")
        extra = root / "MODs/Alpha/player.cfg"
        extra.write_bytes(b"player settings")
        install(root, releases[0], mode="repair")
        assert edited.read_bytes() == b"player edit" and extra.exists()
        backup = root / mod(root, command="remove")
        assert (backup / "assets/0.pak").read_bytes() == b"player edit"
        assert (backup / "player.cfg").read_bytes() == b"player settings"
        assert not (root / "MODs/Alpha").exists()
        mod(root, command="remove", mid="../Data", code=1)
        mod(root, command="remove", mid="Alpha/assets", code=1)
        mod(root, mid="Beta")
        install(root, releases[0], command="verify")
        mod(root, command="remove", mid="Beta")

        # Faults immediately before/after each atomic move leave a whole folder
        # on exactly one side of the move. Recovery may discard only staging.
        for fault in (1, 2):
            crash_root = temp / f"crash-{fault}"
            install(crash_root, releases[0])
            mod(crash_root, fault=fault, code=86)
            install(crash_root, releases[0], command="recover")
            assert (crash_root / "MODs/Alpha").exists() == (fault == 2)
            if fault == 1:
                mod(crash_root)
            mod(crash_root, command="remove", fault=fault, code=86)
            assert (crash_root / "MODs/Alpha").exists() == (fault == 1)
            backups = list((crash_root / ".launcher/backups").glob("removed-mods/*/Alpha"))
            assert len(backups) == (1 if fault == 2 else 0)
            install(crash_root, releases[0], command="verify")

        # Competing updater lock blocks both operations.
        holder = subprocess.Popen([installer, "hold-lock", str(root), str(releases[0]), public, "windows-x64", "en", "update"],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        try:
            assert holder.stdout.readline().strip() == "locked"
            mod(root, code=1)
            mod(root, command="remove", code=1)
        finally:
            holder.communicate("\n", timeout=10)
        # Explicitly selected local folders can be removed while retaining edits.
        local = root / "MODs/Local mod"
        local.mkdir()
        (local / "save.txt").write_bytes(b"local save")
        mod(root, code=1)
        backup = root / mod(root, command="remove", mid="Local mod")
        assert (backup / "save.txt").read_bytes() == b"local save"
        outside = temp / "outside"
        outside.mkdir()
        (outside / "keep.txt").write_bytes(b"keep")
        hardlinked = root / "MODs/Hardlinked"
        hardlinked.mkdir()
        (hardlinked / "linked.txt").hardlink_to(outside / "keep.txt")
        mod(root, command="remove", mid="Hardlinked", code=1)
        assert (outside / "keep.txt").read_bytes() == b"keep"
        (hardlinked / "linked.txt").unlink()
        hardlinked.rmdir()
        try:
            (root / "MODs/Linked").symlink_to(outside, target_is_directory=True)
        except OSError:
            skipped.append("directory symlink creation unavailable")
        else:
            mod(root, code=1)
            mod(root, command="remove", mid="Linked", code=1)
            assert (outside / "keep.txt").read_bytes() == b"keep"
            (root / "MODs/Linked").unlink()

        # Exact official MODs opt-ins remain protected base content.
        official = source / "shared/MODs/Official/content.pak"
        official.parent.mkdir(parents=True)
        official.write_bytes(b"base-owned")
        protected_release = temp / "protected"
        build(protected_release, allowlist=["MODs/Official/content.pak"])
        sign_release(protected_release, key, "fixture-only")
        install(root, protected_release)
        assert mod(root, command="list") == "Official protected"
        mod(root, command="remove", mid="Official", code=1)
        assert (root / "MODs/Official/content.pak").read_bytes() == b"base-owned"
        overlap = source / "shared/MODs/Alpha/official.txt"
        overlap.parent.mkdir(parents=True)
        overlap.write_bytes(b"illegal mixed ownership")
        try:
            build(temp / "mixed-owner", allowlist=["MODs/Alpha/official.txt"])
            raise AssertionError("Base and optional mod can own the same folder")
        except ValueError:
            checks += 1

    print(json.dumps({"shared_data_and_mod_checks": checks, "skipped": skipped}))


if __name__ == "__main__":
    main()
