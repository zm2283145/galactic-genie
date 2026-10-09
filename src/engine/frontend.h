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
    // The original's Achievements screen (screen info 50061): score and the
    // military, economy, technology and society statistics per player, and
    // the score timeline.
    Achievements,
    // The in-game Diplomacy dialog (exe 0x45b080, "dlg_dip" 50014): stances
    // and tribute, drawn over the paused game.
    Diplomacy,
    // Chat: send a numbered taunt to all, allies or one player (the
    // original's Chat dialog; computer players react to some taunts).
    Chat,
    // The Technology Tree (TribeTechHelpScreen, screen info 50007).
    TechTree,
};

struct DiplomacyResult {
    std::vector<std::pair<int, int>> stances;                // player, stance
    std::vector<std::pair<int, std::array<int, 4>>> tributes; // player, amounts by resource
    bool alliedVictory = false;
};

struct AchievementsData {
    std::vector<AchievementsPlayer> players;
    float elapsedSeconds = 0.0f;
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
    OpenDiplomacy,   // fill the dialog (setDiplomacy) before it is drawn
    ApplyDiplomacy,  // OK: apply diplomacyResult()
    OpenChat,        // fill setChatPlayers / setTaunts first
    SendChat,        // chatRecipient() / chatTaunt()
    TypeChat,        // open the on-screen keyboard; send to chatRecipient()
    OpenTechTree,    // setTechTree(game.techTreeData(techTreeCivilization())) and art
};

using FrontendStringLookup =
    std::function<std::string(int, const std::string &)>;
using FrontendSoundPlayer =
    std::function<void(int)>;

