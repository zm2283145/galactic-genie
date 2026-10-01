// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <vector>

namespace swgb {
namespace {

constexpr char kMagic[8] = {'S', 'W', 'G', 'B', 'S', 'E', 'T', '1'};
constexpr size_t kSettingsSize = 36;

void writeU32(std::vector<uint8_t> &out, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back((uint8_t)(value >> shift));
}

uint32_t readU32(const uint8_t *data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

uint32_t checksum(const uint8_t *data, size_t size) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

bool writeAtomic(
    const std::string &path,
    const std::vector<uint8_t> &bytes,
    std::string *err) {
    const std::string temporary = path + ".tmp";
    const std::string backup = path + ".bak";
    FILE *file = std::fopen(temporary.c_str(), "wb");
    if (!file) {
        if (err)
            *err = "could not create temporary settings file: " +
                   std::string(std::strerror(errno));
        return false;
    }
    const bool written =
        std::fwrite(bytes.data(), 1, bytes.size(), file) ==
            bytes.size() &&
        std::fflush(file) == 0;
    const int closeResult = std::fclose(file);
    if (!written || closeResult != 0) {
        std::remove(temporary.c_str());
        if (err) *err = "could not write complete settings file";
        return false;
    }
    std::remove(backup.c_str());
    const bool hadOriginal =
        std::rename(path.c_str(), backup.c_str()) == 0;
    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
        if (hadOriginal)
            std::rename(backup.c_str(), path.c_str());
        std::remove(temporary.c_str());
        if (err)
            *err = "could not replace settings file: " +
                   std::string(std::strerror(errno));
        return false;
    }
    if (hadOriginal) std::remove(backup.c_str());
    return true;
}

} // namespace

const char *controlPresetName(ControlPreset preset) {
    switch (preset) {
    case ControlPreset::Standard:
        return "STANDARD";
    case ControlPreset::LeftHanded:
        return "LEFT-HANDED";
    }
    return "UNSUPPORTED";
}

bool validateSettings(
    const UserSettings &settings,
    std::string *err) {
    const int values[] = {
        settings.masterVolume,
        settings.musicVolume,
        settings.dialogueVolume,
        settings.effectsVolume,
    };
    for (int value : values)
        if (value < 0 || value > 100) {
            if (err) *err = "volume setting is outside 0-100";
            return false;
        }
    if (settings.controls != ControlPreset::Standard &&
        settings.controls != ControlPreset::LeftHanded) {
        if (err) *err = "unsupported control preset";
        return false;
    }
    return true;
}

bool loadSettings(
    const std::string &path,
    UserSettings &settings,
    std::string *err) {
    FILE *file = std::fopen(path.c_str(), "rb");
    if (!file) {
        if (errno == ENOENT) {
            settings = UserSettings{};
            return true;
        }
        if (err)
            *err = "could not open settings: " +
                   std::string(std::strerror(errno));
        return false;
    }
    std::vector<uint8_t> bytes(kSettingsSize + 1);
    const size_t size =
        std::fread(bytes.data(), 1, bytes.size(), file);
    const bool readError = std::ferror(file) != 0;
    std::fclose(file);
    if (readError || size != kSettingsSize ||
        std::memcmp(bytes.data(), kMagic, sizeof kMagic) != 0) {
        if (err)
            *err = "settings file is corrupt or unsupported";
        return false;
    }
    const uint32_t expected =
        readU32(bytes.data() + kSettingsSize - 4);
    if (expected != checksum(
                        bytes.data(),
                        kSettingsSize - 4)) {
        if (err) *err = "settings checksum mismatch";
        return false;
    }
    if (readU32(bytes.data() + 8) != 1) {
        if (err) *err = "unsupported settings version";
        return false;
    }
    UserSettings loaded;
    loaded.masterVolume =
        (int)readU32(bytes.data() + 12);
    loaded.musicVolume =
        (int)readU32(bytes.data() + 16);
    loaded.dialogueVolume =
        (int)readU32(bytes.data() + 20);
    loaded.effectsVolume =
        (int)readU32(bytes.data() + 24);
    loaded.controls =
        (ControlPreset)readU32(bytes.data() + 28);
    if (!validateSettings(loaded, err)) return false;
    settings = loaded;
    return true;
}

bool saveSettings(
    const std::string &path,
    const UserSettings &settings,
    std::string *err) {
    if (!validateSettings(settings, err)) return false;
    std::vector<uint8_t> bytes(
        kMagic, kMagic + sizeof kMagic);
    writeU32(bytes, 1);
    writeU32(bytes, (uint32_t)settings.masterVolume);
    writeU32(bytes, (uint32_t)settings.musicVolume);
    writeU32(bytes, (uint32_t)settings.dialogueVolume);
    writeU32(bytes, (uint32_t)settings.effectsVolume);
    writeU32(bytes, (uint32_t)settings.controls);
    writeU32(bytes, checksum(bytes.data(), bytes.size()));
    return writeAtomic(path, bytes, err);
}

} // namespace swgb
