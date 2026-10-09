// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend.h"
#include "campaign_scene.h"
#include "menu_framework.h"
#include "ui_text.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

namespace swgb {
namespace {

constexpr float kMenuTop = 154.0f;
constexpr float kMenuRow = 46.0f;
constexpr float kLobbyTop = 91.0f;
constexpr float kLobbyRow = 28.0f;
constexpr float kOriginalMenuTop = 93.0f;
constexpr float kOriginalMenuRow = 58.5f;
constexpr float kOriginalMenuEntryHeight = 38.0f;
constexpr float kOriginalMissionTop = 108.0f;
constexpr float kOriginalMissionRow = 43.0f;
struct OriginalHotspot {
    float x;
    float y;
    float w;
    float h;
};
// Internal order keeps Single Player as the default controller selection.
constexpr std::array<OriginalHotspot, 8>
    kOriginalMainHotspots{{
        {282.0f, 170.0f, 174.0f, 153.0f},
        {272.0f, 54.0f, 177.0f, 147.0f},
        {55.0f, 58.0f, 185.0f, 90.0f},
        {16.0f, 176.0f, 141.0f, 169.0f},
        {55.0f, 306.0f, 100.0f, 124.0f},
        {190.0f, 370.0f, 214.0f, 74.0f},
        {198.0f, 474.0f, 138.0f, 80.0f},
        {0.0f, 540.0f, 93.0f, 50.0f},
    }};
constexpr std::array<OriginalHotspot, 8>
    kExpandingFrontsMainHotspots{{
        {283.0f, 195.0f, 125.0f, 93.0f},
        {282.0f, 97.0f, 122.0f, 77.0f},
        {100.0f, 92.0f, 107.0f, 79.0f},
        {37.0f, 212.0f, 100.0f, 90.0f},
        {31.0f, 369.0f, 111.0f, 119.0f},
        {206.0f, 363.0f, 184.0f, 110.0f},
        {183.0f, 518.0f, 123.0f, 74.0f},
        {0.0f, 541.0f, 93.0f, 50.0f},
    }};

const std::array<OriginalHotspot, 8> &
mainHotspots(bool expandingFronts) {
    return expandingFronts
               ? kExpandingFrontsMainHotspots
               : kOriginalMainHotspots;
}
struct OriginalCampaignPlacement {
    float iconX;
    float iconY;
    float labelCenterX;
    float labelY;
    size_t labelWidth;
};
constexpr std::array<OriginalCampaignPlacement, 6>
    kOriginalCampaignPlacements{{
        {130.0f, 297.0f, 242.0f, 369.0f, 18},
        {138.0f, 184.0f, 269.0f, 255.0f, 20},
        {240.0f, 95.0f, 331.0f, 166.0f, 20},
        {481.0f, 95.0f, 479.0f, 166.0f, 20},
        {565.0f, 184.0f, 510.0f, 255.0f, 22},
        {594.0f, 297.0f, 579.0f, 369.0f, 20},
    }};

// Clone Campaigns selection screen (interfac_x1.drs 53231, "1cam_pic_pos"):
// icon x y, label box x y w.
constexpr std::array<OriginalCampaignPlacement, 2>
    kCloneCampaignPlacements{{
        {194.0f, 272.0f, 230.0f, 383.0f, 16},
        {525.0f, 272.0f, 560.0f, 383.0f, 16},
    }};

// Where a campaign's icon and label go on its selection screen, and the
// number its label shows; nullptr when it has no place there.
const OriginalCampaignPlacement *campaignPlacement(
    const CampaignInfo &campaign, size_t &number);

size_t originalCampaignSlot(
    const CampaignInfo &campaign) {
    switch (campaign.originalNumber) {
    case 8: return 0;
    case 1: return 1;
    case 2: return 2;
    case 3: return 3;
    case 4: return 4;
    case 5: return 5;
    default: return kOriginalCampaignPlacements.size();
    }
}

const OriginalCampaignPlacement *campaignPlacement(
    const CampaignInfo &campaign, size_t &number) {
    if (campaign.custom) return nullptr;
    if (campaign.expansion) {
        if (campaign.originalNumber < 1 ||
            (size_t)campaign.originalNumber > kCloneCampaignPlacements.size())
            return nullptr;
        number = (size_t)campaign.originalNumber;
        return &kCloneCampaignPlacements[(size_t)campaign.originalNumber - 1];
    }
    const size_t slot = originalCampaignSlot(campaign);
    if (slot >= kOriginalCampaignPlacements.size()) return nullptr;
    number = slot + 1;
    return &kOriginalCampaignPlacements[slot];
}

void centeredText(
    Renderer &renderer, const std::string &value,
    float y, float scale, int screenW,
    uint8_t r, uint8_t g, uint8_t b) {
    drawUiText(
        renderer, {value},
        (screenW - uiTextWidth(value, scale)) * 0.5f,
        y, scale, r, g, b);
}

void panel(
    Renderer &renderer, float x, float y,
    float w, float h) {
    drawModernPanel(renderer, x, y, w, h);
}

void wrappedText(
    Renderer &renderer, const std::string &value,
    float x, float y, float scale,
    size_t lineLength, size_t maxLines,
    uint8_t r, uint8_t g, uint8_t b) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < value.size() &&
           lines.size() < maxLines) {
        size_t end = std::min(
            value.size(), start + lineLength);
        if (end < value.size()) {
            const size_t breakAt =
                value.rfind(' ', end);
            if (breakAt != std::string::npos &&
                breakAt > start)
                end = breakAt;
        }
        std::string line =
            value.substr(start, end - start);
        std::replace(
            line.begin(), line.end(), '\n', ' ');
        lines.push_back(line);
        start = end;
        while (start < value.size() &&
               (value[start] == ' ' ||
                value[start] == '\n' ||
                value[start] == '\r'))
            ++start;
    }
    drawUiText(renderer, lines, x, y, scale, r, g, b);
}

std::string optionValue(
    const UserSettings &settings, size_t row) {
    switch (row) {
    case 0: return std::to_string(settings.masterVolume) + "%";
    case 1: return std::to_string(settings.musicVolume) + "%";
    case 2: return std::to_string(settings.dialogueVolume) + "%";
    case 3: return std::to_string(settings.effectsVolume) + "%";
    case 4: return controlPresetName(settings.controls);
    case 5: return "BACK";
    default: return {};
    }
}

const char *optionLabel(size_t row) {
    static constexpr std::array<const char *, 6> labels{{
        "Master Volume", "Music Volume",
        "Dialogue Volume", "Effects Volume",
        "Control Preset", "",
    }};
    return row < labels.size() ? labels[row] : "";
}

std::string lobbyValue(
    const SkirmishSettings &settings,
    size_t page, size_t selectedSlot,
    size_t row) {
    if (page == 0) {
        const SkirmishSlot &slot =
            settings.slots[selectedSlot];
        switch (row) {
        case 0:
            return "SLOT " +
                   std::to_string(selectedSlot + 1);
        case 1: return slotTypeName(slot.type);
        case 2: return slot.name;
        case 3:
            return std::to_string(slot.color + 1);
        case 4:
            return civilizationName(
                slot.civilization);
        case 5:
            return slot.type ==
                           SkirmishSlotType::Computer
                       ? difficultyName(
                             slot.difficulty)
                       : "N/A";
        case 6:
            return slot.type ==
                           SkirmishSlotType::Computer
                       ? personalityName(
                             slot.personality)
                       : "N/A";
        case 7:
            return slot.team
                       ? std::to_string(slot.team)
                       : "NO TEAM";
        case 8:
            return slot.alliedVictory
                       ? "ENABLED"
                       : "DISABLED";
        case 9: return "MATCH SETTINGS";
        case 10: return "START MATCH";
        case 11: return "BACK";
        default: return {};
        }
    }
    switch (row) {
    case 0: return mapStyleName(settings.mapStyle);
    case 1:
        return std::to_string(settings.mapSize) + " x " +
               std::to_string(settings.mapSize);
    case 2: return std::to_string(settings.startingResources);
    case 3: return std::to_string(settings.populationCap);
    case 4:
        return "TECH LEVEL " +
               std::to_string(settings.startingTechLevel);
    case 5:
        return "TECH LEVEL " +
               std::to_string(settings.endingTechLevel);
    case 6: return revealName(settings.reveal);
    case 7:
        return settings.teamsLocked ? "LOCKED" : "UNLOCKED";
    case 8:
        return settings.cheatsEnabled ? "ENABLED" : "DISABLED";
    case 9: return gameSpeedName(settings.gameSpeed);
    case 10: return victoryName(settings.victory);
    case 11:
        if (settings.victory ==
            SkirmishVictory::TimeLimit)
            return std::to_string(
                       settings.timeLimitMinutes) +
                   " MINUTES";
        if (settings.victory ==
            SkirmishVictory::Score)
            return std::to_string(
                settings.scoreLimit);
        return "AUTOMATIC";
    case 12: {
        char value[16];
        std::snprintf(
            value, sizeof value, "0x%08X",
            settings.seed);
        return value;
    }
    case 13: return "PLAYER SLOTS";
    case 14: return "START MATCH";
    case 15: return "BACK";
    default: return {};
    }
}

const char *lobbyLabel(size_t page, size_t row) {
    static constexpr std::array<const char *, 12> slotLabels{{
        "Edit", "State", "Name", "Color",
        "Civilization", "Difficulty", "Personality",
        "Team", "Allied Victory", "", "", "",
    }};
    static constexpr std::array<const char *, 16> matchLabels{{
        "Random Map", "Map Size", "Starting Resources",
        "Population", "Starting Age", "Ending Age",
        "Reveal Map", "Teams", "Cheats", "Game Speed",
        "Victory", "Victory Target", "Seed", "", "", "",
    }};
    return page == 0
               ? (row < slotLabels.size()
                      ? slotLabels[row]
                      : "")
               : (row < matchLabels.size()
                      ? matchLabels[row]
                      : "");
}

} // namespace

std::string Frontend::text(
    int id, const std::string &fallback) const {
    return strings_ ? strings_(id, fallback) : fallback;
}

const CampaignMission *
Frontend::selectedCampaignMission() const {
    if (!catalog_ ||
        campaignSelection_ >=
            catalog_->campaigns().size())
        return nullptr;
    const CampaignInfo &campaign =
        catalog_->campaigns()[campaignSelection_];
    return missionSelection_ < campaign.missions.size()
               ? &campaign.missions[missionSelection_]
               : nullptr;
}

void Frontend::moveSelection(
    int direction, size_t count) {
    if (!count) return;
    selection_ =
        (selection_ + count +
         (direction < 0 ? count - 1 : 1)) %
        count;
    if (sounds_) sounds_(50301);
}

size_t Frontend::rowFromPointer(
    const InputState &input, float top,
    float rowHeight, size_t count) const {
    if (!input.pointerTap ||
        input.pointerY < top ||
        input.pointerY >= top + rowHeight * count)
        return count;
    return std::min(
        count - 1,
        (size_t)((input.pointerY - top) / rowHeight));
}

