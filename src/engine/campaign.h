// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../core/scenario.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace swgb {

using CampaignStringLookup =
    std::function<std::string(int, const std::string &)>;

struct CampaignMission {
    std::string archivePath;
    std::string archiveName;
    std::string key;
    std::string filename;
    std::string title;
    std::string description;
    std::string objectives;
    std::string intelligence;
    std::string reconnaissance;
    std::string faction;
    uint32_t entry = 0;
    uint32_t civilization = 0;
    uint32_t mapSize = 0;
    uint32_t unitCount = 0;
};

struct CampaignInfo {
    std::string archivePath;
    std::string archiveName;
    std::string title;
    std::string description;
    int originalNumber = 0;
    std::vector<CampaignMission> missions;
};

class CampaignCatalog {
public:
    bool discover(
        const std::string &directory,
        const CampaignStringLookup &strings,
        std::string *err = nullptr);
    bool loadScenario(
        size_t campaign, size_t mission,
        Scenario &scenario,
        std::string *err = nullptr) const;
    size_t missionCount() const;
    const std::vector<CampaignInfo> &campaigns() const {
        return campaigns_;
    }

private:
    std::vector<CampaignInfo> campaigns_;
};

struct CampaignProgress {
    std::string missionKey;
    bool completed = false;
};

struct CampaignProfile {
    bool developmentAccess = true;
    int difficulty = 2;
    std::vector<CampaignProgress> progress;

    bool isCompleted(const std::string &key) const;
    bool isUnlocked(
        const CampaignInfo &campaign,
        size_t mission) const;
    void complete(const std::string &key);
};

bool loadCampaignProfile(
    const std::string &path,
    CampaignProfile &profile,
    std::string *err = nullptr);
bool saveCampaignProfile(
    const std::string &path,
    const CampaignProfile &profile,
    std::string *err = nullptr);

} // namespace swgb
