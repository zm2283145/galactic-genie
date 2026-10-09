// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "editor_document.h"
#include "game.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace swgb {

class Assets;
class Renderer;

enum class EditorAction : uint8_t {
    None,
    Close,
    Playtest,
};

enum class EditorPage : uint8_t {
    Hub,
    Map,
    Objects,
    Players,
    Triggers,
    Scenario,
    Validate,
    Files,
};

enum class EditorMapTool : uint8_t {
    Terrain,
    Elevation,
    Water,
    Cliff,
    Foundation,
    Fill,
    Replace,
    Area,
    PlayerStart,
    Camera,
};

class ScenarioEditor {
public:
    ScenarioEditor(
        Assets &assets, std::string rootDirectory,
        std::string importDirectory);

    void enter();
    EditorAction update(
        float elapsedSeconds, const InputState &input);
    void render(
        Renderer &renderer, int screenWidth,
        int screenHeight) const;
    void playtestFinished(
        bool completed, const std::string &diagnostic);

    bool buildPlaytestScenario(
        Scenario &scenario,
        std::string *error = nullptr) const;
    const EditableScenarioDocument &document() const {
        return document_;
    }
    EditableScenarioDocument &documentForTesting() {
        return document_;
    }
    const ValidationReport &validation() const {
        return validation_;
    }
    EditorPage page() const { return page_; }
    bool dirty() const {
        return workspaceOpen_ &&
               (currentPath_.empty() || undo_.dirty());
    }
    const std::string &status() const { return status_; }
    const std::string &currentPath() const {
        return currentPath_;
    }
    int playtestDifficulty() const;
    // Custom campaigns are written to this folder (the game's Campaign
    // folder, where Custom Campaigns are listed from).
    void setCampaignDirectory(std::string directory) {
        campaignDirectory_ = std::move(directory);
    }
    // True once after a campaign archive was saved (the catalog reloads).
    bool takeCampaignSaved() {
        const bool saved = campaignSaved_;
        campaignSaved_ = false;
        return saved;
    }
    // Campaign builder (testing): scenarios from the import folder.
    bool buildCampaignForTesting(const std::string &name,
                                 const std::vector<std::string> &scenarioPaths,
                                 std::string *error = nullptr);

    bool createForTesting(
        ScenarioTemplate scenarioTemplate,
        uint32_t mapSize, uint32_t seed,
        uint32_t playerCount,
        std::string *error = nullptr);
    bool applyBrushForTesting(
        int tileX, int tileY, EditorMapTool tool,
        uint8_t value, int size, bool circular,
        std::string *error = nullptr);
    bool placeObjectForTesting(
        uint16_t unitId, uint8_t player,
        float x, float y,
        std::string *error = nullptr);
    bool saveForTesting(
        const std::string &path,
        std::string *error = nullptr);
    bool loadForTesting(
        const std::string &path,
        std::string *error = nullptr);

private:
    bool createTemplate(
        ScenarioTemplate scenarioTemplate);
    bool loadNative(const std::string &path);
    bool importScx(const std::string &path);
    bool saveNative(bool autosave);
    bool exportScx();
    bool recoverAutosave();
    enum class PendingAction : uint8_t {
        None,
        NewBlank,
        NewIslands,
        NewSkirmishBase,
        NewTriggerTutorial,
        Recover,
        Import,
        SaveOverwrite,
    };
    void requestAction(PendingAction action);
    void performPendingAction();
    void updateConfirmation(const InputState &input);
    void refreshValidation();
    void markChanged(
        const EditableScenarioDocument &before,
        const char *status);
    void updateHub(const InputState &input);
    void updateWorkspace(
        float elapsedSeconds,
        const InputState &input);
    void updateMap(const InputState &input);
    void updateObjects(const InputState &input);
    void updatePlayers(const InputState &input);
    void updateTriggers(const InputState &input);
    void updateScenario(const InputState &input);
    void updateValidation(const InputState &input);
    void updateFiles(const InputState &input);
    bool screenToTile(
        float x, float y, int &tileX,
        int &tileY) const;
    void renderHub(
        Renderer &renderer, int screenWidth,
        int screenHeight) const;
    void renderWorkspace(
        Renderer &renderer, int screenWidth,
        int screenHeight) const;
    void renderMap(
        Renderer &renderer) const;
    void renderObjects(
        Renderer &renderer) const;
    void renderPlayers(
        Renderer &renderer) const;
    void renderTriggers(
        Renderer &renderer) const;
    void renderScenario(
        Renderer &renderer) const;
    void renderValidation(
        Renderer &renderer) const;
    void renderFiles(
        Renderer &renderer) const;
    void renderConfirmation(
        Renderer &renderer) const;
    std::string objectName(uint16_t id) const;
    const dat::Unit *unitDefinition(
        uint16_t id, uint8_t player) const;
    bool objectFits(
        const dat::Unit &unit, float x, float y,
        uint32_t ignoreSpawnId,
        std::string *error) const;
    uint16_t nextPlaceableUnit(
        uint16_t current, int direction) const;
    std::string triggerSummary(
        const EditorTrigger &trigger) const;

    Assets &assets_;
    std::string rootDirectory_;
    std::string importDirectory_;
    EditableScenarioDocument document_;
    EditableScenarioUndo undo_{32, 24u * 1024u * 1024u};
    ValidationReport validation_;
    EditorStoragePaths paths_;
    std::string currentPath_;
    std::string recentPath_;
    std::string status_;
    std::string playtestDiagnostic_;
    EditorPage page_ = EditorPage::Hub;
    size_t selection_ = 0;
    size_t subSelection_ = 0;
    size_t selectedPlayer_ = 0;
    size_t selectedDiplomacyTarget_ = 1;
    bool playerTechnologyPanel_ = false;
    uint8_t playerTechnologyKind_ = 0;
    uint32_t playerTechnologyId_ = 0;
    size_t selectedTrigger_ = 0;
    size_t selectedObject_ = 0;
    size_t selectedIssue_ = 0;
    EditorMapTool mapTool_ = EditorMapTool::Terrain;
    // Campaign builder (hub entry "BUILD CAMPAIGN").
    bool campaignMode_ = false;
    std::string campaignDirectory_;
    bool campaignSaved_ = false;
    std::vector<std::string> campaignAvailable_; // .scx paths
    std::vector<std::string> campaignEntries_;
    size_t campaignColumn_ = 0;
    size_t campaignAvailableSelection_ = 0;
    size_t campaignEntrySelection_ = 0;
    int campaignNumber_ = 1;
    void openCampaignBuilder();
    void updateCampaignBuilder(const InputState &input);
    void renderCampaignBuilder(Renderer &renderer, int screenWidth) const;
    bool writeCampaign(const std::string &name, const std::vector<std::string> &paths,
                       std::string *error);
    uint8_t brushValue_ = 0;
    int brushSize_ = 1;
    bool circularBrush_ = true;
    float mapZoom_ = 1.0f;
    float mapPanX_ = 0;
    float mapPanY_ = 0;
    uint16_t paletteUnit_ = 0;
    uint8_t objectPlayer_ = 1;
    uint32_t templateSeed_ = 0x5A17u;
    uint32_t templateSize_ = 96;
    uint32_t templatePlayers_ = 2;
    float autosaveElapsed_ = 0;
    uint64_t lastAutosaveGeneration_ = 0;
    bool workspaceOpen_ = false;
    PendingAction pendingAction_ =
        PendingAction::None;
    size_t confirmationSelection_ = 0;
};

} // namespace swgb
