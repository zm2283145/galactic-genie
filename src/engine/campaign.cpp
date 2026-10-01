// SPDX-License-Identifier: GPL-3.0-or-later
#include "campaign.h"

#include "../core/cpx.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>

namespace swgb {
namespace {

constexpr char kProfileMagic[8] = {
    'S', 'W', 'G', 'B', 'C', 'A', 'M', 'P'};
constexpr uint32_t kProfileVersion = 1;
constexpr size_t kMaxProfileBytes = 64 * 1024;
constexpr size_t kMaxProgressEntries = 256;

uint32_t hashBytes(const uint8_t *data, size_t size) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

void putU32(std::vector<uint8_t> &out, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back((uint8_t)(value >> shift));
}

uint32_t getU32(const uint8_t *data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

void putString(
    std::vector<uint8_t> &out,
    const std::string &value) {
    putU32(out, (uint32_t)value.size());
    out.insert(out.end(), value.begin(), value.end());
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
            *err = "could not create campaign profile: " +
                   std::string(std::strerror(errno));
        return false;
    }
    const bool written =
        std::fwrite(bytes.data(), 1, bytes.size(), file) ==
            bytes.size() &&
        std::fflush(file) == 0;
    const int closed = std::fclose(file);
    if (!written || closed != 0) {
        std::remove(temporary.c_str());
        if (err) *err = "could not write complete campaign profile";
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
            *err = "could not replace campaign profile: " +
                   std::string(std::strerror(errno));
        return false;
    }
    if (hadOriginal) std::remove(backup.c_str());
    return true;
}

std::string lower(std::string value) {
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](unsigned char c) {
            return (char)std::tolower(c);
        });
    return value;
}

bool archiveNumber(
    const std::string &name, int &number) {
    const std::string value = lower(name);
    if (value.size() < 9 ||
        value.compare(0, 4, "xcam") != 0 ||
        value.compare(value.size() - 4, 4, ".cpx") != 0)
        return false;
    const std::string digits =
        value.substr(4, value.size() - 8);
    if (digits.empty() || digits.size() > 2)
        return false;
    number = 0;
    for (char c : digits) {
        if (c < '0' || c > '9') return false;
        number = number * 10 + c - '0';
    }
    return number > 0;
}

std::string joinPath(
    const std::string &dir,
    const std::string &name) {
    if (dir.empty()) return name;
    const char last = dir.back();
    return last == '/' || last == '\\'
               ? dir + name
               : dir + "/" + name;
}

struct OriginalCampaignStrings {
    int menu = 0;
    int firstMission = 0;
    int description = 0;
};

OriginalCampaignStrings stringIds(int number) {
    switch (number) {
    case 1: return {35228, 35229, 36228};
    case 2: return {35258, 35259, 36258};
    case 3: return {35288, 35289, 36288};
    case 4: return {35318, 35319, 36318};
    case 5: return {35348, 35349, 36348};
    case 8: return {35438, 35439, 36438};
    default: return {};
    }
}

std::string trimNumberPrefix(std::string value) {
    const size_t dot = value.find('.');
    if (dot != std::string::npos && dot < 3) {
        size_t start = dot + 1;
        while (start < value.size() &&
               value[start] == ' ')
            ++start;
        value.erase(0, start);
    }
    return value;
}

std::string missionObjectives(const Scenario &scenario) {
    std::string result;
    size_t count = 0;
    for (const ScenarioTrigger &trigger : scenario.triggers) {
        if (!trigger.objective ||
            trigger.description.empty())
            continue;
        if (!result.empty()) result += "\n";
        result += trigger.description;
        if (++count == 4) break;
    }
    return result;
}

} // namespace

