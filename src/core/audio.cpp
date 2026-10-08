// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio.h"

#include <cmath>

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
    // Same linear interpolation as before, without 64-bit divisions per
    // sample (a library call on the Vita): the source position advances by
    // sampleRate/kSampleRate each output frame, and the interpolation term
    // ((b - a) * fraction) / kSampleRate is computed in double, which is
    // exact here (|numerator| < 2^32, so truncation cannot differ).
    uint64_t sourcePosition = 0; // == (frame * sampleRate) / kSampleRate
    uint32_t fraction = 0;       // == (frame * sampleRate) % kSampleRate
    for (uint64_t frame = 0; frame < outputFrames; frame++) {
        const uint64_t sourceFrame = std::min(sourcePosition, sourceFrames - 1);
        const uint64_t nextFrame = std::min(sourceFrame + 1, sourceFrames - 1);
        for (uint32_t channel = 0; channel < AudioClip::kChannels; channel++) {
            const int32_t a = sampleAt(samples, sourceFrame, channels, channel);
            const int32_t b = sampleAt(samples, nextFrame, channels, channel);
            const int32_t step =
                fraction == 0 || a == b
                    ? 0
                    : (int32_t)((double)((int64_t)(b - a) * fraction) /
                                (double)AudioClip::kSampleRate);
            clip.samples[(size_t)frame * AudioClip::kChannels + channel] =
                (int16_t)(a + step);
        }
        fraction += sampleRate;
        while (fraction >= AudioClip::kSampleRate) {
            fraction -= AudioClip::kSampleRate;
            sourcePosition++;
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

bool readAudioFile(const std::string &path, std::vector<uint8_t> &data) {
    data.clear();
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) return false;
    bool ok = fseek(fp, 0, SEEK_END) == 0;
    const long length = ok ? ftell(fp) : -1;
    ok = ok && length > 0 && fseek(fp, 0, SEEK_SET) == 0;
    if (ok) {
        data.resize((size_t)length);
        ok = fread(data.data(), 1, data.size(), fp) == data.size();
    }
    fclose(fp);
    if (!ok) data.clear();
    return ok;
}

bool mp3OutputFrameCount(const std::vector<uint8_t> &data, uint64_t &frames) {
    frames = 0;
    if (data.empty()) return false;
    drmp3 mp3;
    if (!drmp3_init_memory(&mp3, data.data(), data.size(), nullptr)) return false;
    const drmp3_uint64 sourceFrames = drmp3_get_pcm_frame_count(&mp3);
    const uint32_t sampleRate = mp3.sampleRate;
    const uint32_t channels = mp3.channels;
    drmp3_uninit(&mp3);
    // Same acceptance tests as decodeMp3() and convertPcm().
    if (sourceFrames == 0 || sampleRate == 0 || (channels != 1 && channels != 2) ||
        sourceFrames > UINT64_MAX / AudioClip::kSampleRate)
        return false;
    const uint64_t outputFrames =
        (sourceFrames * AudioClip::kSampleRate + sampleRate - 1) / sampleRate;
    if (outputFrames > kMaxDecodedBytes / (sizeof(int16_t) * AudioClip::kChannels))
        return false;
    frames = outputFrames;
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

bool wavFileOutputFrameCount(const std::string &path, uint64_t &frames) {
    frames = 0;
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) return false;
    bool ok = fseek(fp, 0, SEEK_END) == 0;
    const long length = ok ? ftell(fp) : -1;
    ok = ok && length > 0 && fseek(fp, 0, SEEK_SET) == 0;
    if (!ok) {
        fclose(fp);
        return false;
    }
    const size_t size = (size_t)length;
    // Mirrors decodeWav() with data.size() == size.
    size_t pcmBytes = size;
    uint32_t sampleRate = 22050;
    uint16_t channels = 1;
    uint16_t bits = 16;
    uint8_t header[16];
    if (size >= 12 && fread(header, 1, 12, fp) == 12 && !std::memcmp(header, "RIFF", 4) &&
        !std::memcmp(header + 8, "WAVE", 4)) {
        bool foundFormat = false, foundData = false;
        uint16_t format = 0;
        for (size_t offset = 12; offset + 8 <= size;) {
            uint8_t chunk[8];
            if (fseek(fp, (long)offset, SEEK_SET) != 0 || fread(chunk, 1, 8, fp) != 8) {
                fclose(fp);
                return false;
            }
            const uint32_t chunkSize = readU32(chunk + 4);
            const size_t payload = offset + 8;
            if (payload + chunkSize > size) break;
            if (!std::memcmp(chunk, "fmt ", 4) && chunkSize >= 16) {
                if (fread(header, 1, 16, fp) != 16) {
                    fclose(fp);
                    return false;
                }
                format = readU16(header);
                channels = readU16(header + 2);
                sampleRate = readU32(header + 4);
                bits = readU16(header + 14);
                foundFormat = true;
            } else if (!std::memcmp(chunk, "data", 4)) {
                pcmBytes = chunkSize;
                foundData = true;
            }
            offset = payload + chunkSize + (chunkSize & 1u);
        }
        if (!foundFormat || !foundData || format != 1) {
            fclose(fp);
            return false;
        }
    }
    fclose(fp);
    if ((channels != 1 && channels != 2) || (bits != 8 && bits != 16) || sampleRate == 0)
        return false;
    const size_t bytesPerSample = bits / 8;
    const uint64_t sourceFrames = pcmBytes / (bytesPerSample * channels);
    if (!sourceFrames || sourceFrames > UINT64_MAX / AudioClip::kSampleRate) return false;
    const uint64_t outputFrames =
        (sourceFrames * AudioClip::kSampleRate + sampleRate - 1) / sampleRate;
    if (outputFrames > kMaxDecodedBytes / (sizeof(int16_t) * AudioClip::kChannels)) return false;
    frames = outputFrames;
    return true;
}

} // namespace swgb