void Frontend::adjustLobbyValue(int direction) {
    static constexpr std::array<int, 4> mapSizes{{64, 96, 128, 160}};
    static constexpr std::array<int, 4> resources{{200, 500, 1000, 20000}};
    static constexpr std::array<int, 5> populations{{50, 100, 150, 200, 250}};
    const auto cycle = [direction](int current, const auto &values) {
        auto found = std::find(values.begin(), values.end(), current);
        size_t index = found == values.end()
                           ? 0
                           : (size_t)(found - values.begin());
        index = (index + values.size() +
                 (direction < 0 ? values.size() - 1 : 1)) %
                values.size();
        return values[index];
    };
    if (lobbyPage_ == 0) {
        SkirmishSlot &slot =
            settings_.slots[lobbySlot_];
        switch (selection_) {
        case 0:
            lobbySlot_ =
                (lobbySlot_ +
                 kMaxSkirmishSlots +
                 (direction < 0 ? -1 : 1)) %
                kMaxSkirmishSlots;
            break;
        case 1:
            slot.type =
                (SkirmishSlotType)(
                    ((int)slot.type + 3 +
                     (direction < 0 ? -1 : 1)) %
                    3);
            break;
        case 2:
            slot.name =
                slot.type ==
                        SkirmishSlotType::Human
                    ? "Player " +
                          std::to_string(
                              lobbySlot_ + 1)
                    : "Computer " +
                          std::to_string(
                              lobbySlot_ + 1);
            break;
        case 3: {
            const uint8_t old = slot.color;
            const uint8_t next =
                (uint8_t)((slot.color +
                           kMaxSkirmishSlots +
                           (direction < 0 ? -1
                                          : 1)) %
                          kMaxSkirmishSlots);
            for (SkirmishSlot &other :
                 settings_.slots)
                if (&other != &slot &&
                    other.color == next) {
                    other.color = old;
                    break;
                }
            slot.color = next;
            break;
        }
        case 4:
            slot.civilization =
                (uint8_t)(1 +
                    (slot.civilization - 1 + 8 +
                     (direction < 0 ? -1 : 1)) %
                        8);
            break;
        case 5:
            slot.difficulty =
                (uint8_t)((slot.difficulty + 5 +
                           (direction < 0 ? -1 : 1)) %
                          5);
            break;
        case 6:
            slot.personality =
                (AiPersonality)(
                    ((int)slot.personality + 2 +
                     (direction < 0 ? -1 : 1)) %
                    2);
            break;
        case 7:
            slot.team =
                (uint8_t)((slot.team +
                           kMaxSkirmishSlots + 1 +
                           (direction < 0 ? -1 : 1)) %
                          (kMaxSkirmishSlots + 1));
            break;
        case 8:
            slot.alliedVictory =
                !slot.alliedVictory;
            break;
        default: break;
        }
        settings_.playerCivilization =
            settings_.slots[0].civilization;
        settings_.computerCivilization =
            settings_.slots[1].civilization;
        settings_.difficulty =
            settings_.slots[1].difficulty;
        settings_.personality =
            settings_.slots[1].personality;
        refreshLobbyPreview();
        return;
    }
    switch (selection_) {
    case 0:
        settings_.mapStyle =
            (SkirmishMapStyle)(((int)settings_.mapStyle + 10 +
                                (direction < 0 ? -1 : 1)) %
                               10);
        if (settings_.mapStyle ==
            SkirmishMapStyle::CompactIslands)
            settings_.mapSize = 96;
        break;
    case 1:
        if (settings_.mapStyle !=
            SkirmishMapStyle::CompactIslands)
            settings_.mapSize =
                cycle(settings_.mapSize, mapSizes);
        break;
    case 2:
        settings_.startingResources =
            cycle(settings_.startingResources, resources);
        break;
    case 3:
        settings_.populationCap =
            cycle(settings_.populationCap, populations);
        break;
    case 4:
        settings_.startingTechLevel =
            1 + (settings_.startingTechLevel - 1 + 4 +
                 (direction < 0 ? -1 : 1)) %
                    4;
        settings_.endingTechLevel =
            std::max(
                settings_.endingTechLevel,
                settings_.startingTechLevel);
        break;
    case 5:
        settings_.endingTechLevel =
            settings_.startingTechLevel +
            (settings_.endingTechLevel -
                 settings_.startingTechLevel +
             5 +
             (direction < 0 ? -1 : 1)) %
                (5 - settings_.startingTechLevel);
        break;
    case 6:
        settings_.reveal =
            (SkirmishReveal)(
                ((int)settings_.reveal + 3 +
                 (direction < 0 ? -1 : 1)) %
                3);
        break;
    case 7:
        settings_.teamsLocked =
            !settings_.teamsLocked;
        break;
    case 8:
        settings_.cheatsEnabled =
            !settings_.cheatsEnabled;
        break;
    case 9:
        settings_.gameSpeed =
            (SkirmishGameSpeed)(
                ((int)settings_.gameSpeed + 3 +
                 (direction < 0 ? -1 : 1)) %
                3);
        break;
    case 10:
        settings_.victory =
            (SkirmishVictory)(((int)settings_.victory + 5 +
                               (direction < 0 ? -1 : 1)) %
                              5);
        break;
    case 11:
        if (settings_.victory ==
            SkirmishVictory::TimeLimit)
            settings_.timeLimitMinutes =
                std::clamp(
                    settings_.timeLimitMinutes +
                        (direction < 0 ? -5 : 5),
                    5, 240);
        else if (settings_.victory ==
                 SkirmishVictory::Score)
            settings_.scoreLimit =
                std::clamp(
                    settings_.scoreLimit +
                        (direction < 0 ? -500 : 500),
                    500, 20000);
        break;
    case 12:
        settings_.seed += direction < 0 ? UINT32_MAX : 1u;
        break;
    default: break;
    }
    refreshLobbyPreview();
}

void Frontend::refreshLobbyPreview() {
    lobbyPreview_ =
        generateSkirmishPreview(settings_);
}

void Frontend::adjustOptionValue(int direction) {
    int *volume = nullptr;
    switch (selection_) {
    case 0: volume = &userSettings_.masterVolume; break;
    case 1: volume = &userSettings_.musicVolume; break;
    case 2: volume = &userSettings_.dialogueVolume; break;
    case 3: volume = &userSettings_.effectsVolume; break;
    case 4:
        userSettings_.controls =
            userSettings_.controls == ControlPreset::Standard
                ? ControlPreset::LeftHanded
                : ControlPreset::Standard;
        settingsChanged_ = true;
        return;
    default: return;
    }
    *volume = std::max(
        0, std::min(100, *volume +
                             (direction < 0 ? -5 : 5)));
    settingsChanged_ = true;
}

void Frontend::beginConfirmation(
    FrontendAction action,
    const std::string &value) {
    confirmReturnScreen_ = screen_;
    confirmedAction_ = action;
    message_ = value;
    selection_ = 1;
    screen_ = FrontendScreen::Confirm;
}

