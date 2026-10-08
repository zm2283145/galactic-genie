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
        screen_ = FrontendScreen::Outcome;
        selection_ = 0;
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
                message_ = "BASIC TRAINING IS NOT YET AVAILABLE";
                if (sounds_) sounds_(50303);
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
            if (selection_ <= 1) {
                if (!catalog_ ||
                    catalog_->campaigns().empty()) {
                    message_ =
                        "NO VALID ORIGINAL CAMPAIGNS WERE DISCOVERED";
                    if (sounds_) sounds_(50303);
                } else {
                    screen_ = FrontendScreen::CampaignBrowser;
                    campaignSelection_ =
                        selection_ == 0
                            ? 0
                            : std::min<size_t>(
                                  3,
                                  catalog_->campaigns().size() - 1);
                    selection_ = campaignSelection_;
                }
            } else if (selection_ == 2) {
                screen_ = FrontendScreen::SkirmishLobby;
                selection_ = 0;
                lobbyPage_ = 0;
                refreshLobbyPreview();
            } else if (selection_ == 3) {
                message_ =
                    "CUSTOM CAMPAIGNS ARE NOT YET AVAILABLE";
                if (sounds_) sounds_(50303);
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
                const size_t slot =
                    originalCampaignSlot(
                        catalog_->campaigns()[index]);
                if (slot >=
                    kOriginalCampaignPlacements.size())
                    continue;
                const auto &placement =
                    kOriginalCampaignPlacements[slot];
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
        if (input.menuUp) moveSelection(-1, count);
        if (input.menuDown) moveSelection(1, count);
        if (input.menuBack || touchedBack) {
            screen_ = FrontendScreen::SinglePlayer;
            selection_ = 0;
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
            screen_ = FrontendScreen::CampaignBrowser;
            selection_ = campaignSelection_;
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
        const size_t count = campaignMatch_ ? 9 : 8;
        const float top = campaignMatch_ ? 101.0f : 120.0f;
        const float row = 39.0f;
        const size_t touched =
            rowFromPointer(input, top, row, count);
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
                message_ = "SAVING MATCH...";
                return FrontendAction::SaveMatch;
            } else if (selection_ == 3) {
                if (!continueAvailable_) {
                    message_ = "NO VALID SAVE IS AVAILABLE";
                } else {
                    beginConfirmation(
                        FrontendAction::LoadMatch,
                        "LOAD SAVE AND REPLACE THIS MATCH?");
                }
            } else if (selection_ == 4) {
                beginConfirmation(
                    FrontendAction::RestartMatch,
                    "RESTART THIS MATCH FROM THE BEGINNING?");
            } else if (selection_ == 5) {
                optionsReturnScreen_ = FrontendScreen::Pause;
                screen_ = FrontendScreen::Options;
                selection_ = 0;
            } else if (selection_ == 6) {
                beginConfirmation(
                    FrontendAction::CompleteCampaignMission,
                    "SURRENDER THIS MATCH?");
            } else if (campaignMatch_ && selection_ == 7) {
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
        const size_t count = campaignMatch_ ? 4 : 2;
        const size_t touched =
            rowFromPointer(input, 286, 45, count);
        if (touched < count) selection_ = touched;
        if (input.menuUp) moveSelection(-1, count);
        if (input.menuDown) moveSelection(1, count);
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
                "Expansion Campaigns",
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
            text(11242, "CAMPAIGNS"),
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
                const size_t slot =
                    originalCampaignSlot(
                        catalog_->campaigns()[index]);
                if (slot >=
                    kOriginalCampaignPlacements.size())
                    continue;
                const auto &placement =
                    kOriginalCampaignPlacements[slot];
                drawOriginalFrame(
                    originalCampaignIcons_[index][
                        index == selection_ ? 1 : 0],
                    placement.iconX,
                    placement.iconY);
                std::string label =
                    std::to_string(slot + 1) +
                    ": " +
                    catalog_->campaigns()[index].title;
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
            "RESUME", "OBJECTIVES / STATUS", "SAVE MATCH",
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
                           : campaignMatch_ ? 101.0f
                                            : 120.0f,
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
                "RETURN TO MAIN MENU"};
        } else {
            entries = {
                "RESTART MATCH", "RETURN TO MAIN MENU"};
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
