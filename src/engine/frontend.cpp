// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend.h"
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
        constexpr size_t count = 6;
        const size_t touched =
            rowFromPointer(input, kMenuTop, kMenuRow, count);
        if (touched < count) selection_ = touched;
        if (input.menuUp) moveSelection(-1, count);
        if (input.menuDown) moveSelection(1, count);
        if (input.menuActivate || touched < count) {
            if (selection_ == 0) {
                screen_ = FrontendScreen::SinglePlayer;
                selection_ = 0;
            } else if (selection_ == 1) {
                message_ = "MULTIPLAYER IS UNAVAILABLE IN THIS BUILD";
            } else if (selection_ == 2) {
                screen_ = FrontendScreen::ScenarioEditor;
                selection_ = 0;
                message_.clear();
                return FrontendAction::OpenScenarioEditor;
            } else if (selection_ == 3) {
                optionsReturnScreen_ = FrontendScreen::MainMenu;
                screen_ = FrontendScreen::Options;
                selection_ = 0;
            } else if (selection_ == 4) {
                screen_ = FrontendScreen::DataStatus;
                selection_ = 0;
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
        constexpr size_t count = 4;
        const size_t touched =
            rowFromPointer(input, kMenuTop, kMenuRow, count);
        if (touched < count) selection_ = touched;
        if (input.menuUp) moveSelection(-1, count);
        if (input.menuDown) moveSelection(1, count);
        if (input.menuBack) {
            screen_ = FrontendScreen::MainMenu;
            selection_ = 0;
        } else if (input.menuActivate || touched < count) {
            if (selection_ == 0) {
                if (!catalog_ ||
                    catalog_->campaigns().empty()) {
                    message_ =
                        "NO VALID ORIGINAL CAMPAIGNS WERE DISCOVERED";
                } else {
                    screen_ = FrontendScreen::CampaignBrowser;
                    selection_ = campaignSelection_;
                }
            } else if (selection_ == 1) {
                screen_ = FrontendScreen::SkirmishLobby;
                selection_ = 0;
                lobbyPage_ = 0;
                refreshLobbyPreview();
            } else if (selection_ == 2) {
                if (!continueAvailable_) {
                    message_ = "NO VALID SAVE IS AVAILABLE";
                } else {
                    screen_ = FrontendScreen::Loading;
                    message_ =
                        continueKind_ == MatchSaveKind::Campaign
                            ? "LOADING CAMPAIGN SAVE..."
                            : "LOADING SKIRMISH SAVE...";
                    return FrontendAction::LoadMatch;
                }
            } else {
                screen_ = FrontendScreen::MainMenu;
                selection_ = 0;
            }
        }
        return FrontendAction::None;
    }

    if (screen_ == FrontendScreen::CampaignBrowser) {
        const size_t count =
            catalog_ ? catalog_->campaigns().size() : 0;
        const size_t touched =
            rowFromPointer(input, 132, 50, count);
        if (touched < count) selection_ = touched;
        if (input.menuUp) moveSelection(-1, count);
        if (input.menuDown) moveSelection(1, count);
        if (input.menuBack) {
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
        const size_t touched =
            rowFromPointer(input, 120, 43, count);
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

    if (screen_ == FrontendScreen::CampaignBriefing) {
        constexpr size_t count = 2;
        const size_t touched =
            rowFromPointer(input, 438, 44, count);
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
        if (input.menuBack || input.menuActivate ||
            input.pausePressed) {
            screen_ = FrontendScreen::Pause;
            selection_ = 1;
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
        screen_ = FrontendScreen::Gameplay;
        selection_ = 0;
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
    renderer.fillRect(
        0, 0, (float)screenW, 82,
        8, 24, 43, 255);
    renderer.fillRect(
        0, 80, (float)screenW, 2,
        202, 168, 74, 255);
    renderer.fillRect(
        0, 82, (float)screenW, 7,
        23, 55, 79, 255);

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
        drawMenu(
            text(9201, "GALACTIC BATTLEGROUNDS"),
            {text(9202, "SINGLE PLAYER"),
             text(9203, "MULTIPLAYER") + " - UNAVAILABLE",
             text(9206, "SCENARIO EDITOR"),
             text(9274, "OPTIONS"),
             text(9209, "CREDITS / DATA STATUS"),
             text(9207, "EXIT")});
    } else if (screen_ == FrontendScreen::SinglePlayer) {
        drawMenu(
            text(9202, "SINGLE PLAYER"),
            {text(11242, "CAMPAIGNS"),
             text(9226, "SKIRMISH"),
             text(9276, "LOAD / CONTINUE") +
                 (continueAvailable_ ? "" : " - NONE"),
             text(11241, "MAIN MENU")});
    } else if (screen_ == FrontendScreen::CampaignBrowser) {
        std::vector<std::string> entries;
        if (catalog_)
            for (const CampaignInfo &campaign :
                 catalog_->campaigns())
                entries.push_back(campaign.title);
        drawMenu(
            text(11242, "CAMPAIGNS"), entries, 132, 50);
        if (catalog_ && selection_ < catalog_->campaigns().size())
            wrappedText(
                renderer,
                catalog_->campaigns()[selection_].description,
                185, 465, 0.72f, 100, 3,
                151, 177, 199);
    } else if (screen_ == FrontendScreen::CampaignMissions) {
        const CampaignInfo *campaign =
            catalog_ &&
                    campaignSelection_ <
                        catalog_->campaigns().size()
                ? &catalog_->campaigns()[campaignSelection_]
                : nullptr;
        std::vector<std::string> entries;
        if (campaign)
            for (size_t i = 0; i < campaign->missions.size(); ++i) {
                const bool unlocked =
                    profile_ &&
                    profile_->isUnlocked(*campaign, i);
                const bool complete =
                    profile_ &&
                    profile_->isCompleted(
                        campaign->missions[i].key);
                entries.push_back(
                    std::string(complete ? "[DONE] " : "") +
                    campaign->missions[i].title +
                    (unlocked ? "" : " [LOCKED]"));
            }
        drawMenu(
            campaign ? campaign->title : "CAMPAIGN",
            entries, 120, 43);
        centeredText(
            renderer,
            std::string("DIFFICULTY: ") +
                difficultyName(
                    profile_ ? profile_->difficulty : 2) +
                "   L/R CHANGE   L TRIGGER: " +
                (profile_ && profile_->developmentAccess
                     ? "DEVELOPMENT ACCESS ON"
                     : "SEQUENTIAL PROGRESSION"),
            493, 0.85f, screenW, 151, 177, 199);
    } else if (screen_ ==
               FrontendScreen::CampaignBriefing) {
        const CampaignMission *mission =
            selectedCampaignMission();
        centeredText(
            renderer,
            mission ? mission->title : "MISSION BRIEFING",
            25, 2.2f, screenW, 235, 213, 145);
        panel(renderer, 70, 101, 820, 316);
        if (mission) {
            drawUiText(
                renderer,
                {mission->faction + "   |   " +
                 std::to_string(mission->mapSize) + "x" +
                 std::to_string(mission->mapSize) +
                 "   |   " +
                 difficultyName(
                     profile_ ? profile_->difficulty : 2)},
                92, 122, 1.0f, 151, 190, 218);
            wrappedText(
                renderer, mission->description,
                92, 156, 0.88f, 98, 10,
                214, 222, 228);
            if (!mission->objectives.empty()) {
                drawUiText(
                    renderer, {"OBJECTIVES"}, 92, 330,
                    1.0f, 235, 213, 145);
                wrappedText(
                    renderer, mission->objectives,
                    92, 356, 0.78f, 105, 3,
                    187, 206, 220);
            }
        }
        std::vector<std::string> entries{
            "BEGIN MISSION", "BACK TO CAMPAIGN"};
        for (size_t i = 0; i < entries.size(); ++i) {
            const float y = 438 + i * 44;
            if (i == selection_)
                renderer.fillRect(
                    280, y - 8, 400, 36,
                    30, 72, 102, 255);
            centeredText(
                renderer, entries[i], y, 1.25f,
                screenW,
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
        centeredText(
            renderer, "OBJECTIVES", 25, 2.5f,
            screenW, 235, 213, 145);
        panel(renderer, 90, 112, 780, 330);
        const CampaignMission *mission =
            selectedCampaignMission();
        wrappedText(
            renderer,
            playtestMatch_ &&
                    !playtestObjectives_.empty()
                ? playtestObjectives_
                : mission && !mission->objectives.empty()
                ? mission->objectives
                : "Complete the active match victory conditions.",
            115, 145, 1.0f, 88, 12,
            210, 222, 231);
        centeredText(
            renderer, "X / O / START: RETURN", 484,
            0.95f, screenW, 151, 177, 199);
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

} // namespace swgb
