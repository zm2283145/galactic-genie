// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "campaign.h"
#include "game.h"
#include "settings.h"
#include "skirmish.h"

#include <cstddef>
#include <array>
#include <functional>
#include <string>
#include <vector>

namespace swgb {

class CampaignScene;

enum class FrontendScreen : uint8_t {
    Title,
    MainMenu,
    SinglePlayer,
    CampaignBrowser,
    CampaignMissions,
    CampaignBriefing,
    SkirmishLobby,
    ScenarioEditor,
    Loading,
    Gameplay,
    Pause,
    Objectives,
    Options,
    DataStatus,
    Confirm,
    Outcome,
    // A won campaign mission's closing scene (Campaign/Media/*_end.mm),
    // shown before the outcome menu.
    CampaignEpilogue,
};

enum class FrontendAction : uint8_t {
    None,
    StartSkirmish,
    StartCampaign,
    OpenScenarioEditor,
    RestartMatch,
    SaveMatch,
    LoadMatch,
    ReturnToCampaignBrowser,
    ReturnToEditor,
    ReturnToMainMenu,
    CompleteCampaignMission,
    Quit,
};

using FrontendStringLookup =
    std::function<std::string(int, const std::string &)>;
using FrontendSoundPlayer =
    std::function<void(int)>;

class Frontend {
public:
    FrontendAction update(
        const InputState &input, int matchOutcome);
    void render(
        Renderer &renderer, int screenW,
        int screenH) const;
    void renderObjectivesOverlay(
        Renderer &renderer, int screenW,
        int screenH) const;
    void loadingFinished(
        bool success,
        const std::string &error = std::string());
    void showGameplay();
    void showEditor();
    void showMainMenu();
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
    void setSoundPlayer(FrontendSoundPlayer player) {
        sounds_ = std::move(player);
    }
    void setOriginalMenuBackground(
        const SpriteFrame *frame) {
        originalMenuBackground_ = frame;
    }
    // The opening scene shown on the campaign briefing screen (null or not
    // loaded: the briefing dialog). Owned by the caller.
    void setCampaignScene(const CampaignScene *scene) {
        campaignScene_ = scene;
    }
    void setOriginalBriefingDialog(
        const SpriteFrame *frame) {
        originalBriefingDialog_ = frame;
    }
    void setOriginalMenuDecoration(
        const SpriteFrame *logo,
        const SpriteFrame *button,
        const SpriteFrame *selectedButton) {
        originalMenuLogo_ = logo;
        originalMenuButton_ = button;
        originalMenuSelectedButton_ =
            selectedButton;
    }
    void setOriginalMainHotspot(
        size_t hotspot,
        const SpriteFrame *normal,
        const SpriteFrame *selected,
        const SpriteFrame *active) {
        if (hotspot >= originalMainHotspots_.size())
            return;
        originalMainHotspots_[hotspot] = {
            normal, selected, active};
    }
    void setExpandingFrontsMenu(bool enabled) {
        expandingFrontsMenu_ = enabled;
    }
    void setOriginalCampaignIcon(
        size_t campaign,
        const SpriteFrame *normal,
        const SpriteFrame *selected) {
        if (campaign >=
            originalCampaignIcons_.size())
            return;
        originalCampaignIcons_[campaign] = {
            normal, selected};
    }
    void clearOriginalMissionNodes() {
        originalMissionNodeCount_ = 0;
        for (auto &node : originalMissionNodes_)
            node.frames.fill(nullptr);
    }
    void setOriginalMissionNode(
        size_t mission, float x, float y,
        const SpriteFrame *normal,
        const SpriteFrame *selected,
        const SpriteFrame *completed,
        const SpriteFrame *locked,
        float textX = -1.0f, float textY = -1.0f,
        float textW = 0.0f, float textH = 0.0f) {
        if (mission >= originalMissionNodes_.size())
            return;
        originalMissionNodes_[mission].x = x;
        originalMissionNodes_[mission].y = y;
        originalMissionNodes_[mission].textX = textX;
        originalMissionNodes_[mission].textY = textY;
        originalMissionNodes_[mission].textW = textW;
        originalMissionNodes_[mission].textH = textH;
        originalMissionNodes_[mission].frames = {
            normal, selected, completed, locked};
        if (originalMissionNodeCount_ <
            mission + 1)
            originalMissionNodeCount_ =
                mission + 1;
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
        if (campaign) playtestMatch_ = false;
    }
    bool campaignMatch() const {
        return campaignMatch_;
    }
    void setPlaytestMatch(bool playtest) {
        playtestMatch_ = playtest;
        if (playtest) campaignMatch_ = false;
    }
    bool playtestMatch() const {
        return playtestMatch_;
    }
    void setPlaytestObjectives(std::string objectives) {
        playtestObjectives_ = std::move(objectives);
    }
    size_t selectedCampaign() const {
        return campaignSelection_;
    }
    bool cloneCampaignsShown() const { return cloneCampaigns_ && !customCampaigns_; }
    bool customCampaignsShown() const { return customCampaigns_; }
    size_t selectedMission() const {
        return missionSelection_;
    }
    void selectCampaignMission(
        size_t campaign, size_t mission) {
        campaignSelection_ = campaign;
        missionSelection_ = mission;
        if (catalog_ && campaign < catalog_->campaigns().size()) {
            cloneCampaigns_ = catalog_->campaigns()[campaign].expansion;
            customCampaigns_ = catalog_->campaigns()[campaign].custom;
        }
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
    // Tests and tools: jump straight to a screen.
    void showScreenForTesting(FrontendScreen screen, size_t selection) {
        screen_ = screen;
        selection_ = selection;
    }
    size_t selectionForTesting() const {
        return selection_;
    }
    const std::string &message() const {
        return message_;
    }

private:
    const SpriteFrame *originalMenuBackground_ =
        nullptr;
    const SpriteFrame *originalBriefingDialog_ =
        nullptr;
    const SpriteFrame *originalMenuLogo_ = nullptr;
    const SpriteFrame *originalMenuButton_ = nullptr;
    const SpriteFrame *originalMenuSelectedButton_ =
        nullptr;
    // Indexed by catalog campaign: six original and two Clone Campaigns.
    std::array<
        std::array<const SpriteFrame *, 2>, 8>
        originalCampaignIcons_{};
    std::array<
        std::array<const SpriteFrame *, 3>, 8>
        originalMainHotspots_{};
    bool expandingFrontsMenu_ = false;
    struct OriginalMissionNode {
        float x = 0.0f;
        float y = 0.0f;
        // Title box from the campaign screen table (textX < 0: none, the
        // title goes under the button).
        float textX = -1.0f, textY = -1.0f, textW = 0.0f, textH = 0.0f;
        std::array<const SpriteFrame *, 4> frames{};
    };
    std::array<OriginalMissionNode, 8>
        originalMissionNodes_{};
    size_t originalMissionNodeCount_ = 0;
    const CampaignScene *campaignScene_ = nullptr;
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
    // The campaign selection screen shows the Clone Campaigns (1cam) rather
    // than the original campaigns (xcam).
    bool cloneCampaigns_ = false;
    // The mission screen was opened by Basic Training on the main menu, so
    // back returns there.
    bool missionsFromMainMenu_ = false;
    // The selection screen lists the Custom Campaigns instead.
    bool customCampaigns_ = false;
    bool inBrowserSet(const CampaignInfo &campaign) const {
        return customCampaigns_ ? campaign.custom
                                : !campaign.custom && campaign.expansion == cloneCampaigns_;
    }
    // Row of a custom campaign on its list.
    size_t browserRow(size_t index) const {
        size_t row = 0;
        if (catalog_)
            for (size_t i = 0; i < index && i < catalog_->campaigns().size(); ++i)
                row += inBrowserSet(catalog_->campaigns()[i]) ? 1 : 0;
        return row;
    }
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
    FrontendSoundPlayer sounds_;
    MatchSaveKind continueKind_ =
        MatchSaveKind::Skirmish;
    size_t dataCampaigns_ = 0;
    size_t dataMissions_ = 0;
    bool optionalMedia_ = false;
    bool campaignMatch_ = false;
    bool playtestMatch_ = false;
    std::string playtestObjectives_;
    bool continueAvailable_ = false;
    bool settingsChanged_ = false;
    bool profileChanged_ = false;
    FrontendScreen objectivesReturnScreen_ =
        FrontendScreen::Gameplay;
    size_t objectivesPage_ = 0;
};

} // namespace swgb
