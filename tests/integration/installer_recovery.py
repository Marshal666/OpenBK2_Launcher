"""Synthetic end-to-end ownership, planning, update and crash recovery tests."""
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from test_workspace import temporary_workspace
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from fixture_builder import build_fixtures
from signing import sign_payload, stable_json, strict_json


def main():
    executable = str(Path(sys.argv[1]).resolve(strict=True))
    checks = 0
    with temporary_workspace() as root:
        key = build_fixtures(root / "fixtures")
        public = key.public_key().public_bytes_raw().hex()
        releases = {i: root / f"fixtures/release-{i}" for i in (1, 2, 3)}

        def command(install, seq=3, *, action="install", lang="en", platform="windows-x64", mode="update", fault=0):
            return [executable, action, str(install), str(releases[seq]), public, platform, lang, mode, str(fault)]

        def run(install, seq=3, *, expected=0, error=None, **kwargs):
            nonlocal checks
            result = subprocess.run(command(install, seq, **kwargs), capture_output=True, text=True, timeout=20, encoding="utf-8")
            assert result.returncode == expected, (kwargs, result.returncode, result.stdout, result.stderr)
            if error:
                assert error in result.stderr, result.stderr
            checks += 1
            return json.loads(result.stdout) if result.stdout.strip() else None

        seed = root / "seed"
        run(seed, 1)
        assert run(seed, action="verify")["sequence"] == 1
        orphan = seed / ".launcher/staging/interrupted-preparation/unused.bin"
        orphan.parent.mkdir(parents=True)
        orphan.write_bytes(b"disposable staging")
        retained = seed / ".launcher/backups/preserved-user-file"
        retained.write_bytes(b"never reclaim a backup speculatively")
        run(seed, action="recover")
        assert not orphan.exists() and retained.exists()
        (seed / "MODs").mkdir(exist_ok=True)
        (seed / "Profiles").mkdir(exist_ok=True)
        for path, data in {"MODs/personal.pak": b"personal mod", "Profiles/player.dat": b"saved profile",
                           "Data/untracked.pak": b"untracked data", "Editor/user.txt": b"editor user content",
                           "Data/texts-fr.pak": b"untracked language"}.items():
            (seed / path).write_bytes(data)

        def clone(name):
            destination = root / name
            shutil.copytree(seed, destination)
            return destination

        def protected(install):
            assert (install/"MODs/personal.pak").read_bytes() == b"personal mod"
            assert (install/"Profiles/player.dat").read_bytes() == b"saved profile"
            assert (install/"Data/untracked.pak").read_bytes() == b"untracked data"
            assert (install/"Data/texts-fr.pak").read_bytes() == b"untracked language"

        update = clone("update-1-2")
        result = run(update, 2)
        assert result["patch"] and not (update/"Data/removed.pak").exists()
        assert (update/"Data/added.pak").exists()
        assert run(update, action="verify")["sequence"] == 2
        protected(update)
        chain = clone("chain-1-3")
        plan = run(chain, action="plan")
        assert plan["patch"] and plan["packages"] == 2
        result = run(chain)
        assert result["patch"] and (chain/"Data/revised.pak").read_bytes() == b"changed at sequence 3\n"
        assert run(chain, action="verify")["sequence"] == 3
        protected(chain)
        fallback = clone("fallback")
        assert run(fallback, mode="missing-patch")["fallback"]
        assert run(fallback, action="verify")["sequence"] == 3

        language = clone("language")
        before = hashlib.sha256((language/"Data/base.pak").read_bytes()).hexdigest()
        run(language, 1, lang="de")
        assert (language/"Data/texts-de.pak").exists() and not (language/"Data/texts-en.pak").exists()
        assert run(language, action="verify")["language"] == "de"
        assert hashlib.sha256((language/"Data/base.pak").read_bytes()).hexdigest() == before
        protected(language)
        run(language, 1, lang="en", mode="missing-language", expected=1)
        assert run(language, action="verify")["language"] == "de"

        linux = root / "linux fixture with spaces"
        run(linux, 1, lang="de", platform="linux-x64")
        assert run(linux, action="verify")["language"] == "de"
        if os.name != "nt":
            assert (linux/"bin/Game").stat().st_mode & 0o111
        unicode_root = root / "Igre žčć 日本"
        run(unicode_root, 1)
        run(unicode_root, action="verify")

        repair = clone("repair")
        (repair/"Data/base.pak").write_bytes(b"modified data must survive in backup")
        (repair/"MODs/official.pak").write_bytes(b"modified official baseline")
        result = run(repair, 1, mode="repair")
        assert result["preserved"] == 2
        preserved = [p.read_bytes() for p in (repair/".launcher/backups").rglob("*.pak")]
        assert b"modified data must survive in backup" in preserved and b"modified official baseline" in preserved
        run(repair, action="verify")
        protected(repair)
        run(repair, 1, mode="reinstall")
        protected(repair)
        quota = clone("backup-quota")
        (quota/"Data/revised.pak").write_bytes(b"do not destroy me")
        run(quota, mode="backup-quota", expected=1, error="retention limit")
        assert (quota/"Data/revised.pak").read_bytes() == b"do not destroy me"

        adopt = root / "adopt"
        shutil.copytree(root/"fixtures/input-windows", adopt)
        (adopt/"Data/revised.pak").write_bytes(b"untracked conflict")
        assert run(adopt)["preserved"] >= 1
        assert any(p.read_bytes() == b"untracked conflict" for p in (adopt/".launcher/backups").rglob("*.pak"))
        assert (adopt/"MODs/user.pak").read_bytes() == b"must not be packaged"
        run(adopt, action="verify")

        forged = clone("forged-state")
        state_path = forged/".launcher/state.json"
        state = strict_json(state_path.read_bytes())
        state["managed_files"].append({"path": "Profiles/player.dat", "size": 13, "sha256": "0"*64})
        state_path.write_bytes(stable_json(state))
        run(forged)
        assert (forged/"Data/removed.pak").exists(), "Unauthenticated state authorized deletion"
        protected(forged)
        no_envelope = clone("missing-envelope")
        (no_envelope/".launcher/installed_manifest.envelope.json").unlink()
        run(no_envelope)
        assert (no_envelope/"Data/removed.pak").exists()
        protected(no_envelope)
        lost_metadata = clone("missing-all-metadata")
        (lost_metadata / ".launcher").rename(root / "retained-old-metadata")
        (lost_metadata / "Data/revised.pak").write_bytes(b"user edit without launcher metadata")
        run(lost_metadata)
        assert (lost_metadata / "Data/removed.pak").exists(), "Missing metadata authorized deletion"
        assert any(p.read_bytes() == b"user edit without launcher metadata"
                   for p in (lost_metadata / ".launcher/backups").rglob("*.pak"))
        protected(lost_metadata)
        run(lost_metadata, action="verify")
        run(chain, 1, expected=1, error="downgrade")
        no_space = clone("no-space")
        run(no_space, mode="no-space", expected=1, error="working space")
        assert run(no_space, action="verify")["sequence"] == 1
        disk_full = clone("disk-full")
        run(disk_full, mode="disk-full", expected=1, error="disk-full")
        assert run(disk_full, action="verify")["sequence"] == 1

        # Large or incomplete advertised chains must select snapshots.
        original_envelope = (releases[3]/"manifest.envelope.json").read_bytes()
        candidate = strict_json((releases[3]/"manifest.payload.json").read_bytes())
        try:
            larger = copy.deepcopy(candidate)
            for package in larger["packages"]:
                if package["kind"] == "patch":
                    package["size"] = 1000000
            (releases[3]/"manifest.envelope.json").write_bytes(sign_payload(stable_json(larger), key, "fixture-only"))
            assert not run(seed, action="plan")["patch"]
            incomplete = copy.deepcopy(candidate)
            for package in incomplete["packages"]:
                if package["kind"] == "patch":
                    for f in package["files"]:
                        if f["path"] == "Data/revised.pak":
                            f["sha256"] = "0"*64
            (releases[3]/"manifest.envelope.json").write_bytes(sign_payload(stable_json(incomplete), key, "fixture-only"))
            assert not run(seed, action="plan")["patch"]
            mutated = copy.deepcopy(candidate); mutated["release"]["created_utc"] = "2026-10-12T09:00:00Z"
            (releases[3]/"manifest.envelope.json").write_bytes(sign_payload(stable_json(mutated), key, "fixture-only"))
            run(chain, expected=1, error="mutation")
        finally:
            (releases[3]/"manifest.envelope.json").write_bytes(original_envelope)

        locked = clone("locked")
        proc = subprocess.Popen(command(locked, action="hold-lock"), stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        try:
            assert proc.stdout.readline().strip() == "locked"
            run(locked, expected=1)
        finally:
            proc.communicate("\n", timeout=10)
        assert run(locked, action="verify")["sequence"] == 1
        if os.name == "nt":
            import ctypes
            from ctypes import wintypes
            kernel = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel.CreateFileW.restype = wintypes.HANDLE
            kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
            kernel.CloseHandle.argtypes = [wintypes.HANDLE]
            game_lock = kernel.CreateFileW(str(locked/"bin/Game.exe"), 0x80000000, 1, None, 3, 0, None)
            assert game_lock != ctypes.c_void_p(-1).value
            try:
                run(locked, expected=1, error="OS error 32")
            finally:
                kernel.CloseHandle(game_lock)
            assert run(locked, action="verify")["sequence"] == 1

        # Count every instrumented write/flush/rename boundary on this route,
        # kill a fresh process at each boundary, and recover in another process.
        reference = clone("fault-reference")
        steps = run(reference)["steps"]
        pending_case = None
        for step in range(1, steps+1):
            case = clone(f"fault-{step}")
            run(case, fault=step, expected=86)
            journal = case/".launcher/transaction.json"
            if journal.exists() and pending_case is None:
                pending_case = root / "rollback-case"
                shutil.copytree(case, pending_case)
            run(case, action="recover")
            state = run(case, action="verify")
            assert state["sequence"] in {1, 3}
            protected(case)
            assert not journal.exists()
        assert pending_case is not None
        forged_journal = root / "forged-journal"
        shutil.copytree(pending_case, forged_journal)
        journal_path = forged_journal/".launcher/transaction.json"
        journal = strict_json(journal_path.read_bytes())
        journal["operations"].append({"path": "Profiles/player.dat", "action": "remove", "original_sha256": "", "original_size": 0})
        journal_path.write_bytes(stable_json(journal))
        run(forged_journal, action="recover", expected=1, error="not authorized")
        protected(forged_journal)
        # If prepared bytes are lost, restore prior files from durable backups.
        for path in (pending_case/".launcher/staging").rglob("revised.pak"):
            if "final" in path.parts:
                path.unlink()
        assert run(pending_case, action="recover")["rolled_back"]
        assert run(pending_case, action="verify")["sequence"] == 1
        protected(pending_case)
    print(f"{checks} installer assertions/process checks passed; {steps} crash boundaries recovered")


if __name__ == "__main__":
    main()
