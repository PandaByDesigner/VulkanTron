#ifndef GLTRON_NO_SOUND

#include "audio/nebu_SourceMusic.h"

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <limits.h>

namespace Sound {
#if defined(GLTRON_SDL2_AUDIO) || defined(GLTRON_SDL3_AUDIO)
  namespace {
    const unsigned kMaxWavBytes = 64u * 1024u * 1024u;

    Uint32 little32(const Uint8 *bytes) {
      return (Uint32)bytes[0] | ((Uint32)bytes[1] << 8) |
             ((Uint32)bytes[2] << 16) | ((Uint32)bytes[3] << 24);
    }

    unsigned little16(const Uint8 *bytes) {
      return (unsigned)bytes[0] | ((unsigned)bytes[1] << 8);
    }

    bool wavExtension(const char *filename) {
      const char *extension = strrchr(filename, '.');
      return extension != NULL && strlen(extension) == 4 &&
        tolower((unsigned char)extension[1]) == 'w' &&
        tolower((unsigned char)extension[2]) == 'a' &&
        tolower((unsigned char)extension[3]) == 'v';
    }

    /* SDL's WAV loader intentionally tolerates some truncated files. Check the
       complete RIFF structure and allocation bounds first, before it allocates.
       The authored music contract is PCM16 mono/stereo, 8..192 kHz. */
    bool validWav(const char *filename, Uint32 *data_bytes,
                  unsigned *channels, Uint32 *rate) {
      FILE *file = fopen(filename, "rb");
      if(file == NULL) return false;
      Uint8 header[16];
      bool valid = false, have_format = false, have_data = false;
      long file_size;
      uint64_t end, cursor;
      if(fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) < 12 ||
         (unsigned long)file_size > kMaxWavBytes ||
         fseek(file, 0, SEEK_SET) != 0 || fread(header, 1, 12, file) != 12 ||
         memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0)
        goto done;
      end = (uint64_t)little32(header + 4) + 8;
      if(end < 12 || end > (uint64_t)file_size) goto done;
      cursor = 12;
      while(cursor < end) {
        if(end - cursor < 8 || fseek(file, (long)cursor, SEEK_SET) != 0 ||
           fread(header, 1, 8, file) != 8) goto done;
        const Uint32 size = little32(header + 4);
        const uint64_t next = cursor + 8 + size + (size & 1);
        if(next > end) goto done;
        if(memcmp(header, "fmt ", 4) == 0) {
          if(have_format || size < 16 || fread(header, 1, 16, file) != 16)
            goto done;
          *channels = little16(header + 2);
          *rate = little32(header + 4);
          if(little16(header) != 1 || (*channels != 1 && *channels != 2) ||
             *rate < 8000 || *rate > 192000 || little16(header + 14) != 16 ||
             little16(header + 12) != *channels * 2 ||
             little32(header + 8) != *rate * *channels * 2) goto done;
          have_format = true;
        } else if(memcmp(header, "data", 4) == 0) {
          if(!have_format || have_data || size == 0 || size % (*channels * 2))
            goto done;
          *data_bytes = size;
          have_data = true;
        }
        cursor = next;
      }
      valid = have_format && have_data;
    done:
      fclose(file);
      return valid;
    }
  }

  int SourceMusic::LoadWav(void) {
    AudioInfo *desired = _system->GetAudioInfo();
    Uint32 data_bytes = 0, rate = 0;
    unsigned channels = 0;
    if(desired->format != NEBU_AUDIO_S16 || desired->channels != 2 ||
       desired->rate != 22050 ||
       !validWav(_filename, &data_bytes, &channels, &rate) ||
       /* Include a small resampler tail in the conservative output bound. */
       (((uint64_t)data_bytes / (channels * 2) * desired->rate / rate) + 64) * 4
         > kMaxWavBytes) {
      fprintf(stderr, "[error] invalid, unsupported or oversized PCM16 music '%s'\n",
              _filename);
      return 0;
    }

    SDL_AudioSpec loaded;
    Uint8 *pcm = NULL;
    Uint32 size = 0;
    if(!SDL_LoadWAV(_filename, &loaded, &pcm, &size)) {
      fprintf(stderr, "[error] failed loading WAV music '%s': %s\n",
              _filename, SDL_GetError());
      return 0;
    }
    if(size != data_bytes || loaded.channels != (int)channels ||
       loaded.freq != (int)rate || size > INT_MAX) {
      SDL_free(pcm);
      return 0;
    }
    int converted_size = (int)size;
    if(loaded.format != desired->format || loaded.channels != desired->channels ||
       loaded.freq != (int)desired->rate) {
#ifdef GLTRON_SDL3_AUDIO
      SDL_AudioSpec target;
      target.format = (SDL_AudioFormat)desired->format;
      target.channels = desired->channels;
      target.freq = desired->rate;
      Uint8 *converted = NULL;
      if(!SDL_ConvertAudioSamples(&loaded, pcm, (int)size, &target,
                                 &converted, &converted_size)) {
        fprintf(stderr, "[error] failed converting WAV music '%s': %s\n",
                _filename, SDL_GetError());
        SDL_free(pcm);
        return 0;
      }
      SDL_free(pcm);
      pcm = converted;
#else
      SDL_AudioCVT cvt;
      const int conversion = SDL_BuildAudioCVT(&cvt, loaded.format,
        loaded.channels, loaded.freq, desired->format, desired->channels,
        desired->rate);
      if(conversion < 0 || cvt.len_mult <= 0 ||
         size > kMaxWavBytes / (unsigned)cvt.len_mult) {
        SDL_free(pcm);
        return 0;
      }
      cvt.len = (int)size;
      cvt.buf = (Uint8*)SDL_malloc(size * cvt.len_mult);
      if(cvt.buf == NULL) { SDL_free(pcm); return 0; }
      memcpy(cvt.buf, pcm, size);
      SDL_free(pcm);
      if(conversion > 0 && SDL_ConvertAudio(&cvt) < 0) {
        SDL_free(cvt.buf);
        return 0;
      }
      pcm = cvt.buf;
      converted_size = conversion > 0 ? cvt.len_cvt : cvt.len;
#endif
    }
    if(converted_size <= 0 || (unsigned)converted_size > kMaxWavBytes ||
       converted_size % 4 != 0) {
      SDL_free(pcm);
      return 0;
    }
    _wav_buffer = pcm;
    _wav_size = converted_size;
    _wav_position = 0;
    return 1;
  }
