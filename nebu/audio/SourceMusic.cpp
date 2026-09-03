#ifndef GLTRON_NO_SOUND

#include "audio/nebu_SourceMusic.h"

#include <string.h>
#include <stdlib.h>

namespace Sound {
  SourceMusic::SourceMusic(System *system) { 
    _system = system;

#ifdef GLTRON_SDL2_AUDIO
    _module = NULL;
#else
    _sample = NULL;
#endif

		_sample_buffersize = 8192;
    _buffersize = 20 * _sample_buffersize;
		_buffer = (Uint8*) malloc( _buffersize );
		if(_buffer != NULL)
			memset(_buffer, 0, _buffersize);
#ifdef GLTRON_SDL2_AUDIO
    _sample_buffer = (Uint8*) malloc(_sample_buffersize);
#endif

		_decoded = 0;
    _read = 0;

    _filename = NULL;
#ifndef GLTRON_SDL2_AUDIO
    _rwops = NULL;
#endif
  }

  SourceMusic::~SourceMusic() { 
    // fprintf(stderr, "nebu_SourceMusic destructor called\n");
#ifndef macintosh
		SDL_SemWait(_sem);
#else
        SDL_LockAudio();
#endif
		free(_buffer);

    CleanUp();
#ifdef GLTRON_SDL2_AUDIO
    free(_sample_buffer);
    _sample_buffer = NULL;
#endif

    if(_filename)
      free(_filename);

#ifndef macintosh		
		SDL_SemPost(_sem);
#else
        SDL_UnlockAudio();
#endif
  }

	/*! 
		\fn int SourceMusic::CreateSample(void)
		
		call this function only between semaphores
	*/

  int SourceMusic::CreateSample(void) {
#ifdef GLTRON_SDL2_AUDIO
    AudioInfo *info = _system->GetAudioInfo();
    if(info->format != AUDIO_S16SYS || info->channels != 2) {
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
		if(_buffer == NULL
#ifdef GLTRON_SDL2_AUDIO
		   || _sample_buffer == NULL
#endif
		  ) {
			fprintf(stderr, "[error] out of memory creating music buffers\n");
			return 0;
		}

		int n = strlen(filename);
		_filename = (char*) malloc(n + 1);
		if(_filename == NULL)
			return 0;
		memcpy(_filename, filename, n + 1);
    return CreateSample();
  }

  int SourceMusic::HasSample(void) const {
#ifdef GLTRON_SDL2_AUDIO
    return _module != NULL;
#else
    return _sample != NULL;
#endif
  }

  void SourceMusic::CleanUp(void) {
		_read = 0;
    _decoded = 0;

#ifdef GLTRON_SDL2_AUDIO
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
		if(SDL_SemTryWait(_sem))
			return 0;
#endif
	if(!HasSample()) {
#ifndef macintosh
			SDL_SemPost(_sem);
#endif
      return 0;
    }
		// printf("mixing %d bytes\n", len);

    int volume = (int)(_volume * SDL_MIX_MAXVOLUME);
    // fprintf(stderr, "setting volume to %.3f -> %d\n", _volume, volume);
    // fprintf(stderr, "entering mixer\n");
		
    if(len < (_decoded - _read + _buffersize) % _buffersize) {
			// enough data to mix
			if(_read + len <= _buffersize) {
#ifdef GLTRON_SDL2_AUDIO
				SDL_MixAudioFormat(data, _buffer + _read, AUDIO_S16SYS, len, volume);
#else
				SDL_MixAudio(data, _buffer + _read, len, volume);
#endif
				_read = (_read + len) % _buffersize;
			} else {
				// wrap around in buffer
#ifdef GLTRON_SDL2_AUDIO
				SDL_MixAudioFormat(data, _buffer + _read, AUDIO_S16SYS,
				                   _buffersize - _read, volume);
#else
				SDL_MixAudio(data, _buffer + _read, _buffersize - _read, volume);
#endif
				len -= _buffersize - _read;
#ifdef GLTRON_SDL2_AUDIO
				SDL_MixAudioFormat(data + _buffersize - _read, _buffer,
				                   AUDIO_S16SYS, len, volume);
#else
				SDL_MixAudio(data + _buffersize - _read, _buffer, len, volume);
#endif
				_read = len;
			}
		} else {
			// buffer under-run; don't do anything
		}

#ifndef macintosh
		SDL_SemPost(_sem);
#endif
    return 1;
	}

	void SourceMusic::Idle(void) {
#ifndef macintosh
		if(SDL_SemWait(_sem))
			return;
#else
		SDL_LockAudio();
#endif

		if(!HasSample()) {
#ifndef macintosh
			SDL_SemPost(_sem);
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
#ifdef GLTRON_SDL2_AUDIO
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
#ifdef GLTRON_SDL2_AUDIO
				memcpy(_buffer + _decoded, _sample_buffer, count);
#else
				memcpy(_buffer + _decoded, _sample->buffer, count);
#endif
			} else {
				// wrapping around end of buffer (usually doesn't happen when 
				// _buffersize is a multiple of _sample_buffersize)
				// printf("wrapping around end of buffer\n");
#ifdef GLTRON_SDL2_AUDIO
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
#ifdef GLTRON_SDL2_AUDIO
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
		SDL_SemPost(_sem);
#else
		SDL_UnlockAudio();
#endif
	}
}

#endif
