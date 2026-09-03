# Faithful Remaster

The Faithful Remaster modernizes how GLTron is presented and supported without
turning it into a different game. The stabilized original is protected by the
`stabilized-classic` tag; remaster work lives on the
`codex/faithful-remaster` branch.

## Faithfulness contract

Until the remaster is complete, these remain the classic GLTron 0.70 behavior:

- four-direction, 90-degree lightcycle movement;
- movement timing, speed oscillation, booster and wall-acceleration rules;
- arena geometry, trail collision, crash scoring, AI decisions, and round end;
- camera positions, movement, and field-of-view setting;
- keyboard and joystick binding numbers already stored in `.gltronrc`;
- original models, artpack, sound effects, and `Revenge of Cats` music; and
- single, stacked two-player, and four-way local viewport topology.

The original artpack will remain selectable when higher-resolution faithful
art is added. New vehicles, arenas, rules, game modes, music, and stylistic art
variants belong to the later modding phase, not this branch's remaster work.

## Foundation milestone

### Deterministic classic behavior

Round reset now initializes AI turn timing, cycle turn timing, and every
transient player-visual field instead of inheriting uninitialized or
previous-round memory. Player state is reset before visual state that depends
on it. The regression poisons those fields before each of several successive
rounds, then calls the production reset implementation and checks the result.

The headless regression directly compiles the production movement, collision,
AI, random-number, geometry, scheduler, event-queue, and player-visual reset
sources. It advances the production `Time_Idle` to `Game_Idle` to
`Game_PhysicsStep` path rather than reproducing that order inside the test.
With seed `12313`, classic movement rules, four level-two AI players, a
200-unit arena, and fast finish disabled for cadence isolation, it locks three
complete rounds:

| Frame cadence | Frames | Elapsed | Winner | State hash |
| --- | ---: | ---: | ---: | --- |
| fixed 20 ms | 324 | 6480 ms | player 2 | `76d51ba3de0d7b09` |
| fixed 10 ms | 759 | 7590 ms | player 4 | `611551b67aed43dd` |
| repeating 7/13/41/3/22 ms | 631 | 10843 ms | player 4 | `c2b9c4b61a32f180` |

Each digest accumulates one selected state snapshot per rendered frame. Those
snapshots contain explicit scalar fields, floating-point bit patterns, live
trail segments, AI state, all transient crash-animation fields, and any queued
events. The harness also proves that the default all-AI fast-finish path at 20
ms is simulation-equivalent to four normal physics steps, that a live human
suppresses that multiplier, and that a terminal stop leaves no dangling event
queue. A separate poisoned reset proves that starting a new round releases and
clears an already queued turn event. Camera movement, rendering, audio, window,
and settings integration are stubbed; pointers, padding, and the user's
settings file are not hashed.

### Aspect-correct display

The original viewport descriptions still use their 32 by 24 virtual grid, but
horizontal and vertical coordinates now scale independently. Viewport edges
are calculated and clamped against the framebuffer, so 16:9, ultrawide, and
portrait output cannot create the oversized and clipped viewports produced by
the old width-only calculation.

Because each viewport is now derived from its edges, a fractional boundary can
round one pixel differently from the legacy origin-plus-size calculation. For
example, the first 800 by 600 split viewport is 288 pixels tall instead of 287.
This is a presentation correction only; split-screen topology is unchanged.

The 3D projection and map fitting now use floating-point aspect ratios. A
single-player view fills the requested framebuffer and gains horizontal view
at widescreen ratios while retaining the configured vertical field of view.

The classic 4:3 menu artwork and logo render inside a centered 4:3 safe canvas
instead of being stretched. The side regions deliberately remain black until
a faithful high-resolution front-end treatment is created; the original
textures have not been edited.

Fresh configurations default to 1280 by 720. Valid existing `.gltronrc` values
continue to override that default; no forced migration was added. The normal
classic save-on-quit and F5 behavior remains unchanged. The screen menu adds
1280 by 720, 1600 by 900, 1920 by 1080, and 2560 by 1440 choices. Command-line
shortcuts `-8` and `-9` select 1280 by 720 and 1920 by 1080.

## Gameplay and HUD milestone

### Scripted production behavior

The production-path regression now locks three more pieces of classic play:

- queued left-then-right human turns retain their order and event metadata,
  and produce the expected continuous trail;
- a wall impact clips the trail at the wall, queues the classic deferred crash
  time, and awards the score on the following simulation step; and
- boost-enabled and boost-disabled fuel drain, speed recovery, and upper
  clamps retain their classic progression.

These checks call the production scheduler, movement, event, collision, score,
and booster code used by the game. They add semantic assertions around the
existing deterministic round hashes; they do not change movement, collision,
scoring, booster, or timing rules. Input gating remains part of the later
input and configuration milestone.

### Resolution-independent classic HUD

The active classic two-dimensional presentation paths now use an explicit HUD
canvas: the minimap, score, AI label, pause and winner banner, FPS display,
console, configure-key prompt, and credits. At 800 by 600, their logical
coordinates and sizes match the classic layout exactly. This intentionally
preserves familiar relationships, including any legacy overlap between the
score, AI label, and minimap, rather than using the remaster as an excuse to
redesign them.

At widescreen, ultrawide, and portrait resolutions, each player pane receives
a centered, uniformly scaled classic canvas. That single scale keeps text and
map proportions intact; it does not stretch one axis or reconstruct logical
positions from rounded viewport pixels. Whole-screen messages use the same
principle in the root display safe area. The minimap is fitted and clamped
inside its owning canvas, including large requested map ratios, while the
classic reduced console-line behavior at low physical resolutions remains
unchanged.

