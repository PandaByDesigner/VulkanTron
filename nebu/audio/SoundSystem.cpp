#include "audio/nebu_SoundSystem.h"

#include <string.h>

namespace Sound {
  System::System(SDL_AudioSpec *spec) {
    _spec = spec;
    _sources.data = NULL;
    _sources.next = NULL;

    _info.format = _spec->format;
    _info.rate = spec->freq;
    _info.channels = spec->channels;
    
    _mix_music = 1; // TODO: add 'master' volume for music and fx
    _mix_fx = 1;

    _status = 0; // sound system is not initialized
  }

  void System::Lock() {
    if(_status == eInitialized)
      SDL_LockAudio();
  }

  void System::Unlock() {
    if(_status == eInitialized)
      SDL_UnlockAudio();
  }

  void System::SetStatus(int status) {
    if(_status == eInitialized) {
      SDL_LockAudio();
      _status = status;
      SDL_UnlockAudio();
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
