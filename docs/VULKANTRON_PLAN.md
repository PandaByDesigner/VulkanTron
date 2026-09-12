# VulkanTron development plan

## Accepted direction

Build a faithful port of GLTron with **direct Vulkan**, using **SDL3 for the
platform layer**. The user chose direct Vulkan deliberately to understand
graphics programming and control GPU resources, synchronization, and rendering.
SDL_GPU is not the renderer abstraction for this project.

VulkanTron is a separate project built from the completed GLTron Faithful
Remaster. Keep that OpenGL implementation runnable as the preservation
reference. Reuse its game rules and content while replacing how the scene is
drawn. The present milestone does not authorize changing physics, AI, music,
art direction, or the original local multiplayer topology.

The first target is Linux on the available GeForce GTX 1660. Its driver reports
Vulkan 1.4.341; the renderer baseline is Vulkan 1.3. Additional hardware and
operating systems require their own capability and behavior checks.

## First milestone: playable direct-Vulkan foundation

The initial slice provides one visible arena with a human player and three AIs,
plus a four-AI demonstration. It includes deterministic seeded resets, relative
turns, boost, pause, original camera calculations, an overview, a resizable SDL3
window, fullscreen switching, and PNG capture.

`vulkantron/renderer.cpp` owns the Vulkan device, resources and memory, depth
buffer, graphics pipeline, commands, submission and presentation. It uses Vulkan
1.3 dynamic rendering and synchronization2, with one frame in flight and a
present semaphore per swapchain image. SDL3 supplies the window and surface.
Shaders compile to SPIR-V and pass `spirv-val` as part of the build. Validation
is opt-in and an unavailable requested layer is an error.

The classic bridge links these production sources without initializing the
OpenGL display, Lua configuration, or audio subsystems:

- `src/game/engine.c`, `event.c`, `computer.c`, and `computer_utilities.c` for
  starts, simulation, AI, collision, event handling, and boost.
- `src/game/camera.c`, `globals.c`, and `src/video/player_visual.c` for the
  original cameras, common state, and reset of visual state used by gameplay.
- `nebu/base/vector.c`, `random.c`, and `util.c` for the original supporting math
  and random-number behavior.

`vulkantron/classic_bridge.c` adapts platform/configuration hooks and exposes
owned copies of positions, camera eye/target values, and trail segments through
`classic_bridge.h`. It does not copy or replace the physics implementation.
The bridge is single-instance and single-threaded. Snapshot pointers expire
at the next mutating bridge call or shutdown; a failed allocation returns no
snapshot. Bridge allocations are checked, while inherited production allocation
failure behavior remains a separate limitation.

The configuration adapter uses classic speed 8.5, arena size 720, enabled boost,
and AI level 2. It supplies the original player colors from `scripts/artpack.lua`.
Fast finish is intentionally disabled for a demonstration at normal speed.
Audio hooks are silent and recognizer flight is a placeholder. The app advances
the actual simulation in 20 ms steps; its wall-clock accumulator caps long
stalls, and the bridge also bounds an individual elapsed-time call to 250 ms.
This scheduling policy must be held constant in renderer comparisons.

`vulkantron/scene.cpp` loads `data/lightcycle-high.obj` and `lightcycle.mtl`,
retaining triangle order, diffuse materials, player hull colors, unscaled mesh
coordinates and the original half-height placement above the floor. It uses
classic cardinal headings and the production camera eye/target, with Z up,
a 105-degree field of view, near plane 0.5, and Vulkan's 0-to-1 depth range.
An additional arena overview is a development aid.

Floor/wall drawing and trails are bootstrap approximations: the untextured
floor has the original 20-unit grid, walls use one fifth of arena size, and
opaque trail RGB values replace the classic blended/textured appearance.
CPU flat diffuse shading approximates smooth/specular lighting. Production
trail coordinates are retained, but classic trail/bow geometry, turn lean,
fog, textures and crash effects are not yet fully rendered. The gameplay hash
excludes cameras and rendering; it checks repeatability of exposed gameplay
state and queued inputs, not every hidden implementation detail or pixel.

