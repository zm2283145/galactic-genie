// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

namespace swgb {

enum class ControlPreset : uint8_t {
    Standard = 0,
    LeftHanded = 1,
};

struct UserSettings {
    int masterVolume = 100;
    int musicVolume = 70;
    int dialogueVolume = 100;
    int effectsVolume = 85;
    ControlPreset controls = ControlPreset::Standard;
    // The original Options dialog (screen info 50018).
    int gameSpeed = 1;         // 0 Slow (1.0), 1 Normal (1.5), 2 Fast (2.0)
    int scrollSpeed = 84;      // 10..109, the original's default 84
    bool audioTaunts = true;   // 9526
    bool oneClickGarrison = false; // 9527 (registry default off)
    bool friendOrFoeColors = false; // 9534
};
// The game speed multiplier for UserSettings::gameSpeed.
inline float gameSpeedMultiplier(int index) {
    return index <= 0 ? 1.0f : index == 1 ? 1.5f : 2.0f;
}

const char *controlPresetName(ControlPreset preset);
bool validateSettings(
    const UserSettings &settings,
    std::string *err = nullptr);
bool loadSettings(
    const std::string &path,
    UserSettings &settings,
    std::string *err = nullptr);
bool saveSettings(
    const std::string &path,
    const UserSettings &settings,
    std::string *err = nullptr);

} // namespace swgb
