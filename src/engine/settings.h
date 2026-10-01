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
};

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
