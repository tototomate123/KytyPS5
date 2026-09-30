#include "libs/ajm/mp3_parser.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace Libs::Audio::Ajm;

static void Check(bool value, const char *message) {
  if (!value) {
    std::fprintf(stderr, "AjmMp3ParserTests: %s\n", message);
    std::abort();
  }
}

int main() {
  AjmDecMp3FrameInfo info{};
  const std::array<uint8_t, 4> stereo{0xff, 0xfb, 0x90, 0x00};
  Check(AjmParseMp3Frame(stereo.data(), 4, 0, &info) == 0, "header-only parse");
  Check(info.frame_size == 417 && info.num_channels == 2 &&
            info.sample_rate == 44100 && info.bitrate == 128000 &&
            info.samples_per_channel == 1152,
        "MPEG1 stereo metadata");
  const std::array<uint8_t, 4> mono{0xff, 0xf3, 0x82, 0xc0};
  Check(AjmParseMp3Frame(mono.data(), 4, 0, &info) == 0 &&
            info.frame_size == 209 && info.num_channels == 1 &&
            info.sample_rate == 22050 && info.samples_per_channel == 576,
        "MPEG2 padded mono");
  const std::array<uint8_t, 4> low_rate{0xff, 0xe3, 0x48, 0xc0};
  Check(AjmParseMp3Frame(low_rate.data(), 4, 0, &info) == 0 &&
            info.frame_size == 288 && info.sample_rate == 8000 &&
            info.bitrate == 32000,
        "MPEG2.5 metadata");
  for (auto invalid : {std::array<uint8_t, 4>{0, 0, 0, 0},
                       std::array<uint8_t, 4>{0xff, 0xeb, 0x90, 0},
                       std::array<uint8_t, 4>{0xff, 0xfd, 0x90, 0},
                       std::array<uint8_t, 4>{0xff, 0xfb, 0x9c, 0},
                       std::array<uint8_t, 4>{0xff, 0xfb, 0x00, 0},
                       std::array<uint8_t, 4>{0xff, 0xfb, 0xf0, 0}}) {
    info.frame_size = 123;
    Check(AjmParseMp3Frame(invalid.data(), 4, 1, &info) < 0 &&
              info.frame_size == 123,
          "invalid header rejected without overwriting output");
  }
  Check(AjmParseMp3Frame(nullptr, 4, 0, &info) < 0 &&
            AjmParseMp3Frame(stereo.data(), 4, 0, nullptr) < 0,
        "null arguments");
  std::vector<uint8_t> frame(417);
  std::copy(stereo.begin(), stereo.end(), frame.begin());
  const std::array<uint8_t, 10> fgh{0xb4, 0x02, 0x40, 0x00, 0x01,
                                    0x19, 0x40, 0,    0,    0xd9};
  std::copy(fgh.begin(), fgh.end(), frame.begin() + 36);
  Check(AjmParseMp3Frame(frame.data(), frame.size(), 1, &info) == 0 &&
            info.encoder_delay == 576 && info.total_samples == 72000 &&
            info.ofl_type == 3,
        "FGH delay, sample count and checksum");
  Check(AjmParseMp3Frame(frame.data(), frame.size(), 0, &info) == 0 &&
            info.encoder_delay == 0 && info.total_samples == 0 &&
            info.ofl_type == 0,
        "optional metadata disabled and output cleared");
  frame[45] ^= 1;
  Check(AjmParseMp3Frame(frame.data(), frame.size(), 1, &info) == 0 &&
            info.ofl_type == 0,
        "bad checksum ignored");
  frame[45] ^= 1;
  // Allocate exactly the advertised length: sanitizers detect any speculative
  // overread.
  for (size_t n = 0; n <= frame.size(); ++n) {
    std::vector<uint8_t> truncated(frame.begin(), frame.begin() + n);
    const int result =
        AjmParseMp3Frame(truncated.data(), truncated.size(), 1, &info);
    Check(n < 4 ? result < 0 : result == 0, "truncated input handled");
    if (n >= 4 && n < 46) {
      Check(info.ofl_type == 0, "truncated tag ignored");
    }
  }
  // A valid tag in the next frame must not be consumed by this frame's parser.
  std::fill(frame.begin() + 4, frame.end(), 0);
  frame.insert(frame.end(), fgh.begin(), fgh.end());
  Check(AjmParseMp3Frame(frame.data(), frame.size(), 1, &info) == 0 &&
            info.ofl_type == 0,
        "metadata bounded by frame size");
  std::puts("AjmMp3ParserTests: passed");
}
