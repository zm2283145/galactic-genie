// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "game.h"
#include "skirmish.h"

#include <cstddef>
#include <string>

namespace swgb {

enum class FrontendScreen : uint8_t {
    Title,
    MainMenu,
    SkirmishLobby,
    Loading,
    Gameplay,
    Pause,
    Outcome,
};

enum class FrontendAction : uint8_t {
    None,
    StartSkirmish,
    StartCampaign,
    RestartMatch,
    ReturnToMainMenu,
    Quit,
};

class Frontend {
public:
    FrontendAction update(
        const InputState &input, int matchOutcome);
    void render(
        Renderer &renderer, int screenW,
        int screenH) const;
    void loadingFinished(
        bool success,
        const std::string &error = std::string());
    void showGameplay();

    FrontendScreen screen() const { return screen_; }
    const SkirmishSettings &settings() const {
        return settings_;
    }
    SkirmishSettings &settingsForTesting() {
        return settings_;
    }
    size_t selectionForTesting() const {
        return selection_;
    }
    const std::string &message() const {
        return message_;
    }

private:
    void moveSelection(int direction, size_t count);
    void adjustLobbyValue(int direction);
    size_t rowFromPointer(
        const InputState &input, float top,
        float rowHeight, size_t count) const;

    FrontendScreen screen_ = FrontendScreen::Title;
    SkirmishSettings settings_;
    size_t selection_ = 0;
    int outcome_ = -1;
    std::string message_;
};

} // namespace swgb
