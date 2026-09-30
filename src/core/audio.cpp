// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio.h"

#define DR_MP3_NO_SIMD
#define DR_MP3_IMPLEMENTATION
#include "../../third_party/dr_libs/dr_mp3.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace swgb {

namespace {

constexpr size_t kMaxDecodedBytes = 32u * 1024u * 1024u;

int16_t sampleAt(const int16_t *samples, uint64_t frame, uint32_t channels, uint32_t channel) {
    return samples[frame * channels + std::min(channel, channels - 1)];
}

bool convertPcm(const int16_t *samples, uint64_t sourceFrames, uint32_t sampleRate,
                uint32_t channels, AudioClip &clip, std::string *err) {
    if (!samples || sourceFrames == 0 || sampleRate == 0 || (channels != 1 && channels != 2)) {
        if (err) *err = "unsupported or empty PCM stream";
        return false;
    }
    if (sourceFrames > UINT64_MAX / AudioClip::kSampleRate) {
        if (err) *err = "PCM sample count is invalid";
        return false;
    }
    const uint64_t outputFrames =
        (sourceFrames * AudioClip::kSampleRate + sampleRate - 1) / sampleRate;
    if (outputFrames > kMaxDecodedBytes / (sizeof(int16_t) * AudioClip::kChannels)) {
        if (err) *err = "decoded PCM exceeds audio memory limit";
        return false;
    }
    clip.samples.resize((size_t)outputFrames * AudioClip::kChannels);
    for (uint64_t frame = 0; frame < outputFrames; frame++) {
        const uint64_t position = frame * sampleRate;
        const uint64_t sourceFrame = std::min(position / AudioClip::kSampleRate, sourceFrames - 1);
        const uint64_t nextFrame = std::min(sourceFrame + 1, sourceFrames - 1);
        const uint32_t fraction = (uint32_t)(position % AudioClip::kSampleRate);
        for (uint32_t channel = 0; channel < AudioClip::kChannels; channel++) {
            const int32_t a = sampleAt(samples, sourceFrame, channels, channel);
            const int32_t b = sampleAt(samples, nextFrame, channels, channel);
            clip.samples[(size_t)frame * AudioClip::kChannels + channel] =
                (int16_t)(a + (int32_t)(((int64_t)(b - a) * fraction) /
                                        AudioClip::kSampleRate));
        }
    }
    return true;
}

uint16_t readU16(const uint8_t *p) {
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

uint32_t readU32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
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

    const bool converted =
        convertPcm(decoded, sourceFrames, config.sampleRate, config.channels, clip, err);
    drmp3_free(decoded, nullptr);
    return converted;
}

bool decodeWav(const std::vector<uint8_t> &data, AudioClip &clip, std::string *err) {
    clip.samples.clear();
    const uint8_t *pcm = data.data();
    size_t pcmBytes = data.size();
    uint32_t sampleRate = 22050;
    uint16_t channels = 1;
    uint16_t bits = 16;

    if (data.size() >= 12 && !std::memcmp(data.data(), "RIFF", 4) &&
        !std::memcmp(data.data() + 8, "WAVE", 4)) {
        bool foundFormat = false, foundData = false;
        uint16_t format = 0;
        for (size_t offset = 12; offset + 8 <= data.size();) {
            const uint8_t *chunk = data.data() + offset;
            const uint32_t size = readU32(chunk + 4);
            const size_t payload = offset + 8;
            if (payload + size > data.size()) break;
            if (!std::memcmp(chunk, "fmt ", 4) && size >= 16) {
                format = readU16(data.data() + payload);
                channels = readU16(data.data() + payload + 2);
                sampleRate = readU32(data.data() + payload + 4);
                bits = readU16(data.data() + payload + 14);
                foundFormat = true;
            } else if (!std::memcmp(chunk, "data", 4)) {
                pcm = data.data() + payload;
                pcmBytes = size;
                foundData = true;
            }
            offset = payload + size + (size & 1u);
        }
        if (!foundFormat || !foundData || format != 1) {
            if (err) *err = "unsupported or corrupt WAV stream";
            return false;
        }
    }

    if ((channels != 1 && channels != 2) || (bits != 8 && bits != 16) || sampleRate == 0) {
        if (err) *err = "unsupported PCM format";
        return false;
    }
    const size_t bytesPerSample = bits / 8;
    const size_t sourceFrames = pcmBytes / (bytesPerSample * channels);
    if (!sourceFrames) {
        if (err) *err = "empty PCM data";
        return false;
    }
    std::vector<int16_t> source(sourceFrames * channels);
    if (bits == 16) {
        for (size_t i = 0; i < source.size(); i++)
            source[i] = (int16_t)readU16(pcm + i * 2);
    } else {
        for (size_t i = 0; i < source.size(); i++)
            source[i] = (int16_t)(((int)pcm[i] - 128) << 8);
    }
    return convertPcm(source.data(), sourceFrames, sampleRate, channels, clip, err);
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

bool loadWav(const std::string &path, AudioClip &clip, std::string *err) {
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
        if (err) *err = "invalid WAV file " + path;
        return false;
    }
    std::vector<uint8_t> data((size_t)length);
    const bool read =
        fread(data.data(), 1, data.size(), fp) == data.size();
    fclose(fp);
    if (!read) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    return decodeWav(data, clip, err);
}

} // namespace swgb