#endif

  SourceMusic::SourceMusic(System *system) { 
    _system = system;

#if defined(GLTRON_SDL2_AUDIO) || defined(GLTRON_SDL3_AUDIO)
    _module = NULL;
    _wav_buffer = NULL;
    _wav_size = 0;
    _wav_position = 0;
#else
    _sample = NULL;
#endif

		_sample_buffersize = 8192;
    _buffersize = 20 * _sample_buffersize;
		_buffer = (Uint8*) malloc( _buffersize );
		if(_buffer != NULL)
			memset(_buffer, 0, _buffersize);
#if defined(GLTRON_SDL2_AUDIO) || defined(GLTRON_SDL3_AUDIO)
    _sample_buffer = (Uint8*) malloc(_sample_buffersize);
#endif

		_decoded = 0;
    _read = 0;

    _filename = NULL;
#if !defined(GLTRON_SDL2_AUDIO) && !defined(GLTRON_SDL3_AUDIO)
    _rwops = NULL;
#endif
  }

  SourceMusic::~SourceMusic() { 
    // fprintf(stderr, "nebu_SourceMusic destructor called\n");
#ifndef macintosh
		nebu_AudioSemWait(_sem);
#else
        SDL_LockAudio();
#endif
		free(_buffer);

    CleanUp();
#if defined(GLTRON_SDL2_AUDIO) || defined(GLTRON_SDL3_AUDIO)
    free(_sample_buffer);
    _sample_buffer = NULL;
#endif

    if(_filename)
      free(_filename);

#ifndef macintosh		
		nebu_AudioSemPost(_sem);
#else
        SDL_UnlockAudio();
#endif
  }

	/*! 
		\fn int SourceMusic::CreateSample(void)
		
		call this function only between semaphores
	*/

  int SourceMusic::CreateSample(void) {
#if defined(GLTRON_SDL2_AUDIO) || defined(GLTRON_SDL3_AUDIO)
    if(wavExtension(_filename))
      return LoadWav();
    AudioInfo *info = _system->GetAudioInfo();
    if(info->format != NEBU_AUDIO_S16 || info->channels != 2) {
      fprintf(stderr,
              "[error] tracker decoder requires signed 16-bit stereo output\n");
      return 0;
    }

    LockDecoder();
    _module = Player_Load(_filename, 64, 0);
    if(_module != NULL) {
      _module->extspd = 1;
      _module->panflag = 1;
      _module->wrap = 0;
      _module->loop = 0;

      if(md_mixfreq == 0)
        md_mixfreq = (UWORD) info->rate;

      Player_Start(_module);
      Player_SetPosition(0);
    }
    UnlockDecoder();

    if(_module == NULL) {
      fprintf(stderr, "[error] failed loading tracker module %s: %s\n",
              _filename, MikMod_strerror(MikMod_errno));
      return 0;
    }
#else
    _rwops = SDL_RWFromFile(_filename, "rb");
		if(_rwops == NULL) {
			fprintf(stderr, "[error] failed opening sample %s: %s\n",
						_filename, SDL_GetError());
			return 0;
		}
	char *ext = _filename;
	for(int i = 0; *(_filename + i); i++)
	{
		if(*(_filename + i) == '.')
			ext = _filename + i + 1;
	}
    _sample = Sound_NewSample(_rwops, ext,
															_system->GetAudioInfo(),
															_sample_buffersize );

    if(_sample == NULL) {
		fprintf(stderr, "[error] failed loading sample type %s, from %s: %s\n", ext,
			_filename, Sound_GetError());
		_rwops = NULL; /* Sound_NewSample owns the RWops even on failure. */
		return 0;
	}
#endif

    _read = 0;
    _decoded = 0;
    // fprintf(stderr, "created sample\n");
    return 1;
  }

  int SourceMusic::Load(char *filename) {
		if(filename == NULL) return 0;
		if(_buffer == NULL
#if defined(GLTRON_SDL2_AUDIO) || defined(GLTRON_SDL3_AUDIO)
		   || _sample_buffer == NULL
#endif
		  ) {
			fprintf(stderr, "[error] out of memory creating music buffers\n");
			return 0;
		}

		CleanUp();
		free(_filename);
		_filename = NULL;
		size_t n = strlen(filename);
		_filename = (char*) malloc(n + 1);
		if(_filename == NULL)
			return 0;
		memcpy(_filename, filename, n + 1);
    return CreateSample();
  }

  int SourceMusic::HasSample(void) const {
#if defined(GLTRON_SDL2_AUDIO) || defined(GLTRON_SDL3_AUDIO)
    return _module != NULL || _wav_buffer != NULL;
#else
    return _sample != NULL;
#endif
  }

  void SourceMusic::CleanUp(void) {
		_read = 0;
    _decoded = 0;

#if defined(GLTRON_SDL2_AUDIO) || defined(GLTRON_SDL3_AUDIO)
    SDL_free(_wav_buffer);
    _wav_buffer = NULL;
    _wav_size = 0;
    _wav_position = 0;
    if(_module != NULL) {
      LockDecoder();
      Player_Free(_module);
      _module = NULL;
      UnlockDecoder();
    }
#else
    if(_sample != NULL) {
      Sound_FreeSample(_sample);
      _sample = NULL;
      _rwops = NULL;
    }
#endif
  }

  int SourceMusic::Mix(Uint8 *data, int len) {
#ifndef macintosh
		if(nebu_AudioSemTryWait(_sem))
			return 0;
#endif
	if(!HasSample()) {
#ifndef macintosh
			nebu_AudioSemPost(_sem);
#endif
      return 0;
    }
		// printf("mixing %d bytes\n", len);

    int volume = (int)(_volume * NEBU_MIX_MAXVOLUME);
#if defined(GLTRON_SDL2_AUDIO) || defined(GLTRON_SDL3_AUDIO)
    if(_wav_buffer != NULL) {
      if(len < 0 || len % 4 != 0) {
#ifndef macintosh
        nebu_AudioSemPost(_sem);
#endif
        return 0;
      }
      while(len > 0 && _isPlaying) {
        int count = _wav_size - _wav_position;
        if(count > len) count = len;
        nebu_MixAudio(data, _wav_buffer + _wav_position, count, volume);
        _wav_position += count;
        data += count;
        len -= count;
        if(_wav_position == _wav_size) {
          if(_loop != 0) {
            if(_loop != 255) --_loop;
            _wav_position = 0;
          } else {
            _isPlaying = 0;
          }
        }
      }
#ifndef macintosh
      nebu_AudioSemPost(_sem);
#endif
      return 1;
    }
#endif
    // fprintf(stderr, "setting volume to %.3f -> %d\n", _volume, volume);
    // fprintf(stderr, "entering mixer\n");
		
    if(len < (_decoded - _read + _buffersize) % _buffersize) {
			// enough data to mix
			if(_read + len <= _buffersize) {
				nebu_MixAudio(data, _buffer + _read, len, volume);
				_read = (_read + len) % _buffersize;
			} else {
				// wrap around in buffer
				nebu_MixAudio(data, _buffer + _read,
				                   _buffersize - _read, volume);
				len -= _buffersize - _read;
				nebu_MixAudio(data + _buffersize - _read, _buffer,
				                   len, volume);
				_read = len;
			}
		} else {
			// buffer under-run; don't do anything
		}

#ifndef macintosh
		nebu_AudioSemPost(_sem);
#endif
    return 1;
	}

	void SourceMusic::Idle(void) {
#if defined(GLTRON_SDL2_AUDIO) || defined(GLTRON_SDL3_AUDIO)
    if(_wav_buffer != NULL) return;
#endif
#ifndef macintosh
		if(nebu_AudioSemWait(_sem))
			return;
#else
		SDL_LockAudio();
#endif

		if(!HasSample()) {
#ifndef macintosh
			nebu_AudioSemPost(_sem);
#else
			SDL_UnlockAudio();
#endif
			return;
		}

		// printf("idling\n");
		while( _read == _decoded || 
					 (_read - _decoded + _buffersize) % _buffersize >
					 _sample_buffersize )	{
			// if(_read == _decoded)	printf("_read == _decoded == %d\n", _read);
			// fill the buffer
#if defined(GLTRON_SDL2_AUDIO) || defined(GLTRON_SDL3_AUDIO)
      int at_end;
      int count;
      LockDecoder();
      Player_Start(_module);
      at_end = !Player_Active();
      count = at_end ? 0 : (int) VC_WriteBytes((SBYTE*) _sample_buffer,
                                               _sample_buffersize);
      UnlockDecoder();
#else
			int count = Sound_Decode(_sample);
#endif
			// printf("adding %d bytes to buffer\n", count);
			if(count <= _buffersize - _decoded) {
#if defined(GLTRON_SDL2_AUDIO) || defined(GLTRON_SDL3_AUDIO)
				memcpy(_buffer + _decoded, _sample_buffer, count);
#else
				memcpy(_buffer + _decoded, _sample->buffer, count);
#endif
			} else {
				// wrapping around end of buffer (usually doesn't happen when 
				// _buffersize is a multiple of _sample_buffersize)
				// printf("wrapping around end of buffer\n");
#if defined(GLTRON_SDL2_AUDIO) || defined(GLTRON_SDL3_AUDIO)
				memcpy(_buffer + _decoded, _sample_buffer, _buffersize - _decoded);
				memcpy(_buffer, _sample_buffer + _buffersize - _decoded,
							 count - (_buffersize - _decoded));
#else
				memcpy(_buffer + _decoded, _sample->buffer, _buffersize - _decoded);
				memcpy(_buffer, (Uint8*) _sample->buffer + _buffersize - _decoded,
							 count - (_buffersize - _decoded));
#endif
			}
			_decoded = (_decoded + count) % _buffersize;

			// check for end of sample, loop
#if defined(GLTRON_SDL2_AUDIO) || defined(GLTRON_SDL3_AUDIO)
      if(at_end) {
#else
			if((_sample->flags & SOUND_SAMPLEFLAG_ERROR) ||
			   (_sample->flags & SOUND_SAMPLEFLAG_EOF)) {
#endif
				// some error has occured, maybe end of sample reached
				// todo: let playback finish, because there's still data
				// in the buffer that has to be mixed
				CleanUp();
				// fprintf(stderr, "end of sample reached!\n");
				if(_loop) {
					// fprintf(stderr, "looping music\n");
					if(_loop != 255) 
						_loop--;
					CreateSample();
					if(!HasSample()) {
#ifndef macintosh
						_system->Lock();
#endif
						_isPlaying = 0;
#ifndef macintosh
						_system->Unlock();
#endif
						break;
					}
				} else {
#ifndef macintosh
					_system->Lock();
#endif
					_isPlaying = 0;
#ifndef macintosh
					_system->Unlock();
#endif
					// todo: notify sound system (maybe load another song?)
					break;
				}
			}
		} // buffer has been filled

#ifndef macintosh
		nebu_AudioSemPost(_sem);
#else
		SDL_UnlockAudio();
#endif
	}
}

#endif
