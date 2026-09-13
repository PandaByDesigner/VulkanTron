#include "audio/sound_glue.h"

#ifdef GLTRON_NO_SOUND

extern "C" {
  void Audio_EnableEngine(void) {}
  void Audio_DisableEngine(void) {}
  void Audio_Idle(void) {}
  void Audio_CrashPlayer(int) {}
  void Audio_LoadPlayers(void) {}
  void Audio_Init(void) {}
  void Audio_Start(void) {}
  void Audio_Quit(void) {}
  void Audio_ReloadPresentation(void) {}
  void Audio_LoadSample(char *, int) {}
  void Audio_LoadMusic(char *) {}
  void Audio_PlayMusic(void) {}
  void Audio_StopMusic(void) {}
  void Audio_SetMusicVolume(float) {}
  void Audio_SetFxVolume(float) {}
  void Audio_StartEngine(int) {}
  void Audio_StopEngine(int) {}
}

#else

#include "Nebu_audio.h"

extern "C" {
#include "game/game.h"
#include "audio/audio.h"
#include "video/video.h" // 3d sound engine needs to know the camera's location!
}
#include "audio/nebu_AudioSDL.h"
#if !defined(GLTRON_SDL2_AUDIO) && !defined(GLTRON_SDL3_AUDIO)
#include "SDL_sound.h"
#endif

static Sound::System *sound = NULL;
static Sound::SourceMusic *music = NULL;
static Sound::SourceSample *sample_crash = NULL;
static Sound::SourceSample *sample_engine = NULL;
static Sound::SourceSample *sample_recognizer = NULL;
static Sound::SourceSample *sample_boost = NULL;
static int last_boost_enabled = 0;
static int decoder_ready = 0;
/* Source names are borrowed debug labels; Source does not free them. */
static char music_name[] = "music";
static char recognizer_name[] = "recognizer";
static char player_names[PLAYERS][32];

static Sound::Source3D *players[PLAYERS];
static Sound::Source3D *recognizerEngine;

namespace {
  class ScopedAudioLock {
  public:
    explicit ScopedAudioLock(Sound::System *system) : _system(system) {
      if(_system)
        _system->Lock();
    }

    ~ScopedAudioLock() {
      if(_system)
        _system->Unlock();
    }

  private:
    ScopedAudioLock(const ScopedAudioLock&);
    ScopedAudioLock& operator=(const ScopedAudioLock&);
    Sound::System *_system;
  };

  class BoostCue : public Sound::SourceCopy {
  public:
    BoostCue(Sound::System *system, Sound::SourceSample *sample)
      : Sound::SourceCopy(sample) { _system = system; }

    virtual void Idle(void) {
      /* Muted effects are skipped by the classic callback. End this transient
         cue instead of retaining a paused thrust tail for a later unmute. */
      ScopedAudioLock lock(_system);
      if(!gSettingsCache.playEffects) Pause();
    }
  };
}

#define TURNLENGTH 250.0f

#if !defined(GLTRON_SDL2_AUDIO) && !defined(GLTRON_SDL3_AUDIO)
static void output_decoders(void)
{
    const Sound_DecoderInfo **rc = Sound_AvailableDecoders();
    const Sound_DecoderInfo **i;
    const char **ext;

    printf("Supported sound formats:\n");
    if (rc == NULL)
        printf(" * Apparently, NONE!\n");
    else
    {
        for (i = rc; *i != NULL; i++)
        {
            printf(" * %s\n", (*i)->description);
            for (ext = (*i)->extensions; *ext != NULL; ext++)
                printf("   File extension \"%s\"\n", *ext);
            printf("   Written by %s.\n   %s\n\n", (*i)->author, (*i)->url);
        } /* for */
    } /* else */

    printf("\n");
} /* output_decoders */
#endif

