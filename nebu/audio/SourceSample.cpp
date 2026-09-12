#ifndef GLTRON_NO_SOUND

#include "audio/nebu_SourceSample.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

namespace Sound {
  SourceSample::SourceSample(System *system) { 
    _system = system;

    _buffer = NULL;
    _buffersize = 8192;

    _position = 0;
    _decoded = 0;
  }

  SourceSample::~SourceSample() {
    // fprintf(stderr, "nebu_SourceSample destructor called\n");
    if(_buffer)
      delete[] _buffer;
    // Source::~Source();
  }

  void SourceSample::Load(char *filename) {
#if defined(GLTRON_SDL2_AUDIO) || defined(GLTRON_SDL3_AUDIO)
    SDL_AudioSpec loaded;
    Uint8 *loaded_buffer = NULL;
    Uint32 loaded_size = 0;
    Uint8 *decoded_buffer = NULL;
    int decoded_size = 0;

    if(!SDL_LoadWAV(filename, &loaded, &loaded_buffer, &loaded_size)) {
      fprintf(stderr, "[error] failed loading sample from '%s': %s\n",
              filename, SDL_GetError());
      return;
    }

    AudioInfo *desired = _system->GetAudioInfo();
    if(loaded_size > INT_MAX) {
      fprintf(stderr, "[error] sample '%s' is too large\n", filename);
      SDL_free(loaded_buffer);
      return;
    }

    if(loaded.format == desired->format &&
       loaded.channels == desired->channels &&
       loaded.freq == (int) desired->rate) {
      decoded_size = (int) loaded_size;
      decoded_buffer = new Uint8[decoded_size];
      memcpy(decoded_buffer, loaded_buffer, decoded_size);
    } else {
#ifdef GLTRON_SDL3_AUDIO
      SDL_AudioSpec target;
      target.freq = desired->rate;
      target.format = (SDL_AudioFormat) desired->format;
      target.channels = desired->channels;
      Uint8 *converted = NULL;
      if(!SDL_ConvertAudioSamples(&loaded, loaded_buffer, (int)loaded_size,
                                  &target, &converted, &decoded_size)) {
        fprintf(stderr, "[error] failed converting sample '%s': %s\n",
                filename, SDL_GetError());
        SDL_free(loaded_buffer);
        return;
      }
      decoded_buffer = new Uint8[decoded_size];
      memcpy(decoded_buffer, converted, decoded_size);
      SDL_free(converted);
#else
      SDL_AudioCVT cvt;
      int conversion = SDL_BuildAudioCVT(&cvt,
                                         loaded.format,
                                         loaded.channels,
                                         loaded.freq,
                                         desired->format,
                                         desired->channels,
                                         desired->rate);
      if(conversion < 0 ||
         loaded_size > (Uint32) (INT_MAX / (cvt.len_mult > 0 ? cvt.len_mult : 1))) {
        fprintf(stderr, "[error] failed preparing sample '%s': %s\n",
                filename, SDL_GetError());
        SDL_free(loaded_buffer);
        return;
      }

      cvt.len = (int) loaded_size;
      cvt.buf = (Uint8*) SDL_malloc(cvt.len * cvt.len_mult);
      if(cvt.buf == NULL) {
        fprintf(stderr, "[error] out of memory loading sample '%s'\n", filename);
        SDL_free(loaded_buffer);
        return;
      }
      memcpy(cvt.buf, loaded_buffer, loaded_size);

      if(conversion > 0 && SDL_ConvertAudio(&cvt) < 0) {
        fprintf(stderr, "[error] failed converting sample '%s': %s\n",
                filename, SDL_GetError());
        SDL_free(cvt.buf);
        SDL_free(loaded_buffer);
        return;
      }

      decoded_size = conversion > 0 ? cvt.len_cvt : cvt.len;
      decoded_buffer = new Uint8[decoded_size];
      memcpy(decoded_buffer, cvt.buf, decoded_size);
      SDL_free(cvt.buf);
#endif
    }

    SDL_free(loaded_buffer);
    if(_buffer != NULL)
      delete[] _buffer;
    _buffer = decoded_buffer;
    _buffersize = decoded_size;
#else
#define BUFSIZE 1024 * 1024
    SDL_RWops *rwops;

    rwops = SDL_RWFromFile(filename, "rb");

    Sound_Sample *sample = Sound_NewSample(rwops, NULL,
					   _system->GetAudioInfo(),
					   _buffersize );
    if(sample == NULL) {
      fprintf(stderr, "[error] failed loading sample from '%s': %s\n",
	      filename, Sound_GetError());
      return;
    }
    
    Sound_DecodeAll(sample);

    _buffersize = sample->buffer_size;
    _buffer = new Uint8[_buffersize];
    memcpy(_buffer, sample->buffer, _buffersize);

    Sound_FreeSample(sample);
#endif
    
    // fprintf(stderr, "done decoding sample '%s'\n", filename);
    _position = 0;
  }

  int SourceSample::Mix(Uint8 *data, int len) {
    if(_buffer == NULL)
      return 0;

    int volume = (int)(_volume * NEBU_MIX_MAXVOLUME);
    assert(len < _buffersize);

    if(len < _buffersize - _position) {
      nebu_MixAudio(data, _buffer + _position, len, volume);
      _position += len;
    } else { 
      nebu_MixAudio(data, _buffer + _position,
		                 _buffersize - _position, volume);
      len -= _buffersize - _position;

      // printf("end of sample reached!\n");
      if(_loop) {
	if(_loop != 255) 
	  _loop--;

	_position = 0;
	nebu_MixAudio(data, _buffer + _position, len, volume);
	_position += len;
      } else {
	_isPlaying = 0;
      }
    }
    return 1;
  }
}

#endif
