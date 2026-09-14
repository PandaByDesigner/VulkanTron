/* Include the production glue so this harness can inspect source lifecycle
   without adding test-only state or counters to the game/audio API. */
#include "../src/audio/sound_glue.cpp"
#include <assert.h>
#include <string>

static const char *asset_root;
static Game test_game;
static Game2 test_game2;
static Player test_players[PLAYERS];
static Data test_data[PLAYERS];
static Camera test_cameras[PLAYERS];

extern "C" {
  Game *game = &test_game;
  Game2 *game2 = &test_game2;
  SettingsCache gSettingsCache;
  int dirsX[] = {0, -1, 0, 1};
  int dirsY[] = {-1, 0, 1, 0};
  float getSettingf(const char *name) {
    assert(strcmp(name, "speed") == 0);
    return 10.0f;
  }
  int getVideoSettingi(const char *name) {
    assert(strcmp(name, "obsidian_arena") == 0);
    return gSettingsCache.obsidian_arena;
  }
  void getPositionFromIndex(float *x, float *y, int index) {
    *x = 100.0f + index * 10.0f;
    *y = 100.0f;
  }
  void getRecognizerPositionVelocity(vec2 *position, vec2 *velocity) {
    position->v[0] = position->v[1] = 0;
    velocity->v[0] = velocity->v[1] = 0;
  }
  /* The production Sound_setup routes assets via Lua/filesystem. This bounded
     harness substitutes those already-tested routes, keeping real SDL sources,
     shared samples, device locks, callbacks, and reload/retirement code. */
  void Sound_setup(void) {
    Audio_Init();
    const char *names[] = {"game_engine.wav", "game_crash.wav", "game_recognizer.wav", "game_boost.wav"};
    for(int i = 0; i < (gSettingsCache.obsidian_arena ? 4 : 3); ++i) {
      std::string path = std::string(asset_root) + "/data/" +
        (gSettingsCache.obsidian_arena ? "obsidian/" : "") + names[i];
      Audio_LoadSample(const_cast<char*>(path.c_str()), i);
    }
    Audio_LoadPlayers();
    std::string path = std::string(asset_root) + "/music/song_forward_pulse.wav";
    Audio_LoadMusic(const_cast<char*>(path.c_str()));
    assert(music != NULL && music->GetLoop() == 255);
    Audio_PlayMusic();
    Audio_SetMusicVolume(1.0f);
    Audio_SetFxVolume(1.0f);
    Audio_Start();
    sound->PauseAudio(1); /* Deterministic production callbacks below. */
  }
}

static bool mixAudible(void) {
  Sint16 pcm[Sound::System::kMixChunkBytes / sizeof(Sint16)] = {};
  sound->Callback((Uint8*)pcm, sizeof(pcm));
  for(size_t i = 0; i < sizeof(pcm) / sizeof(pcm[0]); ++i)
    if(pcm[i] != 0) return true;
  return false;
}

static unsigned long spatialEnergy(Sound::Source3D *source, float volume) {
  Sint16 pcm[Sound::System::kMixChunkBytes / sizeof(Sint16)] = {};
  source->_position = 0;
  source->_location = sound->GetListener()._location;
  Audio_SetFxVolume(volume);
  assert(source->Mix((Uint8*)pcm, sizeof(pcm)));
  unsigned long energy = 0;
  for(size_t i = 0; i < sizeof(pcm) / sizeof(pcm[0]); ++i)
    energy += (unsigned long)abs((int)pcm[i]);
  return energy;
}

static void verifySpatialGain(void) {
  sample_recognizer->Start();
  /* Player 0's boost/pitch subclass, the other cycles and the recognizer all
     use the presentation opt-in; the default still produces classic PCM. */
  for(int i = 0; i <= PLAYERS; ++i) {
    Sound::Source3D *source = i == PLAYERS ? recognizerEngine : players[i];
    const unsigned long full = spatialEnergy(source, 1.0f);
    const unsigned long half = spatialEnergy(source, i == PLAYERS ? 0.4f : 0.5f);
    const unsigned long muted = spatialEnergy(source, 0.0f);
    assert(full > 0);
    if(gSettingsCache.obsidian_arena) {
      assert(half > full * 48 / 100 && half < full * 52 / 100);
      assert(muted == 0);
      assert(spatialEnergy(source, 0.02f) > 0); /* No slider dead zone. */
    } else {
      assert(full == half && full == muted);
    }
  }
  Audio_SetFxVolume(1.0f);
  sample_recognizer->Stop();
}

