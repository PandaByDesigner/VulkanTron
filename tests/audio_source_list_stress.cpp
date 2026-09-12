#include "audio/nebu_SoundSystem.h"
#include "audio/nebu_SourceCopy.h"
#include "audio/nebu_SourceMusic.h"
#include "audio/nebu_SourceSample.h"

#include "audio/nebu_AudioSDL.h"
#if !defined(GLTRON_SDL2_AUDIO) && !defined(GLTRON_SDL3_AUDIO)
#include <SDL_sound.h>
#endif

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {

std::atomic<unsigned long> created(0);
std::atomic<unsigned long> destroyed(0);
std::atomic<unsigned long> mixed(0);
std::atomic<unsigned long> copies_destroyed(0);
std::atomic<unsigned long> copies_mixed(0);
std::atomic<unsigned long> music_destroyed(0);
std::atomic<unsigned long> music_mixed(0);
std::atomic<unsigned long> blocking_destroyed(0);
std::atomic<unsigned long> invalid_mix_blocks(0);

class DecoderGuard {
public:
  DecoderGuard() {}

  ~DecoderGuard() {
    Sound::QuitDecoder();
    SDL_Quit();
  }

private:
  DecoderGuard(const DecoderGuard &);
  DecoderGuard &operator=(const DecoderGuard &);
};

class OneShotSource : public Sound::Source {
public:
  OneShotSource() { ++created; }

  virtual ~OneShotSource() { ++destroyed; }

  virtual int Mix(Uint8 *, int len) {
#ifdef GLTRON_SDL3_AUDIO
    if(len != Sound::System::kMixChunkBytes)
      ++invalid_mix_blocks;
#else
    (void)len;
#endif
    ++mixed;
    _isPlaying = 0;
    return 1;
  }
};

class TrackedSourceCopy : public Sound::SourceCopy {
public:
  explicit TrackedSourceCopy(Sound::SourceSample *source)
      : Sound::SourceCopy(source) {}

  virtual ~TrackedSourceCopy() { ++copies_destroyed; }

  virtual int Mix(Uint8 *data, int len) {
    ++copies_mixed;
    return Sound::SourceCopy::Mix(data, len);
  }
};

class TrackedMusic : public Sound::SourceMusic {
public:
  explicit TrackedMusic(Sound::System *system) : Sound::SourceMusic(system) {}

  virtual ~TrackedMusic() { ++music_destroyed; }

  virtual int Mix(Uint8 *data, int len) {
    int result = Sound::SourceMusic::Mix(data, len);
    if (result)
      ++music_mixed;
    return result;
  }
};

class BlockingSource : public Sound::Source {
public:
  BlockingSource(NebuAudioSemaphore *entered, NebuAudioSemaphore *release)
      : entered_(entered), release_(release) {}

  virtual ~BlockingSource() { ++blocking_destroyed; }

  virtual int Mix(Uint8 *, int) {
    nebu_AudioSemPost(entered_);
    nebu_AudioSemWait(release_);
    _isPlaying = 0;
    return 1;
  }

private:
  NebuAudioSemaphore *entered_;
  NebuAudioSemaphore *release_;
};

bool parseSourceCount(const char *value, unsigned long *result) {
  char *end = NULL;
  errno = 0;
  unsigned long parsed = std::strtoul(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || parsed == 0 ||
      parsed > 10000000UL)
    return false;

  *result = parsed;
  return true;
}

bool drainSources(Sound::System *system,
                  std::atomic<unsigned long> *destroyed_counter,
                  unsigned long expected, Uint32 timeout_ms) {
  Uint32 deadline = SDL_GetTicks() + timeout_ms;
  while (destroyed_counter->load() < expected) {
    system->Idle();
    if (static_cast<Sint32>(SDL_GetTicks() - deadline) >= 0)
      return false;
    SDL_Delay(1);
  }
  return true;
}

void closeAudio(Sound::System *system) {
  system->PauseAudio(1);
  system->SetStatus(Sound::eUninitialized);
  system->CloseAudio();
}

bool runContentionCase(Sound::System *system, bool add_source) {
  NebuAudioSemaphore *entered = SDL_CreateSemaphore(0);
  NebuAudioSemaphore *begin_release = SDL_CreateSemaphore(0);
  NebuAudioSemaphore *release = SDL_CreateSemaphore(0);
  if (entered == NULL || begin_release == NULL || release == NULL) {
    std::fprintf(stderr, "failed to allocate contention semaphores: %s\n",
                 SDL_GetError());
    if (entered != NULL)
      SDL_DestroySemaphore(entered);
    if (begin_release != NULL)
      SDL_DestroySemaphore(begin_release);
    if (release != NULL)
      SDL_DestroySemaphore(release);
    return false;
  }

  BlockingSource *blocking = new BlockingSource(entered, release);
  blocking->SetRemovable();
  blocking->Start();
  system->AddSource(blocking);

  if (nebu_AudioSemWaitTimeout(entered, 5000) != 0) {
    std::fprintf(stderr, "audio callback did not enter contention source\n");
    nebu_AudioSemPost(release);
    system->Lock();
    blocking->Pause();
    system->Unlock();
    system->Idle();
    SDL_DestroySemaphore(entered);
    SDL_DestroySemaphore(begin_release);
    SDL_DestroySemaphore(release);
    return false;
  }

  std::thread releaser([begin_release, release]() {
    nebu_AudioSemWait(begin_release);
    SDL_Delay(30);
    nebu_AudioSemPost(release);
  });

  nebu_AudioSemPost(begin_release);
  Uint32 started = SDL_GetTicks();
  if (add_source) {
    OneShotSource *inserted = new OneShotSource;
    inserted->SetRemovable();
    system->AddSource(inserted);
  } else {
    system->Idle();
  }
  Uint32 elapsed = SDL_GetTicks() - started;
  releaser.join();

  system->Idle();
  SDL_DestroySemaphore(entered);
  SDL_DestroySemaphore(begin_release);
  SDL_DestroySemaphore(release);

  if (elapsed < 15) {
    std::fprintf(stderr,
                 "%s completed in %u ms while callback held audio lock\n",
                 add_source ? "AddSource" : "Idle", elapsed);
    return false;
  }
  return true;
}

} // namespace

