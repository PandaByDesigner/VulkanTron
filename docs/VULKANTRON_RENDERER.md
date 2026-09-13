# Direct Vulkan renderer architecture

VulkanTron retains GLTron's production game, Lua configuration, menus, assets,
audio, and input. The graphics boundary has two implementations in this source
tree: the OpenGL reference and a direct Vulkan renderer. SDL3 creates the window,
supplies its Vulkan surface, and handles platform services.

The Vulkan implementation does not create an OpenGL context, call SDL_GPU, or
send drawing commands through an OpenGL driver. It records the particular
fixed-function operations used by GLTron and implements them with Vulkan
resources, pipelines, shaders, and command buffers. The old `gl*` spellings in
the shared game source map to `vt_gl*` functions only in the Vulkan build.
Compile-time GL types and constants come from SDL's headers.

## From game drawing to a Vulkan frame

1. The original drawing code chooses geometry, matrices, textures, materials,
   lights, viewports, and draw order. Gameplay and camera calculations remain in
   the existing production sources.
2. `vulkantron/fixed_function.cpp` records that state. It transforms vertices
   into homogeneous clip coordinates, evaluates the classic vertex lighting,
   and converts the original primitive types into Vulkan draw batches. Texture
   versions remain alive for every batch that references them.
3. `vulkantron/faithful_frame.hpp` defines the recorded frame. It contains draw
   batches, ordered clear operations, and optional per-viewport bloom commands,
   including the depth and stencil state needed by the original shadows.
4. `vulkantron/renderer.cpp` and its private implementation own Vulkan memory,
   uploads, descriptors, pipelines, depth/stencil attachments, command buffers,
   synchronization, swapchain images, presentation, and GPU readback. The
   faithful shaders apply texture environments and fog to the recorded vertex
   output.
5. `vulkantron/faithful_platform.cpp` connects the renderer to the production
   window and screenshot functions. A screenshot submits the completed frame
   once, caches its pixels for any additional capture, and prevents the
   subsequent swap from presenting that frame twice.

This is a game-specific adapter, not a general OpenGL implementation. Unsupported
operations must produce an error rather than silently change the picture.
Preserving the original draw source makes differences easier to diagnose and
keeps the OpenGL reference useful while the renderer is developed.

## Coordinates and color

The adapter retains homogeneous W for perspective-correct interpolation and
clipping and reverses clip Y. Where `VK_EXT_depth_clip_control` is available,
the renderer uses the original OpenGL clip Z and negative-one-to-one depth
convention. Otherwise it converts clip Z to Vulkan's zero-to-one range.
`VULKANTRON_NATIVE_CLIP_DEPTH=0` explicitly exercises this fallback. Recorded
viewports use the game's original bottom-left coordinates; the Vulkan backend
converts them to framebuffer coordinates.

The faithful path preserves the numeric color and blending behavior of the
classic framebuffer. This is separate from the initial arena prototype's color
pipeline. Texture sampling, fog, alpha, front/back lighting, stencil shadows,
and rasterization need matched reference captures; a valid Vulkan command
stream alone does not establish visual fidelity. The renderer uses an UNORM
offscreen color attachment and copies its numeric color values to the
swapchain, avoiding an extra sRGB conversion of the original artwork.

Some original explosion surfaces overlap at the same depth. The geometry and
draw order are preserved, so API-specific depth rounding can change which
surface wins at a few pixels. The comparison report retains the strict image
metrics and documents the limited tolerance for these three fixtures.

## Lifetime and failure reporting

Obsidian records bloom after each world's drawing and before its minimap/HUD.
`renderer_bloom.inc` pauses rendering, copies the viewport into a sampled
image, then resumes with attachment contents preserved and adds a 25-tap
bright-pass blur. Both sampling and rasterization stay inside that viewport.
The classic artpacks never issue this command. This is an artistic UNORM
bright-pixel effect, not an HDR lighting pipeline.

Bloom images and descriptors follow frame-fence/idle ownership through resize
and shutdown. Failed lazy creation destroys all partial resources before
propagating the error; the native bloom test exercises repeated missing-shader
failures followed by a successful retry.

Renderer shutdown completes before reporting the final validation count and
before SDL destroys the window. Both a graphics-menu context restart and a
normal application exit use that boundary. Shader and texture failures must
leave partially created resources safe to destroy.

VulkanTron defaults to its own XDG configuration directory and screenshot
directory, with explicit `VULKANTRON_CONFIG_DIR` and
`VULKANTRON_SCREENSHOT_DIR` overrides. Asset selection also accepts the legacy
`GLTRON_DATA_DIR` used by the shared regression harness.

## Verification and future optimization

The complete-game native harness is compiled against both renderer builds. It
uses the same simulated clock and inputs, compares recorded game and visual
state, and captures menus, fonts, cameras, local multiplayer, textures, and
effects. Image differences are assessed separately from Vulkan validation and
CPU sanitizer results.

The adapter deliberately begins with CPU evaluation of the legacy vertex
pipeline. That is an implementation choice for fidelity and inspection, not a
performance claim. Profile it before moving work into shaders or introducing
parallel recording. Preserve the same reference scenes when optimizing.

This document describes the implementation boundary. Actual completed checks
and remaining limitations belong in `VULKANTRON_VERIFICATION.md` for the
faithful port and `OBSIDIAN_VERIFICATION.md` for the 0.3.0 presentation.