The standalone HUD geometry test locks the 800 by 600 coordinates, pane-local
single-player, stacked, and four-way layouts, representative modern widescreen
and portrait placements, translated viewport origins, minimap containment,
representative text containment, and zero- or low-resolution edge handling.
The original accidentally concatenated 94-character credit entry still clips
at 800 by 600 exactly as it did in the classic; correcting that source data is
separate from this layout-preservation milestone. These tests are pure geometry
checks in addition to the production gameplay regression; they do not
substitute for native OpenGL visual inspection.

## Camera and local multiplayer parity milestone

### Classic camera behavior

A standalone regression now compiles the production camera implementation and
locks all four classic modes: circling, follow, cockpit, and free mouse. It
checks their default movement values and flags, initial placement, exact
20-millisecond camera and target positions, both wrapped turn-interpolation
seams, left and right glance offsets, radius and elevation clamps,
mode-specific mouse freedoms, and circling-camera rotation. It also locks the
classic distinction between live, crashed, and gone players: a crashed cycle
keeps its player camera until it becomes gone, then the view moves to the
recognizer.

The same harness exercises the production global movement and camera-cycle
entry points. Mouse deltas are consumed after all player cameras update;
human, computer, and inactive player routes retain their classic behavior;
camera cycling reinitializes human cameras only; and the four original console
labels remain unchanged. The paused-game `dt == 0` wall-clock path is covered
as well.

This audit exposed two inherited default-persistence defects in `camera.c`:
azimuth and elevation write to each other's default slots, and radius is saved
before its live value is clamped. Both defects sit outside this checkpoint's
live-motion boundary, so it does not enshrine or silently repair them. The
input/configuration seam will correct persistence with a focused
before-and-after comparison.

### Local player ownership

A second regression compiles the production viewport selector together with
the production physics-step, movement, collision, event, score, AI, and display
layout paths. Explicit single, stacked two-player, and four-way modes retain
their classic player order and exact 800 by 600 pane geometry. Automatic mode
is checked both directly and through the stored display-setting route used by
startup and reshape, with no humans, one human outside slot one, two
noncontiguous human slots, three humans with a gap, and four humans. The
unusual classic three-human rule is intentionally preserved: it selects
four-way mode and restores the default four player slots.

The noncontiguous two-human case then runs scripted production physics steps.
Both players turn independently in the same physics quantum, extend their own
continuous trails, return-turn independently, and retain their assigned split
panes. A controlled wall collision locks deferred crash processing, clipped
impact coordinates, score ownership, and crash-animation timing. No production
camera, gameplay, AI, scoring, or viewport behavior changed in this milestone;
the additions are regression coverage and documentation only.

## Regression commands

Run the classic gameplay, camera, local multiplayer, display, and HUD checks:

```sh
./tests/run_faithful_regression.sh plain
./tests/run_faithful_regression.sh asan
```

The second command enables AddressSanitizer and UndefinedBehaviorSanitizer.
LeakSanitizer is disabled because it cannot operate under the traced Codex
runner. The harness releases its allocations explicitly.

The stabilized audio matrix remains part of every remaster gate:

```sh
./tests/run_audio_stress.sh all
```

## Native visual verification

For the foundation milestone, the optimized build was exercised in the target
Wayland session using the original default artpack and
`song_revenge_of_cats.it`. At 800 by 600, the
classic menu and a live single-player view retain their original 4:3
composition. At 1280 by 720, the menu remains centered and unstretched, and
live stacked split-screen and four-way views stay inside their exact tested
viewport edges. Both native sessions shut down cleanly, and the user's
`.gltronrc` checksum remained unchanged.

The HUD checkpoint was then exercised in the same native Wayland/OpenGL path.
At 800 by 600, live and paused single-player views retain the classic map,
score, and banner placement. At 1280 by 720, single-player, stacked, and
four-way captures confirm that every map, score, and AI label stays inside its
own pane while console and pause or winner text remain in the centered classic
root frame. The temporary repository-local test binary was removed after both
clean shutdowns, and the user's `.gltronrc` checksum was again unchanged.

The camera and multiplayer checkpoint changes only the headless regression
harness and this documentation. The optimized runtime and assets are unchanged,
so the preceding native visual results remain the applicable executable
baseline.

## Current platform boundary

This milestone intentionally retains SDL 1.2 through `sdl12-compat` and the
fixed-function OpenGL renderer. Existing Lua preferences store raw SDL 1 key
numbers, and SDL_sound shares SDL 1 audio and RWops types. A direct header swap
to SDL 2 would break those contracts. The next platform layer must first create
stable GLTron-owned input IDs and separate logical, window, and drawable sizes.

The supported optimized `--enable-localdata` build passes with this HUD code.
A separate optimized non-localdata, temporary-prefix link attempt exposed an
unresolved legacy `dirSetup` symbol caused by static-library ordering. The
failure occurs in the existing filesystem/archive linkage rather than the new
HUD units, so it is recorded as a deferred platform/build concern instead of
being folded into this presentation milestone.

## Remaster sequence before modding

1. Complete: deterministic rounds, turn order and trail continuity, wall
   collision and scoring timing, booster behavior, all four camera modes, and
   explicit and automatic local multiplayer ownership are locked through
   production-path regressions.
2. Complete for the active classic two-dimensional paths: the HUD and front
   end use explicit, uniformly scaled pane or root canvases and safe areas.
3. Introduce a stable input/configuration seam, harden controller bounds and
   settings writes, then move the backend to SDL 2 without changing controls.
4. Add resize, borderless desktop fullscreen, HiDPI drawable sizing, and
   screenshot correctness.
5. Create a separate high-resolution faithful artpack and fonts while keeping
   the original artpack available for direct comparison.
6. Modernize the renderer behind screenshot and gameplay-state comparisons.

Only after these parity gates pass does the separate creative modding phase
begin.
