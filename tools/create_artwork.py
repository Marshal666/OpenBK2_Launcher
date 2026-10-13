"""Prepare the embedded hero BMP from res/launcher_image.png using Pillow."""
from pathlib import Path
from PIL import Image


def main():
    root = Path(__file__).resolve().parents[1]
    with Image.open(root / "res/launcher_image.png") as source:
        image = source.convert("RGBA")
    # SDL already reads BMP: retain the original pixels and dimensions without
    # adding a PNG decoder to the launcher. Opaque artwork needs only 24 bits.
    if image.getchannel("A").getextrema() == (255, 255):
        image = image.convert("RGB")
    target = root / "assets/hero.bmp"
    image.save(target, format="BMP")
    print(f"Embedded artwork: {image.width}x{image.height}, {target.stat().st_size} bytes")


if __name__ == "__main__":
    main()