FrontendAction Frontend::update(
    const InputState &input, int matchOutcome) {
    if (screen_ == FrontendScreen::Gameplay &&
        matchOutcome >= 0) {
        outcome_ = matchOutcome;
        if (campaignMatch_ && matchOutcome == 1 &&
            profile_) {
            const CampaignMission *mission =
                selectedCampaignMission();
            if (mission) {
                profile_->complete(mission->key);
                profileChanged_ = true;
            }
        }
        // A won campaign mission plays its closing scene first.
        screen_ = campaignMatch_ && matchOutcome == 1 ? FrontendScreen::CampaignEpilogue
                                                      : FrontendScreen::Outcome;
        selection_ = 0;
        return FrontendAction::None;
    }
    if (screen_ == FrontendScreen::CampaignEpilogue) {
        // The scene is loaded by the platform layer when this screen opens;
        // without one (or once it ends or is skipped) the outcome menu follows.
        if (!campaignScene_ || !campaignScene_->loaded() || campaignScene_->finished() ||
            input.menuActivate || input.menuBack || input.pointerTap) {
            screen_ = FrontendScreen::Outcome;
            selection_ = 0;
        }
        return FrontendAction::None;
    }
    if (screen_ == FrontendScreen::Title) {
        if (input.menuActivate || input.pointerTap) {
            screen_ = FrontendScreen::MainMenu;
            selection_ = 0;
        }
        return FrontendAction::None;
    }
    if (screen_ == FrontendScreen::Loading)
        return FrontendAction::None;
    if (screen_ == FrontendScreen::Gameplay) {
        if (input.pausePressed) {
            screen_ = FrontendScreen::Pause;
            selection_ = 0;
        }
        return FrontendAction::None;
    }

    if (screen_ == FrontendScreen::MainMenu) {
        constexpr size_t count = 8;
        const auto &hotspots =
            mainHotspots(expandingFrontsMenu_);
        size_t touched = count;
        if (input.pointerTap &&
            input.screenW > 0 &&
            input.screenH > 0) {
            const float x =
                input.pointerX * 800.0f /
                input.screenW;
            const float y =
                input.pointerY * 600.0f /
                input.screenH;
            for (size_t index = 0;
                 index < count; ++index) {
                const OriginalHotspot &hotspot =
                     hotspots[index];
                if (x >= hotspot.x &&
                    x < hotspot.x + hotspot.w &&
                    y >= hotspot.y &&
                    y < hotspot.y + hotspot.h) {
                    touched = index;
                    break;
                }
            }
        }
        if (touched < count) selection_ = touched;
        if (input.menuUp) moveSelection(-1, count);
        if (input.menuDown) moveSelection(1, count);
        if (input.menuActivate || touched < count) {
            if (sounds_) sounds_(50300);
            if (selection_ == 0) {
                screen_ = FrontendScreen::SinglePlayer;
                selection_ = 0;
            } else if (selection_ == 1) {
                // Basic Training: the original opens the mission screen of
                // the training campaign (xcam8) straight from the main menu.
                size_t training = catalog_ ? catalog_->campaigns().size() : 0;
                for (size_t index = 0; index < training; ++index)
                    if (!catalog_->campaigns()[index].expansion &&
                        catalog_->campaigns()[index].originalNumber == 8) {
                        training = index;
                        break;
                    }
                if (!catalog_ || training >= catalog_->campaigns().size()) {
                    message_ = "BASIC TRAINING (XCAM8.CPX) WAS NOT FOUND";
                    if (sounds_) sounds_(50303);
                } else {
                    cloneCampaigns_ = false;
                    customCampaigns_ = false;
                    campaignSelection_ = training;
                    missionSelection_ = 0;
                    missionsFromMainMenu_ = true;
                    screen_ = FrontendScreen::CampaignMissions;
                    selection_ = 0;
                }
            } else if (selection_ == 2) {
                message_ = "COMMUNITY SERVICES ARE UNAVAILABLE";
                if (sounds_) sounds_(50303);
            } else if (selection_ == 3) {
                screen_ = FrontendScreen::DataStatus;
                selection_ = 0;
            } else if (selection_ == 4) {
                optionsReturnScreen_ = FrontendScreen::MainMenu;
                screen_ = FrontendScreen::Options;
                selection_ = 0;
            } else if (selection_ == 5) {
                message_ = "MULTIPLAYER IS UNAVAILABLE IN THIS BUILD";
                if (sounds_) sounds_(50303);
            } else if (selection_ == 6) {
                screen_ = FrontendScreen::ScenarioEditor;
                selection_ = 0;
                message_.clear();
                return FrontendAction::OpenScenarioEditor;
            } else {
                beginConfirmation(
                    FrontendAction::Quit,
                    "EXIT GALACTIC BATTLEGROUNDS?");
            }
            if (screen_ == FrontendScreen::ScenarioEditor)
                return FrontendAction::None;
        }
        return FrontendAction::None;
    }

    if (screen_ == FrontendScreen::SinglePlayer) {
        constexpr size_t count = 5;
        size_t touched = count;
        if (input.pointerTap &&
            input.screenW > 0 &&
            input.screenH > 0) {
            const float x =
                input.pointerX * 800.0f /
                input.screenW;
            const float y =
                input.pointerY * 600.0f /
                input.screenH;
            for (size_t index = 0;
                 index < count; ++index) {
                const float rowY =
                    kOriginalMenuTop +
                    index * kOriginalMenuRow;
                if (x >= 442.0f && x < 764.0f &&
                    y >= rowY &&
                    y < rowY +
                            kOriginalMenuEntryHeight) {
                    touched = index;
                    break;
                }
            }
        }
        if (touched < count) selection_ = touched;
        if (input.menuUp) moveSelection(-1, count);
        if (input.menuDown) moveSelection(1, count);
        if (input.menuBack) {
            screen_ = FrontendScreen::MainMenu;
            selection_ = 0;
            if (sounds_) sounds_(50301);
        } else if (input.menuActivate || touched < count) {
            if (sounds_) sounds_(50300);
            if (selection_ <= 1 || selection_ == 3) {
                // Original Campaigns (xcam), Clone Campaigns (1cam) or Custom
                // Campaigns (any other archive in the campaign folder).
                const bool clone = selection_ == 1;
                const bool customSet = selection_ == 3;
                size_t first = catalog_ ? catalog_->campaigns().size() : 0;
                for (size_t index = 0; index < first; ++index) {
                    const CampaignInfo &candidate = catalog_->campaigns()[index];
                    if (customSet ? candidate.custom
                                  : !candidate.custom && candidate.expansion == clone) {
                        first = index;
                        break;
                    }
                }
                if (!catalog_ || first >= catalog_->campaigns().size()) {
                    message_ = customSet ? "NO CUSTOM CAMPAIGNS WERE FOUND IN THE CAMPAIGN FOLDER"
                               : clone   ? "NO CLONE CAMPAIGNS WERE DISCOVERED"
                                         : "NO VALID ORIGINAL CAMPAIGNS WERE DISCOVERED";
                    if (sounds_) sounds_(50303);
                } else {
                    cloneCampaigns_ = clone;
                    customCampaigns_ = customSet;
                    missionsFromMainMenu_ = false;
                    screen_ = FrontendScreen::CampaignBrowser;
                    campaignSelection_ = first;
                    selection_ = campaignSelection_;
                }
            } else if (selection_ == 2) {
                screen_ = FrontendScreen::SkirmishLobby;
                selection_ = 0;
                lobbyPage_ = 0;
                refreshLobbyPreview();
            } else if (false) {
            } else {
                if (!continueAvailable_) {
                    message_ = "NO VALID SAVE IS AVAILABLE";
                    if (sounds_) sounds_(50303);
                } else {
                    screen_ = FrontendScreen::Loading;
                    message_ =
                        continueKind_ == MatchSaveKind::Campaign
                            ? "LOADING CAMPAIGN SAVE..."
                            : "LOADING SKIRMISH SAVE...";
                    return FrontendAction::LoadMatch;
                }
            }
        }
        return FrontendAction::None;
    }

    if (screen_ == FrontendScreen::CampaignBrowser) {
        const size_t count =
            catalog_ ? catalog_->campaigns().size() : 0;
        size_t touched = count;
        bool touchedBack = false;
        if (input.pointerTap &&
            input.screenW > 0 && input.screenH > 0) {
            const float logicalX =
                input.pointerX * 800.0f /
                input.screenW;
            const float logicalY =
                input.pointerY * 600.0f /
                input.screenH;
            touchedBack =
                logicalX < 187.0f &&
                logicalY < 44.0f;
            for (size_t index = 0;
                 index < count;
                 ++index) {
                if (!inBrowserSet(catalog_->campaigns()[index]))
                    continue;
                if (customCampaigns_) {
                    // Custom Campaigns are a list (rows as on the mission list).
                    const float rowY = kOriginalMissionTop + browserRow(index) * kOriginalMissionRow;
                    if (logicalX >= 54.0f && logicalX < 459.0f && logicalY >= rowY - 7.0f &&
                        logicalY < rowY + 24.0f) {
                        touched = index;
                        break;
                    }
                    continue;
                }
                size_t number = 0;
                const OriginalCampaignPlacement *found =
                    campaignPlacement(catalog_->campaigns()[index], number);
                if (!found) continue;
                const auto &placement = *found;
                const SpriteFrame *frame =
                    index <
                            originalCampaignIcons_
                                .size()
                        ? originalCampaignIcons_[
                              index][0]
                        : nullptr;
                const float width =
                    frame ? (float)frame->w
                          : 63.0f;
                const float height =
                    frame ? (float)frame->h
                          : 57.0f;
                if (logicalX >= placement.iconX &&
                    logicalX <
                        placement.iconX + width &&
                    logicalY >= placement.iconY &&
                    logicalY <
                        placement.iconY + height) {
                    touched = index;
                    break;
                }
            }
        }
        if (touched < count) selection_ = touched;
        // Up/down step through this screen's set only.
        for (int direction : {input.menuUp ? -1 : 0, input.menuDown ? 1 : 0}) {
            if (!direction || !count) continue;
            size_t next = selection_;
            for (size_t step = 0; step < count; ++step) {
                next = (next + count + (size_t)(direction > 0 ? 1 : count - 1)) % count;
                if (inBrowserSet(catalog_->campaigns()[next])) break;
            }
            if (inBrowserSet(catalog_->campaigns()[next])) selection_ = next;
        }
        if (input.menuBack || touchedBack) {
            screen_ = FrontendScreen::SinglePlayer;
            selection_ = customCampaigns_ ? 3 : cloneCampaigns_ ? 1 : 0;
        } else if ((input.menuActivate || touched < count) &&
                   count) {
            campaignSelection_ = selection_;
            missionSelection_ = 0;
            screen_ = FrontendScreen::CampaignMissions;
            selection_ = 0;
        }
        return FrontendAction::None;
    }

    if (screen_ == FrontendScreen::CampaignMissions) {
        const CampaignInfo *campaign =
            catalog_ &&
                    campaignSelection_ <
                        catalog_->campaigns().size()
                ? &catalog_->campaigns()[campaignSelection_]
                : nullptr;
        const size_t count =
            campaign ? campaign->missions.size() : 0;
        size_t touched = count;
        if (input.pointerTap &&
            input.screenW > 0 &&
            input.screenH > 0 &&
            originalMissionNodeCount_ >= count) {
            const float x =
                input.pointerX * 800.0f /
                input.screenW;
            const float y =
                input.pointerY * 600.0f /
                input.screenH;
            for (size_t index = 0;
                 index < count; ++index) {
                const OriginalMissionNode &node =
                    originalMissionNodes_[index];
                const SpriteFrame *frame =
                    node.frames[0];
                if (frame &&
                    x >= node.x &&
                    x < node.x + frame->w &&
                    y >= node.y &&
                    y < node.y + frame->h) {
                    touched = index;
                    break;
                }
            }
        } else {
            const float scaleY =
                input.screenH > 0
                    ? input.screenH / 600.0f
                    : 1.0f;
            touched =
                rowFromPointer(
                    input,
                    kOriginalMissionTop * scaleY,
                    kOriginalMissionRow * scaleY,
                    count);
        }
        if (touched < count) selection_ = touched;
        if (input.menuUp) moveSelection(-1, count);
        if (input.menuDown) moveSelection(1, count);
        if (input.menuLeft && profile_) {
            profile_->difficulty =
                (profile_->difficulty + 4) % 5;
            profileChanged_ = true;
        }
        if (input.menuRight && profile_) {
            profile_->difficulty =
                (profile_->difficulty + 1) % 5;
            profileChanged_ = true;
        }
        if (input.actionTabLeft && profile_) {
            profile_->developmentAccess =
                !profile_->developmentAccess;
            profileChanged_ = true;
        }
        if (input.menuBack) {
            if (missionsFromMainMenu_) {
                missionsFromMainMenu_ = false;
                screen_ = FrontendScreen::MainMenu;
                selection_ = 1;
            } else {
                screen_ = FrontendScreen::CampaignBrowser;
                selection_ = campaignSelection_;
            }
        } else if ((input.menuActivate || touched < count) &&
                   campaign && profile_ && count) {
            if (!profile_->isUnlocked(*campaign, selection_)) {
                message_ =
                    "MISSION LOCKED - COMPLETE THE PREVIOUS MISSION";
            } else {
                missionSelection_ = selection_;
                screen_ = FrontendScreen::CampaignBriefing;
                selection_ = 0;
                message_.clear();
            }
        }
        return FrontendAction::None;
    }

    if (screen_ == FrontendScreen::CampaignBriefing &&
        campaignScene_ && campaignScene_->loaded()) {
        // The mission's opening scene (Multimedia Screen): it starts the
        // mission when it ends; X/tap skips it, O goes back.
        if (input.menuBack) {
            screen_ = FrontendScreen::CampaignMissions;
            selection_ = missionSelection_;
            return FrontendAction::None;
        }
        if (input.menuActivate || input.pointerTap || campaignScene_->finished()) {
            campaignMatch_ = true;
            screen_ = FrontendScreen::Loading;
            const CampaignMission *mission = selectedCampaignMission();
            message_ = mission ? "INITIALIZING " + mission->title + "..."
                               : "INITIALIZING CAMPAIGN...";
            return FrontendAction::StartCampaign;
        }
        return FrontendAction::None;
    }
    if (screen_ == FrontendScreen::CampaignBriefing) {
        constexpr size_t count = 2;
        size_t touched = count;
        if (input.pointerTap &&
            input.screenW > 0 && input.screenH > 0) {
            const float logicalX =
                input.pointerX * 800.0f /
                input.screenW;
            const float logicalY =
                input.pointerY * 600.0f /
                input.screenH;
            if (logicalY >= 458.0f &&
                logicalY < 500.0f) {
                if (logicalX >= 228.0f &&
                    logicalX < 415.0f)
                    touched = 0;
                else if (logicalX >= 428.0f &&
                         logicalX < 615.0f)
                    touched = 1;
            }
        }
        if (touched < count) selection_ = touched;
        if (input.menuLeft || input.menuUp)
            moveSelection(-1, count);
        if (input.menuRight || input.menuDown)
            moveSelection(1, count);
        if (input.menuBack) {
            screen_ = FrontendScreen::CampaignMissions;
            selection_ = missionSelection_;
        } else if (input.menuActivate || touched < count) {
            if (selection_ == 0) {
                campaignMatch_ = true;
                screen_ = FrontendScreen::Loading;
                const CampaignMission *mission =
                    selectedCampaignMission();
                message_ = mission
                               ? "INITIALIZING " + mission->title + "..."
                               : "INITIALIZING CAMPAIGN...";
                return FrontendAction::StartCampaign;
            }
            screen_ = FrontendScreen::CampaignMissions;
            selection_ = missionSelection_;
        }
        return FrontendAction::None;
    }

    if (screen_ == FrontendScreen::SkirmishLobby) {
        const size_t count =
            lobbyPage_ == 0 ? 12u : 16u;
        const size_t touched =
            rowFromPointer(input, kLobbyTop, kLobbyRow, count);
        if (touched < count) selection_ = touched;
        if (input.actionTabLeft ||
            input.actionTabRight) {
            lobbyPage_ = 1 - lobbyPage_;
            selection_ = 0;
        }
        if (input.menuUp) moveSelection(-1, count);
        if (input.menuDown) moveSelection(1, count);
        if (input.menuLeft) adjustLobbyValue(-1);
        if (input.menuRight) adjustLobbyValue(1);
        const bool activated =
            input.menuActivate || touched < count;
        const size_t switchRow =
            lobbyPage_ == 0 ? 9u : 13u;
        const size_t startRow =
            lobbyPage_ == 0 ? 10u : 14u;
        const size_t backRow =
            lobbyPage_ == 0 ? 11u : 15u;
        if (activated &&
            selection_ < switchRow) {
            if (lobbyPage_ == 1 &&
                selection_ == 12) {
                settings_.seed =
                    settings_.seed * 1664525u +
                    1013904223u;
                refreshLobbyPreview();
            } else {
                adjustLobbyValue(1);
            }
        }
        if (activated &&
            selection_ == switchRow) {
            lobbyPage_ = 1 - lobbyPage_;
            selection_ = 0;
        } else if (activated &&
                   selection_ == startRow) {
            std::string validationError;
            if (!validateSkirmishSettings(
                    settings_,
                    &validationError)) {
                message_ = validationError;
            } else {
                campaignMatch_ = false;
                screen_ = FrontendScreen::Loading;
                message_ = "GENERATING RANDOM MAP...";
                return FrontendAction::StartSkirmish;
            }
        } else if (input.menuBack ||
                   (activated &&
                    selection_ == backRow)) {
            screen_ = FrontendScreen::SinglePlayer;
            selection_ = 1;
        }
        return FrontendAction::None;
    }

    if (screen_ == FrontendScreen::Pause) {
        if (playtestMatch_) {
            constexpr size_t count = 7;
            const size_t touched =
                rowFromPointer(input, 132, 42, count);
            if (touched < count) selection_ = touched;
            if (input.menuUp) moveSelection(-1, count);
            if (input.menuDown) moveSelection(1, count);
            if (input.menuBack || input.pausePressed) {
                screen_ = FrontendScreen::Gameplay;
            } else if (input.menuActivate || touched < count) {
                if (selection_ == 0) {
                    screen_ = FrontendScreen::Gameplay;
                } else if (selection_ == 1) {
                    screen_ = FrontendScreen::Objectives;
                    objectivesReturnScreen_ =
                        FrontendScreen::Pause;
                    objectivesPage_ = 0;
                    selection_ = 0;
                } else if (selection_ == 2) {
                    message_ = "SAVING PLAYTEST SNAPSHOT...";
                    return FrontendAction::SaveMatch;
                } else if (selection_ == 3) {
                    screen_ = FrontendScreen::Loading;
                    message_ = "RESTARTING PLAYTEST...";
                    return FrontendAction::RestartMatch;
                } else if (selection_ == 4) {
                    optionsReturnScreen_ = FrontendScreen::Pause;
                    screen_ = FrontendScreen::Options;
                    selection_ = 0;
                } else if (selection_ == 5) {
                    beginConfirmation(
                        FrontendAction::CompleteCampaignMission,
                        "END THIS PLAYTEST?");
                } else {
                    screen_ = FrontendScreen::ScenarioEditor;
                    selection_ = 0;
                    return FrontendAction::ReturnToEditor;
                }
            }
            return FrontendAction::None;
        }
        const size_t count = campaignMatch_ ? 11 : 10;
        const float top = campaignMatch_ ? 80.0f : 100.0f;
        const float row = 39.0f;
        const size_t touched =
            rowFromPointer(input, top, row, count);
        if (touched < count) selection_ = touched;
        if (input.menuUp) moveSelection(-1, count);
        if (input.menuDown) moveSelection(1, count);
        if (input.menuBack || input.pausePressed) {
            screen_ = FrontendScreen::Gameplay;
        } else if ((input.menuActivate || touched < count) && selection_ == 2) {
            // Diplomacy (the original's menu-bar button, action 14).
            screen_ = FrontendScreen::Diplomacy;
            return FrontendAction::OpenDiplomacy;
        } else if ((input.menuActivate || touched < count) && selection_ == 3) {
            // Chat (menu-bar button, action 13).
            screen_ = FrontendScreen::Chat;
            chatSelection_ = 0;
            return FrontendAction::OpenChat;
        } else if (input.menuActivate || touched < count) {
            // Entries after Diplomacy and Chat keep their old numbering.
            const size_t pick = selection_ > 3 ? selection_ - 2 : selection_;
            if (pick == 0) {
                screen_ = FrontendScreen::Gameplay;
            } else if (pick == 1) {
                screen_ = FrontendScreen::Objectives;
                objectivesReturnScreen_ =
                    FrontendScreen::Pause;
                objectivesPage_ = 0;
                selection_ = 0;
            } else if (pick == 2) {
                message_ = "SAVING MATCH...";
                return FrontendAction::SaveMatch;
            } else if (pick == 3) {
                if (!continueAvailable_) {
                    message_ = "NO VALID SAVE IS AVAILABLE";
                } else {
                    beginConfirmation(
                        FrontendAction::LoadMatch,
                        "LOAD SAVE AND REPLACE THIS MATCH?");
                }
            } else if (pick == 4) {
                beginConfirmation(
                    FrontendAction::RestartMatch,
                    "RESTART THIS MATCH FROM THE BEGINNING?");
            } else if (pick == 5) {
                optionsReturnScreen_ = FrontendScreen::Pause;
                screen_ = FrontendScreen::Options;
                selection_ = 0;
            } else if (pick == 6) {
                beginConfirmation(
                    FrontendAction::CompleteCampaignMission,
                    "SURRENDER THIS MATCH?");
            } else if (campaignMatch_ && pick == 7) {
                beginConfirmation(
                    FrontendAction::ReturnToCampaignBrowser,
                    "ABANDON THIS MISSION?");
            } else {
                beginConfirmation(
                    FrontendAction::ReturnToMainMenu,
                    "ABANDON THIS MATCH?");
            }
        }
        return FrontendAction::None;
    }

    if (screen_ == FrontendScreen::Objectives) {
        constexpr size_t count = 4;
        size_t touched = count;
        if (input.pointerTap &&
            input.screenW > 0 &&
            input.screenH > 0) {
            const float logicalX =
                input.pointerX * 960.0f /
                input.screenW;
            const float logicalY =
                input.pointerY * 544.0f /
                input.screenH;
            if (logicalY >= 411.0f &&
                logicalY < 453.0f &&
                logicalX >= 259.0f &&
                logicalX < 701.0f)
                touched = std::min<size_t>(
                    3,
                    (size_t)((logicalX -
                              259.0f) /
                             111.0f));
        }
        if (touched < count)
            selection_ = touched;
        if (input.menuLeft || input.menuUp)
            moveSelection(-1, count);
        if (input.menuRight || input.menuDown)
            moveSelection(1, count);
        if ((input.menuActivate ||
             touched < count) &&
            selection_ < 3) {
            objectivesPage_ = selection_;
            if (sounds_) sounds_(50300);
        }
        if (input.menuBack || input.pausePressed ||
            ((input.menuActivate ||
              touched < count) &&
             selection_ == 3)) {
            if (sounds_) sounds_(50301);
            screen_ = objectivesReturnScreen_;
            selection_ =
                objectivesReturnScreen_ ==
                        FrontendScreen::Pause
                    ? 1
                    : 0;
        }
        return FrontendAction::None;
    }

    if (screen_ == FrontendScreen::Options) {
        constexpr size_t count = 6;
        const size_t touched =
            rowFromPointer(input, 154, 48, count);
        if (touched < count) selection_ = touched;
        if (input.menuUp) moveSelection(-1, count);
        if (input.menuDown) moveSelection(1, count);
        if (input.menuLeft) adjustOptionValue(-1);
        if (input.menuRight) adjustOptionValue(1);
        if ((input.menuActivate || touched < count) &&
            selection_ < 5)
            adjustOptionValue(1);
        if (input.menuBack ||
            ((input.menuActivate || touched < count) &&
             selection_ == 5)) {
            screen_ = optionsReturnScreen_;
            selection_ = 0;
        }
        return FrontendAction::None;
    }

    if (screen_ == FrontendScreen::DataStatus) {
        if (input.menuBack || input.menuActivate ||
            input.pointerTap) {
            screen_ = FrontendScreen::MainMenu;
            selection_ = 4;
        }
        return FrontendAction::None;
    }

    if (screen_ == FrontendScreen::Confirm) {
        constexpr size_t count = 2;
        const size_t touched =
            rowFromPointer(input, 300, kMenuRow, count);
        if (touched < count) selection_ = touched;
        if (input.menuUp || input.menuLeft)
            moveSelection(-1, count);
        if (input.menuDown || input.menuRight)
            moveSelection(1, count);
        if (input.menuBack) {
            screen_ = confirmReturnScreen_;
            selection_ = 0;
            message_.clear();
        } else if (input.menuActivate || touched < count) {
            if (selection_ == 0) {
                screen_ = confirmReturnScreen_;
                selection_ = 0;
                message_.clear();
                return FrontendAction::None;
            }
            const FrontendAction action = confirmedAction_;
            if (action == FrontendAction::ReturnToMainMenu ||
                action ==
                    FrontendAction::ReturnToCampaignBrowser) {
                screen_ =
                    action ==
                            FrontendAction::ReturnToCampaignBrowser
                        ? FrontendScreen::CampaignMissions
                        : FrontendScreen::MainMenu;
                selection_ =
                    action ==
                            FrontendAction::ReturnToCampaignBrowser
                        ? missionSelection_
                        : 0;
            } else if (action ==
                       FrontendAction::CompleteCampaignMission) {
                screen_ = FrontendScreen::Outcome;
                outcome_ = 0;
                selection_ = 0;
            } else if (action == FrontendAction::Quit) {
                return action;
            } else {
                screen_ = FrontendScreen::Loading;
                message_ =
                    action == FrontendAction::LoadMatch
                        ? "LOADING SAVED MATCH..."
                        : "RESTARTING MATCH...";
            }
            return action;
        }
        return FrontendAction::None;
    }

    if (screen_ == FrontendScreen::Diplomacy)
        return updateDiplomacy(input);
    if (screen_ == FrontendScreen::Chat)
        return updateChat(input);

    if (screen_ == FrontendScreen::Achievements) {
        // Six tabs along the bottom (Score .. Timeline) and the Back button.
        constexpr size_t tabs = 6;
        if (input.menuLeft || input.actionTabLeft)
            achievementsTab_ = (achievementsTab_ + tabs - 1) % tabs;
        if (input.menuRight || input.actionTabRight)
            achievementsTab_ = (achievementsTab_ + 1) % tabs;
        if (input.pointerTap) {
            const float x = input.pointerX * 800.0f / 960.0f;
            const float y = input.pointerY * 600.0f / 544.0f;
            for (size_t tab = 0; tab < tabs; ++tab)
                if (x >= 82.0f + 108.0f * tab && x < 182.0f + 108.0f * tab && y >= 541.0f &&
                    y < 589.0f)
                    achievementsTab_ = tab;
            if (x >= 645.0f && x < 775.0f && y >= 495.0f && y < 533.0f) {
                screen_ = achievementsReturn_;
                selection_ = 0;
            }
        }
        if (input.menuBack || input.menuActivate || input.pausePressed) {
            screen_ = achievementsReturn_;
            selection_ = 0;
        }
        return FrontendAction::None;
    }

    if (screen_ == FrontendScreen::Outcome) {
        if (playtestMatch_) {
            constexpr size_t count = 3;
            const size_t touched =
                rowFromPointer(input, 286, 45, count);
            if (touched < count) selection_ = touched;
            if (input.menuUp) moveSelection(-1, count);
            if (input.menuDown) moveSelection(1, count);
            if (input.menuActivate || touched < count) {
                if (selection_ == 0) {
                    screen_ = FrontendScreen::Loading;
                    message_ = "RESTARTING PLAYTEST...";
                    return FrontendAction::RestartMatch;
                }
                if (selection_ == 1) {
                    screen_ = FrontendScreen::ScenarioEditor;
                    selection_ = 0;
                    return FrontendAction::ReturnToEditor;
                }
                screen_ = FrontendScreen::MainMenu;
                selection_ = 0;
                playtestMatch_ = false;
                return FrontendAction::ReturnToMainMenu;
            }
            return FrontendAction::None;
        }
        // The last entry opens the Achievements screen.
        const size_t count = campaignMatch_ ? 5 : 3;
        const size_t touched =
            rowFromPointer(input, 286, 45, count);
        if (touched < count) selection_ = touched;
        if (input.menuUp) moveSelection(-1, count);
        if (input.menuDown) moveSelection(1, count);
        if ((input.menuActivate || touched < count) && selection_ == count - 1) {
            achievementsReturn_ = FrontendScreen::Outcome;
            achievementsTab_ = 0;
            screen_ = FrontendScreen::Achievements;
            return FrontendAction::None;
        }
        if (input.menuActivate || touched < count) {
            if (campaignMatch_) {
                if (selection_ == 0 && outcome_ == 1 &&
                    catalog_) {
                    const CampaignInfo &campaign =
                        catalog_->campaigns()[campaignSelection_];
                    if (missionSelection_ + 1 <
                        campaign.missions.size()) {
                        ++missionSelection_;
                        screen_ =
                            FrontendScreen::CampaignBriefing;
                        selection_ = 0;
                        return FrontendAction::ReturnToCampaignBrowser;
                    }
                    screen_ = FrontendScreen::CampaignMissions;
                    selection_ = missionSelection_;
                    return FrontendAction::ReturnToCampaignBrowser;
                }
                if (selection_ <= 1) {
                    screen_ = FrontendScreen::Loading;
                    message_ = "RESTARTING CAMPAIGN MISSION...";
                    return FrontendAction::RestartMatch;
                }
                if (selection_ == 2) {
                    screen_ = FrontendScreen::CampaignMissions;
                    selection_ = missionSelection_;
                    return FrontendAction::ReturnToCampaignBrowser;
                }
                screen_ = FrontendScreen::MainMenu;
                selection_ = 0;
                return FrontendAction::ReturnToMainMenu;
            }
            if (selection_ == 0) {
                screen_ = FrontendScreen::Loading;
                message_ = "RESTARTING MATCH...";
                return FrontendAction::RestartMatch;
            }
            screen_ = FrontendScreen::MainMenu;
            selection_ = 0;
            return FrontendAction::ReturnToMainMenu;
        }
    }
    return FrontendAction::None;
}