The following remain outside this milestone: menus and HUD/font rendering,
texture upload and artpack selection, fog, full lighting/shadows/transparency
and crash effects, recognizer presentation, audio/music, split-screen,
configurable input, and saved preferences. The retained OpenGL executable
provides those features while the Vulkan implementation is incomplete.

## Evidence required before widening scope

The headless bridge gate must verify deterministic reset and repeated stepping,
production left/right events and boost, camera changes that leave gameplay
unchanged, dynamic trail growth, safe snapshot use, and cleanup. Scene checks
must verify finite positions and matrices at 4:3, widescreen, and portrait
aspect ratios without modifying gameplay. Retain inherited classic tests as
independent guards on physics, camera, layout, input, audio, and resources.

Native Vulkan checks must separately exercise actual device/pipeline creation,
a visible frame, bounded capture, resize, minimize/restore, fullscreen transitions,
and orderly shutdown with validation enabled. Log the device, Vulkan baseline,
drawable size, frame count, validation errors, and simulation hash. A successful
headless test or a reported device API version is insufficient for this gate.
Repeat relevant checks under ASan/UBSan; document driver or compositor limits.

The inherited [release checklist](RELEASE_CHECKLIST.md) and
[faithful README](FAITHFUL_REFERENCE_README.md) document the OpenGL remaster.
They must never be used as evidence that a new Vulkan rendering feature passed.
Record actual Vulkan results with the revision and commands used after running
them. This initial plan makes no claim that native Vulkan checks have passed.

## Testable parity milestones

| Milestone | Work | Evidence needed |
| --- | --- | --- |
| Stable renderer foundation | Device/resources, synchronization, swapchain, depth, shaders, capture, and window lifecycle | Real validation runs and repeatable bounded captures; no invalid resource lifetime or layout use |
| Classic geometry and camera | Exact cycle transforms, trails and bows, floor/walls, recognizer, and four camera modes | Fixed seed/input/clock state comparisons and matched OpenGL/Vulkan views; long-trail and camera-transition cases |
| Faithful appearance | Original textures/artpacks, materials, lighting, fog, blending, shadows, impact and crash effects | Matched captures and visual review of sampling, alpha, depth, winding, color and draw order |
| Complete game presentation | Menus, font metrics, HUD, pause/results, and single/two/four-player layouts | Original topology/layout checks plus native play and capture at 4:3, wide and HiDPI sizes |
| Complete platform experience | Existing controls, configuration, audio/music and asset discovery | Production input/settings/audio regressions plus real interaction, restart and packaged-asset checks |
| Measured optimization and release | Profile demonstrated bottlenecks; validate packages and supported systems | Reproducible timings, preserved gameplay/visual results, clean installed launch, and a truthful release checklist |

Preserve original assets and authorship throughout. Port a feature, compare
it, and keep the reference available. Intentional visual or gameplay divergence
needs a separate decision and documentation; an API change alone does not
justify changing the game.

## Learning and future reuse

Use this project to learn Vulkan resource lifetimes, memory allocation and
uploads, command recording, synchronization, swapchain recreation, pipelines,
SPIR-V shaders, and profiling on a small, understandable game. Keep experiments
bounded and record what a measurement or validation error actually established.

Those skills can inform Break Orbit and Concrete & Light. A reusable upload
helper or synchronization pattern should emerge from working code and a second
project's concrete needs. VulkanTron does not promise a universal engine or
establish that another project's SDL_GPU renderer must be replaced. Evaluate
performance and feature needs in each game rather than treating direct Vulkan
as an automatic visual-quality upgrade.

For current build commands and controls, see the [project README](../README.md).
The [GPL license](../COPYING), [Lua notice](../packaging/LUA-COPYRIGHT.txt), and
[asset provenance](ASSET_PROVENANCE.md) remain part of the source and distribution.
