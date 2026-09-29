// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace swgb {

struct AudioClip {
    static constexpr uint32_t kSampleRate = 48000;
    static constexpr uint32_t kChannels = 2;

    std::vector<int16_t> samples;
    size_t frameCount() const { return samples.size() / kChannels; }
};

bool decodeMp3(const std::vector<uint8_t> &data, AudioClip &clip, std::string *err = nullptr);
bool loadMp3(const std::string &path, AudioClip &clip, std::string *err = nullptr);

} // namespace swgb