extern "C" {


  void Audio_EnableEngine(void) {
    ScopedAudioLock lock(sound);
		int i;
		for(i = 0; i < game->players; i++)
			if( game->player[i].data->speed > 0)
				players[i]->Start();
    last_boost_enabled = 0;
    sample_engine->Start();
    if (gSettingsCache.show_recognizer)
      sample_recognizer->Start();
    // printf("[audio] turning on engine sound\n");
  }

  void Audio_DisableEngine(void) {
    ScopedAudioLock lock(sound);
    sample_engine->Stop();
    sample_recognizer->Stop();
    // printf("[audio] turning off engine sound\n");
  }

  void Audio_Idle(void) {
    int play_boost = 0;
    {
      ScopedAudioLock lock(sound);
    // The callback reads all state updated in this block.
    // Iterate over all the players and update the engines.
    if(sample_engine->IsPlaying()) {
      const int boosting = game->player[0].data->boost_enabled && game->player[0].data->speed > 0;
      play_boost = gSettingsCache.obsidian_arena && gSettingsCache.playEffects &&
        boosting && !last_boost_enabled && sample_boost != NULL;
      last_boost_enabled = boosting;
      for(int i = 0; i < PLAYERS; i++) {
				Player *p;
				Sound::Source3D *p3d;
				float x, y;
				p3d = players[i];
				p = game->player + i;
				getPositionFromIndex(&x, &y, i);
				p3d->_location = Vector3(x, y, 0);
				float V = p->data->speed;

				int dt = game2->time.current - p->data->turn_time;
				if(dt < TURN_LENGTH) {
					float t = (float)dt / TURNLENGTH;

					float vx = (1 - t) * dirsX[p->data->last_dir] +
						t * dirsX[p->data->dir];
					float vy = (1 - t) * dirsY[p->data->last_dir] +
						t * dirsY[p->data->dir];
					p3d->_velocity = Vector3(V * vx, V * vy, 0);
				} else {
					p3d->_velocity = Vector3(V * dirsX[p->data->dir], 
																	 V * dirsY[p->data->dir], 
																	 0);
				}
				if(i == 0) {
					if(p->data->boost_enabled) {
						( (Sound::SourceEngine*) p3d )->_speedShift = 1.2f;
					} else {
						( (Sound::SourceEngine*) p3d )->_speedShift = 1.0f;
					}
					( (Sound::SourceEngine*) p3d )->_pitchShift =
						p->data->speed / getSettingf("speed");
				}
						
#if 0
				if(i == 0) {
					if( dt < TURNLENGTH ) {
						float t = (float)dt / TURNLENGTH;
						float speedShift = ( 1 - t ) * 0.4 + t * 0.3;
						float pitchShift = ( 1 - t ) * 0.9 + t * 1.0;
						( (Sound::SourceEngine*) p3d )->_speedShift = speedShift;
						( (Sound::SourceEngine*) p3d )->_pitchShift = pitchShift;
					} else {
						( (Sound::SourceEngine*) p3d )->_speedShift = 0.3;
						( (Sound::SourceEngine*) p3d )->_pitchShift = 1.0;
					}
				}
#endif
      }
    }

    if(sample_recognizer->IsPlaying()) {
      if (gSettingsCache.show_recognizer) {
				vec2 p, v;
				getRecognizerPositionVelocity(&p, &v);
				// recognizerEngine->_location = Vector3(p.x, p.y, RECOGNIZER_HEIGHT);
				recognizerEngine->_location = Vector3(p.v[0], p.v[1], 10.0f);
				recognizerEngine->_velocity = Vector3(v.v[0], v.v[1], 0);
      }
    }

    Sound::Listener& listener = sound->GetListener();

    listener._location = Vector3(game->player[0].camera->cam);
		Vector3 v1 = Vector3(game->player[0].camera->target);
		Vector3 v2 = Vector3(game->player[0].camera->cam);
    listener._direction = v1 - v2;
      
    // listener._location = players[0]->_location;
    // listener._direction = players[0]->_velocity;
    listener._velocity = players[0]->_velocity;

    listener._up = Vector3(0, 0, 1);

    sound->SetMixMusic(gSettingsCache.playMusic);
    sound->SetMixFX(gSettingsCache.playEffects);
    }
    if(play_boost) {
      Sound::SourceCopy *copy = new BoostCue(sound, sample_boost);
      copy->Start(); copy->SetRemovable(); copy->SetType(Sound::eSoundFX);
      sound->AddSource(copy);
    }
    sound->Idle();
  }

  void Audio_CrashPlayer(int) {
    Sound::SourceCopy *copy = new Sound::SourceCopy(sample_crash);
    copy->Start();
    copy->SetRemovable();
    copy->SetType(Sound::eSoundFX);
    sound->AddSource(copy);
  }

  void Audio_Init(void) {
    decoder_ready = Sound::InitDecoder();
    if(!decoder_ready) {
#if defined(GLTRON_SDL2_AUDIO) || defined(GLTRON_SDL3_AUDIO)
      fprintf(stderr, "[error] tracker music decoder is unavailable\n");
#else
      fprintf(stderr, "[error] SDL_sound initialization failed: %s\n",
              Sound_GetError());
#endif
    }
    // output_decoders();

    SDL_AudioSpec spec;
    SDL_memset(&spec, 0, sizeof(spec));
    spec.freq = 22050;
    spec.format = NEBU_AUDIO_S16;
    spec.channels = 2;
#ifndef GLTRON_SDL3_AUDIO
    spec.samples = 1024;
#endif

    sound = new Sound::System(&spec);

#ifndef GLTRON_SDL3_AUDIO
    spec.userdata = sound;
    spec.callback = sound->GetCallback();
#endif

		SDL_AudioSpec obtained;
    SDL_memset(&obtained, 0, sizeof(obtained));

    if(sound->OpenAudio(&spec, &obtained) != 0) {
      fprintf(stderr, "[error] %s\n", SDL_GetError());
      sound->SetStatus(Sound::eUninitialized);
    } else {
      sound->SetStatus(Sound::eInitialized);
			/*
			fprintf(stderr, "[sound] frequency: %d\n", obtained.freq);
			fprintf(stderr, "[sound] format: %d\n", obtained.format);
			fprintf(stderr, "[sound] channels: %d\n", obtained.channels);
			fprintf(stderr, "[sound] silence: %d\n", obtained.silence);
			fprintf(stderr, "[sound] buffer in samples: %d\n", obtained.samples);
			fprintf(stderr, "[sound] buffer in bytes: %d\n", obtained.size);
			*/
    }
    sound->SetMixMusic(gSettingsCache.playMusic);
    sound->SetMixFX(gSettingsCache.playEffects);
  }

  void Audio_Start(void) {
    if(sound != NULL)
      sound->PauseAudio(0);
  }

  void Audio_Quit(void) {
    if(sound == NULL) {
      if(decoder_ready)
        Sound::QuitDecoder();
      decoder_ready = 0;
      return;
    }

    sound->PauseAudio(1);
    sound->SetStatus(Sound::eUninitialized);
    sound->CloseAudio();

    delete sound;
    sound = NULL;
    music = NULL;
    recognizerEngine = NULL;
    for(int i = 0; i < PLAYERS; i++)
      players[i] = NULL;

    delete sample_crash;
    sample_crash = NULL;
    delete sample_engine;
    sample_engine = NULL;
    delete sample_recognizer;
    sample_recognizer = NULL;

    delete sample_boost;
    sample_boost = NULL;
    last_boost_enabled = 0;

    if(decoder_ready)
      Sound::QuitDecoder();
    decoder_ready = 0;
  }

  void Audio_ReloadPresentation(void) {
    if(sound == NULL) return;
    int engine_running, music_running;
    {
      ScopedAudioLock lock(sound);
      engine_running = sample_engine != NULL && sample_engine->IsPlaying();
      music_running = music != NULL && music->IsPlaying();
    }
    Audio_Quit();
    Sound_setup();
    if(!music_running) Audio_StopMusic();
    if(engine_running) Audio_EnableEngine();
  }

  void Audio_LoadMusic(char *name) {
    if(name == NULL || sound == NULL) return;
#if defined(GLTRON_SDL2_AUDIO) || defined(GLTRON_SDL3_AUDIO)
    const char *extension = strrchr(name, '.');
    int recorded = extension != NULL && SDL_strcasecmp(extension, ".wav") == 0;
#else
    int recorded = 0;
#endif
    if(!decoder_ready && !recorded) {
      fprintf(stderr, "[error] cannot load music without a decoder\n");
      return;
    }

    Sound::SourceMusic *new_music = new Sound::SourceMusic(sound);
    if(!new_music->Load(name)) {
      delete new_music;
      return;
    }
    new_music->SetLoop(255);
    new_music->SetType(Sound::eSoundMusic);

    new_music->SetName(music_name);

    if(music != NULL) {
      ScopedAudioLock lock(sound);
      music->Pause();
      music->SetRemovable();
    }
    sound->AddSource(new_music);
    music = new_music;
  }

  void Audio_PlayMusic(void) {
    ScopedAudioLock lock(sound);
    if(music != NULL)
      music->Start();
  }

  void Audio_StopMusic(void) {
    ScopedAudioLock lock(sound);
    if(music != NULL)
      music->Stop();
  }

  void Audio_SetMusicVolume(float volume) {
    ScopedAudioLock lock(sound);
    if(music != NULL)
      music->SetVolume(volume);
  }

  void Audio_SetFxVolume(float volume) {
    ScopedAudioLock lock(sound);
    sample_engine->SetVolume(volume);
    sample_crash->SetVolume(volume);
    if(sample_boost) sample_boost->SetVolume(volume);
    if(volume > 0.8f)
      sample_recognizer->SetVolume(volume);
    else 
      sample_recognizer->SetVolume(volume * 1.25f);
  }

  void Audio_StartEngine(int iPlayer) {
    ScopedAudioLock lock(sound);
    players[iPlayer]->Start();
  }

  void Audio_StopEngine(int iPlayer) {
    ScopedAudioLock lock(sound);
    players[iPlayer]->Stop();
  }
 
  void Audio_LoadPlayers(void) {
    const bool use_sample_volume = getVideoSettingi("obsidian_arena") != 0;
    for(int i = 0; i < PLAYERS; i++) {
      if(i != 0) {
				players[i] = new Sound::Source3D(sound, sample_engine);
      } else {
				players[i] = new Sound::SourceEngine(sound, sample_engine);
      }
      snprintf(player_names[i], sizeof(player_names[i]), "player %d", i);
      players[i]->SetName(player_names[i]);
      players[i]->SetSampleVolumeEnabled(use_sample_volume);
      players[i]->SetType(Sound::eSoundFX);
      sound->AddSource(players[i]);
    }
    recognizerEngine = new Sound::Source3D(sound, sample_recognizer);
    recognizerEngine->SetSampleVolumeEnabled(use_sample_volume);
    recognizerEngine->SetType(Sound::eSoundFX);
    recognizerEngine->Start();
    sound->AddSource(recognizerEngine);

    recognizerEngine->SetName(recognizer_name);

  }

  void Audio_LoadSample(char *name, int number) {
    int can_load = 1;
#if !defined(GLTRON_SDL2_AUDIO) && !defined(GLTRON_SDL3_AUDIO)
    /* SDL1 effects also depend on SDL_sound; SDL2 WAV effects do not depend
       on the tracker decoder and remain available if libmikmod cannot start. */
    can_load = decoder_ready;
#endif

    switch(number) {
    case 0:
      sample_engine = new Sound::SourceSample(sound);
      if(can_load)
        sample_engine->Load(name);
      break;
    case 1:
      sample_crash = new Sound::SourceSample(sound);
      if(can_load)
        sample_crash->Load(name);
      break;
    case 2:
      sample_recognizer = new Sound::SourceSample(sound);
      if(can_load)
        sample_recognizer->Load(name);
      break;
    case 3:
      sample_boost = new Sound::SourceSample(sound);
      if(can_load) sample_boost->Load(name);
      if(sample_boost->_buffer == NULL ||
         sample_boost->_buffersize <= Sound::System::kMixChunkBytes) {
        fprintf(stderr, "[sound] optional boost sample is unavailable or too short: %s\n", name);
        delete sample_boost;
        sample_boost = NULL;
      }
      break;
    default:
      /* programmer error, but non-critical */
      fprintf(stderr, "[error] unkown sample %d: '%s'\n", number, name);
    }
  }
}

#endif
