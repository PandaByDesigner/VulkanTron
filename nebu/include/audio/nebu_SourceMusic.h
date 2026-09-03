#ifndef NEBU_Sound_SourceMusic_H
#define NEBU_Sound_SourceMusic_H

#include "nebu_Sound.h"

#include "nebu_Source.h"
#include "nebu_SoundSystem.h"

#ifdef GLTRON_SDL2_AUDIO
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
#ifdef GLTRON_SDL2_AUDIO
    MODULE* _module;
    Uint8* _sample_buffer;
#else
    Sound_Sample* _sample;
#endif
		int _sample_buffersize;
		
		Uint8* _buffer;
    int _buffersize;
    int _read;
    int _decoded;

		char *_filename;
#ifndef GLTRON_SDL2_AUDIO
    SDL_RWops *_rwops;
#endif
  };
}
#endif
