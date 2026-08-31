# Stabilized classic

This layer keeps GLTron 0.70's gameplay, rendering, original art, and original
music intact while addressing failures that prevent the classic release from
being dependable on a modern Linux desktop.

## Confirmed audio failure

Three captured Linux core dumps ended in the SDL audio callback thread. Source
mapping and inspection strongly support concurrent access to the sound system's
linked list as the cause:

- The main thread appended, unlinked, and deleted sound sources.
- The SDL callback simultaneously walked the same nodes to mix audio.
- A newly appended tail was published before it was fully initialized.
- Adjacent stopped sources could be skipped during cleanup.

That combination is consistent with the callback following a stale or
partially initialized node after a collision sound was added or retired. A
postmortem core cannot prove the exact thread interleaving.

## Stabilization changes

- Serialize source-list publication and unlinking with SDL's audio lock.
- Initialize a new list tail completely before making it visible to the
  callback.
- Unlink under the lock, then destroy sources after releasing it; music decode
  work also stays outside the callback lock.
- Keep the current list position after removal so consecutive stopped sources
  are all reclaimed.
- Synchronize runtime listener, engine, volume, playback, music-reload, and
  shutdown state shared with the callback.
- Protect the streaming-music ring buffer while decoding and mixing, and clear
  the sample pointer immediately after freeing it.
- Match array allocation with `delete[]`, initialize source names, and reject a
  3D mix that would overrun its fixed temporary buffer.
- Repair the filesystem header guard so repeated includes are actually guarded.
- Remove logging from real-time callback paths.
- Stop and close audio before SDL_sound teardown, and make a window close exit
  the application instead of continuing to run after `SDL_Quit()`.

## Audio regression suite

Run the complete matrix from the repository root:

```sh
./tests/run_audio_stress.sh all
```

The default run uses SDL's dummy audio device and validates each of these in a
plain build, an AddressSanitizer/UndefinedBehaviorSanitizer build, and a
ThreadSanitizer build:

- 100,000 callback-driven synthetic sources are mixed and reclaimed.
- 10,001 short collision-style `SourceCopy` objects are mixed and reclaimed.
- Three adjacent stopped sources are reclaimed in one idle pass.
- Deterministic contention gates prove both insertion and removal wait while a
  callback deliberately holds the audio lock.
- Sixteen original tracker-music streams are repeatedly loaded and retired.
- One non-looping original WAV reaches EOF and is retired.
- Created and destroyed counters return to their expected baselines.

Use a smaller or larger source count when iterating locally:

```sh
GLTRON_STRESS_SOURCES=250000 ./tests/run_audio_stress.sh tsan
```

The script accepts `plain`, `asan`, `tsan`, or `all`. Sanitizer binaries and
other test products are built in a temporary directory and removed afterward.
Each run also has an outer 60-second watchdog so a lock regression fails the
matrix instead of hanging indefinitely.

## Verified target behavior

On the target Arch/Omarchy host, the stabilized tree:

- completed a fresh out-of-tree optimized build;
- passed the plain, ASan/UBSan, and TSan audio matrix;
- opened the classic OpenGL menu in the host's Wayland session;
- initialized the original `song_revenge_of_cats.it` through the host audio
  stack;
- handled a native close request with exit status 0; and
- left no process, new core dump, or preference-file change behind.

## Deliberate next boundary

This phase does not claim to have eliminated every warning in the 2003 codebase.
The protected result is tagged `stabilized-classic`. Faithful remaster work,
including its deterministic production-loop regression for the unreliable
built-in timedemo, is documented in `docs/FAITHFUL_REMASTER.md`. Modern
sanitizer-flag handling and the remaining memory, bounds, lifetime, and warning
candidates remain explicitly deferred; this remaster milestone does not claim
to resolve them.