void Frontend::loadingFinished(
    bool success, const std::string &error) {
    if (success) {
        if (campaignMatch_) {
            screen_ = FrontendScreen::Objectives;
            objectivesReturnScreen_ =
                FrontendScreen::Gameplay;
            objectivesPage_ = 2;
            selection_ = 2;
        } else {
            screen_ = FrontendScreen::Gameplay;
            selection_ = 0;
        }
        message_.clear();
    } else {
        screen_ = playtestMatch_
                      ? FrontendScreen::ScenarioEditor
                      : campaignMatch_
                      ? FrontendScreen::CampaignBriefing
                      : FrontendScreen::SinglePlayer;
        selection_ = 0;
        message_ = error.empty()
                       ? "COULD NOT START MATCH"
                       : error;
    }
}

void Frontend::actionFinished(
    FrontendAction action, bool success,
    const std::string &error) {
    if (action == FrontendAction::SaveMatch) {
        screen_ = FrontendScreen::Pause;
        selection_ = 0;
        message_ = success
                       ? "MATCH SAVED"
                       : error.empty()
                             ? "MATCH COULD NOT BE SAVED"
                             : error;
        if (success) {
            if (!playtestMatch_) {
                continueAvailable_ = true;
                continueKind_ =
                    campaignMatch_ ? MatchSaveKind::Campaign
                                   : MatchSaveKind::Skirmish;
            }
        }
        return;
    }
    loadingFinished(success, error);
}

void Frontend::showGameplay() {
    screen_ = FrontendScreen::Gameplay;
    selection_ = 0;
}

void Frontend::showEditor() {
    screen_ = FrontendScreen::ScenarioEditor;
    selection_ = 0;
    message_.clear();
}

void Frontend::showMainMenu() {
    screen_ = FrontendScreen::MainMenu;
    selection_ = 0;
    message_.clear();
}

