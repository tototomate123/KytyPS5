#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Libs::Audio::Ajm {

struct AjmMp3FrameHeader {
	uint32_t header              = 0;
	uint32_t frame_size          = 0;
	uint32_t num_channels        = 0;
	uint32_t samples_per_channel = 0;
	uint32_t bitrate             = 0;
	uint32_t sample_rate         = 0;
	uint8_t  crc                 = 0;
	uint8_t  mode                = 0;
	uint8_t  mode_extension      = 0;
	uint8_t  copyright           = 0;
	uint8_t  original            = 0;
	uint8_t  emphasis            = 0;
};

static uint32_t AjmReadBe32(const uint8_t* data) {
	return (static_cast<uint32_t>(data[0]) << 24u) | (static_cast<uint32_t>(data[1]) << 16u) |
	       (static_cast<uint32_t>(data[2]) << 8u) | static_cast<uint32_t>(data[3]);
}

static bool AjmParseMp3Header(const uint8_t* data, size_t size, AjmMp3FrameHeader* out) {
	if (data == nullptr || size < 4 || out == nullptr) {
		return false;
	}

	const uint32_t header = AjmReadBe32(data);
	if ((header & 0xffe00000u) != 0xffe00000u) {
		return false;
	}

	const auto version_id     = (header >> 19u) & 0x3u;
	const auto layer_id       = (header >> 17u) & 0x3u;
	const auto protection_bit = (header >> 16u) & 0x1u;
	const auto bitrate_index  = (header >> 12u) & 0xfu;
	const auto sample_index   = (header >> 10u) & 0x3u;
	const auto padding        = (header >> 9u) & 0x1u;
	const auto channel_mode   = (header >> 6u) & 0x3u;
	const auto mode_extension = (header >> 4u) & 0x3u;
	const auto copyright      = (header >> 3u) & 0x1u;
	const auto original       = (header >> 2u) & 0x1u;
	const auto emphasis       = header & 0x3u;

	if (version_id == 1 || layer_id != 1 || sample_index == 3 || bitrate_index == 0 ||
	    bitrate_index == 15) {
		return false;
	}

	static constexpr uint32_t SAMPLE_RATES[4][3] = {
	    {11025, 12000, 8000},
	    {0, 0, 0},
	    {22050, 24000, 16000},
	    {44100, 48000, 32000},
	};
	static constexpr uint32_t BITRATES_MPEG1[16] = {
	    0,      32000,  40000,  48000,  56000,  64000,  80000,  96000,
	    112000, 128000, 160000, 192000, 224000, 256000, 320000, 0,
	};
	static constexpr uint32_t BITRATES_MPEG2[16] = {
	    0,     8000,  16000, 24000,  32000,  40000,  48000,  56000,
	    64000, 80000, 96000, 112000, 128000, 144000, 160000, 0,
	};

	const auto sample_rate = SAMPLE_RATES[version_id][sample_index];
	const auto bitrate =
	    (version_id == 3 ? BITRATES_MPEG1[bitrate_index] : BITRATES_MPEG2[bitrate_index]);
	if (sample_rate == 0 || bitrate == 0) {
		return false;
	}

	const bool mpeg1 = (version_id == 3);

	out->header = header;
	out->frame_size =
	    (mpeg1 ? (144u * bitrate) / sample_rate : (72u * bitrate) / sample_rate) + padding;
	out->num_channels        = (channel_mode == 3 ? 1u : 2u);
	out->samples_per_channel = (mpeg1 ? 1152u : 576u);
	out->bitrate             = bitrate;
	out->sample_rate         = sample_rate;
	out->crc                 = static_cast<uint8_t>(protection_bit);
	out->mode                = static_cast<uint8_t>(channel_mode);
	out->mode_extension      = static_cast<uint8_t>(mode_extension);
	out->copyright           = static_cast<uint8_t>(copyright);
	out->original            = static_cast<uint8_t>(original);
	out->emphasis            = static_cast<uint8_t>(emphasis);
	return true;
}

