# Faithful Remaster completion requirements

The agreed public project is **GLTron Faithful Remaster**, using SDL3 and
OpenGL. A later **VulkanTron** project will carry the Vulkan renderer and its
own artwork/music. The faithful release preserves the classic game and original
assets as a selectable reference. The requirements below are matched to the
local release verification evidence in the table.

## Required for the faithful release

- Preserve classic movement, collision/scoring timing, AI, booster, all cameras,
  stored binding numbers, and single/stacked/four-way player ownership. Existing
  production regressions must pass after final changes.
- Native SDL3 platform, input and audio with the same saved controls and mixer
  output. Preserve SDL1/SDL2 references and pass the applicable decoder,
  production-mixer, contention, sanitizer and input parity gates.
- Resizable windows, borderless desktop fullscreen, correct logical/pixel sizing,
  viewports and readable HUD on HiDPI displays. Resizing must preserve the GL
  context and game state. Mouse camera sensitivity must not depend on DPI.
- Complete-frame PNG and BMP screenshots at real drawable dimensions, including
  odd widths, correct orientation, checked writes and repeated captures.
- Separate high-resolution faithful artwork and fonts, with the original
  artpack still selectable. Verify both in menus, live play, pause, all cameras
  and local multiplayer. Preserve the original music and sound effects.
- A reproducible SDL3/OpenGL release build with source and binary packaging,
  documented dependencies, desktop integration, robust asset discovery, and
  isolated configuration for verification. Test installation outside the source
  tree and launching from an unrelated working directory.
- Fresh real graphical/audio sessions, normal closes, no new crashes, and the
  user's existing preferences left untouched by testing.
- Updated player/developer README, release notes, attribution and accurate
  platform/validation limits. Audit package contents and public-release asset
  provenance before publication; no credentials or local runtime data included.

## Implementation and evidence

Implementation started from `79fd7f4`. The **0.70.1** SDL3/OpenGL release includes
the platform and reliability work below plus the separately selectable faithful
artpack. The user approved deterministic upscaling of the original artwork.

| Gate | Evidence / status |
| --- | --- |
| Classic gameplay | Production round hashes remain `76d51ba3de0d7b09`, `611551b67aed43dd`, and `c2b9c4b61a32f180`; turns, collisions, scoring, boost, cameras and player ownership pass. |
| Long matches | 2,005 production turns retain every segment across storage growth; generated meshes through 20,000 segments pass bounds and 32-bit index checks. |
| SDL input/audio | SDL1/2/3 input, decoder, production mixer and source-list stress comparisons pass; native SDL3 retains the fixed classic mixer blocks and sample values. |
| Window/screenshot fixture | SDL3 X11 passes ASan/UBSan; SDL2 X11 passes. Includes resize/context/texture retention, external fullscreen, restoration, exact PNG/BMP RGB pixels, odd widths, competing BMP writers and canceled screenshot requests. |
| Real game rendering | Both Wayland at 2x scale and staged X11 pass the production game ASan/UBSan smoke: 14 captures covering menu, play, pause, all cameras, multiplayer, a 1,100-turn trail, fullscreen and restore. Two actual settings save/load cycles pass. |
| Final artwork integration | All three graphical CTests pass on X11 with ASan/UBSan: window fixture, original pack (14 captures), and faithful pack (16 captures). The faithful pack also passes the 16-capture Wayland run at 2x density. All 16 game GPU textures and four font atlases resolve from the selected pack at the expected dimensions; switching default/faithful preserves the GL context and player state. All 14 headless sanitizer tests pass again after integration. |
| Resource fidelity | All 22 original PNGs and complete mip chains match the old loaders; 95 ASCII glyph draw coordinates match. Corrupt files and repeated font reloads pass cleanup checks. A 1024-pixel test atlas preserves the classic logical font metrics. |
| Hardware audio | Exact application-only seven-second PipeWire capture is non-silent for music, effects and combined playback; production mixer is 22050 Hz/S16/stereo; hardware output and clean shutdown verified. |
| Builds/install | SDL1/SDL2/SDL3, each with audio on/off, build and pass their CTests. Full-audio configurations have 14 headless tests; no-audio configurations have 12. Desktop integration, binary TGZ extraction and SHA256 checks pass. |
| Relocation/launch | The extracted 0.70.1 executable runs from `/tmp`, selects its packaged faithful artpack and original song with no data override, accepts a normal close sent only to its own window, and exits zero. Original-pack staged launch was also verified. |
| Source packaging | Fresh 0.70.1 source extraction builds SDL3 with audio without Git metadata and passes all 14 CTests; its faithful artpack regenerates exactly. Both archives contain byte-identical original and faithful assets, including Lua markers and the provenance manifest. Source ignore rules use literal-dot character classes to survive CPack serialization. |
| Documentation/credits | README, release notes, original notices, Lua notice and asset provenance included. Original art, data and music match `upstream-0.70` byte-for-byte. |
| User preferences | All verification uses private directories. The existing home `.gltronrc` checksum remains unchanged. |
| Sharper artpack | Complete: 22 deterministic 4x PNG upscales, including cell-isolated font atlases, reproduce exactly with the recorded toolchain. Original images, metadata and music remain unchanged. Native menu, play, pause, camera and multiplayer captures were visually reviewed against the original presentation; no missing textures, wrong glyphs, clipping or layout drift were found. See ASSET_PROVENANCE.md and art/faithful/manifest.txt for the method and hashes. |

## Limits and final release gate

The required local release gates are complete for version **0.70.1**. Source
and Linux binary TGZ packages include SHA256 files. The maintained default
release preset clears the old preview suffix when upgrading an existing build.

The verified machine is Linux x86-64 with SDL3 3.4.16, libmikmod 3.3.13,
NVIDIA GTX 1660 and OpenGL 4.6 compatibility support. Linux is the tested platform
for this release; other operating systems have not been verified. A tiling
compositor can override requested window sizes; the game renders at the actual drawable dimensions and keeps its context
when the requested size is declined. The strict window-size fixture passes on
X11; Wayland's compositor-controlled sizing prevents that exact-size assertion,
although the real Wayland game smoke passes at 2x density.

ASan/UBSan runs disable leak detection where the traced runner cannot support
it. Separate untraced resource and music-enumeration LeakSanitizer checks pass;
this is not a whole-game leak certification. Binary archives depend on system
libraries and graphics drivers. A clean build from source is the portable
reproduction path; byte-identical binaries across different toolchains are not
claimed.

The faithful artpack uses interpolation of the original samples, as approved;
it does not reconstruct missing source detail. GitHub publication and the
VulkanTron fork are separate actions; this work has not published either
repository.

The OpenGL work here preserves presentation and robust resource handling. A
full shader-renderer rewrite is reserved for VulkanTron under the agreed
two-project direction.
