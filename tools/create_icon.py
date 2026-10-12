"""Regenerate the launcher's original, embedded Windows icon (stdlib only)."""
from pathlib import Path
import struct

def main():
    size = 32
    pixels = bytearray()
    for y in range(size - 1, -1, -1):
        for x in range(size):
            ring = 6 <= x < 26 and 5 <= y < 27 and not (11 <= x < 21 and 10 <= y < 22)
            r, g, b = (74, 184, 139) if ring else (13, 24, 29)
            pixels.extend((b, g, r, 255))
    dib = struct.pack("<IiiHHIIiiII", 40, size, size*2, 1, 32, 0, len(pixels), 0, 0, 0, 0) + pixels + bytes(128)
    icon = struct.pack("<HHH", 0, 1, 1) + struct.pack("<BBBBHHII", size, size, 0, 0, 1, 32, len(dib), 22) + dib
    (Path(__file__).resolve().parents[1] / "assets/launcher.ico").write_bytes(icon)

if __name__ == "__main__":
    main()