void Frontend::render(
    Renderer &renderer, int screenW,
    int screenH) const {
    renderer.beginFrame(screenW, screenH, 1.0f, 2, 6, 15);
    renderer.fillRect(
        0, 0, (float)screenW, (float)screenH,
        2, 6, 15, 255);
    if (originalMenuBackground_ &&
        originalMenuBackground_->tex &&
        screen_ != FrontendScreen::Loading) {
        renderer.draw(
            originalMenuBackground_->tex,
            Quad{
                0, 0, (float)screenW, (float)screenH,
                originalMenuBackground_->u,
                originalMenuBackground_->v,
                originalMenuBackground_->u +
                    originalMenuBackground_->w,
                originalMenuBackground_->v +
                    originalMenuBackground_->h});
    }
    if (screen_ == FrontendScreen::Achievements) {
        renderAchievements(renderer, screenW, screenH);
        renderer.endFrame();
        return;
    }
    const bool originalLayout =
        originalMenuBackground_ &&
        (screen_ == FrontendScreen::MainMenu ||
         screen_ == FrontendScreen::SinglePlayer ||
         screen_ == FrontendScreen::CampaignBrowser ||
         screen_ == FrontendScreen::CampaignMissions ||
         screen_ == FrontendScreen::CampaignBriefing);
    if (!originalLayout) {
        renderer.fillRect(
            0, 0, (float)screenW, 82,
            8, 24, 43, 255);
        renderer.fillRect(
            0, 80, (float)screenW, 2,
            202, 168, 74, 255);
        renderer.fillRect(
            0, 82, (float)screenW, 7,
            23, 55, 79, 255);
    }
    const float originalScaleX =
        screenW / 800.0f;
    const float originalScaleY =
        screenH / 600.0f;
    const auto drawOriginalFrame =
        [&](const SpriteFrame *frame,
            float x, float y,
            float width = -1.0f,
            float height = -1.0f) {
            if (!frame || !frame->tex) return;
            if (width < 0.0f) width = frame->w;
            if (height < 0.0f) height = frame->h;
            renderer.draw(
                frame->tex,
                Quad{
                    x * originalScaleX,
                    y * originalScaleY,
                    width * originalScaleX,
                    height * originalScaleY,
                    frame->u, frame->v,
                    frame->u + frame->w,
                    frame->v + frame->h});
        };
    const auto drawOriginalText =
        [&](const std::string &value,
            float x, float y, float scale,
            uint8_t red, uint8_t green,
            uint8_t blue) {
            drawUiText(
                renderer, {value},
                x * originalScaleX,
                y * originalScaleY,
                scale * originalScaleY,
                red, green, blue);
        };
    const auto centerOriginalText =
        [&](const std::string &value,
            float centerX, float y,
            float scale, uint8_t red,
            uint8_t green, uint8_t blue) {
            const float actualScale =
                scale * originalScaleY;
            drawUiText(
                renderer, {value},
                centerX * originalScaleX -
                    uiTextWidth(
                        value, actualScale) *
                        0.5f,
                y * originalScaleY,
                actualScale,
                red, green, blue);
        };
    const auto drawOriginalHotspots =
        [&]() {
            if (originalMenuLogo_)
                drawOriginalFrame(
                    originalMenuLogo_, 0, 0,
                    originalMenuLogo_->w,
                    originalMenuLogo_->h);
            const size_t selectedHotspot =
                screen_ ==
                        FrontendScreen::SinglePlayer
                    ? 0
                    : selection_;
            const auto &hotspots =
                mainHotspots(expandingFrontsMenu_);
            for (size_t index = 0;
                 index < hotspots.size();
                 ++index) {
                const OriginalHotspot &hotspot =
                    hotspots[index];
                size_t state = 0;
                if (screen_ ==
                        FrontendScreen::SinglePlayer &&
                    index == 0)
                    state = 2;
                else if (index == selectedHotspot)
                    state = 1;
                drawOriginalFrame(
                    originalMainHotspots_[index][state],
                    hotspot.x, hotspot.y,
                    hotspot.w, hotspot.h);
            }
        };
    const auto drawOriginalPaneEntry =
        [&](const std::string &value,
            size_t index) {
            const float y =
                kOriginalMenuTop +
                index * kOriginalMenuRow;
            renderer.fillRect(
                442.0f * originalScaleX,
                y * originalScaleY,
                322.0f * originalScaleX,
                kOriginalMenuEntryHeight *
                    originalScaleY,
                index == selection_ ? 20 : 8,
                index == selection_ ? 56 : 29,
                index == selection_ ? 74 : 48,
                225);
            renderer.fillRect(
                442.0f * originalScaleX,
                y * originalScaleY,
                322.0f * originalScaleX,
                1.0f * originalScaleY,
                index == selection_ ? 205 : 81,
                index == selection_ ? 55 : 118,
                index == selection_ ? 43 : 145,
                255);
            centerOriginalText(
                value, 603.0f, y + 9.0f,
                0.96f,
                index == selection_ ? 255 : 210,
                index == selection_ ? 239 : 220,
                index == selection_ ? 214 : 230);
        };

    const auto drawMenu =
        [&](const std::string &title,
            const std::vector<std::string> &entries,
            float top = kMenuTop,
            float row = kMenuRow) {
            centeredText(
                renderer, title, 25, 2.55f, screenW,
                235, 213, 145);
            panel(
                renderer, 160, top - 28, 640,
                row * entries.size() + 34);
            for (size_t i = 0; i < entries.size(); ++i) {
                const float y = top + i * row;
                if (i == selection_) {
                    renderer.fillRect(
                        176, y - 9, 608, row - 5,
                        30, 72, 102, 255);
                    renderer.fillRect(
                        176, y - 9, 5, row - 5,
                        236, 190, 79, 255);
                }
                centeredText(
                    renderer, entries[i], y, 1.35f,
                    screenW,
                    i == selection_ ? 255 : 196,
                    i == selection_ ? 231 : 211,
                    i == selection_ ? 159 : 225);
            }
        };

    if (screen_ == FrontendScreen::Title) {
        centeredText(
            renderer, "STAR WARS", 128, 4.0f,
            screenW, 239, 215, 135);
        centeredText(
            renderer, "GALACTIC BATTLEGROUNDS", 198, 3.0f,
            screenW, 239, 215, 135);
        centeredText(
            renderer, "CLONE CAMPAIGNS", 260, 2.0f,
            screenW, 168, 202, 228);
        renderer.fillRect(
            280, 326, 400, 2, 72, 111, 145, 255);
        centeredText(
            renderer, "PRESS X OR TAP TO CONTINUE",
            412, 1.3f, screenW, 218, 229, 236);
    } else if (screen_ == FrontendScreen::Loading) {
        centeredText(
            renderer, "LOADING", 202, 3.0f,
            screenW, 235, 213, 145);
        panel(renderer, 210, 266, 540, 72);
        centeredText(
            renderer, message_, 290, 1.1f,
            screenW, 202, 218, 231);
        renderer.fillRect(
            230, 321, 500, 4, 37, 72, 98, 255);
        renderer.fillRect(
            230, 321, 360, 4, 222, 183, 76, 255);
    } else if (screen_ == FrontendScreen::MainMenu) {
        drawOriginalHotspots();
        const std::array<const char *, 8>
            descriptions{{
                "Play campaigns, standard games, or saved games.",
                "Learn the fundamentals of Galactic Battlegrounds.",
                "Community services are not available on Vita.",
                "View original-data and implementation status.",
                "Configure audio and controls.",
                "Multiplayer is unavailable in this build.",
                "Create and play custom scenarios.",
                "Exit Galactic Battlegrounds.",
            }};
        wrappedText(
            renderer, message_.empty()
                          ? descriptions[selection_]
                          : message_,
            405 * originalScaleX,
            511 * originalScaleY,
            0.66f * originalScaleY,
            54, 3, 201, 216, 228);
    } else if (screen_ == FrontendScreen::SinglePlayer) {
        drawOriginalHotspots();
        centerOriginalText(
            text(9202, "SINGLE PLAYER"),
            589, 14, 1.15f,
            215, 226, 233);
        centerOriginalText(
            "Player", 589, 39, 0.68f,
            151, 177, 199);
        const std::array<std::string, 5>
            entries{{
                "Original Campaigns",
                "Clone Campaigns",
                "Standard Game",
                "Custom Campaign",
                "Saved Game",
            }};
        for (size_t index = 0;
             index < entries.size(); ++index)
            drawOriginalPaneEntry(
                entries[index], index);
        if (!message_.empty())
            wrappedText(
                renderer, message_,
                405 * originalScaleX,
                511 * originalScaleY,
                0.66f * originalScaleY,
                54, 3, 201, 216, 228);
    } else if (screen_ == FrontendScreen::CampaignBrowser) {
        centerOriginalText(
            customCampaigns_ ? text(9229, "Custom Campaign")
            : cloneCampaigns_ ? std::string("CLONE CAMPAIGNS")
                              : text(11242, "CAMPAIGNS"),
            405, 13, 1.3f,
            215, 226, 233);
        centerOriginalText(
            text(11241, "MAIN MENU"),
            92, 17, 0.9f,
            151, 177, 199);
        if (catalog_)
            for (size_t index = 0;
                 index < std::min(
                     catalog_->campaigns().size(),
                     originalCampaignIcons_.size());
                 ++index) {
                if (!inBrowserSet(catalog_->campaigns()[index]))
                    continue;
                if (customCampaigns_) {
                    const float y = kOriginalMissionTop + browserRow(index) * kOriginalMissionRow;
                    if (index == selection_)
                        renderer.fillRect(54 * originalScaleX, (y - 7) * originalScaleY,
                                          405 * originalScaleX, 31 * originalScaleY, 18, 38, 53, 205);
                    drawOriginalText(catalog_->campaigns()[index].title + "  (" +
                                         std::to_string(catalog_->campaigns()[index].missions.size()) +
                                         " missions)",
                                     72, y, 0.92f, index == selection_ ? 255 : 190,
                                     index == selection_ ? 226 : 210, index == selection_ ? 122 : 222);
                    continue;
                }
                size_t number = 0;
                const OriginalCampaignPlacement *found =
                    campaignPlacement(catalog_->campaigns()[index], number);
                if (!found) continue;
                const auto &placement = *found;
                drawOriginalFrame(
                    originalCampaignIcons_[index][
                        index == selection_ ? 1 : 0],
                    placement.iconX,
                    placement.iconY);
                // The titles already carry their number ("2: OOM-9").
                const std::string &title = catalog_->campaigns()[index].title;
                const size_t colon = title.find(':');
                const bool numbered = colon != std::string::npos && colon > 0 && colon < 3 &&
                                      std::all_of(title.begin(), title.begin() + (ptrdiff_t)colon,
                                                  [](char c) { return c >= '0' && c <= '9'; });
                std::string label =
                    numbered ? title : std::to_string(number) + ": " + title;
                std::vector<std::string> lines;
                size_t start = 0;
                while (start < label.size() &&
                       lines.size() < 3) {
                    size_t end = std::min(
                        label.size(),
                        start + placement.labelWidth);
                    if (end < label.size()) {
                        const size_t breakAt =
                            label.rfind(' ', end);
                        if (breakAt !=
                                std::string::npos &&
                            breakAt > start)
                            end = breakAt;
                    }
                    lines.push_back(
                        label.substr(
                            start, end - start));
                    start = end;
                    while (start < label.size() &&
                           label[start] == ' ')
                        ++start;
                }
                const float scale =
                    0.66f * originalScaleY;
                for (size_t line = 0;
                     line < lines.size(); ++line)
                    drawUiText(
                        renderer, {lines[line]},
                        placement.labelCenterX *
                                originalScaleX -
                            uiTextWidth(
                                lines[line], scale) *
                                0.5f,
                        (placement.labelY +
                         line * 17.0f) *
                            originalScaleY,
                        scale,
                        index == selection_ ? 255 : 225,
                        index == selection_ ? 236 : 225,
                        index == selection_ ? 164 : 225);
            }
        if (catalog_ &&
            selection_ < catalog_->campaigns().size()) {
            centerOriginalText(
                catalog_->campaigns()[selection_].title,
                400, 455, 1.1f,
                255, 226, 122);
            wrappedText(
                renderer,
                catalog_->campaigns()[selection_].description,
                50 * originalScaleX,
                485 * originalScaleY,
                0.66f * originalScaleY,
                105, 3,
                151, 177, 199);
        }
    } else if (screen_ == FrontendScreen::CampaignMissions) {
        const CampaignInfo *campaign =
            catalog_ &&
                    campaignSelection_ <
                        catalog_->campaigns().size()
                ? &catalog_->campaigns()[campaignSelection_]
                : nullptr;
        centerOriginalText(
            campaign ? campaign->title : "CAMPAIGN",
            400, 18, 1.3f,
            215, 226, 233);
        const size_t missionCount =
            campaign ? campaign->missions.size() : 0;
        if (campaign &&
            originalMissionNodeCount_ >= missionCount) {
            for (size_t index = 0;
                 index < missionCount; ++index) {
                const bool unlocked =
                    profile_ &&
                    profile_->isUnlocked(
                        *campaign, index);
                const bool complete =
                    profile_ &&
                    profile_->isCompleted(
                        campaign->missions[index].key);
                size_t state = 0;
                if (!unlocked)
                    state = 3;
                else if (index == selection_)
                    state = 1;
                else if (complete)
                    state = 2;
                const OriginalMissionNode &node =
                    originalMissionNodes_[index];
                const SpriteFrame *frame =
                    node.frames[state];
                if (frame)
                    drawOriginalFrame(
                        frame, node.x, node.y,
                        frame->w, frame->h);
                const std::string &title =
                    campaign->missions[index].title;
                std::vector<std::string> lines;
                const bool titleBox = node.textX >= 0.0f && node.textW > 0.0f;
                const float labelScale =
                    0.48f * originalScaleY;
                // Characters per line from the box (or button) width.
                const float boxWidth =
                    titleBox ? node.textW : frame ? (float)frame->w : 100.0f;
                const float charWidth =
                    std::max(1.0f, uiTextWidth("M", labelScale) / originalScaleX);
                const size_t lineLength =
                    std::max<size_t>(8, (size_t)(boxWidth / (charWidth * 0.8f)));
                size_t start = 0;
                while (start < title.size() &&
                       lines.size() < 3) {
                    size_t end =
                        std::min(
                            title.size(),
                            start + lineLength);
                    if (end < title.size()) {
                        const size_t breakAt =
                            title.rfind(' ', end);
                        if (breakAt !=
                                std::string::npos &&
                            breakAt > start)
                            end = breakAt;
                    }
                    lines.push_back(
                        title.substr(
                            start, end - start));
                    start = end;
                    while (start < title.size() &&
                           title[start] == ' ')
                        ++start;
                }
                const float labelCenter =
                    (titleBox ? node.textX + node.textW * 0.5f
                              : node.x + (frame ? frame->w * 0.5f : 0.0f)) *
                    originalScaleX;
                const float labelY =
                    (titleBox ? node.textY
                              : node.y + (frame ? frame->h : 0) + 2.0f) *
                    originalScaleY;
                for (size_t line = 0;
                     line < lines.size(); ++line)
                    drawUiText(
                        renderer, {lines[line]},
                        labelCenter -
                            uiTextWidth(
                                lines[line],
                                labelScale) *
                                0.5f,
                        labelY +
                            line * 9.0f *
                                originalScaleY,
                        labelScale,
                        index == selection_
                            ? 255
                            : 215,
                        index == selection_
                            ? 226
                            : 226,
                        index == selection_
                            ? 122
                            : 233);
            }
            if (selection_ < missionCount)
                wrappedText(
                    renderer,
                    campaign->missions[selection_].title,
                    600 * originalScaleX,
                    458 * originalScaleY,
                    0.64f * originalScaleY,
                    19, 2,
                    215, 226, 233);
        } else {
            for (size_t index = 0;
                 index < missionCount; ++index) {
                const bool unlocked =
                    profile_ &&
                    profile_->isUnlocked(
                        *campaign, index);
                const bool complete =
                    profile_ &&
                    profile_->isCompleted(
                        campaign->missions[index].key);
                const std::string entry =
                    std::string(
                        complete ? "[DONE] " : "") +
                    campaign->missions[index].title +
                    (unlocked ? "" : " [LOCKED]");
                const float y =
                    kOriginalMissionTop +
                    index * kOriginalMissionRow;
                if (index == selection_)
                    renderer.fillRect(
                        54 * originalScaleX,
                        (y - 7) * originalScaleY,
                        405 * originalScaleX,
                        31 * originalScaleY,
                        18, 38, 53, 205);
                drawOriginalText(
                    entry, 72, y, 0.92f,
                    index == selection_ ? 255 : 190,
                    index == selection_ ? 226 : 210,
                    index == selection_ ? 122 : 222);
            }
        }
        // Vita: difficulty (L/R) and progression mode under the selected
        // mission's title, inside the screen's info box.
        drawOriginalText(
            std::string("DIFFICULTY: ") +
                difficultyName(profile_ ? profile_->difficulty : 2) + "  (L/R)",
            600, 484, 0.5f, 151, 177, 199);
        drawOriginalText(
            profile_ && profile_->developmentAccess ? "ALL MISSIONS OPEN"
                                                    : "SEQUENTIAL PROGRESSION",
            600, 494, 0.5f, 151, 177, 199);
    } else if (screen_ == FrontendScreen::CampaignEpilogue &&
               campaignScene_ && campaignScene_->loaded()) {
        campaignScene_->render(renderer, screenW, screenH, [&](int id) {
            return strings_ ? strings_(id, std::string()) : std::string();
        });
        drawOriginalText("X  CONTINUE", 24, 6, 0.55f, 205, 214, 222);
    } else if (screen_ == FrontendScreen::CampaignBriefing &&
               campaignScene_ && campaignScene_->loaded()) {
        campaignScene_->render(renderer, screenW, screenH, [&](int id) {
            return strings_ ? strings_(id, std::string()) : std::string();
        });
        // Vita controls hint, in the top border of the scene frame.
        drawOriginalText("X  START MISSION     O  BACK", 24, 6, 0.55f, 205, 214, 222);
    } else if (screen_ ==
               FrontendScreen::CampaignBriefing) {
        const CampaignMission *mission =
            selectedCampaignMission();
        drawOriginalFrame(
            originalBriefingDialog_,
            150, 98, 499, 404);
        centerOriginalText(
            mission ? mission->title : "MISSION BRIEFING",
            399, 116, 1.12f,
            225, 231, 234);
        if (mission) {
            drawOriginalText(
                mission->faction + "   |   " +
                    std::to_string(mission->mapSize) +
                    "x" +
                    std::to_string(mission->mapSize) +
                    "   |   " +
                    difficultyName(
                        profile_
                            ? profile_->difficulty
                            : 2),
                220, 158, 0.72f,
                151, 190, 218);
            wrappedText(
                renderer,
                mission->description,
                220 * originalScaleX,
                190 * originalScaleY,
                0.68f * originalScaleY,
                70, 9,
                214, 222, 228);
            if (!mission->objectives.empty()) {
                drawOriginalText(
                    "OBJECTIVES", 220, 338,
                    0.75f, 235, 213, 145);
                wrappedText(
                    renderer,
                    mission->objectives,
                    220 * originalScaleX,
                    360 * originalScaleY,
                    0.61f * originalScaleY,
                    73, 4,
                    187, 206, 220);
            }
        }
        std::vector<std::string> entries{
            "BEGIN MISSION", "BACK TO CAMPAIGN"};
        for (size_t i = 0; i < entries.size(); ++i) {
            const float x = i ? 444.0f : 244.0f;
            if (i == selection_) {
                renderer.fillRect(
                    (x - 16) * originalScaleX,
                    458 * originalScaleY,
                    171 * originalScaleX,
                    32 * originalScaleY,
                    31, 61, 70, 225);
                renderer.fillRect(
                    (x - 16) * originalScaleX,
                    458 * originalScaleY,
                    4 * originalScaleX,
                    32 * originalScaleY,
                    219, 181, 76, 255);
            }
            drawOriginalText(
                entries[i], x, 467, 0.78f,
                i == selection_ ? 255 : 196,
                i == selection_ ? 231 : 211,
                i == selection_ ? 159 : 225);
        }
    } else if (screen_ == FrontendScreen::SkirmishLobby) {
        centeredText(
            renderer,
            lobbyPage_ == 0
                ? "SKIRMISH PLAYERS"
                : "RANDOM MAP SETTINGS",
            25, 2.35f,
            screenW, 235, 213, 145);
        panel(renderer, 48, 73, 864, 455);
        const size_t count =
            lobbyPage_ == 0 ? 12u : 16u;
        for (size_t row = 0; row < count; ++row) {
            const float y = kLobbyTop + row * kLobbyRow;
            if (row == selection_)
                renderer.fillRect(
                    64, y - 4, 536, kLobbyRow - 1,
                    30, 72, 102, 255);
            if (*lobbyLabel(lobbyPage_, row))
                drawUiText(
                    renderer,
                    {lobbyLabel(lobbyPage_, row)},
                    80, y, 0.92f,
                    196, 211, 225);
            drawUiText(
                renderer,
                {lobbyValue(
                    settings_, lobbyPage_,
                    lobbySlot_, row)},
                324.0f, y, 0.92f,
                row == selection_ ? 255 : 220,
                row == selection_ ? 231 : 226,
                row == selection_ ? 159 : 232);
        }
        if (lobbyPage_ == 0) {
            drawUiText(
                renderer,
                {"ACTIVE SLOTS"}, 638, 96,
                1.0f, 235, 213, 145);
            float y = 129.0f;
            for (int slot = 0;
                 slot < kMaxSkirmishSlots;
                 ++slot) {
                const SkirmishSlot &player =
                    settings_.slots[(size_t)slot];
                const std::string summary =
                    std::to_string(slot + 1) +
                    "  " +
                    slotTypeName(player.type) +
                    "  C" +
                    std::to_string(
                        player.color + 1) +
                    "  T" +
                    (player.team
                         ? std::to_string(
                               player.team)
                         : "-");
                drawUiText(
                    renderer, {summary},
                    638, y, 0.78f,
                    (size_t)slot == lobbySlot_
                        ? 255
                        : 175,
                    (size_t)slot == lobbySlot_
                        ? 226
                        : 196,
                    (size_t)slot == lobbySlot_
                        ? 139
                        : 210);
                y += 34.0f;
            }
            drawUiText(
                renderer,
                {"L/R CHANGE  L/R TRIGGER PAGE"},
                638, 426, 0.66f,
                143, 172, 194);
        } else {
            constexpr float previewX = 664.0f;
            constexpr float previewY = 110.0f;
            constexpr float pixel = 4.0f;
            renderer.fillRect(
                previewX - 4, previewY - 4,
                SkirmishPreview::kWidth * pixel + 8,
                SkirmishPreview::kHeight * pixel + 8,
                12, 26, 38, 255);
            for (int y = 0;
                 y < SkirmishPreview::kHeight;
                 ++y)
                for (int x = 0;
                     x < SkirmishPreview::kWidth;
                     ++x) {
                    const uint8_t terrain =
                        lobbyPreview_.terrain[
                            (size_t)y *
                                SkirmishPreview::kWidth +
                            x];
                    const uint8_t r =
                        terrain == 2
                            ? 20
                        : terrain == 1
                            ? 175
                            : 53;
                    const uint8_t g =
                        terrain == 2
                            ? 72
                        : terrain == 1
                            ? 157
                            : 112;
                    const uint8_t b =
                        terrain == 2
                            ? 135
                        : terrain == 1
                            ? 90
                            : 58;
                    renderer.fillRect(
                        previewX + x * pixel,
                        previewY + y * pixel,
                        pixel, pixel, r, g, b, 255);
                }
            for (int slot = 0;
                 slot < kMaxSkirmishSlots;
                 ++slot) {
                if (settings_.slots[(size_t)slot].type ==
                    SkirmishSlotType::Closed)
                    continue;
                const auto &start =
                    lobbyPreview_.starts[
                        (size_t)slot];
                const float x =
                    previewX +
                    start[0] / settings_.mapSize *
                        SkirmishPreview::kWidth *
                        pixel;
                const float y =
                    previewY +
                    start[1] / settings_.mapSize *
                        SkirmishPreview::kHeight *
                        pixel;
                renderer.fillRect(
                    x - 3, y - 3, 7, 7,
                    255, 220, 92, 255);
            }
            drawUiText(
                renderer,
                {"MINIMAP PREVIEW",
                 "HASH " +
                     std::to_string(
                         lobbyPreview_.hash),
                 "",
                 "VITA SAFE CAP: 160 x 160",
                 "L/R CHANGE",
                 "X ON SEED: RANDOMIZE",
                 "L/R TRIGGER: PLAYER SLOTS"},
                652, 264, 0.68f,
                163, 190, 208);
        }
    } else if (screen_ == FrontendScreen::Pause) {
        std::vector<std::string> entries =
            playtestMatch_
                ? std::vector<std::string>{
                      "RESUME TEST", "OBJECTIVES / STATUS",
                      "SAVE PLAYTEST", "RESTART PLAYTEST",
                      "OPTIONS", "END TEST",
                      "RETURN TO EDITOR"}
                : std::vector<std::string>{
            "RESUME", "OBJECTIVES / STATUS", "DIPLOMACY", "CHAT", "SAVE MATCH",
            "LOAD MATCH", "RESTART MATCH", "OPTIONS",
            "SURRENDER"};
        if (!playtestMatch_ && campaignMatch_)
            entries.push_back("RETURN TO CAMPAIGN");
        if (!playtestMatch_)
            entries.push_back("RETURN TO MAIN MENU");
        drawMenu(
            playtestMatch_
                ? "EDITOR PLAYTEST PAUSED"
                : campaignMatch_ ? "CAMPAIGN PAUSED"
                                 : "MATCH PAUSED",
            entries,
            playtestMatch_ ? 132.0f
                           : campaignMatch_ ? 80.0f
                                            : 100.0f,
            playtestMatch_ ? 42.0f : 39.0f);
    } else if (screen_ == FrontendScreen::Objectives) {
        renderObjectivesOverlay(
            renderer, screenW, screenH);
    } else if (screen_ == FrontendScreen::Options) {
        centeredText(
            renderer, text(9274, "OPTIONS"), 25, 2.5f,
            screenW, 235, 213, 145);
        panel(renderer, 170, 128, 620, 342);
        for (size_t row = 0; row < 6; ++row) {
            const float y = 154 + row * 48;
            if (row == selection_)
                renderer.fillRect(
                    188, y - 10, 584, 42,
                    30, 72, 102, 255);
            if (*optionLabel(row))
                drawUiText(
                    renderer, {optionLabel(row)}, 218, y,
                    1.3f, 201, 216, 228);
            drawUiText(
                renderer, {optionValue(userSettings_, row)},
                row < 5 ? 545.0f : 440.0f, y,
                1.3f,
                row == selection_ ? 255 : 220,
                row == selection_ ? 231 : 226,
                row == selection_ ? 159 : 232);
        }
    } else if (screen_ == FrontendScreen::DataStatus) {
        centeredText(
            renderer, "CREDITS / ORIGINAL DATA", 25, 2.4f,
            screenW, 235, 213, 145);
        panel(renderer, 110, 112, 740, 340);
        drawUiText(
            renderer,
            {"GALACTIC GENIE - NATIVE CLEAN-ROOM ENGINE",
             "Original game data is loaded from your installation.",
             "",
             "CAMPAIGN ARCHIVES: " +
                 std::to_string(dataCampaigns_),
             "VALIDATED MISSIONS: " +
                 std::to_string(dataMissions_),
             "OPTIONAL INTRO MEDIA: " +
                 std::string(optionalMedia_ ? "DETECTED"
                                            : "NOT INSTALLED"),
             "",
             "MULTIPLAYER: UNAVAILABLE",
             "SCENARIO EDITOR: NATIVE / ENABLED"},
            145, 145, 1.03f, 196, 215, 229);
        centeredText(
            renderer, "X / O: CLOSE", 486, 0.95f,
            screenW, 151, 177, 199);
    } else if (screen_ == FrontendScreen::Confirm) {
        centeredText(
            renderer, "CONFIRM", 82, 2.5f,
            screenW, 255, 178, 93);
        panel(renderer, 170, 188, 620, 210);
        centeredText(
            renderer, message_, 225, 1.15f,
            screenW, 229, 224, 213);
        const char *entries[] = {"CANCEL", "CONFIRM"};
        for (size_t i = 0; i < 2; ++i) {
            const float y = 300 + i * kMenuRow;
            if (i == selection_)
                renderer.fillRect(
                    188, y - 9, 584, 38,
                    91, 52, 38, 255);
            centeredText(
                renderer, entries[i], y, 1.35f,
                screenW,
                i == selection_ ? 255 : 205,
                i == selection_ ? 228 : 213,
                i == selection_ ? 153 : 222);
        }
    } else if (screen_ == FrontendScreen::Outcome) {
        std::vector<std::string> entries;
        if (playtestMatch_) {
            entries = {
                "RESTART PLAYTEST", "RETURN TO EDITOR",
                "RETURN TO MAIN MENU"};
        } else if (campaignMatch_) {
            entries = {
                outcome_ == 1 ? "CONTINUE TO NEXT MISSION"
                              : "RETRY MISSION",
                "REPLAY MISSION", "RETURN TO CAMPAIGN",
                "RETURN TO MAIN MENU", "ACHIEVEMENTS"};
        } else {
            entries = {
                "RESTART MATCH", "RETURN TO MAIN MENU", "ACHIEVEMENTS"};
        }
        drawMenu(
            playtestMatch_
                ? outcome_ == 1
                      ? "PLAYTEST VICTORY"
                      : "PLAYTEST ENDED"
                : outcome_ == 1 ? "VICTORY" : "DEFEAT",
            entries, 286, 45);
    }
    if (!message_.empty() &&
        screen_ != FrontendScreen::Loading &&
        screen_ != FrontendScreen::Confirm)
        centeredText(
            renderer, message_, 516, 0.85f,
            screenW, 255, 130, 91);
    renderer.endFrame();
}

