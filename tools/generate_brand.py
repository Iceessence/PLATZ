"""Generate PLATZ's Qt and Windows icons from the same capital P drawing."""

from pathlib import Path
import struct
import zlib


SIZES = (16, 24, 32, 48, 64, 128, 256)
MINT = (118, 227, 187)
INK = (18, 57, 47)


def mark(x, y):
    # Model units are 64 square. The stem, bowl and counter keep their widths
    # at the small Windows title-bar sizes.
    stem = 17 <= x <= 26 and 13 <= y <= 51
    bowl = 17 <= x <= 35 and 13 <= y <= 36.5
    bowl = bowl or (x >= 35 and ((x - 35) / 13) ** 2 + ((y - 24.75) / 11.75) ** 2 <= 1)
    counter = 26 < x <= 35 and 21 < y < 29
    counter = counter or (x >= 35 and ((x - 35) / 4.5) ** 2 + ((y - 25) / 4) ** 2 < 1)
    return (stem or bowl) and not counter


def inside_tile(x, y):
    dx = max(0, abs(x - 32) - 18)
    dy = max(0, abs(y - 32) - 18)
    return dx * dx + dy * dy <= 13 * 13


def pixels(size):
    samples = 4
    result = bytearray()
    for row in range(size):
        for column in range(size):
            coverage = dark = 0
            for sy in range(samples):
                for sx in range(samples):
                    x = (column + (sx + 0.5) / samples) * 64 / size
                    y = (row + (sy + 0.5) / samples) * 64 / size
                    if inside_tile(x, y):
                        coverage += 1
                        dark += mark(x, y)
            if coverage:
                result.extend(round((d * dark + m * (coverage - dark)) / coverage) for d, m in zip(INK, MINT))
                result.append(round(coverage * 255 / (samples * samples)))
            else:
                result.extend(b"\0\0\0\0")
    return result


def chunk(name, data):
    return struct.pack(">I", len(data)) + name + data + struct.pack(">I", zlib.crc32(name + data))


def png(size, rgba):
    stride = size * 4
    rows = b"".join(b"\0" + rgba[y * stride:(y + 1) * stride] for y in range(size))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def dib(size, rgba):
    # Windows' small-image DIBs are bottom-up BGRA followed by a 1-bit mask.
    stride = ((size + 31) // 32) * 4
    mask = bytearray(stride * size)
    bgra = bytearray()
    for y in range(size):
        for x in range(size):
            i = ((size - 1 - y) * size + x) * 4
            r, g, b, a = rgba[i:i + 4]
            bgra.extend((b, g, r, a))
            if a == 0:
                mask[y * stride + x // 8] |= 128 >> (x % 8)
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, len(bgra) + len(mask), 0, 0, 0, 0)
    return header + bgra + mask


def main():
    folder = Path(__file__).resolve().parents[1] / "src" / "studio" / "brand"
    folder.mkdir(exist_ok=True)
    directory = bytearray(struct.pack("<HHH", 0, 1, len(SIZES)))
    payload = bytearray()
    for size in SIZES:
        rgba = pixels(size)
        rendered = png(size, rgba)
        (folder / f"platz-{size}.png").write_bytes(rendered)
        icon_image = rendered if size == 256 else dib(size, rgba)
        dimension = size if size < 256 else 0
        directory.extend(struct.pack("<BBBBHHII", dimension, dimension, 0, 0, 1, 32,
                                     len(icon_image), 6 + 16 * len(SIZES) + len(payload)))
        payload.extend(icon_image)
    (folder / "PLATZ.ico").write_bytes(directory + payload)


if __name__ == "__main__":
    main()
