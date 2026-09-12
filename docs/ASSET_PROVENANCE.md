# Asset provenance for the faithful release

GLTron Faithful Remaster 0.70.1 retains the assets from this repository's
`upstream-0.70` reference. A byte-level Git comparison of `art/default`, `data`, and `music`
against that tag has no differences. This includes the original models, font
atlases and metadata, textures, WAV/OGG effects, and Impulse Tracker soundtrack.

## Derived faithful artpack

The separate `art/faithful` pack contains deterministic upscales of all 22
original PNG images. Select `faithful` under
**Video → Artpack**; select `default` for the original images. The derived pack
keeps the original designs and authorship. It contains no newly illustrated
logos, textures, or glyphs. Installing it leaves the selected artpack setting
unchanged.

Images are enlarged to four times their original width and height with Pillow's
Lanczos resampling. Images with transparency are resampled with premultiplied
alpha to avoid introducing color fringes around transparent edges. Font glyph
cells are enlarged separately so neighboring glyphs do not bleed into one
another. Fonts retain their original `.ftx` metadata through the existing
resource fallback, preserving logical glyph sizes, advances, and placement
in menus and the HUD.

The source checkout includes `tools/upscale_faithful_art.py` and
`tools/art-requirements.txt`, which pins Pillow to version 12.3.0. From the
checkout root, install the optional image tooling and verify the pack with:

```sh
python3 -m venv /tmp/gltron-art-tools
/tmp/gltron-art-tools/bin/python -m pip install -r tools/art-requirements.txt
/tmp/gltron-art-tools/bin/python tools/upscale_faithful_art.py --check
```

Omitting `--check` regenerates `art/faithful`. Its `manifest.txt` records source
and output dimensions, SHA256 hashes, and Pillow/zlib versions. These versions
identify the environment used to reproduce the PNG files. The pack's
`artpack.lua` is a marker containing comments; classic rendering settings and
font metadata continue to come from the retained resources.

Exact PNG byte checks also require the encoder recorded in the manifest;
Pillow wheels may bundle a different zlib or zlib-ng build. Normal game builds
use the checked-in PNGs and need neither Python nor Pillow.

Interpolation smooths existing image samples; it cannot recover missing source
detail or create authentic higher-resolution artwork. This pack is a derived
presentation option, not a newly authored replacement for the original art.
Models, music, sound effects, gameplay, and the original `default` pack remain
unchanged. Current runtime and package verification is recorded in
[RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md).

## Original credits and notices

The original credits name Andreas Umbach for GLTron; Nicolas Zimmermann,
Charles Babbage, Tracy Brown, Tyler Esselstrom, and Allen Bond for artwork;
Peter Hajba for music; and Damon Law for sound. The original README explicitly
credits **Revenge of Cats** to Peter Hajba (Skaven). The source does not provide
an author-to-file mapping for every texture or font; these are the inherited
credits, not newly asserted file-level authorship.

The program's GPL version 2-or-later notice remains in `README` and `COPYING`.
The bundled Lua 4.0.1 permission notice from `lua/include/lua.h` is also included
in binary packages as `licenses/LUA-COPYRIGHT.txt`. Original notices and credits
must accompany redistributions. A code license should not be described as a
new grant of ownership over third-party names or assets.

No TRON film soundtrack, film frames, or new third-party asset downloads have
been added for this release. A generated logo experiment was rejected for
changing the original design and is not included in the source or binary
package. The faithful upscales derive solely from the retained original assets;
resampling does not change their underlying attribution or notices.
