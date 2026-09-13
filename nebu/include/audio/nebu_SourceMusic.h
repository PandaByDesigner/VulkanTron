#ifndef NEBU_Sound_SourceMusic_H
#define NEBU_Sound_SourceMusic_H

#include "nebu_Sound.h"

#include "nebu_Source.h"
#include "nebu_SoundSystem.h"

#if defined(GLTRON_SDL2_AUDIO) || defined(GLTRON_SDL3_AUDIO)
#include <mikmod.h>
#endif

namespace Sound {
  class SourceMusic : public Source {
  public:
    SourceMusic(System *system);
    virtual ~SourceMusic();
    int Load(char *filename);
    virtual int Mix(Uint8 *data, int len);
		virtual void Idle(void);

  protected:
    virtual void Reset(void) {
#if defined(GLTRON_SDL2_AUDIO) || defined(GLTRON_SDL3_AUDIO)
      if(_wav_buffer != NULL) {
        _wav_position = 0;
        return;
      }
#endif
      if(HasSample()) {
				CleanUp();
				if(!CreateSample())
					_isPlaying = 0;
				// fprintf(stderr, "sample resetted\n");
      } else {
				_isPlaying = 0;
      }
    };
    int HasSample(void) const;
    void CleanUp(void);
    int CreateSample(void);

  private:
#if defined(GLTRON_SDL2_AUDIO) || defined(GLTRON_SDL3_AUDIO)
    MODULE* _module;
    Uint8* _sample_buffer;
    /* Recorded music is loaded/conformed before the source is published.
       The real-time mixer only reads PCM; loops never reopen the file. */
    Uint8* _wav_buffer;
    int _wav_size;
    int _wav_position;
    int LoadWav(void);
#else
    Sound_Sample* _sample;
#endif
		int _sample_buffersize;
		
		Uint8* _buffer;
    int _buffersize;
    int _read;
    int _decoded;

		char *_filename;
#if !defined(GLTRON_SDL2_AUDIO) && !defined(GLTRON_SDL3_AUDIO)
    SDL_RWops *_rwops;
#endif
  };
}
#endif
