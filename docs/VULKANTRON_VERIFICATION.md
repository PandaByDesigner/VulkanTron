# VulkanTron 0.2.0 verification

Recorded 2026-09-13 for the complete faithful game, based on GLTron Faithful
Remaster `99d2f080ea2e9d3c1db693cb4f7c21354e9114a4`. The early simplified arena
has separate [historical evidence](VULKANTRON_BOOTSTRAP_VERIFICATION.md).
The installed `share/vulkantron/build-info.txt` identifies the source revision
and toolchain of each package; native invocation manifests also hash their
executables.

## Scope and platform

The production game, Lua menus, settings, input, audio and assets are compiled
into `vulkantron`. The graphics boundary uses the game-specific state recorder
and direct Vulkan backend described in [the architecture guide](VULKANTRON_RENDERER.md).
The OpenGL reference remains available as `gltron`; `vulkantron-lab` is a
separate development tool.

The tested system is Linux x86-64, NVIDIA GeForce GTX 1660, driver 610.57.04,
device API 1.4.341, Vulkan loader/validation 1.4.357, SDL3 3.4.16, and Hyprland
0.56.2. The renderer requests Vulkan 1.3 dynamic rendering and synchronization2.
This GPU provides the optional presentation fences, line rasterization and
negative-one-to-one depth-clip mode used by these tests. Depth/stencil was
24/8 bits in both renderers.

## Completed checks

| Area | Evidence |
| --- | --- |
| CPU regressions | All 19 Release tests and all 19 ASan/UBSan tests pass. They include the inherited gameplay, camera, layout, input, settings, assets, audio and trail tests, plus five Vulkan development/adapter/platform tests. |
| Recorder and capture lifetime | Tests cover matrix/lighting state, primitive conversion, texture versions/mips, bounded resources, screenshot packing, once-only submission, multiple readbacks, swap, skipped frames and renderer restart. Isolated adapter/platform leak checks pass; whole-program leak freedom is not claimed. |
| Direct renderer | Shader compilation and SPIR-V validation pass. ELF dependency and undefined-symbol inspection find no OpenGL rendering or SDL_GPU calls in the Vulkan executable. |
| Native X11 | Release full-game harness: 39 scenes for each of the default and faithful artpacks; game state, visual state, time and drawable sizes match the OpenGL reference exactly. |
| Native Wayland | The same 78 comparisons pass under ASan/UBSan. Logical and pixel dimensions differ by 2x on the HiDPI display. |
| Reference stability | Two independent OpenGL runs per artpack and window backend match decoded RGB pixels exactly before Vulkan comparison. |
| GPU correctness | Every final Vulkan harness run reports zero core/synchronization validation errors, zero adapter errors and successful explicit cleanup after 274 frames. No depth-mode environment override was set. |
| Window lifecycle | Actual fullscreen entry/exit, drawable changes and hide/show restoration are observed. The compositor does not perform requested minimization; see limits below. |
| Installed game | A relocated installation launches from `/tmp`, finds its own packaged assets/shaders, loads the faithful artpack and Revenge of Cats, and registers an active PipeWire playback node. |
| Native input and screenshots | Compositor-delivered menu, turn, boost, screenshot and save keys reach the installed production game. F11 BMP and F12 PNG files decode fully at 1600x1200 and 1920x1080; the application exits with zero Vulkan/adapter errors after 8,187 frames. |
| Profile isolation | The installed wrapper seeds its own faithful profile. F5 saves there; ambient classic profile/screenshot overrides are ignored and their canary contents remain unchanged. |
| Failure cleanup | An invalid faithful shader produces a clear failure and nonzero process exit, with zero Vulkan validation or cleanup errors. |
| Launcher | Repeated installation verifies complete content, preserves existing versions, and passes real GLib desktop launching even with spaces, backslashes, quotes, percent signs and shell punctuation in the prefix. Help/version do not create preferences. |

The shared gameplay sources, scripts, original art, models, fonts, music and
Lua are unchanged from the fork baseline. One inert `glVertex3f` outside any
primitive in `explosion.c` was removed from both renderer builds: it drew
nothing and only raised an OpenGL error. Other shared changes select the
Vulkan window/render boundary, application identity and private profile paths.

## Image comparison and visual review