bool CampaignCatalog::discover(
    const std::string &directory,
    const CampaignStringLookup &strings,
    std::string *err) {
    campaigns_.clear();
    DIR *dir = opendir(directory.c_str());
    if (!dir) {
        if (err)
            *err = "campaign directory is unavailable: " +
                   directory;
        return false;
    }
    std::vector<std::pair<int, std::string>> paths;
    while (dirent *entry = readdir(dir)) {
        int number = 0;
        if (archiveNumber(entry->d_name, number) &&
            stringIds(number).menu != 0)
            paths.push_back({
                number,
                joinPath(directory, entry->d_name)});
    }
    closedir(dir);
    std::sort(paths.begin(), paths.end());
    for (const auto &path : paths) {
        std::string archiveError;
        auto archive =
            CpxArchive::open(path.second, &archiveError);
        if (!archive) {
            if (err) *err = archiveError;
            campaigns_.clear();
            return false;
        }
        CampaignInfo campaign;
        campaign.archivePath = path.second;
        campaign.archiveName = archive->name();
        campaign.originalNumber = path.first;
        const OriginalCampaignStrings ids =
            stringIds(path.first);
        campaign.title =
            strings && ids.menu
                ? strings(
                      ids.menu,
                      archive->name())
                : archive->name();
        campaign.title =
            trimNumberPrefix(campaign.title);
        campaign.description =
            strings && ids.description
                ? strings(ids.description, "")
                : "";
        for (size_t i = 0;
             i < archive->entries().size(); ++i) {
            std::vector<uint8_t> bytes;
            Scenario scenario;
            if (!archive->read(i, bytes, &archiveError) ||
                !scenario.load(bytes, &archiveError)) {
                if (err)
                    *err = archive->name() + " mission " +
                           std::to_string(i + 1) + ": " +
                           archiveError;
                campaigns_.clear();
                return false;
            }
            CampaignMission mission;
            mission.archivePath = path.second;
            mission.archiveName = archive->name();
            mission.entry = (uint32_t)i;
            mission.filename =
                archive->entries()[i].filename;
            mission.key =
                lower(archive->name()) + ":" +
                std::to_string(i + 1);
            const std::string fallback =
                archive->entries()[i].identifier;
            mission.title =
                strings && ids.firstMission
                    ? strings(
                          ids.firstMission + (int)i,
                          fallback)
                    : fallback;
            mission.title =
                trimNumberPrefix(mission.title);
            mission.description = scenario.instructions;
            mission.objectives =
                missionObjectives(scenario);
            mission.mapSize = scenario.map.width;
            mission.unitCount =
                (uint32_t)scenario.units.size();
            for (const ScenarioPlayer &player :
                 scenario.players) {
                if (!player.active || !player.human)
                    continue;
                mission.faction = player.name;
                mission.civilization =
                    player.civilization;
                break;
            }
            campaign.missions.push_back(
                std::move(mission));
        }
        campaigns_.push_back(std::move(campaign));
    }
    if (campaigns_.empty()) {
        if (err)
            *err = "no XCAM*.CPX archives were found in " +
                   directory;
        return false;
    }
    return true;
}

bool CampaignCatalog::loadScenario(
    size_t campaign, size_t mission,
    Scenario &scenario, std::string *err) const {
    if (campaign >= campaigns_.size() ||
        mission >= campaigns_[campaign].missions.size()) {
        if (err) *err = "campaign mission index is out of range";
        return false;
    }
    const CampaignMission &selected =
        campaigns_[campaign].missions[mission];
    auto archive =
        CpxArchive::open(selected.archivePath, err);
    if (!archive) return false;
    std::vector<uint8_t> bytes;
    return archive->read(selected.entry, bytes, err) &&
           scenario.load(bytes, err);
}

size_t CampaignCatalog::missionCount() const {
    size_t total = 0;
    for (const CampaignInfo &campaign : campaigns_)
        total += campaign.missions.size();
    return total;
}

bool CampaignProfile::isCompleted(
    const std::string &key) const {
    for (const CampaignProgress &entry : progress)
        if (entry.missionKey == key)
            return entry.completed;
    return false;
}

bool CampaignProfile::isUnlocked(
    const CampaignInfo &campaign,
    size_t mission) const {
    return developmentAccess || mission == 0 ||
           (mission <= campaign.missions.size() &&
            isCompleted(
                campaign.missions[mission - 1].key));
}

