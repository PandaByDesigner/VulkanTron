#include <SDL.h>

#ifdef GLTRON_AUDIO_CLASSIC
#include <SDL_sound.h>
#else
#include <mikmod.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define OUTPUT_RATE 22050
#define OUTPUT_CHANNELS 2
#define DECODE_CHUNK 8192u
#define TRACK_PREFIX_BYTES (4u * 1024u * 1024u)
#define MAX_TRACK_BYTES \
  ((size_t)OUTPUT_RATE * OUTPUT_CHANNELS * sizeof(int16_t) * 600u)

typedef struct DecodeResult {
  uint64_t prefix_hash;
  size_t prefix_bytes;
  size_t total_bytes;
  unsigned int format;
  unsigned int channels;
  unsigned int rate;
  int eof;
} DecodeResult;

static uint64_t hash_bytes(uint64_t hash, const unsigned char *data,
                           size_t size) {
  size_t i;

  for(i = 0; i < size; ++i) {
    hash ^= data[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static void consume_bytes(DecodeResult *result, const unsigned char *data,
                          size_t size) {
  size_t remaining = TRACK_PREFIX_BYTES - result->prefix_bytes;
  size_t hashed = size < remaining ? size : remaining;

  result->prefix_hash = hash_bytes(result->prefix_hash, data, hashed);
  result->prefix_bytes += hashed;
  result->total_bytes += size;
}

#ifdef GLTRON_AUDIO_CLASSIC

static int decode_asset(const char *kind, const char *path,
                        DecodeResult *result) {
  Sound_AudioInfo desired;
  Sound_Sample *sample;

  (void)kind;
  desired.format = AUDIO_S16SYS;
  desired.channels = OUTPUT_CHANNELS;
  desired.rate = OUTPUT_RATE;

  if(!Sound_Init()) {
    fprintf(stderr, "Sound_Init failed: %s\n", Sound_GetError());
    return 0;
  }

  sample = Sound_NewSampleFromFile(path, &desired, DECODE_CHUNK);
  if(sample == NULL) {
    fprintf(stderr, "SDL_sound failed to load %s: %s\n", path,
            Sound_GetError());
    Sound_Quit();
    return 0;
  }

  result->format = sample->desired.format;
  result->channels = sample->desired.channels;
  result->rate = sample->desired.rate;

  for(;;) {
    Uint32 count = Sound_Decode(sample);

    if(count != 0)
      consume_bytes(result, (const unsigned char *)sample->buffer, count);

    if(sample->flags & SOUND_SAMPLEFLAG_ERROR) {
      fprintf(stderr, "SDL_sound failed while decoding %s: %s\n", path,
              Sound_GetError());
      Sound_FreeSample(sample);
      Sound_Quit();
      return 0;
    }
    if(sample->flags & SOUND_SAMPLEFLAG_EOF) {
      result->eof = 1;
      break;
    }
    if(count == 0) {
      fprintf(stderr, "SDL_sound stalled while decoding %s\n", path);
      Sound_FreeSample(sample);
      Sound_Quit();
      return 0;
    }
    if(result->total_bytes > MAX_TRACK_BYTES) {
      fprintf(stderr, "SDL_sound exceeded the ten-minute decode bound: %s\n",
              path);
      Sound_FreeSample(sample);
      Sound_Quit();
      return 0;
    }
  }

  Sound_FreeSample(sample);
  Sound_Quit();
  return 1;
}

#else

static int decode_wav(const char *path, DecodeResult *result) {
  SDL_AudioSpec spec;
  Uint8 *buffer = NULL;
  Uint32 size = 0;

  if(SDL_LoadWAV(path, &spec, &buffer, &size) == NULL) {
    fprintf(stderr, "SDL2 failed to load %s: %s\n", path, SDL_GetError());
    return 0;
  }

  result->format = spec.format;
  result->channels = spec.channels;
  result->rate = (unsigned int)spec.freq;
  consume_bytes(result, buffer, size);
  result->eof = 1;
  SDL_FreeWAV(buffer);
  return 1;
}

static int decode_tracker(const char *path, DecodeResult *result) {
  unsigned char buffer[DECODE_CHUNK];
  MODULE *module = NULL;
  int player_started = 0;
  int success = 0;

  MikMod_RegisterDriver(&drv_nos);
  if(MikMod_InfoLoader() == NULL)
    MikMod_RegisterAllLoaders();

  md_mode |= DMODE_SOFT_MUSIC | DMODE_16BITS | DMODE_STEREO;
  md_mode &= ~DMODE_FLOAT;
  md_mixfreq = 0;
  md_reverb = 1;

  if(MikMod_Init("")) {
    fprintf(stderr, "MikMod_Init failed: %s\n",
            MikMod_strerror(MikMod_errno));
    return 0;
  }

  module = Player_Load(path, 64, 0);
  if(module == NULL) {
    fprintf(stderr, "libmikmod failed to load %s: %s\n", path,
            MikMod_strerror(MikMod_errno));
    goto cleanup;
  }

  module->extspd = 1;
  module->panflag = 1;
  module->wrap = 0;
  module->loop = 0;
  if(md_mixfreq == 0)
    md_mixfreq = OUTPUT_RATE;

  result->format = AUDIO_S16SYS;
  result->channels = OUTPUT_CHANNELS;
  result->rate = OUTPUT_RATE;

  Player_Start(module);
  player_started = 1;
  Player_SetPosition(0);

  for(;;) {
    ULONG count;

    /* SDL_sound's MikMod decoder selects its module before every block. */
    Player_Start(module);
    if(!Player_Active()) {
      result->eof = 1;
      success = 1;
      break;
    }

    count = VC_WriteBytes((SBYTE *)buffer, DECODE_CHUNK);
    if(count == 0 || count > DECODE_CHUNK) {
      fprintf(stderr, "libmikmod returned an invalid block size for %s\n",
              path);
      break;
    }
    consume_bytes(result, buffer, count);
    if(result->total_bytes > MAX_TRACK_BYTES) {
      fprintf(stderr, "libmikmod exceeded the ten-minute decode bound: %s\n",
              path);
      break;
    }
  }

cleanup:
  if(player_started)
    Player_Stop();
  if(module != NULL)
    Player_Free(module);
  MikMod_Exit();
  md_mixfreq = 0;
  return success;
}

static int decode_asset(const char *kind, const char *path,
                        DecodeResult *result) {
  if(strcmp(kind, "wav") == 0)
    return decode_wav(path, result);
  if(strcmp(kind, "it") == 0)
    return decode_tracker(path, result);

  fprintf(stderr, "unsupported asset kind: %s\n", kind);
  return 0;
}

#endif

int main(int argc, char **argv) {
  DecodeResult result;

  if(argc != 3 ||
     (strcmp(argv[1], "wav") != 0 && strcmp(argv[1], "it") != 0)) {
    fprintf(stderr, "usage: %s wav|it asset\n", argv[0]);
    return 2;
  }

  memset(&result, 0, sizeof(result));
  result.prefix_hash = UINT64_C(14695981039346656037);

  if(SDL_Init(0) != 0) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }
  if(!decode_asset(argv[1], argv[2], &result)) {
    SDL_Quit();
    return 1;
  }
  SDL_Quit();

  if(result.total_bytes % (OUTPUT_CHANNELS * sizeof(int16_t)) != 0) {
    fprintf(stderr, "decoded byte count is not frame-aligned: %zu\n",
            result.total_bytes);
    return 1;
  }

  printf("format=%u channels=%u rate=%u prefix_bytes=%zu "
         "prefix_fnv1a=%016llx total_bytes=%zu frames=%zu eof=%d\n",
         result.format, result.channels, result.rate, result.prefix_bytes,
         (unsigned long long)result.prefix_hash, result.total_bytes,
         result.total_bytes / (OUTPUT_CHANNELS * sizeof(int16_t)),
         result.eof);
  return result.eof ? 0 : 1;
}
