# Obsidian Arena artwork

Obsidian Arena is an original optional presentation for VulkanTron's production
lightcycle game. It uses graphite architecture, cyan and amber arena edges,
four distinct player colors, smooth dark cycle bodies, illuminated wheel rings,
restrained reflections, and a brief shower of luminous fragments on impact.
It is inspired by the atmosphere of a cinematic electronic arena. It contains
no extracted film assets, logos, models, photographs, or soundtrack material.

Select `obsidian` through the existing artpack menu or use the dedicated
Obsidian launch mode. The original and faithful artpacks remain available.
Changing the pack switches the cycle models as well as textures and colors;
switching back restores the original models. The existing game rules, collision
square, turn and boost handling, cameras, scores, and local viewport ownership
continue to drive the game. The surrounding towers stand outside that square.

## Assets and regeneration

All new visual assets in `art/obsidian` are authored in
`tools/create_obsidian_art.py`. Run:

```sh
python3 tools/create_obsidian_art.py
```

Python and Pillow are the only generator dependencies. No fonts, input images,
input models, network services, or random state are required. PNGs, OBJ/MTL
files, and `manifest.json` are checked in; Pillow is unnecessary to play.
The manifest records SHA-256 hashes, mesh counts, and bounding boxes. Regenerate
into an independent directory with `--output /tmp/obsidian-art-check` to compare.

The artwork includes a repeating architectural floor tile, four boundary wall
textures, six consistent skybox faces, trail masks, an impact texture, a menu
plate, and an original chamfered wordmark. Classic font atlases fall back to
`art/default`, retaining readable menus and their original character metrics.

Three original cycle LODs contain 3,552, 1,860, and 972 triangles. Each shares
the same bounding box, including symmetric Z bounds of -1.52 to 1.52. Forward
is -Y, matching the original cycle. The game still uses its original gameplay
collision logic; these meshes are visual only. The `Hull` material identifies
the illuminated rings and body strips. Armor, glass, and tire groups receive
normal lighting. Material libraries resolve relative to their owning OBJ.

## Production integration

`video.settings.obsidian_arena` selects the additional drawing behavior.
`scripts/artpack.lua` resets it before each artpack's own script runs, and
`art/obsidian/artpack.lua` enables it. The flag is cached with other video
settings. The faithful path does not execute these branches.

- `graphics_world.c`: 48-unit floor tiles, low illuminated collision walls,
  and 36 exterior architectural towers.
- `gamegraphics.c`: emissive material groups, shortened dim cycle/trail
  reflections, and 48 deterministic crash shards. Shards use visual animation
  time and never consume the simulation's random stream. The original flying
  recognizer and its projected floor shadow follow the existing Recognizers
  setting, using the same flight path as the classic artpacks.
- `trail_render.c`: player-colored, unlit trail masks with clear edges.
- `graphics_fx.c` and `explosion.c`: restrained cycle glow and cool impact waves.
- `visuals_2d.c` and `graphics_hud.c`: a dark translucent minimap, cyan border,
  directional player markers, and a cool neutral score color, using the same
  viewport and HUD layout calculations.
- `artpack.c`, `video.c`, `model.c`, and `material.c`: coordinated model reload
  with owned-memory cleanup and material-file closure.

The floor reflections are a deliberate graphic treatment: mirrored geometry
is compressed vertically, dimmed, and drawn over the floor before opaque
scenery. They are not ray tracing or physically based surface reflections.
They are disabled when the floor texture is disabled. Vulkan bloom is requested
after each world viewport and before its minimap and HUD; the renderer owns
that separate effect and clips it to the current viewport.

## Verification boundaries

The generator has been checked for byte-identical repeat output, PNG decoding,
manifest hashes, valid OBJ vertex/normal/material references, nondegenerate
triangles, the legacy parser's line-length limits, and signed-short runtime
index budgets. Native production capture and gameplay/audio acceptance are
recorded in the integration verification report. Asset validation alone does
not certify a Vulkan frame, a complete match, or performance on another GPU.
