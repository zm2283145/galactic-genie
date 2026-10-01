// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend.h"
#include "ui_text.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

namespace swgb {
namespace {

constexpr float kMenuTop = 188.0f;
constexpr float kMenuRowHeight = 48.0f;
constexpr float kLobbyTop = 90.0f;
constexpr float kLobbyRowHeight = 33.0f;

const char *mainMenuLabel(size_t index) {
    static constexpr std::array<const char *, 5> labels{{
        "SKIRMISH",
        "CLONE CAMPAIGNS - BREAKING BREAD",
        "MULTIPLAYER - NOT IMPLEMENTED",
        "OPTIONS - NOT IMPLEMENTED",
        "EXIT",
    }};
    return index < labels.size() ? labels[index] : "";
}

std::string lobbyValue(
    const SkirmishSettings &settings, size_t row) {
    switch (row) {
    case 0:
        return civilizationName(
            settings.playerCivilization);
    case 1:
        return civilizationName(
            settings.computerCivilization);
    case 2:
        return difficultyName(settings.difficulty);
    case 3:
        return personalityName(settings.personality);
    case 4:
        return settings.allied
                   ? "Same Team / Allied"
                   : "Separate Teams / Enemy";
    case 5:
        return mapStyleName(settings.mapStyle);
    case 6:
        return std::to_string(settings.mapSize) +
               " x " +
               std::to_string(settings.mapSize);
    case 7:
        return std::to_string(
            settings.startingResources);
    case 8:
        return std::to_string(settings.populationCap);
    case 9:
        return victoryName(settings.victory);
    case 10: {
        char text[16];
        std::snprintf(
            text, sizeof text, "0x%08X",
            settings.seed);
        return text;
    }
    case 11:
        return "START MATCH";
    case 12:
        return "BACK";
    }
    return {};
}

const char *lobbyLabel(size_t row) {
    static constexpr std::array<const char *, 13> labels{{
        "Civilization",
        "Computer Civilization",
        "AI Difficulty",
        "AI Personality",
        "Teams / Diplomacy",
        "Map",
        "Map Size",
        "Starting Resources",
        "Population Cap",
        "Victory Condition",
        "Random Map Seed",
        "",
        "",
    }};
    return row < labels.size() ? labels[row] : "";
}

void centeredText(
    Renderer &renderer, const std::string &text,
    float y, float scale,
    int screenW, uint8_t r, uint8_t g, uint8_t b) {
    drawUiText(
        renderer, {text},
        (screenW - uiTextWidth(text, scale)) * 0.5f,
        y, scale, r, g, b);
}

void drawPanel(
    Renderer &renderer, float x, float y,
    float width, float height) {
    renderer.fillRect(
        x, y, width, height, 5, 11, 22, 236);
    renderer.fillRect(
        x, y, width, 2, 187, 154, 74, 255);
    renderer.fillRect(
        x, y + height - 2, width, 2,
        187, 154, 74, 255);
}

} // namespace

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
        (size_t)((input.pointerY - top) /
                 rowHeight));
}

void Frontend::adjustLobbyValue(int direction) {
    static constexpr std::array<int, 3>
        mapSizes{{64, 96, 128}};
    static constexpr std::array<int, 3>
        resourceLevels{{200, 500, 1000}};
    static constexpr std::array<int, 5>
        populationCaps{{50, 100, 150, 200, 250}};
    const auto cycle = [direction](
                           int current,
                           const auto &values) {
        auto found = std::find(
            values.begin(), values.end(), current);
        size_t index =
            found == values.end()
                ? 0
                : (size_t)(found - values.begin());
        index =
            (index + values.size() +
             (direction < 0 ? values.size() - 1 : 1)) %
            values.size();
        return values[index];
    };
    switch (selection_) {
    case 0:
        settings_.playerCivilization =
            1 + (settings_.playerCivilization - 1 +
                 8 + (direction < 0 ? -1 : 1)) %
                    8;
        break;
    case 1:
        settings_.computerCivilization =
            1 + (settings_.computerCivilization - 1 +
                 8 + (direction < 0 ? -1 : 1)) %
                    8;
        break;
    case 2:
        settings_.difficulty =
            (settings_.difficulty + 5 +
             (direction < 0 ? -1 : 1)) %
            5;
        break;
    case 3:
        settings_.personality =
            (AiPersonality)(
                ((int)settings_.personality + 2 +
                 (direction < 0 ? -1 : 1)) %
                2);
        break;
    case 4:
        settings_.allied = !settings_.allied;
        break;
    case 5:
        settings_.mapStyle =
            (SkirmishMapStyle)(
                ((int)settings_.mapStyle + 3 +
                 (direction < 0 ? -1 : 1)) %
                3);
        if (settings_.mapStyle ==
            SkirmishMapStyle::CompactIslands)
            settings_.mapSize = 96;
        break;
    case 6:
        if (settings_.mapStyle !=
            SkirmishMapStyle::CompactIslands)
            settings_.mapSize =
                cycle(settings_.mapSize, mapSizes);
        break;
    case 7:
        settings_.startingResources =
            cycle(settings_.startingResources,
                  resourceLevels);
        break;
    case 8:
        settings_.populationCap =
            cycle(settings_.populationCap,
                  populationCaps);
        break;
    case 9:
        settings_.victory =
            settings_.victory ==
                    SkirmishVictory::Conquest
                ? SkirmishVictory::CommandCenter
                : SkirmishVictory::Conquest;
        break;
    case 10:
        settings_.seed +=
            direction < 0 ? UINT32_MAX : 1u;
        break;
    default:
        break;
    }
}