namespace swgb {

Mp3Stream::~Mp3Stream() { close(); }

bool Mp3Stream::open(const std::string &path) {
    close();
    auto *mp3 = new drmp3;
    if (!drmp3_init_file(mp3, path.c_str(), nullptr)) {
        delete mp3;
        return false;
    }
    if (mp3->channels < 1 || mp3->sampleRate == 0) {
        drmp3_uninit(mp3);
        delete mp3;
        return false;
    }
    decoder_ = mp3;
    sourceRate_ = mp3->sampleRate;
    sourceChannels_ = mp3->channels;
    source_.clear();
    sourceFrames_ = 0;
    position_ = 0.0;
    ended_ = false;
    return true;
}

void Mp3Stream::close() {
    if (decoder_) {
        drmp3_uninit(static_cast<drmp3 *>(decoder_));
        delete static_cast<drmp3 *>(decoder_);
    }
    decoder_ = nullptr;
    source_.clear();
    sourceFrames_ = 0;
    position_ = 0.0;
    ended_ = false;
}

bool Mp3Stream::rewind() {
    if (!decoder_) return false;
    if (!drmp3_seek_to_pcm_frame(static_cast<drmp3 *>(decoder_), 0)) return false;
    source_.clear();
    sourceFrames_ = 0;
    position_ = 0.0;
    ended_ = false;
    return true;
}

// Appends decoded frames, keeping the frame before the read position.
bool Mp3Stream::refill() {
    if (ended_ || !decoder_) return false;
    const size_t keepFrom = position_ >= 1.0 ? (size_t)position_ : 0;
    if (keepFrom > 0) {
        source_.erase(source_.begin(), source_.begin() + keepFrom * 2);
        sourceFrames_ -= keepFrom;
        position_ -= (double)keepFrom;
    }
    constexpr size_t kChunk = 2304;
    std::vector<int16_t> decoded(kChunk * sourceChannels_);
    const drmp3_uint64 got =
        drmp3_read_pcm_frames_s16(static_cast<drmp3 *>(decoder_), kChunk, decoded.data());
    if (got == 0) {
        ended_ = true;
        return false;
    }
    for (drmp3_uint64 i = 0; i < got; ++i) {
        const int16_t left = decoded[i * sourceChannels_];
        const int16_t right = sourceChannels_ > 1 ? decoded[i * sourceChannels_ + 1] : left;
        source_.push_back(left);
        source_.push_back(right);
    }
    sourceFrames_ += (size_t)got;
    return true;
}

size_t Mp3Stream::read(int16_t *out, size_t frames) {
    if (!decoder_) return 0;
    const double step = (double)sourceRate_ / (double)AudioClip::kSampleRate;
    size_t written = 0;
    while (written < frames) {
        size_t index = (size_t)position_;
        while (index + 1 >= sourceFrames_) {
            if (!refill()) break;
            index = (size_t)position_;
        }
        if (index + 1 >= sourceFrames_) {
            // End of the file: the last frame on its own.
            if (index < sourceFrames_) {
                out[written * 2] = source_[index * 2];
                out[written * 2 + 1] = source_[index * 2 + 1];
                ++written;
                position_ += 1.0;
            }
            break;
        }
        const double fraction = position_ - (double)index;
        for (int channel = 0; channel < 2; ++channel) {
            const double a = source_[index * 2 + channel];
            const double b = source_[(index + 1) * 2 + channel];
            out[written * 2 + channel] = (int16_t)std::lround(a + (b - a) * fraction);
        }
        ++written;
        position_ += step;
    }
    return written;
}

} // namespace swgb
