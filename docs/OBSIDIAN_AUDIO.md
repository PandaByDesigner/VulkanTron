# Obsidian audio

The Obsidian arena's current theme is **Forward Pulse**, contributed by
PandaByDesigner and generated using Suno.
The earlier **Obsidian Circuit** electronic study remains selectable, alongside
four original procedurally synthesized effects. The earlier study and effects
use no audio samples or reference recordings from the films, their soundtracks,
or another game. Their YuE2 provenance does not describe the supplied theme.

## Runtime assets

| Asset | Purpose | Format |
| --- | --- | --- |
| `music/song_forward_pulse.wav` | Current theme: 133.12-second Forward Pulse | 22050 Hz stereo, signed 16-bit PCM WAV |
| `music/song_obsidian_arena.wav` | Retained 53.999-second Obsidian Circuit music loop | 22050 Hz stereo, signed 16-bit PCM WAV |
| `data/obsidian/game_engine.wav` | 2-second electric cycle drive loop | 22050 Hz mono, signed 16-bit PCM WAV |
| `data/obsidian/game_recognizer.wav` | 4-second distant vehicle hum loop | 22050 Hz mono, signed 16-bit PCM WAV |
| `data/obsidian/game_boost.wav` | 0.62-second thrust activation cue | 22050 Hz mono, signed 16-bit PCM WAV |
| `data/obsidian/game_crash.wav` | 1.25-second impact and debris cue | 22050 Hz mono, signed 16-bit PCM WAV |

## Current theme: Forward Pulse

The [track credit and inclusion notice](../music/obsidian/FORWARD_PULSE_NOTICE.md)
records the maintainer's confirmation of paid-plan creation and an official
paid-plan download. The recording is documented separately from the program's
GPL license; no exclusive copyright in AI-generated audio is claimed.

The supplied master is `/home/pandabydsgn/Music/Forward Pulse.wav`, a
133.12-second, 48000 Hz stereo PCM16 file. A separate runtime conversion is
bundled as `music/song_forward_pulse.wav`; the supplied master and original
YuE2 music are retained. The conversion preserves the full song, resamples to
22050 Hz, applies a constant -4.19 dB gain, and fades only the first and last
5 milliseconds to soften the repeat boundary. It measures -20.04 LUFS and
-7.12 dBTP with no clipped samples. The game's existing player repeats the song.
The source and runtime hashes, conversion, and technical checks are recorded
separately in `music/obsidian/forward_pulse.json`.

Fresh launcher profiles and the game's `--obsidian` session option select
Forward Pulse. The installer's `--select-obsidian` flag updates an existing
VulkanTron profile's artpack and track selections after saving its original
bytes. Other preferences remain intact. The original GLTron tracker song and
Obsidian Circuit remain available in the music menu.

## Synthesized effects

The engine's harmonic frequencies fit the loop period exactly. Its subtle air
layer uses periodic filtered noise. The recognizer hum is also periodic. Both
buffers preserve an ordinary sample-to-sample transition across their wrap.
Boost and crash use short, zero-ended envelopes. These sounds are designed to
leave room for positional mixing and the engine pitch changes already applied
by the game.

## Retained YuE2 study: Obsidian Circuit

The finished Obsidian Circuit study measures -20.0 LUFS and -5.8 dBFS sample
peak, with no clipped samples. Its boundary step is 0.0162 full scale, below the
track's 99th-percentile ordinary sample step of 0.0824. The 59.999-second raw render took 369.2 seconds
in the YuE2 runtime on this machine. It touched full scale on two of 5,759,872
PCM samples; this is retained in the raw-source receipt. The delivered mix
has zero full-scale samples and ample headroom.

The soundtrack request calls for instrumental electronic music at 112 BPM in
D minor, with an original four-note motif, analog bass, interlocking arpeggios,
restrained drums, and distant orchestral texture. Those are conditioning
instructions, not a claim that the generated performance exactly obeys its
meter, instrumentation, or vocal exclusions. The render uses the locally
installed YuE2 model; the SFX use mathematical synthesis because this installed
YuE2 route is intended for music.

### Reproduction of Obsidian Circuit and effects

Run from this checkout with Python 3, NumPy, and FFmpeg installed. No script
downloads dependencies or model weights.

```sh
python tools/create_obsidian_audio.py effects
python tools/create_obsidian_audio.py generate \
  --yue2 /path/to/existing/Music \
  --output /path/to/new/render-directory
python tools/create_obsidian_audio.py finish \
  --raw /path/to/new/render-directory/obsidian-circuit-raw.wav
```

The saved YuE2 request is `music/obsidian/request.json`: seed `913260112`, CPU,
six threads, eight synthesis steps, symbolic planning off, 55-second minimum,
and 60-second cap. The generator runs at nice level 10 and refuses to replace
an existing raw WAV. It invokes the local `audio.cpp` CLI with absolute model
paths; generated files stay in the supplied output directory.

Runtime revision: `cda0e3a4762d855e865980506f934ec0e6928691`.
Model revision: `eb116220931de5f373d024d48800338178c7de51`, using the
`yue2-3b-q4_0.gguf` main model and `yue2-vae-f16.gguf` decoder. The installed
runtime includes its existing check rejecting nonfinite generated audio. CPU
is the verified backend for this machine's install.

`finish` resamples the raw stereo render to the mixer's rate, trims two seconds
at each end, and joins the ends with a two-second raised-cosine overlap. It
removes DC and applies a single linear gain: at most -3.5 dBFS sample peak,
with attenuation towards -20 LUFS. The soundtrack has no opening or ending fade
that would interrupt continuous arena play.

The effects are deterministic for the recorded script/NumPy environment.
`music/obsidian/effects.json` records their hashes, measured level, and loop
boundary checks. `generation.json` records the source request, binary hash,
command, elapsed generation time, and raw output hash. `track.json` records
the finished music's hash, duration, level, seam, and exact edit.

### Obsidian Circuit source preservation and review

The initial release recorded preservation of the unedited first render,
generation log, request, and receipt in the Music project's output directory:
`/run/media/pandabydsgn/1AA2C91BA2C8FBEF/Users/jumpe/Documents/ChatGPT/Music/outputs/vulkantron-obsidian-20260913/`.
All four files were copied from the temporary generation directory and their
SHA-256 hashes verified at that time. That original location is unavailable
in the current environment. The shipped finished WAV and its provenance remain
in the repository. Generative output can differ on another runtime or hardware
even with the same seed; retained audio hashes identify the exact render.

Validation checks file decoding, finite non-silent signal, clipping, DC, level,
and sample continuity at the loop boundary. It does not establish a subjective
listening review. In particular, absence of vocals and the musical phrasing of
the crossfade still require a listener's check.

The installed YuE2 model card identifies its **weights** as CC BY-NC 4.0. This
records the model metadata without asserting a license for its generated
output. Effects are newly authored synthesis, not YuE2 output or stock samples.
