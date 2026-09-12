#ifndef NEBU_Sound_System_H
#define NEBU_Sound_System_H

extern "C" {
	#include "base/nebu_types.h"
}

#include "audio/nebu_Source.h"
#include "base/nebu_Vector3.h"

#include "audio/nebu_AudioSDL.h"

#if !defined(GLTRON_SDL2_AUDIO) && !defined(GLTRON_SDL3_AUDIO)
#include "SDL_sound.h"
#endif

namespace Sound {
  #if defined(GLTRON_SDL2_AUDIO) || defined(GLTRON_SDL3_AUDIO)
  struct AudioInfo {
    Uint16 format;
    Uint8 channels;
    Uint32 rate;
  };
  #else
  typedef Sound_AudioInfo AudioInfo;
  #endif

  int InitDecoder();
  void QuitDecoder();
  void LockDecoder();
  void UnlockDecoder();

  extern "C" {
    void c_callback(void *userdata, Uint8 *stream, int len);
  }

  class Listener {
  public:
    Listener() { };
    Vector3 _location;
    Vector3 _velocity;
    Vector3 _direction;
    Vector3 _up;
  };

  enum { eUninitialized, eInitialized };

  class System {
  public:

    System(SDL_AudioSpec *spec);
    ~System();
    enum { kMixChunkBytes = 4096 };
    typedef void(*Audio_Callback)(void *userdata, Uint8* data, int len);
    Audio_Callback GetCallback() { return c_callback; };
    int OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained);
    void PauseAudio(int pause_on);
    void CloseAudio();
    void Callback(Uint8* data, int len);
    void Idle(); /* remove dead sound sources */
    void AddSource(Source* source);
    void Lock();
    void Unlock();
    AudioInfo* GetAudioInfo() { return &_info; };
    Listener& GetListener() { return _listener; };
    void SetMixMusic(int value) { _mix_music = value; };
    void SetMixFX(int value) { _mix_fx = value; };
    void SetStatus(int eStatus);

  protected:
    AudioInfo _info;
#ifdef GLTRON_SDL3_AUDIO
    SDL_AudioStream *_stream;
#elif defined(GLTRON_SDL2_AUDIO)
    SDL_AudioDeviceID _device;
#endif
    Listener _listener;
    List _sources;
    int _mix_music;
    int _mix_fx;
    int _status;
  };

}

#endif