int main(int argc, char **argv) {
  unsigned long source_count = 100000;
  const unsigned long batch_size = 128;
  const char *music_path = NULL;
#if !defined(GLTRON_SDL2_AUDIO) && !defined(GLTRON_SDL3_AUDIO)
  const char *one_shot_music_path = NULL;
#endif

#if defined(GLTRON_SDL2_AUDIO) || defined(GLTRON_SDL3_AUDIO)
  if (argc > 3 || (argc >= 2 && !parseSourceCount(argv[1], &source_count))) {
    std::fprintf(stderr,
                 "usage: %s [positive-source-count [music-path]]\n",
                 argv[0]);
    return 2;
  }
#else
  if (argc > 4 || (argc >= 2 && !parseSourceCount(argv[1], &source_count))) {
    std::fprintf(stderr,
                 "usage: %s [positive-source-count [music-path "
                 "[one-shot-music-path]]]\n",
                 argv[0]);
    return 2;
  }
#endif
  if (argc >= 3)
    music_path = argv[2];
#if !defined(GLTRON_SDL2_AUDIO) && !defined(GLTRON_SDL3_AUDIO)
  if (argc == 4)
    one_shot_music_path = argv[3];
#endif

  if (!nebu_InitAudio()) {
    std::fprintf(stderr, "SDL audio initialization failed: %s\n",
                 SDL_GetError());
    return 1;
  }
  if (!Sound::InitDecoder()) {
#if defined(GLTRON_SDL2_AUDIO) || defined(GLTRON_SDL3_AUDIO)
    std::fprintf(stderr, "tracker decoder initialization failed\n");
#else
    std::fprintf(stderr, "SDL_sound initialization failed: %s\n",
                 Sound_GetError());
#endif
    SDL_Quit();
    return 1;
  }
  DecoderGuard decoder_guard;

  SDL_AudioSpec requested;
  SDL_AudioSpec obtained;
  SDL_memset(&requested, 0, sizeof(requested));
  SDL_memset(&obtained, 0, sizeof(obtained));
  requested.freq = 22050;
  requested.format = NEBU_AUDIO_S16;
  requested.channels = 2;
#ifndef GLTRON_SDL3_AUDIO
  requested.samples = 64;
#endif

  Sound::System system(&requested);

  // A single idle pass must reap adjacent stopped sources without skipping any.
  const unsigned long deterministic_sources = 3;
  for (unsigned long i = 0; i < deterministic_sources; ++i) {
    OneShotSource *source = new OneShotSource;
    source->SetRemovable();
    system.AddSource(source);
  }
  system.Idle();
  if (destroyed.load() != deterministic_sources) {
    std::fprintf(stderr, "adjacent removal failed: created=%lu destroyed=%lu\n",
                 created.load(), destroyed.load());
    return 1;
  }

#ifndef GLTRON_SDL3_AUDIO
  requested.callback = system.GetCallback();
  requested.userdata = &system;
#endif
  if (system.OpenAudio(&requested, &obtained) != 0) {
    std::fprintf(stderr, "SDL dummy audio open failed: %s\n", SDL_GetError());
    return 1;
  }

  system.SetStatus(Sound::eInitialized);
  system.PauseAudio(0);

  if (!runContentionCase(&system, true) || !runContentionCase(&system, false)) {
    closeAudio(&system);
    return 1;
  }

  unsigned long added = 0;
  while (added < source_count) {
    unsigned long remaining = source_count - added;
    unsigned long this_batch = remaining < batch_size ? remaining : batch_size;

    for (unsigned long i = 0; i < this_batch; ++i) {
      OneShotSource *source = new OneShotSource;
      source->SetRemovable();
      source->Start();
      system.AddSource(source);
    }
    added += this_batch;

    const unsigned long expected = deterministic_sources + added;
    if (!drainSources(&system, &destroyed, expected, 5000)) {
      std::fprintf(stderr,
                   "audio source drain timed out: added=%lu mixed=%lu "
                   "destroyed=%lu\n",
                   added, mixed.load(), destroyed.load());
      closeAudio(&system);
      return 1;
    }
  }

  Sound::SourceSample *sample = new Sound::SourceSample(&system);
#ifdef GLTRON_SDL3_AUDIO
  const int callback_bytes = Sound::System::kMixChunkBytes;
#else
  const int callback_bytes =
      obtained.size > 0
          ? static_cast<int>(obtained.size)
          : requested.samples * requested.channels * sizeof(Sint16);
#endif
  sample->_buffersize = callback_bytes * 2;
  sample->_buffer = new Uint8[sample->_buffersize];
  std::memset(sample->_buffer, 0, sample->_buffersize);

  const unsigned long copy_count = source_count / 10 + 1;
  unsigned long copies_added = 0;
  while (copies_added < copy_count) {
    unsigned long remaining = copy_count - copies_added;
    unsigned long this_batch = remaining < batch_size ? remaining : batch_size;

    for (unsigned long i = 0; i < this_batch; ++i) {
      TrackedSourceCopy *copy = new TrackedSourceCopy(sample);
      copy->SetRemovable();
      copy->SetType(Sound::eSoundFX);
      copy->Start();
      system.AddSource(copy);
    }
    copies_added += this_batch;

    if (!drainSources(&system, &copies_destroyed, copies_added, 5000)) {
      std::fprintf(stderr,
                   "collision-copy drain timed out: added=%lu mixed=%lu "
                   "destroyed=%lu\n",
                   copies_added, copies_mixed.load(), copies_destroyed.load());
      closeAudio(&system);
      delete sample;
      return 1;
    }
  }

  const unsigned long music_reloads = music_path == NULL ? 0 : 16;
  TrackedMusic *active_music = NULL;
  for (unsigned long reload = 0; reload < music_reloads; ++reload) {
    TrackedMusic *new_music = new TrackedMusic(&system);
    if (!new_music->Load(const_cast<char *>(music_path))) {
      std::fprintf(stderr, "tracker music failed to load: %s\n", music_path);
      delete new_music;
      closeAudio(&system);
      delete sample;
      return 1;
    }
    new_music->SetLoop(255);
    new_music->SetType(Sound::eSoundMusic);

    if (active_music != NULL) {
      system.Lock();
      active_music->Pause();
      active_music->SetRemovable();
      system.Unlock();
    }
    system.AddSource(new_music);
    system.Lock();
    new_music->Start();
    system.Unlock();
    active_music = new_music;

    for (int i = 0; i < 20; ++i) {
      system.Idle();
      SDL_Delay(1);
    }
  }

  if (active_music != NULL) {
    system.Lock();
    active_music->Pause();
    active_music->SetRemovable();
    system.Unlock();
  }
  if (!drainSources(&system, &music_destroyed, music_reloads, 5000)) {
    std::fprintf(stderr,
                 "music reload drain timed out: mixed=%lu destroyed=%lu\n",
                 music_mixed.load(), music_destroyed.load());
    closeAudio(&system);
    delete sample;
    return 1;
  }

  unsigned long expected_music_destroyed = music_reloads;
#if !defined(GLTRON_SDL2_AUDIO) && !defined(GLTRON_SDL3_AUDIO)
  const unsigned long one_shot_music_count = one_shot_music_path == NULL ? 0 : 1;
  if (one_shot_music_path != NULL) {
    TrackedMusic *one_shot_music = new TrackedMusic(&system);
    if (!one_shot_music->Load(const_cast<char *>(one_shot_music_path))) {
      std::fprintf(stderr, "one-shot music failed to load: %s\n",
                   one_shot_music_path);
      delete one_shot_music;
      closeAudio(&system);
      delete sample;
      return 1;
    }
    one_shot_music->SetLoop(0);
    one_shot_music->SetType(Sound::eSoundMusic);
    system.AddSource(one_shot_music);
    system.Lock();
    one_shot_music->Start();
    system.Unlock();

    Uint32 deadline = SDL_GetTicks() + 5000;
    bool playing = true;
    while (playing) {
      system.Idle();
      system.Lock();
      playing = one_shot_music->IsPlaying() != 0;
      system.Unlock();
      if (static_cast<Sint32>(SDL_GetTicks() - deadline) >= 0)
        break;
      SDL_Delay(1);
    }
    if (playing) {
      std::fprintf(stderr, "one-shot music did not reach EOF\n");
      closeAudio(&system);
      delete sample;
      return 1;
    }

    system.Lock();
    one_shot_music->SetRemovable();
    system.Unlock();
  }
  expected_music_destroyed += one_shot_music_count;
#endif

  if (!drainSources(&system, &music_destroyed, expected_music_destroyed,
                    5000)) {
    std::fprintf(stderr, "one-shot music drain timed out: destroyed=%lu\n",
                 music_destroyed.load());
    closeAudio(&system);
    delete sample;
    return 1;
  }

  closeAudio(&system);
  delete sample;

  const unsigned long contention_insert_sources = 1;
  const unsigned long expected_created =
      deterministic_sources + contention_insert_sources + source_count;
  if (created.load() != expected_created ||
      destroyed.load() != expected_created || mixed.load() != source_count ||
      blocking_destroyed.load() != 2 || invalid_mix_blocks.load() != 0 ||
      copies_destroyed.load() != copy_count ||
      copies_mixed.load() < copy_count ||
      (expected_music_destroyed != 0 &&
       (music_destroyed.load() != expected_music_destroyed ||
        music_mixed.load() == 0))) {
    std::fprintf(stderr,
                 "counter mismatch: created=%lu destroyed=%lu mixed=%lu "
                 "expected=%lu copies=%lu/%lu music=%lu/%lu\n",
                 created.load(), destroyed.load(), mixed.load(),
                 expected_created, copies_mixed.load(), copies_destroyed.load(),
                 music_mixed.load(), music_destroyed.load());
    return 1;
  }

  std::printf("PASS: %lu synthetic and %lu collision-copy sources reaped; "
              "%lu adjacent stopped sources reaped in one idle pass; "
              "2 callback-lock contention gates held; "
              "%lu music streams retired cleanly\n",
              source_count, copy_count, deterministic_sources,
              expected_music_destroyed);
  return 0;
}
