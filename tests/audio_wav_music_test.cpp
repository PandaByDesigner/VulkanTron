#include "audio/nebu_SourceMusic.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <vector>

namespace {
  void put16(std::vector<Uint8>& bytes, size_t offset, unsigned value) {
    bytes[offset] = (Uint8)value;
    bytes[offset + 1] = (Uint8)(value >> 8);
  }
  void put32(std::vector<Uint8>& bytes, size_t offset, Uint32 value) {
    put16(bytes, offset, value);
    put16(bytes, offset + 2, value >> 16);
  }
  std::vector<Uint8> wave(unsigned channels, unsigned rate, size_t frames,
                          bool constant = false) {
    std::vector<Uint8> bytes(44 + frames * channels * 2, 0);
    memcpy(bytes.data(), "RIFF", 4);
    put32(bytes, 4, (Uint32)bytes.size() - 8);
    memcpy(bytes.data() + 8, "WAVEfmt ", 8);
    put32(bytes, 16, 16);
    put16(bytes, 20, 1);
    put16(bytes, 22, channels);
    put32(bytes, 24, rate);
    put32(bytes, 28, rate * channels * 2);
    put16(bytes, 32, channels * 2);
    put16(bytes, 34, 16);
    memcpy(bytes.data() + 36, "data", 4);
    put32(bytes, 40, (Uint32)(frames * channels * 2));
    for(size_t i = 0; i < frames * channels; ++i) {
      const Sint16 sample = constant ? 12000 : (Sint16)((i * 137) % 30001 - 15000);
      put16(bytes, 44 + i * 2, (Uint16)sample);
    }
    return bytes;
  }
  void writeFile(const char *path, const std::vector<Uint8>& bytes) {
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size());
    assert(fclose(file) == 0);
  }
  Sint16 sampleAt(const std::vector<Uint8>& bytes, size_t index) {
    return (Sint16)((unsigned)bytes[44 + index * 2] |
                    ((unsigned)bytes[45 + index * 2] << 8));
  }
  void verifyExact(Sound::System& system, const char *path, unsigned channels) {
    const size_t frames = 1027; /* Deliberately not a mixer-block multiple. */
    const std::vector<Uint8> bytes = wave(channels, 22050, frames);
    writeFile(path, bytes);
    Sound::SourceMusic source(&system);
    assert(source.Load(const_cast<char*>(path)));
    assert(unlink(path) == 0); /* Start/reset/loop must never reopen the WAV. */
    source.SetVolume(1.0f);
    source.SetLoop(2);
    source.Start();
    size_t played_frames = 0;
    while(source.IsPlaying()) {
      Sint16 output[259 * 2] = {};
      assert(played_frames < frames * 3);
      source.Idle();
      assert(source.Mix((Uint8*)output, sizeof(output)));
      for(size_t i = 0; i < 259; ++i) {
        for(size_t channel = 0; channel < 2; ++channel) {
          Sint16 expected = 0;
          if(played_frames + i < frames * 3) {
            size_t frame = (played_frames + i) % frames;
            expected = sampleAt(bytes, frame * channels + (channels == 1 ? 0 : channel));
          }
          assert(output[i * 2 + channel] == expected);
        }
      }
      played_frames += 259;
    }
    assert(source.GetLoop() == 0);
    source.SetLoop(255);
    source.Start();
    for(int i = 0; i < 32; ++i) {
      Sint16 output[4096] = {};
      assert(source.Mix((Uint8*)output, sizeof(output)));
      assert(source.IsPlaying() && source.GetLoop() == 255);
    }
    source.Stop();
    assert(!source.IsPlaying());
    source.SetLoop(0);
    source.SetVolume(0.5f);
    source.Start();
    Sint16 first[2] = {};
    assert(source.Mix((Uint8*)first, sizeof(first)));
    assert(first[0] == sampleAt(bytes, 0) / 2);
    assert(first[1] == sampleAt(bytes, channels == 1 ? 0 : 1) / 2);
    assert(!source.Mix((Uint8*)first, 3));
    source.Stop();
  }
  void verifyResample(Sound::System& system, const char *path) {
    const std::vector<Uint8> bytes = wave(1, 44100, 4410, true);
    writeFile(path, bytes);
    Sound::SourceMusic source(&system);
    assert(source.Load(const_cast<char*>(path)));
    source.SetVolume(1.0f);
    source.SetLoop(0);
    source.Start();
    unsigned frames = 0;
    while(source.IsPlaying()) {
      Sint16 frame[2] = {};
      assert(source.Mix((Uint8*)frame, sizeof(frame)));
      assert(frame[0] == frame[1]);
      if(frames > 64 && frames < 2140)
        assert(frame[0] >= 11990 && frame[0] <= 12010);
      assert(++frames <= 2300);
    }
    assert(frames >= 2200 && frames <= 2210);
    assert(unlink(path) == 0);
  }
  void expectReject(Sound::System& system, const char *path,
                    const std::vector<Uint8>& bytes) {
    writeFile(path, bytes);
    Sound::SourceMusic source(&system);
    assert(!source.Load(const_cast<char*>(path)));
    source.Start();
    assert(!source.IsPlaying());
    Uint8 output[16] = {};
    source.Idle();
    assert(!source.Mix(output, sizeof(output)));
    assert(unlink(path) == 0);
  }
  void verifyMalformed(Sound::System& system, const char *path) {
    const std::vector<Uint8> valid = wave(2, 22050, 128);
    std::vector<Uint8> bad = valid;
    bad.resize(bad.size() - 1); expectReject(system, path, bad);
    bad = valid; put32(bad, 4, 0xffffffffu); expectReject(system, path, bad);
    bad = valid; put32(bad, 40, 0xffffffffu); expectReject(system, path, bad);
    bad = valid; put32(bad, 40, 1); expectReject(system, path, bad);
    bad = valid; put16(bad, 20, 3); expectReject(system, path, bad); /* float */
    bad = valid; put16(bad, 22, 6); expectReject(system, path, bad);
    bad = valid; put16(bad, 34, 8); expectReject(system, path, bad);
    bad = valid; put16(bad, 32, 3); expectReject(system, path, bad);
    bad = valid; put32(bad, 28, 1); expectReject(system, path, bad);
    bad = valid; put32(bad, 24, 0); expectReject(system, path, bad);
    bad = valid; memcpy(bad.data(), "RF64", 4); expectReject(system, path, bad);
    bad = valid; memcpy(bad.data() + 8, "NOPE", 4); expectReject(system, path, bad);
    bad = wave(2, 22050, 0); expectReject(system, path, bad);
    bad = valid; bad.resize(11); expectReject(system, path, bad);
    writeFile(path, valid);
    assert(truncate(path, 64 * 1024 * 1024 + 1) == 0); /* bounded sparse input */
    Sound::SourceMusic oversized(&system);
    assert(!oversized.Load(const_cast<char*>(path)));
    assert(unlink(path) == 0);
    Sound::SourceMusic missing(&system);
    assert(!missing.Load(const_cast<char*>(path)));
  }
}

int main() {
  assert(nebu_InitAudio());
  SDL_AudioSpec spec = {};
  spec.freq = 22050;
  spec.format = NEBU_AUDIO_S16;
  spec.channels = 2;
  char directory[] = "/tmp/vulkantron-wav-test.XXXXXX";
  assert(mkdtemp(directory) != NULL);
  char path[256];
  snprintf(path, sizeof(path), "%s/original.WaV", directory);
  {
    Sound::System system(&spec);
    verifyExact(system, path, 2);
    verifyExact(system, path, 1);
    verifyResample(system, path);
    verifyMalformed(system, path);
  }
  assert(rmdir(directory) == 0);
  SDL_Quit();
  puts("PASS: WAV exact PCM, mono conversion, resampling, tail/finite/infinite loops, reset without IO, gain, malformed and bounded inputs");
  return 0;
}
