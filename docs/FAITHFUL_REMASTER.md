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

## Regression commands

Run the classic behavior and display geometry checks:

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

The optimized build was exercised in the target Wayland session using the
original default artpack and `song_revenge_of_cats.it`. At 800 by 600, the
classic menu and a live single-player view retain their original 4:3
composition. At 1280 by 720, the menu remains centered and unstretched, and
live stacked split-screen and four-way views stay inside their exact tested
viewport edges. Both native sessions shut down cleanly, and the user's
`.gltronrc` checksum remained unchanged.

The pause banner and several HUD elements still use legacy whole-screen or
fixed-pixel placement. That visible limitation is intentionally assigned to
the next resolution-independent HUD milestone rather than hidden by this
foundation change.

## Current platform boundary

This milestone intentionally retains SDL 1.2 through `sdl12-compat` and the
fixed-function OpenGL renderer. Existing Lua preferences store raw SDL 1 key
numbers, and SDL_sound shares SDL 1 audio and RWops types. A direct header swap
to SDL 2 would break those contracts. The next platform layer must first create
stable GLTron-owned input IDs and separate logical, window, and drawable sizes.

## Remaster sequence before modding

1. Lock additional scripted turn, collision, booster, camera, and split-screen
   scenarios into the regression suite.
2. Make the HUD and front end resolution-independent, with explicit anchors and
   safe areas rather than mixed fixed pixels and screen fractions.
3. Introduce a stable input/configuration seam, harden controller bounds and
   settings writes, then move the backend to SDL 2 without changing controls.
4. Add resize, borderless desktop fullscreen, HiDPI drawable sizing, and
   screenshot correctness.
5. Create a separate high-resolution faithful artpack and fonts while keeping
   the original artpack available for direct comparison.
6. Modernize the renderer behind screenshot and gameplay-state comparisons.

Only after these parity gates pass does the separate creative modding phase
begin.