namespace {
// Diplomacy dialog geometry (exe 0x45b080, dialog coordinates on the 796x533
// dlg_dip art, centred on the 800x600 screen).
constexpr float kDipX = 2.0f, kDipY = 33.0f;
constexpr std::array<float, 3> kDipStanceX{{352.0f, 422.0f, 492.0f}};
constexpr std::array<int, 3> kDipStances{{0, 1, 3}};
// Tribute columns: carbon, food, nova, ore (resource types 1, 0, 3, 2).
constexpr std::array<float, 4> kDipTributeX{{540.0f, 589.0f, 636.0f, 687.0f}};
constexpr std::array<int, 4> kDipTributeResource{{1, 0, 3, 2}};
constexpr std::array<int, 4> kDipTributeIcon{{0, 2, 3, 1}};
// Bottom controls: Allied Victory box, OK, Clear Tributes, Cancel.
constexpr std::array<float, 4> kDipBottomX{{352.0f, 42.0f, 274.0f, 506.0f}};
constexpr std::array<float, 4> kDipBottomW{{30.0f, 217.0f, 217.0f, 217.0f}};
constexpr std::array<float, 4> kDipBottomY{{383.0f, 423.0f, 423.0f, 423.0f}};
} // namespace

FrontendAction Frontend::updateChat(const InputState &input) {
    const size_t recipients = 2 + chatPlayers_.size();
    if (input.menuBack || input.pausePressed) {
        screen_ = FrontendScreen::Gameplay;
        selection_ = 0;
        return FrontendAction::None;
    }
    if (input.menuLeft || input.actionTabLeft)
        chatRecipient_ = (chatRecipient_ + recipients - 1) % recipients;
    if (input.menuRight || input.actionTabRight)
        chatRecipient_ = (chatRecipient_ + 1) % recipients;
    if (!taunts_.empty()) {
        if (input.menuUp) chatSelection_ = (chatSelection_ + taunts_.size() - 1) % taunts_.size();
        if (input.menuDown) chatSelection_ = (chatSelection_ + 1) % taunts_.size();
    }
    if (input.pointerTap && input.screenW > 0 && input.screenH > 0 && !taunts_.empty()) {
        const float y = input.pointerY * 544.0f / input.screenH;
        const size_t first = chatSelection_ >= 6 ? chatSelection_ - 6 : 0;
        if (y >= 150.0f && y < 150.0f + 13 * 24.0f) {
            chatSelection_ = std::min(taunts_.size() - 1, first + (size_t)((y - 150.0f) / 24.0f));
            screen_ = FrontendScreen::Gameplay;
            return FrontendAction::SendChat;
        }
    }
    if (input.menuActivate && !taunts_.empty()) {
        screen_ = FrontendScreen::Gameplay;
        return FrontendAction::SendChat;
    }
    return FrontendAction::None;
}

