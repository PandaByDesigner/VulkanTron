#!/usr/bin/env python3
"""Reproduce the optional faithful artpack from the untouched original PNGs.

This is interpolation of existing artwork, not generation of new details.
The game and its build do not depend on Python or Pillow.
"""

import argparse
import hashlib
import io
from pathlib import Path
import sys

import PIL
from PIL import Image, features


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "art" / "default"
DESTINATION = ROOT / "art" / "faithful"
SCALE = 4
FONT_CELL = 32
FONT_ATLASES = {f"{font}.{page}.png" for font in ("babbage", "xenotron")
                for page in range(2)}
MARKER = ("-- Faithful 4x interpolation of art/default; see manifest.txt.\n"
          "-- Inherit the original art settings and logical font metadata.\n").encode()


def resize(image):
    """Filter stored channel values; premultiply transparency to avoid halos."""
    size = (image.width * SCALE, image.height * SCALE)
    if image.mode == "RGBA":
        return image.convert("RGBa").resize(size, Image.Resampling.LANCZOS).convert("RGBA")
    if image.mode == "RGB":
        return image.resize(size, Image.Resampling.LANCZOS)
    raise ValueError(f"Unexpected original image mode: {image.mode}")


def upscale(image, name):
    if name not in FONT_ATLASES:
        return resize(image)
    # Filtering a whole atlas mixes neighboring letters across cell boundaries.
    # Retain the original 8x8 layout and normalized UVs; scale each cell alone.
    if image.size != (256, 256):
        raise ValueError(f"Unexpected original font atlas dimensions: {name}")
    result = Image.new(image.mode, (image.width * SCALE, image.height * SCALE))
    for y in range(0, image.height, FONT_CELL):
        for x in range(0, image.width, FONT_CELL):
            cell = image.crop((x, y, x + FONT_CELL, y + FONT_CELL))
            result.paste(resize(cell), (x * SCALE, y * SCALE))
    return result


def encoded_png(image):
    buffer = io.BytesIO()
    image.save(buffer, format="PNG", compress_level=9, optimize=False)
    return buffer.getvalue()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="verify exact regeneration without changing any files")
    args = parser.parse_args()
    sources = sorted(SOURCE.glob("*.png"))
    if len(sources) != 22 or not FONT_ATLASES <= {p.name for p in sources}:
        parser.error("Expected the 22 original PNGs, including all four font atlases")
    outputs = {"artpack.lua": MARKER}
    manifest = [
        "GLTron faithful artpack: deterministic 4x interpolation",
        "Source: art/default from upstream-0.70; original authorship is retained.",
        "Method: Lanczos; premultiplied RGBA; separate 32x32 source font cells.",
        "Original font metadata, geometry, colors, music and art settings are retained.",
        "Interpolation smooths existing artwork; it does not reconstruct new detail.",
        f"Generator: tools/upscale_faithful_art.py; Pillow {PIL.__version__}; "
        f"PNG encoder zlib {features.version('zlib')}; "
        f"zlib-ng {features.version_feature('zlib_ng') if features.check_feature('zlib_ng') else 'disabled'}",
        "Columns: filename source_dimensions output_dimensions source_sha256 output_sha256",
    ]
    for path in sources:
        with Image.open(path) as image:
            image.load()
            result = upscale(image, path.name)
            outputs[path.name] = encoded_png(result)
            manifest.append(f"{path.name} {image.width}x{image.height} "
                            f"{result.width}x{result.height} {digest(path.read_bytes())} "
                            f"{digest(outputs[path.name])}")
    outputs["manifest.txt"] = ("\n".join(manifest) + "\n").encode()
    if args.check:
        mismatches = [name for name, data in outputs.items()
                      if not (DESTINATION / name).is_file()
                      or (DESTINATION / name).read_bytes() != data]
        if mismatches:
            print("Faithful artpack differs from regeneration: " + ", ".join(mismatches),
                  file=sys.stderr)
            print("Use the pinned Pillow version in tools/art-requirements.txt; "
                  "see manifest.txt for the PNG encoder version.", file=sys.stderr)
            return 1
        print(f"PASS: all {len(sources)} faithful PNGs, artpack marker and manifest reproduce exactly")
    else:
        DESTINATION.mkdir(parents=True, exist_ok=True)
        for name, data in outputs.items():
            (DESTINATION / name).write_bytes(data)
        print(f"Wrote {len(sources)} faithful PNGs and provenance to {DESTINATION}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