void CampaignProfile::complete(
    const std::string &key) {
    for (CampaignProgress &entry : progress)
        if (entry.missionKey == key) {
            entry.completed = true;
            return;
        }
    if (progress.size() < kMaxProgressEntries)
        progress.push_back({key, true});
}

bool loadCampaignProfile(
    const std::string &path,
    CampaignProfile &profile,
    std::string *err) {
    FILE *file = std::fopen(path.c_str(), "rb");
    if (!file) {
        if (errno == ENOENT) {
            profile = CampaignProfile{};
            return true;
        }
        if (err)
            *err = "could not open campaign profile: " +
                   std::string(std::strerror(errno));
        return false;
    }
    std::vector<uint8_t> bytes(kMaxProfileBytes + 1);
    const size_t size =
        std::fread(bytes.data(), 1, bytes.size(), file);
    const bool failed = std::ferror(file) != 0;
    std::fclose(file);
    bytes.resize(size);
    if (failed || size < 25 ||
        size > kMaxProfileBytes ||
        std::memcmp(
            bytes.data(), kProfileMagic,
            sizeof kProfileMagic) != 0) {
        if (err)
            *err = "campaign profile is corrupt or oversized";
        return false;
    }
    const uint32_t expected =
        getU32(bytes.data() + size - 4);
    if (expected != hashBytes(
                        bytes.data(), size - 4)) {
        if (err) *err = "campaign profile checksum mismatch";
        return false;
    }
    size_t offset = 8;
    const uint32_t version =
        getU32(bytes.data() + offset);
    offset += 4;
    if (version != kProfileVersion) {
        if (err) *err = "unsupported campaign profile version";
        return false;
    }
    CampaignProfile loaded;
    loaded.developmentAccess =
        getU32(bytes.data() + offset) != 0;
    offset += 4;
    loaded.difficulty =
        (int)getU32(bytes.data() + offset);
    offset += 4;
    const uint32_t count =
        getU32(bytes.data() + offset);
    offset += 4;
    if (loaded.difficulty < 0 ||
        loaded.difficulty > 4 ||
        count > kMaxProgressEntries) {
        if (err) *err = "campaign profile contains invalid values";
        return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (offset + 4 > size - 4) {
            if (err) *err = "campaign profile is truncated";
            return false;
        }
        const uint32_t length =
            getU32(bytes.data() + offset);
        offset += 4;
        if (length > 128 ||
            offset + length + 1 > size - 4) {
            if (err)
                *err = "campaign profile entry is invalid";
            return false;
        }
        CampaignProgress progress;
        progress.missionKey.assign(
            reinterpret_cast<const char *>(
                bytes.data() + offset),
            length);
        offset += length;
        progress.completed = bytes[offset++] != 0;
        loaded.progress.push_back(
            std::move(progress));
    }
    if (offset != size - 4) {
        if (err) *err = "campaign profile has trailing data";
        return false;
    }
    profile = std::move(loaded);
    return true;
}

bool saveCampaignProfile(
    const std::string &path,
    const CampaignProfile &profile,
    std::string *err) {
    if (profile.difficulty < 0 ||
        profile.difficulty > 4 ||
        profile.progress.size() >
            kMaxProgressEntries) {
        if (err) *err = "invalid campaign profile values";
        return false;
    }
    std::vector<uint8_t> bytes(
        kProfileMagic,
        kProfileMagic + sizeof kProfileMagic);
    putU32(bytes, kProfileVersion);
    putU32(bytes, profile.developmentAccess ? 1u : 0u);
    putU32(bytes, (uint32_t)profile.difficulty);
    putU32(bytes, (uint32_t)profile.progress.size());
    for (const CampaignProgress &entry :
         profile.progress) {
        if (entry.missionKey.size() > 128) {
            if (err) *err = "campaign mission key is too long";
            return false;
        }
        putString(bytes, entry.missionKey);
        bytes.push_back(entry.completed ? 1 : 0);
    }
    putU32(bytes, hashBytes(bytes.data(), bytes.size()));
    return writeAtomic(path, bytes, err);
}

} // namespace swgb