void Frontend::renderChatOverlay(Renderer &renderer, int screenW, int screenH) const {
    const float sx = screenW / 960.0f, sy = screenH / 544.0f;
    renderer.fillRect(0, 0, (float)screenW, (float)screenH, 0, 0, 0, 92);
    drawModernPanel(renderer, 250 * sx, 60 * sy, 460 * sx, 440 * sy);
    centeredText(renderer, text(4113, "Chat"), 76, 1.6f, screenW, 235, 213, 145);
    std::string to = chatRecipient_ == 0   ? std::string("ALL")
                     : chatRecipient_ == 1 ? std::string("ALLIES")
                                           : chatPlayers_[chatRecipient_ - 2].second;
    centeredText(renderer, "< TO: " + to + " >", 116, 1.1f, screenW, 205, 228, 255);
    const size_t first = chatSelection_ >= 6 ? chatSelection_ - 6 : 0;
    for (size_t row = 0; row < 13 && first + row < taunts_.size(); ++row) {
        const size_t index = first + row;
        const float y = 150.0f + row * 24.0f;
        if (index == chatSelection_)
            renderer.fillRect(268 * sx, (y - 3) * sy, 424 * sx, 22 * sy, 30, 72, 102, 255);
        drawUiText(renderer,
                   {std::to_string(taunts_[index].first) + "  " + taunts_[index].second},
                   280 * sx, y * sy, 1.05f * sy, index == chatSelection_ ? 255 : 205,
                   index == chatSelection_ ? 228 : 213, index == chatSelection_ ? 153 : 222);
    }
    centeredText(renderer, "X: SEND TAUNT   L/R: RECIPIENT   O: BACK", 474, 0.9f, screenW, 205,
                 213, 222);
}

int Frontend::diplomacyRemaining(size_t column) const {
    const int resource = kDipTributeResource[column];
    float pending = 0.0f;
    for (const auto &row : diplomacyPending_) pending += row[column];
    return (int)(diplomacy_.stock[(size_t)resource] - pending * (1.0f + diplomacy_.fee));
}

FrontendAction Frontend::updateDiplomacy(const InputState &input) {
    const size_t rows = diplomacy_.rows.size();
    const size_t bottom = rows; // the row of bottom controls
    const auto tributeAllowed = [&](size_t row) {
        const DiplomacyRow &entry = diplomacy_.rows[row];
        return diplomacy_.hasMarket && !entry.local && !entry.defeated &&
               (!diplomacy_.lockTeams || entry.ourStance == 0);
    };
    const auto selectable = [&](size_t row) {
        return row == bottom || (row < rows && !diplomacy_.rows[row].local);
    };
    const auto close = [&]() {
        screen_ = FrontendScreen::Gameplay;
        selection_ = 0;
    };
    const auto apply = [&]() {
        diplomacyResult_ = {};
        for (size_t row = 0; row < rows && row < diplomacyStances_.size(); ++row) {
            const DiplomacyRow &entry = diplomacy_.rows[row];
            if (!entry.local && diplomacyStances_[row] != entry.ourStance)
                diplomacyResult_.stances.push_back({entry.player, diplomacyStances_[row]});
            std::array<int, 4> amounts{};
            bool any = false;
            for (size_t column = 0; column < 4; ++column) {
                amounts[(size_t)kDipTributeResource[column]] = diplomacyPending_[row][column];
                any = any || diplomacyPending_[row][column] > 0;
            }
            if (any) diplomacyResult_.tributes.push_back({entry.player, amounts});
        }
        diplomacyResult_.alliedVictory = diplomacyAlliedVictory_;
        close();
        return FrontendAction::ApplyDiplomacy;
    };
    const auto activate = [&](size_t row, size_t column) -> FrontendAction {
        if (row == bottom) {
            if (column == 0) diplomacyAlliedVictory_ = !diplomacyAlliedVictory_;
            else if (column == 1) return apply();
            else if (column == 2) diplomacyPending_ = {};
            else close();
            return FrontendAction::None;
        }
        if (row >= rows || diplomacy_.rows[row].local) return FrontendAction::None;
        if (column < 3) {
            if (!diplomacy_.lockTeams) diplomacyStances_[row] = kDipStances[column];
        } else if (tributeAllowed(row) && row < diplomacyPending_.size()) {
            // 100 a click; less when the stock (after the fee) is short.
            const size_t slot = column - 3;
            int amount = 100;
            const int remaining = diplomacyRemaining(slot);
            if ((float)remaining < amount * (1.0f + diplomacy_.fee))
                amount = (int)(remaining / (1.0f + diplomacy_.fee));
            if (amount > 0) diplomacyPending_[row][slot] += amount;
        }
        return FrontendAction::None;
    };
    if (input.menuBack) {
        close();
        return FrontendAction::None;
    }
    if (input.pausePressed) return apply();
    // Touch: map the point to a cell.
    if (input.pointerTap && input.screenW > 0 && input.screenH > 0) {
        const float x = input.pointerX * 800.0f / input.screenW - kDipX;
        const float y = input.pointerY * 600.0f / input.screenH - kDipY;
        for (size_t column = 0; column < 4; ++column)
            if (x >= kDipBottomX[column] && x < kDipBottomX[column] + kDipBottomW[column] &&
                y >= kDipBottomY[column] && y < kDipBottomY[column] + 30.0f) {
                diplomacyRow_ = bottom;
                diplomacyColumn_ = column;
                return activate(bottom, column);
            }
        if (y >= 136.0f) {
            const size_t row = (size_t)((y - 136.0f) / 30.0f);
            if (row < rows && selectable(row)) {
                size_t column = SIZE_MAX;
                for (size_t c = 0; c < 3; ++c)
                    if (x >= kDipStanceX[c] && x < kDipStanceX[c] + 30.0f) column = c;
                for (size_t c = 0; c < 4; ++c)
                    if (x >= kDipTributeX[c] && x < kDipTributeX[c] + 40.0f) column = 3 + c;
                if (column != SIZE_MAX) {
                    diplomacyRow_ = row;
                    diplomacyColumn_ = column;
                    return activate(row, column);
                }
            }
        }
    }
    // D-pad: rows of players, then the bottom controls.
    if (input.menuUp || input.menuDown) {
        const int direction = input.menuDown ? 1 : -1;
        size_t row = diplomacyRow_;
        for (size_t tries = 0; tries <= rows; ++tries) {
            row = (size_t)(((int)row + direction + (int)rows + 1) % ((int)rows + 1));
            if (selectable(row)) break;
        }
        diplomacyRow_ = row;
        const size_t columns = row == bottom ? 4 : 7;
        diplomacyColumn_ = std::min(diplomacyColumn_, columns - 1);
    }
    if (input.menuLeft || input.menuRight) {
        const size_t columns = diplomacyRow_ == bottom ? 4 : 7;
        diplomacyColumn_ = (diplomacyColumn_ + columns + (input.menuRight ? 1 : columns - 1)) % columns;
    }
    if (input.menuActivate) return activate(diplomacyRow_, diplomacyColumn_);
    return FrontendAction::None;
}

