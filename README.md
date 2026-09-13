# VulkanTron

<img src="packaging/icons/vulkantron.svg" alt="VulkanTron icon" width="128">

**GLTron's faithful remaster, rendered with direct Vulkan.** VulkanTron keeps
its original gameplay, menus, lightcycles, cameras, local multiplayer, artpacks,
fonts, effects, and music. SDL3 handles windows, input, controllers, and audio.
The renderer uses Vulkan directly; it does not use SDL_GPU or an OpenGL driver.

The preserved OpenGL remaster is included as a comparison executable. The
initial simplified Vulkan arena is now a separate development tool,
`vulkantron-lab`; the normal `vulkantron` executable runs the complete game.

Version **0.2.0** completes the faithful full-game port on the tested Linux
system. Release and sanitizer tests pass, and the native OpenGL/Vulkan
comparison covers both original and faithful artpacks. Three explosion
fixtures have documented coplanar depth differences; this is not a claim of
bit-identical rendering on every GPU.
See the [renderer architecture](docs/VULKANTRON_RENDERER.md),
[development plan](docs/VULKANTRON_PLAN.md), and
[verification report](docs/VULKANTRON_VERIFICATION.md) for scope and evidence.
Linux is the tested platform; other operating systems require their own ports
and verification.

## Build and play

Required: a C99/C++20 compiler, CMake 3.21 or newer, Ninja, pkg-config, SDL3 3.2
or newer, Vulkan headers and loader, `glslc`, `spirv-val`, libpng, zlib, and
libmikmod. Lua 4.0.1 is bundled. The shared build also needs OpenGL development
files for the retained reference executable.

The graphics baseline is Vulkan 1.3 with dynamic rendering and synchronization2.
The faithful renderer uses a depth/stencil attachment and swapchain transfer
support. Classic smooth lines use the optional line-rasterization extension;
unsupported requested features produce a clear error. The development target
is the NVIDIA GeForce GTX 1660. Broader GPU support requires separate testing.

On Arch Linux, `shaderc` supplies `glslc`, `spirv-tools` supplies `spirv-val`,
and `vulkan-validation-layers` supplies the optional Khronos validation layer.

```sh
./run-vulkantron.sh
```

The helper configures the release preset, builds the full game, and runs it.
It honors `VULKAN_SDK`; it also recognizes the optional user-local headers at
`~/.local/opt/vulkan-headers/1.4.357.0` on the development machine.
For a regular SDK installation, the equivalent commands are:

```sh
cmake --preset release
cmake --build --preset release
./build/release/bin/vulkantron
```

Use the original menus to start a game and configure controls, artpacks, music,
effects, cameras, and one-, two-, or four-player layouts. Useful options:

```sh
./build/release/bin/vulkantron --help
./build/release/bin/vulkantron --validation
./build/release/bin/vulkantron -i -8
```

`--validation` requires the Khronos validation layer and fails explicitly if
it is unavailable. `-i -8` requests a 1280×720 window; the desktop may constrain
its actual size. In game, F5 saves preferences, F11 saves BMP, and F12 saves PNG.

## App-menu installation and preferences

After a full release build, install a separate user-local copy and launcher:

```sh
python3 tools/install_vulkantron_launcher.py
```

The installer requires Python 3.11 or newer and `desktop-file-validate`. It
installs under `~/.local/opt/vulkantron`, adds `~/.local/bin/vulkantron`, and
creates the **VulkanTron** application entry with its own icon. It verifies
installed content and preserves previous versions. A fresh launcher profile
selects the optional faithful artwork; the original artpacks remain selectable.

Preferences default to `~/.config/vulkantron/.gltronrc`; screenshots default to
`~/.local/state/vulkantron/screenshots`. The corresponding XDG environment
variables are honored. VulkanTron does not import or overwrite the remaster's
profile. Advanced overrides are:

- `VULKANTRON_DATA_DIR`: directory containing `art`, `data`, `music`, and `scripts`.
- `VULKANTRON_CONFIG_DIR`: directory for `.gltronrc`.
- `VULKANTRON_SCREENSHOT_DIR`: screenshot directory.
- `VULKANTRON_SHADER_DIR`: directory containing the compiled faithful shaders.

`GLTRON_DATA_DIR` remains supported for shared test assets; `VULKANTRON_DATA_DIR`
takes precedence. Profiles use VulkanTron's variables above. Installed executables find
assets and shaders relative to their location and can run from another working
directory.

## Tests and renderer comparison

```sh
cmake --build --preset release
ctest --preset release
cmake --preset asan
cmake --build --preset asan
ctest --preset asan
```

The suite covers original gameplay, cameras, local layouts, input, settings,
assets, audio, and dynamic trails. Vulkan-specific tests cover matrix and light
state, geometry recording, texture versions and mip uploads, readback packing,
and frame capture/lifetime behavior. CPU-only tests do not certify GPU output.
The sanitizer preset enables AddressSanitizer and UndefinedBehaviorSanitizer;
whole-program leak checking is separate.

For a real desktop comparison, build both native harnesses and capture the same
production game with a fixed clock and seed. The image checker additionally
needs NumPy and Pillow; see `tools/renderer-check-requirements.txt`:

```sh
cmake --preset release -DGLTRON_BUILD_NATIVE_TESTS=ON
cmake --build --preset release
python3 tools/check_vulkantron_faithful.py \
  --opengl build/release/vulkantron-reference-smoke \
  --vulkan build/release/vulkantron-faithful-smoke \
  --assets . --output /tmp/vulkantron-comparison --driver x11
```

The output directory must be new. The checker first requires repeated OpenGL
runs to match exactly, then compares game state, visual state, and full-size
images across renderers. It records real window transitions, settings
roundtrips, Vulkan validation, and diagnostic differences. Image metrics still
require visual review. `--driver wayland` tests native Wayland separately.

Run the OpenGL reference with `./build/release/bin/gltron`. Its original
instructions are retained in the [faithful README](docs/FAITHFUL_REFERENCE_README.md).
That executable uses the original GLTron profile unless `GLTRON_CONFIG_DIR` is
set. Reference-only builds can set `-DVULKANTRON_BUILD_DIRECT_RENDERER=OFF`.

## Learning renderer development

The [architecture guide](docs/VULKANTRON_RENDERER.md) follows production draw
calls through the state recorder to Vulkan textures, pipelines, synchronization,
and presentation. The adapter evaluates the classic vertex pipeline on the
CPU and submits its output through Vulkan. It is a specific implementation of
this game's drawing needs, not a general OpenGL replacement or a universal
engine. Future optimization should preserve the same reference fixtures and
be justified by measurements.

The earlier lab remains available after a full build:

```sh
./build/release/bin/vulkantron-lab --demo --overview --validation
./build/release/bin/vulkantron-lab --self-test
```

Its simplified visuals and controls are development aids. The full game's
menus and original presentation live in `vulkantron`.

## Credits and license

GLTron was created by Andreas Umbach and its original contributors. Original
artwork, models, fonts, and audio retain their authorship. **Revenge of Cats**
is copyright Peter Hajba (Skaven). See [asset provenance](docs/ASSET_PROVENANCE.md),
the original [README](README), and the retained in-game credits.

The program is distributed under the GNU General Public License, version 2 or
later, without warranty; see [COPYING](COPYING). Bundled Lua retains its separate
[copyright and permission notice](packaging/LUA-COPYRIGHT.txt). VulkanTron is a
fan project; no official TRON affiliation is claimed. Renderer work does not
transfer ownership of the original game's assets.
