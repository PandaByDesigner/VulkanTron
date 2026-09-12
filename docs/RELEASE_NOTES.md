# 0.70.1-preview

This preview brings GLTron 0.70 onto SDL3 while retaining its classic OpenGL
presentation and gameplay. The original artpack, models, effects, and
**Revenge of Cats** music remain unchanged.

## Player-visible improvements

- Resize without rebuilding the OpenGL context or resetting the match.
- Use borderless fullscreen at the desktop mode and render at the actual HiDPI
  framebuffer size. Keep classic HUD/menu proportions at wide aspect ratios.
- Capture the completed frame as PNG or BMP with correct dimensions, row
  alignment, and orientation.
- Reuse existing key bindings, including classic keypad and joystick identifiers.
  Controller reconnects retain fixed player slots.
- Save settings repeatedly without damaging the previous valid file.
- Ignore incomplete artpacks and source-build files in content menus; handle
  missing or corrupt replacement textures/fonts without leaking resources.
- Launch an installed or extracted copy from any working directory. Development
  builds use their own scripts even when an older GLTron is installed.

## Preservation and reliability

The production regressions retain the original deterministic round results,
turn ordering, collision/scoring timing, booster rules, camera motion, and local
player ownership. SDL3 audio retains the original mixer samples and tracker
output through comparisons with the SDL1 reference.

Native sanitizer testing additionally found inherited trail-rendering memory
errors. Trail storage and rendering buffers are sized to the actual number
of segments, avoiding fixed-array overruns during longer matches. This repair
preserves existing segments and leaves the classic movement rules unchanged.
The regression covers 2,005 production turns and meshes with 20,000 segments;
the native game also renders a 1,100-turn fixture under address/undefined checks.

## Release status

Linux is the verified build target; other operating systems have not been
certified. Native resize fixture results depend on the compositor honoring
requested window sizes. The binary TGZ uses system libraries rather than
bundling a complete runtime.

The optional sharper faithful artpack and final integration/package checks are
still open. See [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md) for current evidence.
The `preview` suffix will remain until those gates are resolved. Vulkan renderer
work and new art/music belong to the separate planned VulkanTron project.