void Frontend::renderDiplomacyOverlay(Renderer &renderer, int screenW, int screenH) const {
    const float sx = screenW / 800.0f, sy = screenH / 600.0f;
    const auto box = [&](float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b,
                         uint8_t a) {
        renderer.fillRect((kDipX + x) * sx, (kDipY + y) * sy, w * sx, h * sy, r, g, b, a);
    };
    const auto outline = [&](float x, float y, float w, float h, uint8_t r, uint8_t g,
                             uint8_t b) {
        box(x, y, w, 2, r, g, b, 255);
        box(x, y + h - 2, w, 2, r, g, b, 255);
        box(x, y, 2, h, r, g, b, 255);
        box(x + w - 2, y, 2, h, r, g, b, 255);
    };
    const auto label = [&](const std::string &value, float x, float y, float w, float h,
                           float scale, bool centre, uint8_t r = 255, uint8_t g = 255,
                           uint8_t b = 255) {
        std::vector<std::string> lines;
        for (size_t start = 0;;) {
            const size_t end = value.find('\n', start);
            lines.push_back(value.substr(start, end == std::string::npos ? end : end - start));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        const float actual = scale * sy;
        const float lineHeight = 9.0f * actual * 1.15f;
        float ty = (kDipY + y) * sy + (h * sy - lineHeight * lines.size()) * 0.5f;
        for (const std::string &line : lines) {
            const float width = uiTextWidth(line, actual);
            const float tx = centre ? (kDipX + x + w * 0.5f) * sx - width * 0.5f
                                    : (kDipX + x) * sx;
            drawUiText(renderer, {line}, tx + 1.0f, ty + 1.0f, actual, 0, 0, 0);
            drawUiText(renderer, {line}, tx, ty, actual, r, g, b);
            ty += lineHeight;
        }
    };
    renderer.fillRect(0, 0, (float)screenW, (float)screenH, 0, 0, 0, 92); // shade 36%
    if (diplomacyBackground_ && diplomacyBackground_->tex)
        renderer.draw(diplomacyBackground_->tex,
                      Quad{kDipX * sx, kDipY * sy, diplomacyBackground_->w * sx,
                           diplomacyBackground_->h * sy, diplomacyBackground_->u,
                           diplomacyBackground_->v,
                           diplomacyBackground_->u + diplomacyBackground_->w,
                           diplomacyBackground_->v + diplomacyBackground_->h});
    else
        box(0, 0, 796, 470, 6, 20, 40, 235);
    label(text(9851, "Diplomacy"), 110, 31, 560, 30, 2.0f, true);
    label(text(9862, "Name"), 37, 115, 108, 20, 1.1f, false);
    label(text(509, "Tech\nLevel"), 145, 96, 40, 40, 1.0f, true);
    label(text(9852, "Civilization"), 185, 115, 102, 20, 1.1f, false);
    label(text(9865, "Their"), 285, 96, 80, 20, 1.1f, true);
    label(text(9866, "Stance"), 287, 115, 80, 20, 1.1f, true);
    label(text(9864, "Our Stance"), 332, 96, 210, 20, 1.1f, true);
    label(text(9853, "Ally"), 332, 115, 70, 20, 1.1f, true);
    label(text(9854, "Neutral"), 402, 115, 70, 20, 1.1f, true);
    label(text(9855, "Enemy"), 472, 115, 70, 20, 1.1f, true);
    {
        std::string tribute = text(9856, "Pay Tribute (cost %d%%)");
        if (const size_t at = tribute.find("%d"); at != std::string::npos)
            tribute.replace(at, 2, std::to_string((int)std::lround(diplomacy_.fee * 100.0f)));
        if (const size_t at = tribute.find("%%"); at != std::string::npos) tribute.replace(at, 2, "%");
        label(tribute, 532, 106, 190, 30, 1.1f, true);
    }
    const auto stanceName = [&](int stance) {
        return stance == 0 ? text(9853, "Ally") : stance == 3 ? text(9855, "Enemy")
                                                               : text(9854, "Neutral");
    };
    const size_t rows = diplomacy_.rows.size();
    for (size_t row = 0; row < rows; ++row) {
        const DiplomacyRow &entry = diplomacy_.rows[row];
        const float y = 136.0f + 30.0f * row;
        box(31, y + 4, 4, 22, entry.red, entry.green, entry.blue, 255);
        label(entry.name, 37, y, 108, 30, 1.15f, false, entry.defeated ? 150 : 255,
              entry.defeated ? 150 : 255, entry.defeated ? 150 : 255);
        label("TL-" + std::to_string(entry.techLevel), 145, y, 40, 30, 1.1f, true);
        label(entry.civilization, 185, y, 102, 30, 1.1f, false);
        if (!entry.local) label(stanceName(entry.theirStance), 287, y, 80, 30, 1.1f, true);
        if (entry.local) {
            // Own row: the stock left after pending tribute.
            for (size_t column = 0; column < 4; ++column)
                label(std::to_string(std::max(0, diplomacyRemaining(column))),
                      kDipTributeX[column] - 4.0f, y, 49, 30, 1.1f, true);
            continue;
        }
        for (size_t column = 0; column < 3; ++column) {
            const bool chosen = row < diplomacyStances_.size() &&
                                diplomacyStances_[row] == kDipStances[column];
            box(kDipStanceX[column] + 8, y + 8, 14, 14, 20, 30, 45, 255);
            outline(kDipStanceX[column] + 7, y + 7, 16, 16, 120, 170, 220);
            if (chosen) box(kDipStanceX[column] + 11, y + 11, 8, 8, 255, 230, 120, 255);
        }
        const bool tributeOk = diplomacy_.hasMarket && !entry.defeated &&
                               (!diplomacy_.lockTeams || entry.ourStance == 0);
        for (size_t column = 0; column < 4 && tributeOk; ++column) {
            const SpriteFrame *icon = diplomacyTributeIcons_[(size_t)kDipTributeIcon[column]];
            if (icon && icon->tex)
                renderer.draw(icon->tex, Quad{(kDipX + kDipTributeX[column] + 3) * sx,
                                              (kDipY + y) * sy, icon->w * sx, icon->h * sy,
                                              icon->u, icon->v, icon->u + icon->w,
                                              icon->v + icon->h});
            const int pending = row < diplomacyPending_.size() ? diplomacyPending_[row][column] : 0;
            if (pending > 0)
                label(std::to_string(pending), kDipTributeX[column], y + 14, 40, 16, 0.9f, true,
                      255, 230, 120);
        }
    }
    if (!diplomacy_.hasMarket)
        label(text(9863, "You need a Spaceport to pay tribute."), 552, 136, 175, 60, 1.0f, true);
    // Allied Victory, OK, Clear Tributes, Cancel.
    box(kDipBottomX[0], kDipBottomY[0], 30, 30, 20, 30, 45, 255);
    outline(kDipBottomX[0], kDipBottomY[0], 30, 30, 120, 170, 220);
    if (diplomacyAlliedVictory_) box(kDipBottomX[0] + 8, kDipBottomY[0] + 8, 14, 14, 255, 230, 120, 255);
    label(text(9857, "Allied Victory"), 388, 386, 300, 30, 1.2f, false);
    const std::array<std::string, 3> buttons{
        {text(4001, "OK"), text(9859, "Clear Tributes"), text(4002, "Cancel")}};
    for (size_t index = 1; index < 4; ++index) {
        box(kDipBottomX[index], kDipBottomY[index], kDipBottomW[index], 30, 14, 40, 70, 235);
        outline(kDipBottomX[index], kDipBottomY[index], kDipBottomW[index], 30, 90, 140, 200);
        label(buttons[index - 1], kDipBottomX[index], kDipBottomY[index], kDipBottomW[index], 30,
              1.3f, true);
    }
    // Cursor.
    if (diplomacyRow_ >= rows) {
        const size_t c = std::min<size_t>(diplomacyColumn_, 3);
        outline(kDipBottomX[c] - 3, kDipBottomY[c] - 3, kDipBottomW[c] + 6, 36, 255, 228, 153);
    } else {
        const float y = 136.0f + 30.0f * diplomacyRow_;
        const float x = diplomacyColumn_ < 3 ? kDipStanceX[diplomacyColumn_]
                                             : kDipTributeX[std::min<size_t>(diplomacyColumn_ - 3, 3)];
        const float w = diplomacyColumn_ < 3 ? 30.0f : 40.0f;
        outline(x - 2, y, w + 4, 30, 255, 228, 153);
    }
    label("X: SELECT   O: CANCEL   START: OK", 0, 470, 796, 24, 1.1f, true, 205, 213, 222);
}

// The Achievements screen on its original art (scr10B 50149, 800x600):
// player rows at y 82 + 52 * row, six stat columns between x 146 and 705, the
// total at 711-780, and the six tabs along the bottom.
void Frontend::renderAchievements(Renderer &renderer, int screenW, int screenH) const {
    const float sx = screenW / 800.0f;
    const float sy = screenH / 600.0f;
    const auto drawFrame = [&](const SpriteFrame *frame, float x, float y) {
        if (!frame || !frame->tex) return;
        renderer.draw(frame->tex, Quad{x * sx, y * sy, frame->w * sx, frame->h * sy, frame->u,
                                       frame->v, frame->u + frame->w, frame->v + frame->h});
    };
    const auto splitLines = [](const std::string &value) {
        std::vector<std::string> lines;
        size_t start = 0;
        while (true) {
            const size_t end = value.find('\n', start);
            lines.push_back(value.substr(start, end == std::string::npos ? end : end - start));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        return lines;
    };
    // Centred (possibly two-line) text in an 800x600 box.
    const auto centred = [&](const std::string &value, float left, float top, float width,
                             float height, float scale, uint8_t red, uint8_t green,
                             uint8_t blue) {
        const std::vector<std::string> lines = splitLines(value);
        const float actual = scale * sy;
        const float lineHeight = 9.0f * actual * 1.15f;
        float y = top * sy + (height * sy - lineHeight * lines.size()) * 0.5f;
        for (const std::string &line : lines) {
            const float w = uiTextWidth(line, actual);
            // White text with a black shadow (text_color1/2 of screen 50061).
            drawUiText(renderer, {line}, (left + width * 0.5f) * sx - w * 0.5f + 1.0f, y + 1.0f,
                       actual, 0, 0, 0);
            drawUiText(renderer, {line}, (left + width * 0.5f) * sx - w * 0.5f, y, actual, red,
                       green, blue);
            y += lineHeight;
        }
    };
    if (originalMenuBackground_ && originalMenuBackground_->tex)
        renderer.draw(originalMenuBackground_->tex,
                      Quad{0, 0, (float)screenW, (float)screenH, originalMenuBackground_->u,
                           originalMenuBackground_->v,
                           originalMenuBackground_->u + originalMenuBackground_->w,
                           originalMenuBackground_->v + originalMenuBackground_->h});

    static constexpr std::array<float, 7> columnEdges{{146, 235, 330, 425, 520, 615, 705}};
    static constexpr std::array<std::array<int, 6>, 5> headerIds{{
        {{9886, 9887, 9888, 9889, -1, -1}},
        {{9896, 9897, 9898, 9899, 9900, 9901}},
        {{9906, 9907, 9908, 9909, 9910, 9911}},
        {{9916, 9917, 9918, 9919, 9920, 9921}},
        {{9926, 9927, 9928, 9929, 9930, 9931}},
    }};
    static constexpr std::array<const char *, 30> headerFallbacks{{
        "Military", "Economy", "Technology", "Society", "", "",
        "Units\nKilled", "Units\nLost", "Buildings\nRazed", "Buildings\nLost", "Units\nTurned", "Largest\nArmy",
        "Food\nCollected", "Carbon\nCollected", "Ore\nCollected", "Nova\nCollected", "Trade\nProfit", "Tribute\nSent / Rcvd",
        "Tech\nLevel 2", "Tech\nLevel 3", "Tech\nLevel 4", "% Map\nExplored", "Research\nCount", "Research\nPercent",
        "Total\nMonuments", "Total\nFortresses", "Holocrons\nCaptured", "Holocron\nNova", "Worker\nHigh", "Survival\nto Finish",
    }};
    static constexpr std::array<int, 6> tabIds{{9938, 9939, 9940, 9941, 9942, 9943}};
    static constexpr std::array<const char *, 6> tabFallbacks{{
        "Score", "Military", "Economy", "Technology", "Society", "Timeline"}};

    centred(text(9936, "Achievements"), 107, 10, 176, 23, 1.45f, 255, 255, 255);
    {
        const int seconds = (int)achievements_.elapsedSeconds;
        char clock[16];
        snprintf(clock, sizeof clock, "%02d:%02d:%02d", seconds / 3600, seconds / 60 % 60,
                 seconds % 60);
        centred(clock, 665, 14, 90, 18, 1.2f, 255, 255, 255);
    }
    const size_t tab = std::min<size_t>(achievementsTab_, 5);
    // Player names on their colour banners.
    for (size_t row = 0; row < achievements_.players.size() && row < 8; ++row) {
        const AchievementsPlayer &player = achievements_.players[row];
        const float top = 82.0f + 52.0f * row;
        const SpriteFrame *banner =
            achievementBannerFrames_[(size_t)std::clamp(player.color, 0, 7)];
        if (banner)
            drawFrame(banner, 4, top);
        else
            renderer.fillRect(4 * sx, top * sy, 138 * sx, 33 * sy, player.red, player.green,
                              player.blue, 255);
        centred(player.name, 14, top, 128, 33, 1.3f, 255, 255, 255);
    }
    if (tab < 5) {
        for (size_t column = 0; column < 6; ++column) {
            const int id = headerIds[tab][column];
            if (id < 0) continue;
            centred(text(id, headerFallbacks[tab * 6 + column]), columnEdges[column], 45,
                    columnEdges[column + 1] - columnEdges[column], 27, 1.05f, 255, 255, 255);
        }
        centred(tab == 0 ? text(9890, "Total Score") : text(9938, "Score"), 711, 45, 69, 27,
                1.05f, 255, 255, 255);
        for (size_t row = 0; row < achievements_.players.size() && row < 8; ++row) {
            const AchievementsPlayer &player = achievements_.players[row];
            const float top = 82.0f + 52.0f * row;
            for (size_t column = 0; column < 6; ++column)
                centred(player.cells[tab][column], columnEdges[column], top,
                        columnEdges[column + 1] - columnEdges[column], 33, 1.35f, 255, 255,
                        255);
            centred(std::to_string(player.total), 711, top, 69, 33, 1.35f, 255, 255, 255);
        }
    } else {
        // Timeline: each minute's share of the total score, stacked by player.
        const float left = 146.0f, right = 780.0f, top = 82.0f, bottom = 479.0f;
        renderer.fillRect(left * sx, top * sy, (right - left) * sx, (bottom - top) * sy, 8, 10,
                          8, 255);
        size_t samples = 0;
        for (const AchievementsPlayer &player : achievements_.players)
            samples = std::max(samples, player.timeline.size());
        if (samples) {
            const float width = (right - left) / samples;
            for (size_t sample = 0; sample < samples; ++sample) {
                int sum = 0;
                for (const AchievementsPlayer &player : achievements_.players)
                    if (sample < player.timeline.size())
                        sum += std::max(0, player.timeline[sample]);
                if (sum <= 0) continue;
                float y = top;
                for (const AchievementsPlayer &player : achievements_.players) {
                    if (sample >= player.timeline.size()) continue;
                    const float h =
                        (bottom - top) * std::max(0, player.timeline[sample]) / (float)sum;
                    renderer.fillRect((left + width * sample) * sx, y * sy,
                                      std::max(1.0f, width * sx), h * sy, player.red,
                                      player.green, player.blue, 255);
                    y += h;
                }
            }
        }
        centred(text(9943, "Timeline"), 146, 45, 634, 27, 1.2f, 255, 255, 255);
    }
    // Tabs and the Back button.
    for (size_t index = 0; index < 6; ++index) {
        const float x = 82.0f + 108.0f * index;
        const SpriteFrame *frame = achievementTabFrames_[index * 2 + (index == tab ? 0 : 1)];
        drawFrame(frame, x, 541);
        const bool selected = index == tab;
        centred(text(tabIds[index], tabFallbacks[index]), x, 541, 99, 48, 1.2f, 255,
                selected ? 228 : 255, selected ? 153 : 255);
    }
    centred(achievementsReturn_ == FrontendScreen::Outcome ? std::string("Back")
                                                         : text(9881, "Return to Game"),
            645, 495, 130, 38, 1.3f, 255, 255, 255);
}

void Frontend::renderObjectivesOverlay(
    Renderer &renderer, int screenW,
    int screenH) const {
    const float scaleX = screenW / 960.0f;
    const float scaleY = screenH / 544.0f;
    const float x = 250.0f * scaleX;
    const float y = 76.0f * scaleY;
    const float width = 460.0f * scaleX;
    const float height = 390.0f * scaleY;
    drawModernPanel(
        renderer, x, y, width, height);

    static constexpr std::array<
        const char *, 3>
        titles{{
            "OBJECTIVES", "INTELLIGENCE",
            "RECONNAISSANCE",
        }};
    const CampaignMission *mission =
        selectedCampaignMission();
    std::string body;
    if (playtestMatch_) {
        body = playtestObjectives_.empty()
                   ? "Complete the active match victory conditions."
                   : playtestObjectives_;
    } else if (mission) {
        if (objectivesPage_ == 0)
            body = mission->objectives;
        else if (objectivesPage_ == 1)
            body = mission->intelligence;
        else
            body = mission->reconnaissance;
    }
    if (body.empty())
        body = objectivesPage_ == 0
                   ? "Complete the active match victory conditions."
                   : "No additional mission information is available.";

    const float titleScale = 1.35f * scaleY;
    drawUiText(
        renderer, {titles[std::min<size_t>(
                      objectivesPage_, 2)]},
        x + (width -
             uiTextWidth(
                 titles[std::min<size_t>(
                     objectivesPage_, 2)],
                 titleScale)) *
                0.5f,
        y + 20.0f * scaleY,
        titleScale, 235, 239, 241);
    wrappedText(
        renderer, body,
        x + 34.0f * scaleX,
        y + 76.0f * scaleY,
        0.78f * scaleY,
        57, 13, 221, 228, 233);

    static constexpr std::array<
        const char *, 4>
        tabs{{
            "Objectives", "Intelligence",
            "Reconnaissance", "OK",
        }};
    for (size_t index = 0;
         index < tabs.size(); ++index) {
        const float tabX =
            (259.0f + index * 111.0f) *
            scaleX;
        const float tabY = 411.0f * scaleY;
        renderer.fillRect(
            tabX, tabY,
            104.0f * scaleX,
            34.0f * scaleY,
            index == selection_ ? 27 : 10,
            index == selection_ ? 68 : 39,
            index == selection_ ? 91 : 59,
            245);
        renderer.fillRect(
            tabX, tabY,
            104.0f * scaleX,
            2.0f * scaleY,
            index == selection_ ? 208 : 74,
            index == selection_ ? 185 : 137,
            index == selection_ ? 86 : 168,
            255);
        const float textScale =
            (index == 2 ? 0.58f : 0.68f) *
            scaleY;
        drawUiText(
            renderer, {tabs[index]},
            tabX +
                (104.0f * scaleX -
                 uiTextWidth(
                     tabs[index],
                     textScale)) *
                    0.5f,
            tabY + 10.0f * scaleY,
            textScale,
            index == selection_ ? 255 : 210,
            index == selection_ ? 239 : 220,
            index == selection_ ? 190 : 230);
    }
}

} // namespace swgb