int main(int argc, char **argv) {
  assert(argc == 2);
  asset_root = argv[1];
  assert(nebu_InitAudio());
  test_game.player = test_players;
  test_game.players = PLAYERS;
  for(int i = 0; i < PLAYERS; ++i) {
    test_players[i].data = &test_data[i];
    test_players[i].camera = &test_cameras[i];
    test_data[i].speed = i == 1 ? -1.0f : 10.0f;
    test_cameras[i].cam[0] = 100;
    test_cameras[i].cam[1] = 95;
    test_cameras[i].target[0] = test_cameras[i].target[1] = 100;
  }
  gSettingsCache.obsidian_arena = 1;
  gSettingsCache.playEffects = 1;
  Sound_setup();
  assert(sample_boost != NULL);
  Audio_EnableEngine();
  Audio_Idle();
  verifySpatialGain();
  Audio_SetFxVolume(0.0f);
  assert(!mixAudible());
  Audio_SetFxVolume(1.0f);
  /* Keep the engine-active flag for boost detection, while isolating the
     one-shot from the spatial engine sources in the mixed signal. */
  for(int i = 0; i < PLAYERS; ++i) players[i]->Pause();
  sample_recognizer->Stop();
  Audio_Idle();
  assert(!mixAudible());
  test_data[0].boost_enabled = 1;
  Audio_Idle();
  assert(last_boost_enabled && mixAudible());
  for(int i = 0; i < 32; ++i) { Audio_Idle(); mixAudible(); }
  assert(!mixAudible()); /* Holding boost must not spawn another one-shot. */
  gSettingsCache.playEffects = 0;
  for(int i = 0; i < 3; ++i) {
    test_data[0].boost_enabled = 0; Audio_Idle();
    test_data[0].boost_enabled = 1; Audio_Idle();
    assert(!mixAudible());
  }
  test_data[0].boost_enabled = 0; Audio_Idle();
  gSettingsCache.playEffects = 1; Audio_Idle();
  assert(!mixAudible()); /* Muted boost presses never queue delayed cues. */
  test_data[0].boost_enabled = 1; Audio_Idle();
  assert(mixAudible());
  gSettingsCache.playEffects = 0; Audio_Idle();
  assert(!mixAudible());
  gSettingsCache.playEffects = 1; Audio_Idle();
  assert(!mixAudible()); /* A muted active cue never resumes its stale tail. */
  test_data[0].boost_enabled = 0;
  Audio_Idle();
  test_data[0].boost_enabled = 1;
  Audio_Idle();
  assert(mixAudible());

  /* Retire a live boost copy while replacing all samples and native devices. */
  for(int i = 0; i < 8; ++i) {
    gSettingsCache.obsidian_arena = i % 2;
    Audio_ReloadPresentation();
    assert(sample_engine->IsPlaying() && music->IsPlaying());
    assert(players[0]->IsPlaying() && !players[1]->IsPlaying());
    assert((sample_boost != NULL) == (gSettingsCache.obsidian_arena != 0));
    assert(music->GetLoop() == 255);
    Audio_Idle();
    verifySpatialGain();
    mixAudible();
  }
  Audio_DisableEngine();
  Audio_StopMusic();
  Audio_ReloadPresentation();
  assert(!sample_engine->IsPlaying() && !music->IsPlaying());
  /* WAV loading does not require the tracker decoder to be available. */
  const int saved_decoder_ready = decoder_ready;
  decoder_ready = 0;
  Sound::SourceMusic *previous_music = music;
  std::string path = std::string(asset_root) + "/music/song_forward_pulse.wav";
  Audio_LoadMusic(const_cast<char*>(path.c_str()));
  assert(music != NULL && music != previous_music);
  decoder_ready = saved_decoder_ready;
  Audio_PlayMusic();
  gSettingsCache.playMusic = 1;
  Audio_Idle();
  assert(mixAudible());
  Audio_Quit();
  assert(sound == NULL && music == NULL && sample_boost == NULL);
  Audio_Init();
  Audio_LoadSample(const_cast<char*>("/missing/optional-boost.wav"), 3);
  assert(sample_boost == NULL);
  Audio_Quit();
  Audio_Quit();
  SDL_Quit();
  puts("PASS: production boost edge/hold/retrigger/mute, live-copy pack replacement, presentation-only spatial gain/mute, engine/music state, WAV decoder independence and shutdown");
  return 0;
}
