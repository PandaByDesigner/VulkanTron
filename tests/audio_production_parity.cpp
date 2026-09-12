#include "audio/nebu_SoundSystem.h"
#include "audio/nebu_SourceCopy.h"
#include "audio/nebu_SourceMusic.h"
#include "audio/nebu_SourceSample.h"
#include "audio/nebu_SourceEngine.h"

#include "audio/nebu_AudioSDL.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define OUTPUT_RATE 22050
#define OUTPUT_CHANNELS 2
#define CALLBACK_BYTES 4096
#define MUSIC_PREFIX_BYTES (4u * 1024u * 1024u)
#define RESET_PREFIX_BYTES (64u * 1024u)
#define SAMPLE_MIX_BYTES (16u * 1024u)
#define COPY_MIX_BYTES 4096u
#define MAX_MIX_ITERATIONS 20000u

namespace {

const uint64_t kFnvOffset = UINT64_C(14695981039346656037);
const uint64_t kFnvPrime = UINT64_C(1099511628211);

struct SampleResult {
  int bytes;
  uint64_t hash;
};

struct MusicResult {
  uint64_t prefix_hash;
  uint64_t first_reset_hash;
  uint64_t loop_reset_hash;
  size_t prefix_bytes;
  size_t loop_mixed_bytes;
  int saw_loop_reset;
  int stopped_at_eof;
};

struct MixerResult {
  uint64_t sample_full_hash;
  uint64_t sample_half_hash;
  uint64_t copy_first_hash;
  uint64_t copy_overlap_hash;
  uint64_t one_shot_hash;
  uint64_t loop_boundary_hash;
  uint64_t spatial_hash;
  uint64_t engine_hash;
  int spatial_cursor;
  int engine_cursor;
  int copy_cursors_independent;
  int one_shot_reset;
  int loop_boundary;
};

uint64_t hashBytes(uint64_t hash, const Uint8 *data, size_t size) {
  for(size_t i = 0; i < size; ++i) {
    hash ^= data[i];
    hash *= kFnvPrime;
  }
  return hash;
}

bool loadSample(Sound::System *system, const char *path,
                SampleResult *result) {
  Sound::SourceSample sample(system);
  sample.Load(const_cast<char *>(path));
  if(sample._buffer == NULL || sample._buffersize <= 0) {
    fprintf(stderr, "production SourceSample failed to load %s\n", path);
    return false;
  }

  result->bytes = sample._buffersize;
  result->hash = hashBytes(kFnvOffset, sample._buffer,
                           static_cast<size_t>(sample._buffersize));
  return true;
}

bool verifySampleMix(Sound::System *system, const char *path,
                     MixerResult *result) {
  Sound::SourceSample sample(system);
  Uint8 full[SAMPLE_MIX_BYTES];
  Uint8 half[SAMPLE_MIX_BYTES];

  sample.Load(const_cast<char *>(path));
  if(sample._buffer == NULL || sample._buffersize <= (int)sizeof(full)) {
    fprintf(stderr, "SourceSample is too small for direct Mix parity: %s\n",
            path);
    return false;
  }

  memset(full, 0, sizeof(full));
  sample.SetVolume(1.0f);
  sample.SetLoop(0);
  sample.Start();
  if(!sample.Mix(full, sizeof(full)) || !sample.IsPlaying() ||
     memcmp(full, sample._buffer, sizeof(full)) != 0) {
    fprintf(stderr, "SourceSample full-volume Mix changed the source PCM\n");
    return false;
  }

  sample.Stop();
  memset(half, 0, sizeof(half));
  sample.SetVolume(0.5f);
  sample.Start();
  if(!sample.Mix(half, sizeof(half)) || !sample.IsPlaying()) {
    fprintf(stderr, "SourceSample half-volume Mix failed\n");
    return false;
  }
  if(memcmp(full, half, sizeof(full)) == 0) {
    fprintf(stderr, "SourceSample half-volume Mix did not change the PCM\n");
    return false;
  }

  result->sample_full_hash = hashBytes(kFnvOffset, full, sizeof(full));
  result->sample_half_hash = hashBytes(kFnvOffset, half, sizeof(half));
  return true;
}

bool verifySourceCopies(Sound::System *system, const char *path,
                        MixerResult *result) {
  Sound::SourceSample sample(system);
  Sound::SourceCopy first(&sample);
  Sound::SourceCopy second(&sample);
  Uint8 first_output[COPY_MIX_BYTES];
  Uint8 second_output[COPY_MIX_BYTES];
  Uint8 overlap[COPY_MIX_BYTES];

  sample.Load(const_cast<char *>(path));
  if(sample._buffer == NULL || sample._buffersize <= (int)sizeof(overlap)) {
    fprintf(stderr, "SourceSample is too small for SourceCopy parity: %s\n",
            path);
    return false;
  }

  sample.SetVolume(0.5f);
  first.Start();
  second.Start();
  memset(first_output, 0, sizeof(first_output));
  memset(second_output, 0, sizeof(second_output));

  if(!first.Mix(first_output, sizeof(first_output)) ||
     first._position != (int)sizeof(first_output) || second._position != 0) {
    fprintf(stderr, "first SourceCopy unexpectedly moved the second cursor\n");
    return false;
  }
  if(!second.Mix(second_output, sizeof(second_output)) ||
     memcmp(first_output, second_output, sizeof(first_output)) != 0 ||
     first._position != second._position) {
    fprintf(stderr, "SourceCopy cursors did not reproduce independently\n");
    return false;
  }

  memset(overlap, 0, sizeof(overlap));
  if(!first.Mix(overlap, sizeof(overlap)) ||
     !second.Mix(overlap, sizeof(overlap)) ||
     first._position != second._position ||
     first._position != 2 * (int)sizeof(overlap)) {
    fprintf(stderr, "overlapping SourceCopy playback lost cursor parity\n");
    return false;
  }

  result->copy_first_hash =
    hashBytes(kFnvOffset, first_output, sizeof(first_output));
  result->copy_overlap_hash = hashBytes(kFnvOffset, overlap, sizeof(overlap));
  result->copy_cursors_independent = 1;
  return true;
}

bool verifySampleBoundaries(Sound::System *system, MixerResult *result) {
  static const Sint16 source_words[] = {
    -30000, 10000, -12000, 22000, 7000, -5000
  };
  Sound::SourceSample sample(system);
  Uint8 output[8];
  Uint8 first_output[8];
  Uint8 expected_tail[8];
  Uint8 expected_wrap[8];
  Uint8 expected_final[8];
  uint64_t one_shot_hash = kFnvOffset;
  uint64_t loop_hash = kFnvOffset;

  sample._buffersize = sizeof(source_words);
  sample._buffer = new Uint8[sample._buffersize];
  memcpy(sample._buffer, source_words, sizeof(source_words));
  sample.SetVolume(1.0f);
  sample.SetLoop(0);
  sample.Start();

  memset(output, 0, sizeof(output));
  if(!sample.Mix(output, sizeof(output)) || !sample.IsPlaying()) {
    fprintf(stderr, "one-shot sample stopped before its boundary\n");
    return false;
  }
  memcpy(first_output, output, sizeof(first_output));
  one_shot_hash = hashBytes(one_shot_hash, output, sizeof(output));

  memcpy(expected_tail, sample._buffer + 8, 4);
  memset(expected_tail + 4, 0, 4);
  memset(output, 0, sizeof(output));
  if(!sample.Mix(output, sizeof(output)) || sample.IsPlaying() ||
     memcmp(output, expected_tail, sizeof(output)) != 0) {
    fprintf(stderr, "one-shot sample boundary did not stop with a silent tail\n");
    return false;
  }
  one_shot_hash = hashBytes(one_shot_hash, output, sizeof(output));

  sample.Start();
  memset(output, 0, sizeof(output));
  if(!sample.Mix(output, sizeof(output)) ||
     memcmp(output, first_output, sizeof(output)) != 0) {
    fprintf(stderr, "one-shot sample Start did not reset to the first frame\n");
    return false;
  }
  result->one_shot_reset = 1;

  sample.Stop();
  sample.SetLoop(1);
  sample.Start();
  memset(output, 0, sizeof(output));
  if(!sample.Mix(output, sizeof(output)) || !sample.IsPlaying()) {
    fprintf(stderr, "looping sample stopped before its boundary\n");
    return false;
  }
  loop_hash = hashBytes(loop_hash, output, sizeof(output));

  /* The classic boundary path mixes the wrapped prefix back into data[0],
     overlapping the final source frame instead of appending after it. Lock
     that long-standing behavior for the native mixer as part of parity. */
  static const Sint16 expected_wrap_words[] = { -23000, 5000, 0, 0 };
  memcpy(expected_wrap, expected_wrap_words, sizeof(expected_wrap));
  memset(output, 0, sizeof(output));
  if(!sample.Mix(output, sizeof(output)) || !sample.IsPlaying() ||
     sample.GetLoop() != 0 ||
     memcmp(output, expected_wrap, sizeof(output)) != 0) {
    fprintf(stderr, "finite sample loop did not wrap exactly once\n");
    return false;
  }
  loop_hash = hashBytes(loop_hash, output, sizeof(output));

  memcpy(expected_final, sample._buffer + 4, sizeof(expected_final));
  memset(output, 0, sizeof(output));
  if(!sample.Mix(output, sizeof(output)) || sample.IsPlaying() ||
     memcmp(output, expected_final, sizeof(output)) != 0) {
    fprintf(stderr, "finite sample loop did not stop at its second boundary\n");
    return false;
  }
  loop_hash = hashBytes(loop_hash, output, sizeof(output));

  result->one_shot_hash = one_shot_hash;
  result->loop_boundary_hash = loop_hash;
  result->loop_boundary = 1;
  return true;
}

bool renderMusicPrefix(Sound::System *system, const char *path,
                       MusicResult *result) {
  Sound::SourceMusic music(system);
  Uint8 output[CALLBACK_BYTES];
  unsigned int iterations = 0;

  if(!music.Load(const_cast<char *>(path))) {
    fprintf(stderr, "production SourceMusic failed to load %s\n", path);
    return false;
  }

  music.SetVolume(1.0f);
  music.SetLoop(0);
  music.Start();
  result->prefix_hash = kFnvOffset;
  result->first_reset_hash = kFnvOffset;
  result->prefix_bytes = 0;

  while(result->prefix_bytes < MUSIC_PREFIX_BYTES &&
        iterations++ < MAX_MIX_ITERATIONS) {
    size_t remaining;
    size_t count;

    music.Idle();
    if(!music.IsPlaying()) {
      fprintf(stderr, "SourceMusic reached EOF before the parity prefix\n");
      return false;
    }

    memset(output, 0, sizeof(output));
    if(!music.Mix(output, sizeof(output))) {
      fprintf(stderr, "SourceMusic failed to mix the parity prefix\n");
      return false;
    }

    remaining = MUSIC_PREFIX_BYTES - result->prefix_bytes;
    count = sizeof(output) < remaining ? sizeof(output) : remaining;
    result->prefix_hash = hashBytes(result->prefix_hash, output, count);
    if(result->prefix_bytes < RESET_PREFIX_BYTES) {
      size_t reset_remaining = RESET_PREFIX_BYTES - result->prefix_bytes;
      size_t reset_count = count < reset_remaining ? count : reset_remaining;
      result->first_reset_hash =
        hashBytes(result->first_reset_hash, output, reset_count);
    }
    result->prefix_bytes += count;
  }

  if(result->prefix_bytes != MUSIC_PREFIX_BYTES) {
    fprintf(stderr, "SourceMusic prefix render exceeded its iteration bound\n");
    return false;
  }
  return true;
}

bool verifyLoopAndEof(Sound::System *system, const char *path,
                      MusicResult *result) {
  Sound::SourceMusic music(system);
  Uint8 output[CALLBACK_BYTES];
  size_t reset_bytes = 0;
  unsigned int iterations = 0;

  if(!music.Load(const_cast<char *>(path))) {
    fprintf(stderr, "production SourceMusic failed to reload %s\n", path);
    return false;
  }

  music.SetVolume(1.0f);
  music.SetLoop(1);
  music.Start();
  result->loop_reset_hash = kFnvOffset;
  result->loop_mixed_bytes = 0;
  result->saw_loop_reset = 0;
  result->stopped_at_eof = 0;

  while(music.IsPlaying() && iterations++ < MAX_MIX_ITERATIONS) {
    music.Idle();
    if(!result->saw_loop_reset && music.GetLoop() == 0) {
      if(!music.IsPlaying()) {
        fprintf(stderr, "SourceMusic stopped instead of resetting its loop\n");
        return false;
      }
      result->saw_loop_reset = 1;
    }

    if(!music.IsPlaying())
      break;

    memset(output, 0, sizeof(output));
    if(!music.Mix(output, sizeof(output))) {
      fprintf(stderr, "SourceMusic failed while exercising loop/EOF\n");
      return false;
    }
    result->loop_mixed_bytes += sizeof(output);

    if(result->saw_loop_reset && reset_bytes < RESET_PREFIX_BYTES) {
      size_t remaining = RESET_PREFIX_BYTES - reset_bytes;
      size_t count = sizeof(output) < remaining ? sizeof(output) : remaining;
      result->loop_reset_hash =
        hashBytes(result->loop_reset_hash, output, count);
      reset_bytes += count;
    }
  }

  if(iterations >= MAX_MIX_ITERATIONS) {
    fprintf(stderr, "SourceMusic loop/EOF test exceeded its iteration bound\n");
    return false;
  }
  if(!result->saw_loop_reset || reset_bytes != RESET_PREFIX_BYTES) {
    fprintf(stderr, "SourceMusic did not expose a complete loop reset prefix\n");
    return false;
  }
  if(music.IsPlaying() || music.GetLoop() != 0) {
    fprintf(stderr, "SourceMusic did not stop after its finite loop\n");
    return false;
  }
  if(result->loop_reset_hash != result->first_reset_hash) {
    fprintf(stderr,
            "SourceMusic loop reset did not restart at the original PCM\n");
    return false;
  }

  result->stopped_at_eof = 1;
  return true;
}

bool verifySpatialMix(Sound::System *system, const char *path,
                       MixerResult *result) {
  Sound::SourceSample sample(system);
  Sound::Source3D spatial(system, &sample);
  Sound::SourceEngine engine(system, &sample);
  Sint16 pcm[CALLBACK_BYTES / sizeof(Sint16)];
  sample.Load(const_cast<char *>(path));
  if(sample._buffer == NULL)
    return false;
  sample.SetLoop(255);
  sample.Start();
  Sound::Listener &listener = system->GetListener();
  listener._location = Vector3(0, 0, 0);
  listener._velocity = Vector3(3, 1, 0);
  listener._direction = Vector3(1, 0, 0);
  listener._up = Vector3(0, 0, 1);
  result->spatial_hash = kFnvOffset;
  result->engine_hash = kFnvOffset;
  for(int block = 0; block < 160; ++block) {
    spatial._location = Vector3(15, block % 2 ? 25 : -25, 0);
    spatial._velocity = Vector3(block % 3 ? 7 : -4, 2, 0);
    engine._location = listener._location;
    engine._velocity = Vector3(5, 0, 0);
    engine._speedShift = block % 2 ? 1.2f : 1.0f;
    engine._pitchShift = block % 3 ? 1.15f : 0.85f;
    memset(pcm, 0, sizeof(pcm));
    if(!spatial.Mix((Uint8*)pcm, sizeof(pcm)))
      return false;
    result->spatial_hash = hashBytes(result->spatial_hash,
                                     (Uint8*)pcm, sizeof(pcm));
    memset(pcm, 0, sizeof(pcm));
    if(!engine.Mix((Uint8*)pcm, sizeof(pcm)))
      return false;
    result->engine_hash = hashBytes(result->engine_hash,
                                    (Uint8*)pcm, sizeof(pcm));
  }
  result->spatial_cursor = spatial._position;
  result->engine_cursor = engine._position;
  return spatial._position > 0 && spatial._position < sample._buffersize &&
         engine._position > 0 && engine._position < sample._buffersize;
}

bool exerciseAudioWrappers(Sound::System *system) {
  SDL_AudioSpec desired;
  SDL_AudioSpec obtained;

  SDL_memset(&desired, 0, sizeof(desired));
  SDL_memset(&obtained, 0, sizeof(obtained));
  desired.freq = OUTPUT_RATE;
  desired.format = NEBU_AUDIO_S16;
  desired.channels = OUTPUT_CHANNELS;
#ifndef GLTRON_SDL3_AUDIO
  desired.samples = 64;
  desired.callback = system->GetCallback();
  desired.userdata = system;
#endif

#ifdef GLTRON_SDL3_AUDIO
  SDL_AudioSpec invalid = desired;
  invalid.freq = 48000;
  if(system->OpenAudio(&invalid, &obtained) == 0) {
    fprintf(stderr, "SDL3 accepted an incompatible mixer rate\n");
    return false;
  }
  system->CloseAudio();
  system->CloseAudio();
  system->PauseAudio(0); /* failed opens must leave lifecycle operations safe */
#endif

  if(system->OpenAudio(&desired, &obtained) != 0) {
    fprintf(stderr, "production System::OpenAudio failed: %s\n",
            SDL_GetError());
    return false;
  }
  /* SDL1 SDL_MixAudio derives its format from the open device and becomes a
     no-op after SDL_CloseAudio. Keep this dummy device open but paused until
     all deterministic, manually driven Mix calls have completed. */
  system->PauseAudio(1);

#ifdef GLTRON_SDL3_AUDIO
  if(system->OpenAudio(&desired, &obtained) == 0) {
    fprintf(stderr, "SDL3 opened a second stream over a live stream\n");
    return false;
  }
#endif

  if(obtained.freq != OUTPUT_RATE || obtained.format != NEBU_AUDIO_S16 ||
     obtained.channels != OUTPUT_CHANNELS) {
    fprintf(stderr, "production audio wrapper changed the requested format\n");
    return false;
  }
  return true;
}

} // namespace

