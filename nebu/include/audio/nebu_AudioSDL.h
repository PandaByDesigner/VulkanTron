#ifndef NEBU_AUDIO_SDL_H
#define NEBU_AUDIO_SDL_H

/* Keep SDL's platform migration outside the classic mixer's PCM contract. */
#ifdef GLTRON_SDL3_AUDIO
#include <SDL3/SDL.h>
typedef SDL_Mutex NebuAudioMutex;
typedef SDL_Semaphore NebuAudioSemaphore;
#define NEBU_AUDIO_S16 SDL_AUDIO_S16
static inline int nebu_AudioSemWait(NebuAudioSemaphore *sem) {
  SDL_WaitSemaphore(sem);
  return 0;
}
static inline int nebu_AudioSemTryWait(NebuAudioSemaphore *sem) {
  return SDL_TryWaitSemaphore(sem) ? 0 : 1;
}
static inline int nebu_AudioSemWaitTimeout(NebuAudioSemaphore *sem, Uint32 ms) {
  return SDL_WaitSemaphoreTimeout(sem, (Sint32)ms) ? 0 : 1;
}
static inline void nebu_AudioSemPost(NebuAudioSemaphore *sem) {
  SDL_SignalSemaphore(sem);
}
#else
#include <SDL.h>
typedef SDL_mutex NebuAudioMutex;
typedef SDL_sem NebuAudioSemaphore;
#define NEBU_AUDIO_S16 AUDIO_S16SYS
#define nebu_AudioSemWait SDL_SemWait
#define nebu_AudioSemTryWait SDL_SemTryWait
#define nebu_AudioSemWaitTimeout SDL_SemWaitTimeout
#define nebu_AudioSemPost SDL_SemPost
#endif

#define NEBU_MIX_MAXVOLUME 128

static inline int nebu_InitAudio(void) {
#ifdef GLTRON_SDL3_AUDIO
  return SDL_Init(SDL_INIT_AUDIO);
#else
  return SDL_Init(SDL_INIT_AUDIO) == 0;
#endif
}

static inline void nebu_MixAudio(Uint8 *dst, Uint8 *src, Uint32 len, int volume) {
#ifdef GLTRON_SDL3_AUDIO
  /* SDL3 mixes with floating-point volume. Preserve SDL1/2's integer division
     and saturation exactly, including negative samples and fractional volume.
     memcpy permits samples with arbitrary byte alignment. */
  Uint32 i;
  for(i = 0; i + sizeof(Sint16) <= len; i += sizeof(Sint16)) {
    Sint16 input, output;
    int mixed;
    SDL_memcpy(&input, src + i, sizeof(input));
    SDL_memcpy(&output, dst + i, sizeof(output));
    mixed = output + (int)input * volume / NEBU_MIX_MAXVOLUME;
    if(mixed > 32767) mixed = 32767;
    if(mixed < -32768) mixed = -32768;
    output = (Sint16)mixed;
    SDL_memcpy(dst + i, &output, sizeof(output));
  }
#elif defined(GLTRON_SDL2_AUDIO)
  SDL_MixAudioFormat(dst, src, AUDIO_S16SYS, len, volume);
#else
  SDL_MixAudio(dst, src, len, volume);
#endif
}

#endif
