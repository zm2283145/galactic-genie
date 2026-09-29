// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio.h"

#define DR_MP3_NO_SIMD
#define DR_MP3_IMPLEMENTATION
#include "../../third_party/dr_libs/dr_mp3.h"

#include <algorithm>
#include <cstdio>

namespace swgb {

namespace {

constexpr size_t kMaxDecodedBytes = 32u * 1024u * 1024u;

int16_t sampleAt(const int16_t *samples, uint64_t frame, uint32_t channels, uint32_t channel) {
    return samples[frame * channels + std::min(channel, channels - 1)];
}

} // namespace

bool decodeMp3(const std::vector<uint8_t> &data, AudioClip &clip, std::string *err) {
    clip.samples.clear();
    if (data.empty()) {
        if (err) *err = "empty MP3 data";
        return false;
    }

    drmp3_config config{};
    drmp3_uint64 sourceFrames = 0;
    drmp3_int16 *decoded = drmp3_open_memory_and_read_pcm_frames_s16(
        data.data(), data.size(), &config, &sourceFrames, nullptr);
    if (!decoded || sourceFrames == 0 || (config.channels != 1 && config.channels != 2) ||
        config.sampleRate == 0) {
        drmp3_free(decoded, nullptr);
        if (err) *err = "unsupported or corrupt MP3 stream";
        return false;
    }

    if (sourceFrames > UINT64_MAX / AudioClip::kSampleRate) {
        drmp3_free(decoded, nullptr);
        if (err) *err = "MP3 sample count is invalid";
        return false;
    }
    const uint64_t outputFrames =
        (sourceFrames * AudioClip::kSampleRate + config.sampleRate - 1) / config.sampleRate;
    if (outputFrames > kMaxDecodedBytes / (sizeof(int16_t) * AudioClip::kChannels)) {
        drmp3_free(decoded, nullptr);
        if (err) *err = "decoded MP3 exceeds audio memory limit";
        return false;
    }
    clip.samples.resize((size_t)outputFrames * AudioClip::kChannels);

    for (uint64_t frame = 0; frame < outputFrames; frame++) {
        const uint64_t position = frame * config.sampleRate;
        const uint64_t sourceFrame = std::min(position / AudioClip::kSampleRate, sourceFrames - 1);
        const uint64_t nextFrame = std::min(sourceFrame + 1, sourceFrames - 1);
        const uint32_t fraction = (uint32_t)(position % AudioClip::kSampleRate);
        for (uint32_t channel = 0; channel < AudioClip::kChannels; channel++) {
            const int32_t a = sampleAt(decoded, sourceFrame, config.channels, channel);
            const int32_t b = sampleAt(decoded, nextFrame, config.channels, channel);
            const int32_t mixed =
                a + (int32_t)(((int64_t)(b - a) * fraction) / AudioClip::kSampleRate);
            clip.samples[(size_t)frame * AudioClip::kChannels + channel] = (int16_t)mixed;
        }
    }

    drmp3_free(decoded, nullptr);
    return true;
}

bool loadMp3(const std::string &path, AudioClip &clip, std::string *err) {
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) {
        if (err) *err = "cannot open " + path;
        return false;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        if (err) *err = "cannot seek " + path;
        return false;
    }
    const long length = ftell(fp);
    if (length <= 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        if (err) *err = "invalid MP3 file " + path;
        return false;
    }
    std::vector<uint8_t> data((size_t)length);
    const bool read = fread(data.data(), 1, data.size(), fp) == data.size();
    fclose(fp);
    if (!read) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    return decodeMp3(data, clip, err);
}

} // namespace swgb
