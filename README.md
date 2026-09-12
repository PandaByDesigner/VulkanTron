# VulkanTron

**An early direct-Vulkan port of GLTron, built to learn renderer development
while preserving the original game.** This checkout contains the `0.1.0-dev`
Vulkan prototype and the GLTron Faithful Remaster 0.70.1 OpenGL reference.

VulkanTron uses **Vulkan 1.3 directly**, with SDL3 providing the window, events,
input, and Vulkan surface. It does not use SDL_GPU. The first target is Linux;
other platforms and a complete faithful renderer port remain future work.

The current slice runs a human player against three original AIs, or four AIs
in demonstration mode. It reuses production classic physics, collision and
event handling, boost rules, random starts, and camera calculations. The scene
loads the original `data/lightcycle-high.obj` and its material file and copies
production trail segments. Floor, walls, grid lines, and trails have simplified
geometry and flat colors; opaque trail colors and CPU flat diffuse shading
approximate the classic appearance. This is a playable renderer foundation,
not completed visual parity.

Menus, textures and artpack selection, fog, full lighting and effects, audio,
split-screen presentation, configurable bindings, and saved preferences are
deferred. The complete OpenGL reference remains available separately in this
checkout. The [development plan](docs/VULKANTRON_PLAN.md) records the accepted
scope and the checks needed before claiming parity.
The [initial verification report](docs/VULKANTRON_VERIFICATION.md) records the
headless and native GPU results, including their limits.

## Build and run

You need a C99/C++20 compiler, CMake 3.21 or newer, Ninja, pkg-config, SDL3 3.2 or
newer, Vulkan development headers and loader supporting at least Vulkan 1.3,
`glslc`, and `spirv-val`. The selected GPU and driver must support Vulkan 1.3,
dynamic rendering, and synchronization2. Install the Khronos validation layer
to use `--validation`; requesting unavailable validation fails explicitly.

The shared build retains the remaster's dependencies: OpenGL development files,
libpng, zlib, and libmikmod for its default audio-enabled configuration. Lua
4.0.1 is bundled. On Arch Linux, `shaderc` provides `glslc`, `spirv-tools`
provides `spirv-val`, and `vulkan-validation-layers` provides validation.

From the project root:

```sh
./run-vulkantron.sh --validation
```

The helper configures the `release` preset, builds the `vulkantron` target, and
runs it. After building, the executable is `./build/release/bin/vulkantron`.
It honors `VULKAN_SDK` and can use the optional signed headers installed locally
under `~/.local/opt/vulkan-headers/1.4.357.0` on the development machine.
No GLTron preferences are read or written by the Vulkan prototype.

```sh
./build/release/bin/vulkantron --demo --overview --validation
./build/release/bin/vulkantron --help
./build/release/bin/vulkantron --version
```

Starting defaults match classic configuration values: a 720-unit arena, speed
8.5, boost enabled, and level-2 AI. Player one is human unless `--demo` is
supplied. Fast finish is disabled so an AI demonstration advances at normal
speed. Resetting uses the same seed, `12313` by default.

| Key | Action |
| --- | --- |
| Left / Right or A / D | Turn relative to your current heading |
| Either Shift key | Hold to boost |
| Space | Pause or resume |
| R | Restart the round with the selected seed |
| F10 | Cycle the original camera modes |
| Tab | Toggle arena overview |
| F11 | Toggle desktop fullscreen |
| F12 | Save a new PNG in the current directory |
| Escape | Quit |

Losing window focus pauses interactive play and releases boost. The prototype
starts with a 1280×720 resizable window and uses its actual drawable size.

For a bounded capture, choose a path that does not already exist:

```sh
./build/release/bin/vulkantron --demo --validation --fixed-step \
  --frames 120 --seed 12313 --capture /tmp/vulkantron-120.png
```

`--frames N` exits after N rendered frames. `--fixed-step` requests 20 ms of
simulation per rendered frame and requires `--frames`. An out-of-date or
temporarily unavailable drawable retries the same simulation state. `--capture NEW.png`
also requires `--frames` and saves the final rendered frame. Pause and restart
are disabled in bounded runs. A fixed seed and stepping schedule support state
comparisons; they do not by themselves certify identical pixels or presentation
timing across drivers.

Other options are `--width N`, `--height N`, `--assets DIR`, and `--shaders DIR`.
The asset root contains `data`, `art`, `scripts`, and `music`; the shader
directory contains compiled `scene.vert.spv` and `scene.frag.spv`. Development
builds discover these locations automatically. The inherited assets remain in
the repository even where the prototype does not render them.

## Tests and the OpenGL reference

Build all targets before running the full test suite:

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
```

The suite retains the remaster's headless regressions and adds bridge and scene
tests plus `vulkantron --self-test`. The bridge test exercises production input events,
boost, deterministic resets and AI, camera independence, trail growth beyond
1,000 turns, and cleanup. The scene self-test checks finite geometry and camera
matrices at multiple aspect ratios and verifies that scene construction leaves
gameplay state unchanged. These tests do not initialize a Vulkan device or
certify native rendering.

```sh
cmake --preset asan
cmake --build --preset asan
ctest --preset asan
```

The sanitizer preset enables AddressSanitizer and UndefinedBehaviorSanitizer.
It disables leak detection because traced runners can prevent LeakSanitizer
from operating. Native GPU validation and presentation checks require a real
graphical session; their results must be recorded separately from headless tests.

To play the retained faithful remaster after a full build:

```sh
./build/release/bin/gltron
```

Its original menus, artpacks, music, multiplayer layouts, controls, and saved
settings are described in the [archived faithful README](docs/FAITHFUL_REFERENCE_README.md).
That executable uses OpenGL and can read/write the original GLTron preferences;
the archived instructions explain how to isolate its settings for comparisons.
To configure only that reference without the Vulkan development dependencies,
use `cmake --preset release -DVULKANTRON_BUILD_DIRECT_RENDERER=OFF`; set the
option back to `ON` when returning to Vulkan work.

The first development machine has a GeForce GTX 1660 whose driver reports
Vulkan 1.4.341. The prototype deliberately targets Vulkan 1.3. Device capability
is not a native test result or a guarantee for other hardware. The inherited
[release checklist](docs/RELEASE_CHECKLIST.md) records the OpenGL remaster's
verification, not completion of this Vulkan port.

## Credits and license

GLTron was created by Andreas Umbach and its original contributors. Original
artwork, models, fonts, and audio retain their authorship. **Revenge of Cats**
is copyright Peter Hajba (Skaven); the retained track is not yet played by the
Vulkan prototype. See [asset provenance](docs/ASSET_PROVENANCE.md), the original
[README](README), and the retained in-game credits.

The program is distributed under the GNU General Public License, version 2 or
later, without warranty; see [COPYING](COPYING). Bundled Lua retains its separate
[copyright and permission notice](packaging/LUA-COPYRIGHT.txt). VulkanTron is a
fan project; no official TRON affiliation is claimed. Renderer work does not
transfer ownership of the original game's assets.