// AJM ABI and FGH metadata layout reference:
// https://github.com/shadps4-emu/shadPS4/blob/main/src/core/libraries/ajm/ajm_mp3.cpp
struct AjmDecMp3FrameInfo {
	uint64_t frame_size;
	uint32_t num_channels;
	uint32_t samples_per_channel;
	uint32_t bitrate;
	uint32_t sample_rate;
	uint32_t encoder_delay;
	uint32_t num_frames;
	uint32_t total_samples;
	uint32_t ofl_type;
};
static_assert(sizeof(AjmDecMp3FrameInfo) == 40);
static_assert(offsetof(AjmDecMp3FrameInfo, encoder_delay) == 24);
static_assert(offsetof(AjmDecMp3FrameInfo, ofl_type) == 36);

inline int AjmParseMp3Frame(const uint8_t* data, uint32_t size, int parse_ofl,
                            AjmDecMp3FrameInfo* out) {
	constexpr int     invalid_parameter = static_cast<int32_t>(0x80930005u);
	AjmMp3FrameHeader header {};
	if (out == nullptr || !AjmParseMp3Header(data, size, &header)) {
		return invalid_parameter;
	}
	AjmDecMp3FrameInfo info {header.frame_size,
	                         header.num_channels,
	                         header.samples_per_channel,
	                         header.bitrate,
	                         header.sample_rate,
	                         0,
	                         0,
	                         0,
	                         0};
	if (parse_ofl != 0) {
		// Xing/LAME and VBRI optional metadata are not decoded yet; the basic frame
		// fields remain valid and unavailable optional fields remain zero.
		// Optional metadata must stay within both the supplied bytes and the first frame.
		const size_t end        = std::min<size_t>(size, header.frame_size);
		const bool   mpeg1      = ((header.header >> 19u) & 3u) == 3;
		const size_t side_start = 4 + (header.crc == 0 ? 2 : 0);
		const size_t side_size =
		    mpeg1 ? (header.num_channels == 1 ? 17 : 32) : (header.num_channels == 1 ? 9 : 17);
		if (side_start + side_size <= end) {
			// Skip the side information and the Huffman/scalefactor data before looking
			// for FGH ancillary metadata, so compressed audio cannot masquerade as a tag.
			size_t bit        = side_start * 8 + (mpeg1 ? (header.num_channels == 1 ? 18 : 20)
			                                            : (header.num_channels == 1 ? 9 : 10));
			size_t audio_bits = 0;
			for (uint32_t i = 0; i < header.num_channels * (mpeg1 ? 2 : 1); ++i) {
				uint32_t length = 0;
				for (unsigned j = 0; j < 12; ++j, ++bit) {
					length = (length << 1u) | ((data[bit / 8] >> (7 - bit % 8)) & 1u);
				}
				audio_bits += length;
				bit += mpeg1 ? 47 : 51;
			}
			const size_t ancillary = side_start + side_size + (audio_bits + 7) / 8;
			for (size_t pos = ancillary; pos + 10 <= end; ++pos) {
				if (data[pos] != 0xb4) {
					continue;
				}
				uint8_t crc = 0xff;
				for (size_t i = 0; i < 9; ++i) {
					for (int shift = 7; shift >= 0; --shift) {
						const bool equal = ((crc >> 7) & 1) == ((data[pos + i] >> shift) & 1);
						crc              = static_cast<uint8_t>((crc << 1) ^ (equal ? 0x45 : 0));
					}
				}
				if (crc == data[pos + 9]) {
					info.encoder_delay = (uint32_t(data[pos + 1]) << 8) | data[pos + 2];
					info.total_samples = AjmReadBe32(data + pos + 3);
					info.ofl_type      = 3; // FGH
					break;
				}
			}
		}
	}
	*out = info;
	return 0;
}

} // namespace Libs::Audio::Ajm