Each run covers the root/game/video/detail/audio/key menus, key configuration,
all 95 printable font glyphs, credits, single/two/four-player views and pause,
four cameras, AI HUD, recognizer, stencil/simple shadows, transparent trails,
untextured floor and fog, early/late crashes, winner/draw messages, trails with
155/243/999/2005 segments, artpack reloads, resize and window restoration.
Captures retain the actual full framebuffer: normally 3792x2052 and fullscreen
3840x2160. Images are never resized, aligned, blurred or masked for acceptance.
Visual review supplements the numerical checks, including the installed 4:3
and widescreen game, menus, HUD, textures, shadows and remaining effect differences.

For each artpack on each window backend, **36 of 39** scenes pass the original
general image budget. **Three** (`crash-late`, `winner`, `draw-result`) pass the
explicit coplanar-effect policy below and remain labeled as exceeding the
general local-tile budget. All scene states match exactly.

| Raw image metric (8-bit RGB) | General limit | Three coplanar scenes | Maximum observed over final Vulkan comparisons |
| --- | ---: | ---: | ---: |
| Mean absolute channel error | 2.0 | 0.10 | 0.07459 |
| Pixels with any channel error >16 | 2% | 0.2% | 0.13961% |
| 99th-percentile maximum channel error | 32 | 32 | 0 |
| Worst 32x32 tile mean error | 12 | 18 | 16.01693 |

The original shockwave strips and impact spires overlap at the same depth.
Full-resolution inspection shows red/white depth-tie coverage differences in
that overlap. Triangulation, matrix composition, fused arithmetic and native
clip-depth experiments narrowed the cause to raster/depth rounding. The final
renderer preserves the original geometry and draw order, uses cached composed
matrices, and prefers native OpenGL-compatible clip depth when supported.
It does not hide the difference by offsetting geometry or changing effects.

The scoped policy tightens full-frame limits while allowing a slightly larger
local error in these three named fixtures. Reports retain every raw metric and
the original general-budget result. The checker's self-test confirms that a
missing small glyph still fails both policies and that no other scene receives
the exception. These are visually faithful comparisons, not bit-identical
OpenGL/Vulkan images.

## Reproducing the checks

```sh
cmake --preset release -DGLTRON_BUILD_NATIVE_TESTS=ON
cmake --build --preset release
ctest --preset release
cmake --preset asan -DGLTRON_BUILD_NATIVE_TESTS=ON
cmake --build --preset asan
ctest --preset asan
python3 tools/check_vulkantron_faithful.py --self-test
python3 tools/check_vulkantron_faithful.py \
  --opengl build/release/vulkantron-reference-smoke \
  --vulkan build/release/vulkantron-faithful-smoke \
  --assets . --driver x11 --output /tmp/vulkantron-x11-new
python3 tools/check_vulkantron_faithful.py \
  --opengl build/asan/vulkantron-reference-smoke \
  --vulkan build/asan/vulkantron-faithful-smoke \
  --assets . --driver wayland --output /tmp/vulkantron-wayland-new
```

Use a new output directory and a working native desktop. NumPy/Pillow are
required for the Python comparison. The checker supplies isolated profiles,
dummy audio, a fixed seed and clock, synchronization validation, timeouts,
manifest verification and final cleanup gates. Real PipeWire and keyboard
checks are separate from that deterministic harness. The final comparisons
reuse the already verified, exact OpenGL baseline captures; their provenance
files preserve the original invocations and hashes.

The development machine keeps the final reports, logs, full-resolution captures,
state manifests, timing samples and executable hashes under the project's
ignored `build/verification` directory. These local artifacts are not bundled
as source or runtime data.

## Performance and remaining limits

The release X11 fixture measures about 16.67 ms per warmed display/presentation
call on this 60 Hz desktop. This includes CPU work, command submission and
presentation waits; it is neither an isolated GPU timing nor evidence of an
OpenGL-to-Vulkan speedup. Sanitizer timings include heavy instrumentation.
The adapter evaluates legacy vertex transforms and lighting on the CPU, uses
one frame in flight, and prioritizes fidelity and inspectable resource lifetime.
Future optimization needs profiling and the same parity fixtures.

Native minimization is not certified because this compositor ignores that
request; hide/show recovery and a headless skipped-frame lifecycle are tested
separately. Physical controller hardware, other GPUs/drivers, other operating
systems and the optional presentation-fence fallback require separate native
verification. The supported release scope is the tested Linux system. CPU
regressions cover input translation, but do not replace controller hardware QA.
Whole-game ASan/UBSan runs disable LeakSanitizer; isolated adapter/platform
leak tests do not establish whole-program leak freedom.
