// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "game.h"
#include "settings.h"
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
    Options,
    Confirm,
    Outcome,
};

enum class FrontendAction : uint8_t {
    None,
    StartSkirmish,
    StartCampaign,
    RestartMatch,
    SaveMatch,
    LoadMatch,
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
    void actionFinished(
        FrontendAction action, bool success,
        const std::string &error = std::string());
    void setContinueAvailable(bool available) {
        continueAvailable_ = available;
    }
    void reportMessage(const std::string &message) {
        message_ = message;
    }
    void setUserSettings(
        const UserSettings &settings) {
        userSettings_ = settings;
    }
    const UserSettings &userSettings() const {
        return userSettings_;
    }
    bool takeSettingsChanged() {
        const bool changed = settingsChanged_;
        settingsChanged_ = false;
        return changed;
    }

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
    void adjustOptionValue(int direction);
    void beginConfirmation(
        FrontendAction action,
        const std::string &message);
    size_t rowFromPointer(
        const InputState &input, float top,
        float rowHeight, size_t count) const;

    FrontendScreen screen_ = FrontendScreen::Title;
    SkirmishSettings settings_;
    size_t selection_ = 0;
    int outcome_ = -1;
    std::string message_;
    UserSettings userSettings_;
    FrontendScreen optionsReturnScreen_ =
        FrontendScreen::MainMenu;
    FrontendScreen confirmReturnScreen_ =
        FrontendScreen::Pause;
    FrontendAction confirmedAction_ =
        FrontendAction::None;
    bool continueAvailable_ = false;
    bool settingsChanged_ = false;
};

} // namespace swgb
