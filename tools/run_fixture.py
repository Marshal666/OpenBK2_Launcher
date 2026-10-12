"""Open an isolated signed demo in the real launcher; developer use only."""
import argparse
import json
from pathlib import Path
import subprocess
from fixture_builder import build_fixtures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--launcher", required=True, type=Path)
    parser.add_argument("--game-binary", required=True, type=Path)
    parser.add_argument("--directory", type=Path, default=Path(__file__).resolve().parents[1] / "build/demo")
    args = parser.parse_args()
    root = args.directory.resolve()
    fixtures, config, game = root / "fixtures", root / "config", root / "game"
    if not fixtures.exists():
        build_fixtures(fixtures, native_game=args.game_binary.resolve(strict=True))
    public = json.loads((fixtures / "trusted-public-key.json").read_text(encoding="utf-8"))["public_key"]
    config.mkdir(parents=True, exist_ok=True)
    if not (config / "settings.json").exists():
        settings = {"settings_schema_version": 1, "installation_root": str(game), "requested_language": "en",
                    "configured": False, "relocate": False, "shortcut": False}
        (config / "settings.json").write_text(json.dumps(settings), encoding="utf-8")
    print("Fixture game/config directory:", root, flush=True)
    return subprocess.call([str(args.launcher.resolve(strict=True)), "--fixture-release", str(fixtures / "release-3"),
                            "--fixture-public-key", public, "--config-dir", str(config), "--root", str(game)])


if __name__ == "__main__":
    raise SystemExit(main())
