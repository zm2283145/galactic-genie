// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace swgb {

struct AudioClip {
    static constexpr uint32_t kSampleRate = 44100;
    static constexpr uint32_t kChannels = 2;

    std::vector<int16_t> samples;
    size_t frameCount() const { return samples.size() / kChannels; }
};

bool decodeMp3(const std::vector<uint8_t> &data, AudioClip &clip, std::string *err = nullptr);
bool decodeWav(const std::vector<uint8_t> &data, AudioClip &clip, std::string *err = nullptr);
bool loadMp3(const std::string &path, AudioClip &clip, std::string *err = nullptr);
bool loadWav(const std::string &path, AudioClip &clip, std::string *err = nullptr);
bool readAudioFile(const std::string &path, std::vector<uint8_t> &data);
// Frame count decodeMp3() would produce for this stream (after conversion to
// kSampleRate), read from the frame headers without decoding. False when the
// stream would not decode or convert; callers then decode it normally.
bool mp3OutputFrameCount(const std::vector<uint8_t> &data, uint64_t &frames);
// Frame count loadWav(path) would produce, from the RIFF chunk headers only
// (no PCM read or resample). False when loadWav would fail.
bool wavFileOutputFrameCount(const std::string &path, uint64_t &frames);

// MP3 file decoded a little at a time (menu music), converted to
// kSampleRate stereo by linear interpolation as it is read.
class Mp3Stream {
public:
    Mp3Stream() = default;
    ~Mp3Stream();
    Mp3Stream(const Mp3Stream &) = delete;
    Mp3Stream &operator=(const Mp3Stream &) = delete;
    bool open(const std::string &path);
    void close();
    bool isOpen() const { return decoder_ != nullptr; }
    // Fills up to frames stereo frames; fewer (0) at the end of the file.
    size_t read(int16_t *out, size_t frames);
    bool rewind();

private:
    bool refill();
    void *decoder_ = nullptr; // drmp3
    uint32_t sourceRate_ = 0, sourceChannels_ = 0;
    std::vector<int16_t> source_; // stereo source frames
    size_t sourceFrames_ = 0;
    double position_ = 0.0;       // in source frames, within source_
    bool ended_ = false;
};

} // namespace swgb
