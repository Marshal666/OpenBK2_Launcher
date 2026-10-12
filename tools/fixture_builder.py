"""Generate three tiny signed releases with an ephemeral in-memory test key."""
import argparse
import hashlib
from pathlib import Path
import random
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from release import build_release, sign_release
from signing import stable_json


def build_fixtures(destination, key=None, native_game=None):
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    key = key or Ed25519PrivateKey.generate()
    roots = {"windows-x64": destination / "input-windows", "linux-x64": destination / "input-linux"}
    languages = destination / "input-languages"
    languages.mkdir()
    (languages / "texts-en.pak").write_bytes(b"English fixture text\n")
    (languages / "texts-de.pak").write_bytes("Deutsche Testtexte\n".encode())
    rng = random.Random(12345)
    unchanged = rng.randbytes(128 * 1024)
    previous = None
    for sequence in (1, 2, 3):
        for platform, root in roots.items():
            for directory in ("bin", "Data", "MODs", "Profiles", "Editor"):
                (root / directory).mkdir(parents=True, exist_ok=True)
            game = root / "bin" / ("Game.exe" if platform == "windows-x64" else "Game")
            game.write_bytes(Path(native_game).read_bytes() if native_game else b"Synthetic file; execution fixture is supplied by the native test target.\n")
            game.chmod(0o755)
            (root / "Data/base.pak").write_bytes(unchanged)
            (root / "Data/revised.pak").write_bytes(f"changed at sequence {sequence}\n".encode())
            (root / "splash.bmp").write_bytes(b"fixture splash")
            (root / "Editor/editor.dat").write_bytes(b"editor")
            (root / "MODs/official.pak").write_bytes(f"official {sequence}".encode())
            (root / "MODs/user.pak").write_bytes(b"must not be packaged")
            (root / "Profiles/player.dat").write_bytes(b"must not be packaged")
            if sequence == 1:
                (root / "Data/removed.pak").write_bytes(b"removed at sequence 2")
            else:
                (root / "Data/removed.pak").unlink(missing_ok=True)
                (root / "Data/added.pak").write_bytes(b"added at sequence 2")
        out = destination / f"release-{sequence}"
        build_release(out=out, version=f"0.{sequence}.0", sequence=sequence, roots=roots,
                      languages_dir=languages, base_url=f"https://fixtures.invalid/releases/{sequence}",
                      previous=previous, allowlist=["MODs/official.pak"], created_utc="2026-10-12T08:00:00Z")
        sign_release(out, key, "fixture-only")
        previous = out
    (destination / "trusted-public-key.json").write_bytes(stable_json({"key_id": "fixture-only", "public_key": key.public_key().public_bytes_raw().hex()}))
    return key


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--native-game", type=Path)
    args = parser.parse_args()
    build_fixtures(args.out, native_game=args.native_game)
    print("Created fixture releases 1, 2, 3. No private key was written.")
