<p align="center">
  <img src="packaging/icons/gltron-faithful.png" alt="GLTron Faithful Remaster lightcycle icon" width="180" height="180">
</p>

# GLTron Faithful Remaster

A preservation-focused remaster of GLTron 0.70: the same lightcycles, rules,
cameras, local multiplayer, original artwork, and **Revenge of Cats** soundtrack,
with a modern SDL3 platform layer and the classic OpenGL renderer.

This checkout builds **0.70.1**. Release evidence and verification limits are
recorded in [the release checklist](docs/RELEASE_CHECKLIST.md).
The later **VulkanTron** project is intended for a new renderer and creative
changes; those changes are outside this faithful remaster.

## What has changed

- SDL3 window, input, and audio support. SDL1 and SDL2 remain buildable references.
- Resizable windows, borderless desktop fullscreen, and rendering at the real
  framebuffer resolution on HiDPI displays.
- Aspect-correct widescreen play with the classic single, stacked two-player,
  and four-way layouts. Menus and HUD keep their original proportions.
- Complete-frame PNG and BMP screenshots, including odd image widths.
- Stable saved key identifiers, bounded joystick hot-plug handling, and atomic,
  repeat-safe settings saves.
- Checked texture/font loading and fallback to original textures when an
  optional replacement cannot be loaded.
- A selectable `faithful` artpack with 4× upscales of the original textures and
  font atlases, retaining the original designs and font layout.
- CMake builds, relocatable installation assets, and source/binary TGZ packages.

Choose **Video → Artpack → faithful** for the upscaled artwork, or `default` for
the original images. The upscales interpolate existing pixels without inventing
new detail. Both packs keep the same game rules, music, models, and effects.
Their sources and exact regeneration method are recorded in
[asset provenance](docs/ASSET_PROVENANCE.md).

## Build and play

The currently verified target is **Linux x86-64**. You need CMake 3.21 or newer,
Ninja, a C/C++ compiler, pkg-config, SDL3 3.2 or newer, OpenGL development files,
libpng, zlib, and libmikmod. Lua 4.0.1 is bundled. On Arch Linux these dependencies
are available in `base-devel cmake ninja pkgconf sdl3 libglvnd libpng zlib libmikmod`.

```sh
git clone https://github.com/PandaByDesigner/gltron-faithful-remaster.git
cd gltron-faithful-remaster
cmake --preset release
cmake --build --preset release
ctest --preset release
./build/release/bin/gltron
```

Use the arrow keys and Enter to navigate menus. Start a game from the Game menu;
configure players and bindings there. Existing `.gltronrc` bindings keep their
original numbers.

| In-game key | Action |
| --- | --- |
| Space | Pause; press a gameplay key to resume |
| Escape | Return through the game/menu screens |
| F1 / F2 / F3 / F4 | Single / stacked / four-way / automatic view |
| F5 | Save settings |
| F10 | Cycle the four camera modes |
| F11 / F12 | Save a BMP / PNG screenshot |

Window/fullscreen and resolution choices are in **Video → Screen Options**.
Window size requests are subject to the desktop's window management policy;
a tiling compositor may choose a different size. Fullscreen uses the desktop
display mode. The original 4:3 menu composition stays centered on wide displays.

```sh
./build/release/bin/gltron --help
./build/release/bin/gltron --version
```

Preferences and screenshots default to your home directory. Testing or running
another copy can use separate directories:

```sh
mkdir -p /tmp/gltron-session/config /tmp/gltron-session/screenshots
GLTRON_CONFIG_DIR=/tmp/gltron-session/config \
GLTRON_SCREENSHOT_DIR=/tmp/gltron-session/screenshots \
./build/release/bin/gltron
```

`GLTRON_DATA_DIR` can select the root containing `art`, `data`, `music`, and
`scripts`. Normally the executable discovers installed assets relative to its
own location, then falls back to its source tree for development builds. It can
be launched from an unrelated working directory. An invalid explicit asset
override fails with a diagnostic.

## Install and package

To add **GLTron Faithful Remaster** to your Linux app menu with its own icon,
build the release first, then run:

```sh
python3 tools/install_user_launcher.py
```

The installer needs `desktop-file-utils` and installs a separate copy under
`~/.local/opt/gltron-faithful/<version>`, with a `gltron-faithful` command and app
entry. It leaves the original GLTron installation available. On first launch,
it copies existing `~/.gltronrc` controls into `~/.config/gltron-faithful` and
selects the faithful artpack in that copy. Later preferences remain independent;
screenshots go to `~/.local/share/gltron-faithful/screenshots`. The launcher
honors `GLTRON_CONFIG_DIR` and `GLTRON_SCREENSHOT_DIR` overrides and the XDG
configuration/data locations. Python is needed only to install the shortcut.

The installed copy includes the assets, copyright notices, and build
information. `cmake --install build/release --prefix <prefix>` remains available
for a conventional installation into a prefix you manage.

```sh
cmake --build build/release --target package
cmake --build build/release --target package_source
```

Archives and SHA256 files appear in `build/release`. The binary archive needs
the system runtime libraries and a working OpenGL driver; it is not a bundled
AppImage or a guarantee of compatibility with older Linux distributions.
Extract the entire archive and run its `bin/gltron`.

## Development and verification

```sh
cmake --preset asan
cmake --build --preset asan
ctest --preset asan
```

The sanitizer preset checks memory access and undefined behavior. Leak detection
is disabled in this preset because traced runners can prevent it from working.
The default CTest suite uses production gameplay, camera, layout, input,
settings, audio, and resource-loading code. It does not open a game window.

Native graphical gates explicitly opt in and require a real OpenGL session:

```sh
cmake --preset debug -DGLTRON_ENABLE_GRAPHICS_TESTS=ON
cmake --build --preset debug
ctest --test-dir build/debug -L graphics --output-on-failure
```

The native game gate uses private settings and captures under the build
directory, exercises menu/play/pause, all cameras, multiplayer and fullscreen,
and verifies two save/load cycles. The stricter window fixture requires a
desktop that permits requested window sizes. On the tested Hyprland session,
X11 passed that fixture; the Wayland compositor overrode its requested sizes.
Do not interpret that constrained fixture as a Wayland resize certification.
To use the verified X11 path on a Wayland desktop with XWayland available:

```sh
SDL_VIDEO_DRIVER=x11 ctest --test-dir build/debug -L graphics --output-on-failure
```

Reference presets `sdl1-reference` and `sdl2-reference` need the respective SDL
development packages. SDL1 audio also needs SDL_sound; SDL2/SDL3 use libmikmod
directly for the shipped Impulse Tracker song. `no-audio` builds without decoders.
Cross-backend scripts under `tests/` require all relevant reference dependencies.
The retained Autotools build is a historical reference; use CMake for SDL3.

[Faithful Remaster](docs/FAITHFUL_REMASTER.md) records the preservation contract
and earlier milestones. [Stabilization](docs/STABILIZATION.md) and
[baseline notes](docs/BASELINE.md) describe the original checkpoint.

## Credits and license

GLTron was created by Andreas Umbach and its original contributors. The original
README and in-game credits remain in this repository. **Revenge of Cats** is
copyright Peter Hajba (Skaven). Original artwork, models, fonts, and audio retain
their original authorship; this remaster does not claim them as new work.

The program is distributed under the GNU General Public License, version 2 or
later, without warranty; see [COPYING](COPYING). Bundled Lua retains its separate
[copyright and permission notice](packaging/LUA-COPYRIGHT.txt). GLTron is a fan
game; no official TRON affiliation is claimed.
