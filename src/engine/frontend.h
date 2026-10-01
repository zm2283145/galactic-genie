// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "campaign.h"
#include "game.h"
#include "settings.h"
#include "skirmish.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace swgb {

enum class FrontendScreen : uint8_t {
    Title,
    MainMenu,
    SinglePlayer,
    CampaignBrowser,
    CampaignMissions,
    CampaignBriefing,
    SkirmishLobby,
    Loading,
    Gameplay,
    Pause,
    Objectives,
    Options,
    DataStatus,
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
    ReturnToCampaignBrowser,
    ReturnToMainMenu,
    CompleteCampaignMission,
    Quit,
};

using FrontendStringLookup =
    std::function<std::string(int, const std::string &)>;

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
    void setContinueKind(MatchSaveKind kind) {
        continueKind_ = kind;
    }
    void setCampaignData(
        const CampaignCatalog *catalog,
        CampaignProfile *profile) {
        catalog_ = catalog;
        profile_ = profile;
    }
    void setStringLookup(FrontendStringLookup lookup) {
        strings_ = std::move(lookup);
    }
    void setDataStatus(
        size_t campaigns, size_t missions,
        bool optionalMedia) {
        dataCampaigns_ = campaigns;
        dataMissions_ = missions;
        optionalMedia_ = optionalMedia;
    }
    void setCampaignMatch(bool campaign) {
        campaignMatch_ = campaign;
    }
    bool campaignMatch() const {
        return campaignMatch_;
    }
    size_t selectedCampaign() const {
        return campaignSelection_;
    }
    size_t selectedMission() const {
        return missionSelection_;
    }
    void selectCampaignMission(
        size_t campaign, size_t mission) {
        campaignSelection_ = campaign;
        missionSelection_ = mission;
    }
    const CampaignMission *selectedCampaignMission() const;
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
    bool takeProfileChanged() {
        const bool changed = profileChanged_;
        profileChanged_ = false;
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
    void refreshLobbyPreview();
    void adjustOptionValue(int direction);
    void beginConfirmation(
        FrontendAction action,
        const std::string &message);
    size_t rowFromPointer(
        const InputState &input, float top,
        float rowHeight, size_t count) const;
    std::string text(
        int id, const std::string &fallback) const;
    FrontendAction updateMenu(
        const InputState &input,
        const std::vector<std::string> &entries,
        float top, float rowHeight);

    FrontendScreen screen_ = FrontendScreen::Title;
    SkirmishSettings settings_;
    SkirmishPreview lobbyPreview_ =
        generateSkirmishPreview(settings_);
    size_t selection_ = 0;
    size_t lobbyPage_ = 0;
    size_t lobbySlot_ = 0;
    size_t campaignSelection_ = 0;
    size_t missionSelection_ = 0;
    int outcome_ = -1;
    std::string message_;
    UserSettings userSettings_;
    FrontendScreen optionsReturnScreen_ =
        FrontendScreen::MainMenu;
    FrontendScreen confirmReturnScreen_ =
        FrontendScreen::Pause;
    FrontendAction confirmedAction_ =
        FrontendAction::None;
    const CampaignCatalog *catalog_ = nullptr;
    CampaignProfile *profile_ = nullptr;
    FrontendStringLookup strings_;
    MatchSaveKind continueKind_ =
        MatchSaveKind::Skirmish;
    size_t dataCampaigns_ = 0;
    size_t dataMissions_ = 0;
    bool optionalMedia_ = false;
    bool campaignMatch_ = false;
    bool continueAvailable_ = false;
    bool settingsChanged_ = false;
    bool profileChanged_ = false;
};

} // namespace swgb