int main(int argc, char **argv) {
  SDL_AudioSpec spec;
  SampleResult crash;
  SampleResult engine;
  SampleResult recognizer;
  MusicResult music;
  MixerResult mixer;
  bool passed = false;

  if(argc != 5) {
    fprintf(stderr, "usage: %s crash.wav engine.wav recognizer.wav track.it\n",
            argv[0]);
    return 2;
  }

  SDL_memset(&spec, 0, sizeof(spec));
  SDL_memset(&music, 0, sizeof(music));
  SDL_memset(&mixer, 0, sizeof(mixer));
  spec.freq = OUTPUT_RATE;
  spec.format = NEBU_AUDIO_S16;
  spec.channels = OUTPUT_CHANNELS;
#ifndef GLTRON_SDL3_AUDIO
  spec.samples = 1024;
#endif

  if(!nebu_InitAudio()) {
    fprintf(stderr, "SDL audio initialization failed: %s\n", SDL_GetError());
    return 1;
  }
  if(!Sound::InitDecoder()) {
    fprintf(stderr, "production decoder initialization failed\n");
    SDL_Quit();
    return 1;
  }

  {
    Sound::System system(&spec);
    passed = exerciseAudioWrappers(&system) &&
             loadSample(&system, argv[1], &crash) &&
             loadSample(&system, argv[2], &engine) &&
             loadSample(&system, argv[3], &recognizer) &&
             verifySampleMix(&system, argv[1], &mixer) &&
             verifySourceCopies(&system, argv[2], &mixer) &&
             verifySampleBoundaries(&system, &mixer) &&
             verifySpatialMix(&system, argv[2], &mixer) &&
             renderMusicPrefix(&system, argv[4], &music) &&
             verifyLoopAndEof(&system, argv[4], &music);
    system.PauseAudio(1);
    system.CloseAudio();
#ifdef GLTRON_SDL3_AUDIO
    if(passed)
      passed = exerciseAudioWrappers(&system); /* a closed stream can reopen */
    system.CloseAudio();
#endif
  }

  Sound::QuitDecoder();
  SDL_Quit();
  if(!passed)
    return 1;

  printf("crash_bytes=%d crash_fnv1a=%016llx "
         "engine_bytes=%d engine_fnv1a=%016llx "
         "recognizer_bytes=%d recognizer_fnv1a=%016llx "
         "music_prefix_bytes=%zu music_prefix_fnv1a=%016llx "
         "reset_prefix_fnv1a=%016llx loop_mixed_bytes=%zu "
         "sample_full_fnv1a=%016llx sample_half_fnv1a=%016llx "
         "copy_first_fnv1a=%016llx copy_overlap_fnv1a=%016llx "
         "one_shot_fnv1a=%016llx loop_boundary_fnv1a=%016llx "
         "copy_independent=%d one_shot_reset=%d sample_loop_boundary=%d "
         "loop_reset=%d eof_stop=%d wrappers=1 "
         "spatial_fnv1a=%016llx engine_mix_fnv1a=%016llx "
         "spatial_cursor=%d engine_cursor=%d\n",
         crash.bytes, (unsigned long long)crash.hash,
         engine.bytes, (unsigned long long)engine.hash,
         recognizer.bytes, (unsigned long long)recognizer.hash,
         music.prefix_bytes, (unsigned long long)music.prefix_hash,
         (unsigned long long)music.loop_reset_hash,
         music.loop_mixed_bytes,
         (unsigned long long)mixer.sample_full_hash,
         (unsigned long long)mixer.sample_half_hash,
         (unsigned long long)mixer.copy_first_hash,
         (unsigned long long)mixer.copy_overlap_hash,
         (unsigned long long)mixer.one_shot_hash,
         (unsigned long long)mixer.loop_boundary_hash,
         mixer.copy_cursors_independent, mixer.one_shot_reset,
         mixer.loop_boundary, music.saw_loop_reset,
         music.stopped_at_eof,
         (unsigned long long)mixer.spatial_hash,
         (unsigned long long)mixer.engine_hash,
         mixer.spatial_cursor, mixer.engine_cursor);
  return 0;
}
