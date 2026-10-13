"""Synthetic schema-v2 shared-data and optional-mod releases (no game download)."""
from pathlib import Path
import random
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from release import build_release, sign_release
from signing import stable_json


def build_shared_fixtures(root, game=None, count=5, hold_game=False):
    root = Path(root)
    key = Ed25519PrivateKey.generate()
    shared, windows, linux, languages, mods = [root / name for name in ("shared", "windows", "linux", "languages", "mods")]

    def write(path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    write(shared / "Data/common.pak", random.Random(4).randbytes(128 * 1024))
    write(windows / "bin/Game.exe", b"synthetic Windows executable")
    write(linux / "bin/Game", b"synthetic Linux executable")
    if game:
        write((windows if sys.platform == "win32" else linux) / ("bin/Game.exe" if sys.platform == "win32" else "bin/Game"), Path(game).read_bytes())
    (linux / "bin/Game").chmod(0o755)
    write(languages / "texts-en.pak", b"English fixture")
    for i in range(3):
        write(mods / f"Alpha/assets/{i}.pak", random.Random(i).randbytes(100 * 1024))
    write(mods / "Beta/readme.txt", b"A different mod")
    releases = []
    for sequence in range(1, count + 1):
        # Repeated content in releases 2/4 and 3/5 exercises shared patch reuse.
        write(shared / "Data/revised.pak", str(sequence if sequence < 4 else sequence - 2).encode())
        out = root / f"release-{sequence}"
        manifest = build_release(out=out, version=f"0.2.{sequence}", sequence=sequence,
                      roots={"windows-x64": windows, "linux-x64": linux}, shared_root=shared,
                      languages_dir=languages, mods_dir=mods, base_url=f"https://example.com/v{sequence}",
                      previous=releases[-1] if releases else None,
                      package_limit=300 * 1024 if not game else 8 * 1024 * 1024)
        if hold_game:
            for target in manifest["platforms"].values():
                target["launch"]["args"] = ["--wait-for-file", "fixture-stop"]
            (out / "manifest.payload.json").write_bytes(stable_json(manifest))
        sign_release(out, key, "fixture-only")
        releases.append(out)
    (root / "public-key.txt").write_text(key.public_key().public_bytes_raw().hex())
    return key, releases