class Frontend {
public:
    // dt (seconds) times the 5 s "You are victorious!" pause before the
    // end-of-game screens; 0 skips it (tests).
    FrontendAction update(
        const InputState &input, int matchOutcome, float dt = 0.0f);
    // The game screen's game-over label (9004/9005) while the end-of-game
    // pause runs; empty otherwise.
    std::string gameOverLabel() const;
    void renderGameOverOverlay(Renderer &renderer, int screenW, int screenH) const;
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
    void setAchievements(AchievementsData data) {
        achievements_ = std::move(data);
    }
    // sat_tabs.slp (50765): frame 2*tab is the selected tab, 2*tab+1 the
    // normal one; PNBnr1.slp (50762): the player-coloured name banners.
    void setAchievementsArt(
        const std::array<const SpriteFrame *, 12> &tabs,
        const std::array<const SpriteFrame *, 8> &banners) {
        achievementTabFrames_ = tabs;
        achievementBannerFrames_ = banners;
    }
    size_t achievementsTab() const { return achievementsTab_; }
    void setDiplomacy(DiplomacyData data) {
        diplomacy_ = std::move(data);
        diplomacyPending_ = {};
        diplomacyStances_.clear();
        for (const DiplomacyRow &row : diplomacy_.rows)
            diplomacyStances_.push_back(row.ourStance);
        diplomacyAlliedVictory_ = diplomacy_.alliedVictory;
        diplomacyRow_ = 0;
        diplomacyColumn_ = 0;
        for (size_t i = 0; i < diplomacy_.rows.size(); ++i)
            if (!diplomacy_.rows[i].local) { diplomacyRow_ = i; break; }
    }
    // sat as for Achievements: tradicon.shp 50732 frames 0 carbon, 1 ore,
    // 2 food, 3 nova; the dialog background dlg_dip 50221.
    void setDiplomacyArt(const SpriteFrame *background,
                         const std::array<const SpriteFrame *, 4> &tributeIcons) {
        diplomacyBackground_ = background;
        diplomacyTributeIcons_ = tributeIcons;
    }
    const DiplomacyResult &diplomacyResult() const { return diplomacyResult_; }
    // Chat: recipients are "All", "Allies", then each other player.
    void setChatPlayers(std::vector<std::pair<int, std::string>> players) {
        chatPlayers_ = std::move(players);
        chatRecipient_ = 0;
    }
    void setTaunts(std::vector<std::pair<int, std::string>> taunts) { taunts_ = std::move(taunts); }
    // 0 all, 1 allies, else the player number.
    int chatRecipient() const {
        return chatRecipient_ < 2 ? (int)chatRecipient_
                                  : chatPlayers_[chatRecipient_ - 2].first;
    }
    int chatTaunt() const {
        // Row 0 is "type a message"; taunts follow.
        return chatSelection_ >= 1 && chatSelection_ - 1 < taunts_.size()
                   ? taunts_[chatSelection_ - 1].first
                   : 0;
    }
    void renderChatOverlay(Renderer &renderer, int screenW, int screenH) const;
    void renderDiplomacyOverlay(Renderer &renderer, int screenW, int screenH) const;
    // Technology Tree: the data and an art lookup (slp, frame) -> frame.
    void setTechTree(TechTreeData data) {
        techTree_ = std::move(data);
        if (techTreeSelection_ >= techTree_.nodes.size()) techTreeSelection_ = 0;
    }
    void setTechTreeArt(std::function<const SpriteFrame *(int, int)> art) {
        techTreeArt_ = std::move(art);
    }
    int techTreeCivilization() const { return techTreeCivilization_; }
    void renderTechTree(Renderer &renderer, int screenW, int screenH) const;
    void showDiplomacyForTesting() { screen_ = FrontendScreen::Diplomacy; }
    void showAchievementsForTesting(size_t tab) {
        if (screen_ != FrontendScreen::Achievements) achievementsReturn_ = screen_;
        screen_ = FrontendScreen::Achievements;
        achievementsTab_ = tab;
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
    // The lobby starts on an original random map (Desert).
    SkirmishSettings settings_ = [] {
        SkirmishSettings defaults;
        defaults.mapType = 9;
        return defaults;
    }();
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
    AchievementsData achievements_;
    size_t achievementsTab_ = 0;
    FrontendScreen achievementsReturn_ = FrontendScreen::Outcome;
    // 2 = end-of-game Achievements (0x4eb000 mode 2): "Scenario Menu" or
    // "Main Menu" plus "Play Again"; 0 = opened from a menu (Back).
    int achievementsMode_ = 0;
    size_t achievementsButton_ = 0;
    bool gameOverPending_ = false;
    float gameOverSeconds_ = 0.0f;
    bool trainingCampaign() const;
    FrontendAction finishGameOver();
    FrontendAction achievementsMainButton();
    std::array<const SpriteFrame *, 12> achievementTabFrames_{};
    std::array<const SpriteFrame *, 8> achievementBannerFrames_{};
    void renderAchievements(Renderer &renderer, int screenW, int screenH) const;
    TechTreeData techTree_;
    std::function<const SpriteFrame *(int, int)> techTreeArt_;
    size_t techTreeSelection_ = 0;
    float techTreeScroll_ = 0.0f;
    int techTreeCivilization_ = -1;
    FrontendAction updateTechTree(const InputState &input);
    DiplomacyData diplomacy_;
    std::vector<int> diplomacyStances_;
    // Pending tribute by row and dialog column (carbon, food, nova, ore).
    std::array<std::array<int, 4>, 16> diplomacyPending_{};
    bool diplomacyAlliedVictory_ = false;
    size_t diplomacyRow_ = 0, diplomacyColumn_ = 0;
    DiplomacyResult diplomacyResult_;
    const SpriteFrame *diplomacyBackground_ = nullptr;
    std::array<const SpriteFrame *, 4> diplomacyTributeIcons_{};
    FrontendAction updateDiplomacy(const InputState &input);
    FrontendAction updateChat(const InputState &input);
    std::vector<std::pair<int, std::string>> chatPlayers_, taunts_;
    size_t chatRecipient_ = 0, chatSelection_ = 0;
    int diplomacyRemaining(size_t column) const;
};

} // namespace swgb
