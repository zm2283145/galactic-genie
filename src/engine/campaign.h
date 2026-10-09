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
    // Clone Campaigns expansion campaign (1camN.cp1) rather than one of the
    // original campaigns (xcamN.cpx); the Single Player menu lists them
    // separately.
    bool expansion = false;
    // Any other campaign archive in the folder (Custom Campaigns).
    bool custom = false;
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
    // Vita/dev toggle (L on the mission list): every mission and campaign
    // open. Off by default, as in the original (sequential unlocks).
    bool developmentAccess = false;
    int difficulty = 2;
    std::vector<CampaignProgress> progress;

    bool isCompleted(const std::string &key) const;
    bool isUnlocked(
        const CampaignInfo &campaign,
        size_t mission) const;
    // Clone Campaigns: the Republic campaign (1cam2) stays locked until
    // the Confederacy campaign's (1cam1) last mission is completed
    // (Campaign Game Screen 0x507f10). Everything else is open.
    bool isCampaignUnlocked(
        const CampaignInfo &campaign,
        const std::vector<CampaignInfo> &all) const;
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
