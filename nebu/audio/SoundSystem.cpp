#ifndef GLTRON_NO_SOUND

#include "audio/nebu_SoundSystem.h"

#include <stdio.h>
#include <string.h>

#ifdef GLTRON_SDL2_AUDIO
#include <mikmod.h>
#endif

namespace Sound {
#ifdef GLTRON_SDL2_AUDIO
  static SDL_mutex *decoder_mutex = NULL;
  static int decoder_initialized = 0;

  int InitDecoder() {
    if(decoder_initialized)
      return 1;

    decoder_mutex = SDL_CreateMutex();
    if(decoder_mutex == NULL) {
      fprintf(stderr, "[error] failed creating tracker decoder lock: %s\n",
              SDL_GetError());
      return 0;
    }

    MikMod_RegisterDriver(&drv_nos);
    /* The faithful checkpoint accepts the shipped Impulse Tracker music.
       Wider format support belongs to the later content/modding layer. */
    if(MikMod_InfoLoader() == NULL)
      MikMod_RegisterLoader(&load_it);

    /* Match SDL_sound 1.0.3's libmikmod output settings exactly. */
    md_mode |= (DMODE_SOFT_MUSIC | DMODE_16BITS);
    md_mixfreq = 0;
    md_reverb = 1;

    if(MikMod_Init("")) {
      fprintf(stderr, "[error] failed initializing tracker decoder: %s\n",
              MikMod_strerror(MikMod_errno));
      SDL_DestroyMutex(decoder_mutex);
      decoder_mutex = NULL;
      return 0;
    }

    decoder_initialized = 1;
    return 1;
  }

  void QuitDecoder() {
    if(!decoder_initialized)
      return;

    LockDecoder();
    MikMod_Exit();
    md_mixfreq = 0;
    decoder_initialized = 0;
    UnlockDecoder();

    SDL_DestroyMutex(decoder_mutex);
    decoder_mutex = NULL;
  }

  void LockDecoder() {
    if(decoder_mutex != NULL)
      SDL_LockMutex(decoder_mutex);
  }

  void UnlockDecoder() {
    if(decoder_mutex != NULL)
      SDL_UnlockMutex(decoder_mutex);
  }
#else
  int InitDecoder() {
    return Sound_Init();
  }

  void QuitDecoder() {
    Sound_Quit();
  }

  void LockDecoder() { }
  void UnlockDecoder() { }
#endif

  System::System(SDL_AudioSpec *spec) {
    _sources.data = NULL;
    _sources.next = NULL;

    _info.format = spec->format;
    _info.rate = spec->freq;
    _info.channels = spec->channels;
#ifdef GLTRON_SDL2_AUDIO
    _device = 0;
#endif
    
    _mix_music = 1; // TODO: add 'master' volume for music and fx
    _mix_fx = 1;

    _status = 0; // sound system is not initialized
  }

  System::~System() {
    while(_sources.next != NULL) {
      Source *source = (Source*) _sources.data;
      List *dead = _sources.next;
      _sources.data = dead->data;
      _sources.next = dead->next;
      delete dead;
      delete source;
    }
  }

  int System::OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained) {
#ifdef GLTRON_SDL2_AUDIO
    _device = SDL_OpenAudioDevice(NULL, 0, desired, obtained, 0);
    if(_device == 0)
      return -1;

    if(obtained != NULL &&
       (obtained->freq != desired->freq ||
        obtained->format != desired->format ||
        obtained->channels != desired->channels)) {
      SDL_CloseAudioDevice(_device);
      _device = 0;
      SDL_SetError("audio device changed GLTron's required output format");
      return -1;
    }
    return 0;
#else
    return SDL_OpenAudio(desired, obtained);
#endif
  }

  void System::PauseAudio(int pause_on) {
#ifdef GLTRON_SDL2_AUDIO
    if(_device != 0)
      SDL_PauseAudioDevice(_device, pause_on);
#else
    SDL_PauseAudio(pause_on);
#endif
  }

  void System::CloseAudio() {
#ifdef GLTRON_SDL2_AUDIO
    if(_device != 0) {
      SDL_CloseAudioDevice(_device);
      _device = 0;
    }
#else
    SDL_CloseAudio();
#endif
  }

  void System::Lock() {
    if(_status == eInitialized) {
#ifdef GLTRON_SDL2_AUDIO
      if(_device != 0)
        SDL_LockAudioDevice(_device);
#else
      SDL_LockAudio();
#endif
    }
  }

  void System::Unlock() {
    if(_status == eInitialized) {
#ifdef GLTRON_SDL2_AUDIO
      if(_device != 0)
        SDL_UnlockAudioDevice(_device);
#else
      SDL_UnlockAudio();
#endif
    }
  }

  void System::SetStatus(int status) {
    if(_status == eInitialized) {
#ifdef GLTRON_SDL2_AUDIO
      if(_device != 0)
        SDL_LockAudioDevice(_device);
      _status = status;
      if(_device != 0)
        SDL_UnlockAudioDevice(_device);
#else
      SDL_LockAudio();
      _status = status;
      SDL_UnlockAudio();
#endif
    } else {
      /* The device is still paused while it becomes initialized. */
      _status = status;
    }
  }

  void System::Callback(Uint8* data, int len) {
    // printf("callback got called for %d bytes of data\n", len);

    // ensure silence
    memset(data, 0, len);

    if(_status == eUninitialized) 
      return;

    List* p;
    for(p = & _sources; p->next != NULL; p = p->next) {
      Source* s = (Source*) p->data;
      if(s->IsPlaying()) {
				// fprintf(stderr, "mixing source\n");
				if(!(
						 (s->GetType() & eSoundFX && ! _mix_fx ) ||
						 (s->GetType() & eSoundMusic && ! _mix_music) )
					 )
					{
						s->Mix(data, len);
					}
      }
    }
  }

  void System::AddSource(Source* source) {
    List* new_tail = new List;
    new_tail->data = NULL;
    new_tail->next = NULL;

    Lock();

    List* p;
    for(p = &_sources; p->next != NULL; p = p->next);
    p->data = source;
    p->next = new_tail; /* publish the fully initialized tail last */

    Unlock();
  }

  void System::Idle(void) {
		List *p = &_sources;

		for(;;) {
			Lock();

			if(p->next == NULL) {
				Unlock();
				break;
			}

			Source *source = (Source*) p->data;
			if(source->IsRemovable() && !source->IsPlaying()) {
				List *dead = p->next;
				p->data = dead->data;
				p->next = dead->next;
				Unlock();

				delete dead;
				delete source;
				continue;
			}

			p = p->next;
			Unlock();

			/* Decoding may block, so never hold the callback lock here. */
			source->Idle();
		}
	}

	extern "C" {
    void c_callback(void *userdata, Uint8 *stream, int len) { 
      // printf("c_callback got called for %d bytes of data\n", len);
      ((System*)userdata)->Callback(stream, len);
    }
  }
}

#endif