FrontendAction Frontend::update(
    const InputState &input, int matchOutcome) {
    if (screen_ == FrontendScreen::Gameplay &&
        matchOutcome >= 0) {
        outcome_ = matchOutcome;
        screen_ = FrontendScreen::Outcome;
        selection_ = 0;
    }
    if (screen_ == FrontendScreen::Title) {
        if (input.menuActivate || input.pointerTap) {
            screen_ = FrontendScreen::MainMenu;
            selection_ = 0;
            message_.clear();
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
        constexpr size_t count = 5;
        const size_t touched = rowFromPointer(
            input, kMenuTop, kMenuRowHeight, count);
        if (touched < count) selection_ = touched;
        if (input.menuUp) moveSelection(-1, count);
        if (input.menuDown) moveSelection(1, count);
        if (input.menuActivate || touched < count) {
            if (selection_ == 0) {
                screen_ = FrontendScreen::SkirmishLobby;
                selection_ = 0;
                message_.clear();
            } else if (selection_ == 1) {
                screen_ = FrontendScreen::Loading;
                message_ = "LOADING BREAKING BREAD...";
                return FrontendAction::StartCampaign;
            } else if (selection_ == 2) {
                message_ =
                    "MULTIPLAYER IS NOT IMPLEMENTED";
            } else if (selection_ == 3) {
                message_ =
                    "OPTIONS ARE NOT IMPLEMENTED";
            } else {
                return FrontendAction::Quit;
            }
        }
        return FrontendAction::None;
    }
    if (screen_ == FrontendScreen::SkirmishLobby) {
        constexpr size_t count = 13;
        const size_t touched = rowFromPointer(
            input, kLobbyTop, kLobbyRowHeight, count);
        if (touched < count) selection_ = touched;
        if (input.menuUp) moveSelection(-1, count);
        if (input.menuDown) moveSelection(1, count);
        if (input.menuLeft) adjustLobbyValue(-1);
        if (input.menuRight) adjustLobbyValue(1);
        if ((input.menuActivate || touched < count) &&
            selection_ < 11)
            adjustLobbyValue(1);
        if ((input.menuActivate || touched < count) &&
            selection_ == 11) {
            screen_ = FrontendScreen::Loading;
            message_ = "GENERATING RANDOM MAP...";
            return FrontendAction::StartSkirmish;
        }
        if (input.menuBack ||
            ((input.menuActivate || touched < count) &&
             selection_ == 12)) {
            screen_ = FrontendScreen::MainMenu;
            selection_ = 0;
        }
        return FrontendAction::None;
    }
    if (screen_ == FrontendScreen::Pause) {
        constexpr size_t count = 3;
        const size_t touched = rowFromPointer(
            input, kMenuTop, kMenuRowHeight, count);
        if (touched < count) selection_ = touched;
        if (input.menuUp) moveSelection(-1, count);
        if (input.menuDown) moveSelection(1, count);
        if (input.menuBack || input.pausePressed) {
            screen_ = FrontendScreen::Gameplay;
        } else if (input.menuActivate || touched < count) {
            if (selection_ == 0)
                screen_ = FrontendScreen::Gameplay;
            else if (selection_ == 1) {
                screen_ = FrontendScreen::Loading;
                message_ = "RESTARTING MATCH...";
                return FrontendAction::RestartMatch;
            } else {
                screen_ = FrontendScreen::MainMenu;
                selection_ = 0;
                return FrontendAction::ReturnToMainMenu;
            }
        }
        return FrontendAction::None;
    }
    if (screen_ == FrontendScreen::Outcome) {
        constexpr size_t count = 2;
        const size_t touched = rowFromPointer(
            input, 310.0f, kMenuRowHeight, count);
        if (touched < count) selection_ = touched;
        if (input.menuUp || input.menuLeft)
            moveSelection(-1, count);
        if (input.menuDown || input.menuRight)
            moveSelection(1, count);
        if (input.menuActivate || touched < count) {
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
        screen_ = FrontendScreen::MainMenu;
        selection_ = 0;
        message_ = error.empty()
                       ? "COULD NOT START MATCH"
                       : error;
    }
}

void Frontend::showGameplay() {
    screen_ = FrontendScreen::Gameplay;
    selection_ = 0;
}

void Frontend::render(
    Renderer &renderer, int screenW,
    int screenH) const {
    renderer.beginFrame(
        screenW, screenH, 1.0f, 2, 5, 14);
    renderer.fillRect(
        0, 0, (float)screenW, (float)screenH,
        2, 5, 14, 255);
    renderer.fillRect(
        0, 0, (float)screenW, 92,
        10, 23, 44, 255);
    renderer.fillRect(
        0, 90, (float)screenW, 2,
        187, 154, 74, 255);
    if (screen_ == FrontendScreen::Title) {
        centeredText(
            renderer, "STAR WARS", 150, 4.0f,
            screenW, 230, 211, 143);
        centeredText(
            renderer, "GALACTIC BATTLEGROUNDS", 215,
            3.1f, screenW, 230, 211, 143);
        centeredText(
            renderer, "CLONE CAMPAIGNS", 280, 2.2f,
            screenW, 184, 202, 224);
        centeredText(
            renderer,
            "PRESS X OR TAP TO CONTINUE",
            430, 1.45f, screenW, 220, 226, 232);
    } else if (screen_ == FrontendScreen::Loading) {
        centeredText(
            renderer, "LOADING", 225, 3.0f,
            screenW, 230, 211, 143);
        centeredText(
            renderer, message_, 292, 1.4f,
            screenW, 210, 220, 232);
    } else if (screen_ ==
               FrontendScreen::SkirmishLobby) {
        centeredText(
            renderer, "SKIRMISH SETUP", 30, 2.6f,
            screenW, 230, 211, 143);
        drawPanel(renderer, 120, 74, 720, 454);
        for (size_t row = 0; row < 13; ++row) {
            const float y =
                kLobbyTop + row * kLobbyRowHeight;
            if (row == selection_)
                renderer.fillRect(
                    132, y - 5, 696,
                    kLobbyRowHeight - 2,
                    35, 65, 96, 255);
            const std::string value =
                lobbyValue(settings_, row);
            if (*lobbyLabel(row))
                drawUiText(
                    renderer, {lobbyLabel(row)},
                    152, y, 1.25f,
                    row == selection_ ? 255 : 205,
                    row == selection_ ? 226 : 213,
                    row == selection_ ? 154 : 220);
            drawUiText(
                renderer, {value},
                row < 11 ? 486.0f : 360.0f,
                y, row < 11 ? 1.25f : 1.45f,
                row == selection_ ? 255 : 224,
                row == selection_ ? 226 : 230,
                row == selection_ ? 154 : 236);
        }
        drawUiText(
            renderer,
            {"D-PAD: SELECT / CHANGE   X: ACCEPT   O: BACK"},
            245, 507, 1.0f, 160, 180, 202);
    } else {
        const bool pause =
            screen_ == FrontendScreen::Pause;
        const bool outcome =
            screen_ == FrontendScreen::Outcome;
        centeredText(
            renderer,
            pause
                ? "MATCH PAUSED"
                : outcome
                      ? (outcome_ == 1
                             ? "VICTORY"
                             : "DEFEAT")
                      : "CLONE CAMPAIGNS",
            30, 2.8f, screenW,
            outcome_ == 0 ? 255 : 230,
            outcome_ == 0 ? 92 : 211,
            outcome_ == 0 ? 92 : 143);
        drawPanel(
            renderer, 170,
            outcome ? 280.0f : 164.0f,
            620, outcome ? 132.0f : 280.0f);
        std::vector<std::string> entries;
        float top = kMenuTop;
        if (pause) {
            entries = {
                "RESUME", "RESTART MATCH",
                "RETURN TO MAIN MENU"};
        } else if (outcome) {
            entries = {
                "RESTART MATCH",
                "RETURN TO MAIN MENU"};
            top = 310.0f;
        } else {
            for (size_t i = 0; i < 5; ++i)
                entries.push_back(mainMenuLabel(i));
        }
        for (size_t row = 0; row < entries.size();
             ++row) {
            const float y =
                top + row * kMenuRowHeight;
            if (row == selection_)
                renderer.fillRect(
                    188, y - 10, 584, 42,
                    35, 65, 96, 255);
            centeredText(
                renderer, entries[row], y, 1.55f,
                screenW,
                row == selection_ ? 255 : 208,
                row == selection_ ? 226 : 218,
                row == selection_ ? 154 : 228);
        }
        if (!message_.empty())
            centeredText(
                renderer, message_, 480, 1.15f,
                screenW, 255, 126, 96);
    }
    renderer.endFrame();
}

} // namespace swgb
