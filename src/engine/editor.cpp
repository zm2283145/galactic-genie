// SPDX-License-Identifier: GPL-3.0-or-later
#include "editor.h"

#include "assets.h"
#include "menu_framework.h"
#include "ui_text.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>
#include <set>

namespace swgb {
namespace {

constexpr float kMapX = 22.0f;
constexpr float kMapY = 88.0f;
constexpr float kMapW = 650.0f;
constexpr float kMapH = 405.0f;
constexpr float kSideX = 692.0f;
constexpr float kSideW = 246.0f;
constexpr size_t kPageCount = 7;

const std::vector<std::string> kWorkspaceTabs{
    "MAP", "OBJECTS", "PLAYERS", "TRIGGERS",
    "SCENARIO", "VALIDATE", "FILES"};

const char *toolName(EditorMapTool tool) {
    switch (tool) {
    case EditorMapTool::Terrain: return "Terrain brush";
    case EditorMapTool::Elevation: return "Elevation brush";
    case EditorMapTool::Water: return "Water brush";
    case EditorMapTool::Cliff: return "Cliff brush";
    case EditorMapTool::Foundation: return "Foundation brush";
    case EditorMapTool::Fill: return "Flood fill";
    case EditorMapTool::Replace: return "Replace all";
    case EditorMapTool::Area: return "Area marker";
    case EditorMapTool::PlayerStart: return "Player start";
    case EditorMapTool::Camera: return "Camera marker";
    }
    return "Tool";
}

const char *templateName(ScenarioTemplate scenarioTemplate) {
    switch (scenarioTemplate) {
    case ScenarioTemplate::BlankLand: return "Blank Land";
    case ScenarioTemplate::Islands: return "Islands";
    case ScenarioTemplate::SkirmishBase: return "Skirmish Base";
    case ScenarioTemplate::TriggerTutorial:
        return "Objective / Trigger Tutorial";
    }
    return "Scenario";
}

uint8_t clampByte(int value, int maximum = 255) {
    return (uint8_t)std::max(0, std::min(maximum, value));
}

std::string number(float value) {
    char text[32];
    std::snprintf(text, sizeof text, "%.1f", value);
    return text;
}

std::string shortPath(const std::string &path) {
    if (path.size() <= 52) return path;
    return "..." + path.substr(path.size() - 49);
}

void title(
    Renderer &renderer, const std::string &text,
    int screenWidth) {
    drawUiText(
        renderer, {text},
        (screenWidth - uiTextWidth(text, 1.55f)) * 0.5f,
        17, 1.55f, 235, 213, 145);
}

void terrainColor(
    uint8_t terrain, uint8_t elevation,
    uint8_t &red, uint8_t &green, uint8_t &blue) {
    if (terrain == 2 || terrain == 3 ||
        terrain == 15 || terrain == 22) {
        red = 22;
        green = 77;
        blue = 137;
    } else if (terrain == 1 || terrain == 10 ||
               terrain == 14) {
        red = 168;
        green = 148;
        blue = 84;
    } else if (terrain == 4 || terrain == 9 ||
               terrain == 17) {
        red = 161;
        green = 185;
        blue = 198;
    } else {
        red = (uint8_t)(45 + (terrain * 19u) % 38u);
        green = (uint8_t)(94 + (terrain * 13u) % 69u);
        blue = (uint8_t)(48 + (terrain * 7u) % 38u);
    }
    const int lift = elevation * 6;
    red = clampByte(red + lift);
    green = clampByte(green + lift);
    blue = clampByte(blue + lift);
}

bool finite(float value) {
    return std::isfinite(value);
}

bool pathExists(const std::string &path) {
    FILE *file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    std::fclose(file);
    return true;
}

} // namespace

ScenarioEditor::ScenarioEditor(
    Assets &assets, std::string rootDirectory,
    std::string importDirectory)
    : assets_(assets),
      rootDirectory_(std::move(rootDirectory)),
      importDirectory_(std::move(importDirectory)) {
    paths_ = scenarioStoragePaths(
        rootDirectory_, "scenario");
    if (pathExists(paths_.recent))
        recentPath_ = paths_.recent;
}

void ScenarioEditor::enter() {
    page_ = workspaceOpen_
                ? EditorPage::Map
                : EditorPage::Hub;
    selection_ = 0;
    status_ =
        workspaceOpen_
            ? "EDITOR RESUMED - L/R CHANGES PANEL"
            : "USER FILES: " +
                  shortPath(paths_.userCreated);
}

bool ScenarioEditor::createForTesting(
    ScenarioTemplate scenarioTemplate,
    uint32_t mapSize, uint32_t seed,
    uint32_t playerCount, std::string *error) {
    EditableScenarioDocument created;
    if (!makeScenarioTemplate(
            scenarioTemplate, mapSize, seed,
            playerCount, created, error))
        return false;
    document_ = std::move(created);
    document_.metadata.title =
        templateName(scenarioTemplate);
    document_.metadata.modifiedTimestamp =
        (int64_t)std::time(nullptr);
    undo_.clear();
    currentPath_.clear();
    workspaceOpen_ = true;
    page_ = EditorPage::Map;
    selection_ = 0;
    subSelection_ = 0;
    mapPanX_ = mapPanY_ = 0;
    mapZoom_ = 1;
    refreshValidation();
    return true;
}

bool ScenarioEditor::createTemplate(
    ScenarioTemplate scenarioTemplate) {
    std::string error;
    if (!createForTesting(
            scenarioTemplate, templateSize_,
            templateSeed_, templatePlayers_,
            &error)) {
        status_ = "CREATE FAILED: " + error;
        return false;
    }
    status_ =
        std::string("CREATED ") +
        templateName(scenarioTemplate) +
        " - UNSAVED";
    return true;
}

bool ScenarioEditor::loadForTesting(
    const std::string &path, std::string *error) {
    EditableScenarioDocument loaded;
    if (!loadEditableScenario(path, loaded, error))
        return false;
    document_ = std::move(loaded);
    currentPath_ = path;
    paths_ = scenarioStoragePaths(
        rootDirectory_,
        sanitizeScenarioFilename(
            document_.metadata.title.empty()
                ? "scenario"
                : document_.metadata.title));
    recentPath_ = path;
    undo_.clear();
    undo_.markClean();
    workspaceOpen_ = true;
    page_ = EditorPage::Map;
    refreshValidation();
    return true;
}

bool ScenarioEditor::loadNative(
    const std::string &path) {
    std::string error;
    if (!loadForTesting(path, &error)) {
        status_ = "OPEN FAILED: " + error;
        return false;
    }
    if (path == paths_.recent)
        currentPath_ = paths_.userCreated;
    status_ = "OPENED " + shortPath(path);
    return true;
}

bool ScenarioEditor::importScx(
    const std::string &path) {
    EditableScenarioDocument imported;
    std::string error;
    if (!importScxFile(path, imported, &error)) {
        status_ = "IMPORT FAILED: " + error;
        return false;
    }
    document_ = std::move(imported);
    if (document_.metadata.title.empty())
        document_.metadata.title = "Imported Scenario";
    paths_ = scenarioStoragePaths(
        rootDirectory_,
        sanitizeScenarioFilename(
            document_.metadata.title));
    currentPath_.clear();
    undo_.clear();
    workspaceOpen_ = true;
    page_ = EditorPage::Map;
    refreshValidation();
    status_ =
        "SCX IMPORTED READ/EDIT-SAFE; SAVE NATIVE SIDECAR";
    return true;
}

bool ScenarioEditor::saveForTesting(
    const std::string &path, std::string *error) {
    if (!saveEditableScenario(path, document_, error))
        return false;
    currentPath_ = path;
    recentPath_ = path;
    undo_.markClean();
    return true;
}

bool ScenarioEditor::saveNative(bool autosave) {
    if (!workspaceOpen_) {
        status_ = "NO DOCUMENT TO SAVE";
        return false;
    }
    const std::string safeName =
        sanitizeScenarioFilename(
            document_.metadata.title.empty()
                ? "scenario"
                : document_.metadata.title);
    paths_ = scenarioStoragePaths(
        rootDirectory_, safeName);
    const std::string path =
        autosave
            ? paths_.autosave
            : currentPath_.empty()
                  ? paths_.userCreated
                  : currentPath_;
    std::string error;
    if (autosave) {
        EditableScenarioDocument autosave = document_;
        if (!saveEditableScenario(
                path, autosave, &error)) {
            status_ = "AUTOSAVE FAILED: " + error;
            return false;
        }
        if (!saveEditableScenario(
                paths_.recovery, autosave, &error)) {
            status_ =
                "RECOVERY INDEX FAILED: " + error;
            return false;
        }
        lastAutosaveGeneration_ = undo_.generation();
        status_ = "AUTOSAVED RECOVERY SNAPSHOT";
        return true;
    }
    if (!saveForTesting(path, &error)) {
        status_ = "SAVE FAILED: " + error;
        return false;
    }
    if (!saveEditableScenario(
            paths_.recent, document_, &error)) {
        status_ =
            "SAVED, BUT RECENT INDEX FAILED: " +
            error;
        return false;
    }
    recentPath_ = paths_.recent;
    status_ = "SAVED " + shortPath(path);
    return true;
}

bool ScenarioEditor::recoverAutosave() {
    if (!loadNative(paths_.recovery))
        return false;
    currentPath_.clear();
    status_ =
        "RECOVERED AUTOSAVE - SAVE TO KEEP CHANGES";
    return true;
}

bool ScenarioEditor::exportScx() {
    const std::string file =
        joinScenarioPath(
            importDirectory_,
            sanitizeScenarioFilename(
                document_.metadata.title.empty()
                    ? "scenario"
                    : document_.metadata.title) +
                ".scx");
    std::string error;
    if (!exportScxFile(file, document_, &error)) {
        status_ =
            "SCX EXPORT REFUSED: " + error;
        return false;
    }
    status_ =
        "EXPORTED BYTE-EXACT SOURCE SCX: " +
        shortPath(file);
    return true;
}

void ScenarioEditor::refreshValidation() {
    validation_ =
        validateEditableScenario(document_, assets_);
}

void ScenarioEditor::markChanged(
    const EditableScenarioDocument &before,
    const char *message) {
    std::string error;
    if (!undo_.begin(before, &error) ||
        !undo_.commit(document_, &error)) {
        undo_.cancel();
        status_ = "UNDO SNAPSHOT FAILED: " + error;
        document_ = before;
        return;
    }
    document_.metadata.modifiedTimestamp =
        (int64_t)std::time(nullptr);
    refreshValidation();
    status_ = message;
}

EditorAction ScenarioEditor::update(
    float elapsedSeconds, const InputState &input) {
    if (pendingAction_ != PendingAction::None) {
        updateConfirmation(input);
        return EditorAction::None;
    }
    if (page_ == EditorPage::Hub) {
        updateHub(input);
        if (status_ == "__CLOSE__") {
            status_.clear();
            return EditorAction::Close;
        }
        return EditorAction::None;
    }
    autosaveElapsed_ +=
        std::max(0.0f, elapsedSeconds);
    updateWorkspace(elapsedSeconds, input);
    if (status_ == "__CLOSE__") {
        status_.clear();
        return EditorAction::Close;
    }
    if (status_ == "__PLAYTEST__") {
        status_ = "VALIDATED PLAYTEST SNAPSHOT READY";
        return EditorAction::Playtest;
    }
    if (undo_.dirty() &&
        undo_.generation() != lastAutosaveGeneration_ &&
        autosaveElapsed_ >= 30.0f) {
        autosaveElapsed_ = 0;
        saveNative(true);
    }
    return EditorAction::None;
}

void ScenarioEditor::updateHub(
    const InputState &input) {
    constexpr size_t count = 11;
    if (input.menuUp)
        selection_ = (selection_ + count - 1) % count;
    if (input.menuDown)
        selection_ = (selection_ + 1) % count;
    if (input.menuLeft || input.menuRight) {
        const int direction =
            input.menuLeft ? -1 : 1;
        if (selection_ == 4)
            templateSize_ =
                (uint32_t)std::clamp(
                    (int)templateSize_ +
                        direction * 16,
                    32, 160);
        else if (selection_ == 5)
            templatePlayers_ =
                (uint32_t)std::clamp(
                    (int)templatePlayers_ +
                        direction,
                    1, 8);
        else if (selection_ == 6)
            templateSeed_ +=
                direction < 0 ? UINT32_MAX : 1u;
    }
    if (input.menuBack) {
        status_ = "__CLOSE__";
        return;
    }
    if (!input.menuActivate) return;
    switch (selection_) {
    case 0:
        requestAction(PendingAction::NewBlank);
        break;
    case 1:
        requestAction(PendingAction::NewIslands);
        break;
    case 2:
        requestAction(PendingAction::NewSkirmishBase);
        break;
    case 3:
        requestAction(PendingAction::NewTriggerTutorial);
        break;
    case 4:
    case 5:
    case 6:
        break;
    case 7:
        if (recentPath_.empty())
            status_ = "RECENT LIST IS EMPTY";
        else
            loadNative(recentPath_);
        break;
    case 8:
        requestAction(PendingAction::Recover);
        break;
    case 9:
        requestAction(PendingAction::Import);
        break;
    default:
        status_ = "__CLOSE__";
        break;
    }
}

void ScenarioEditor::requestAction(
    PendingAction action) {
    const bool replacement =
        action != PendingAction::SaveOverwrite;
    const std::string savePath =
        currentPath_.empty()
            ? paths_.userCreated
            : currentPath_;
    if ((replacement && dirty()) ||
        (action == PendingAction::SaveOverwrite &&
         pathExists(savePath))) {
        pendingAction_ = action;
        confirmationSelection_ =
            action == PendingAction::SaveOverwrite
                ? 0
                : 2;
        status_ =
            action == PendingAction::SaveOverwrite
                ? "CONFIRM OVERWRITE"
                : "UNSAVED CHANGES REQUIRE A DECISION";
        return;
    }
    pendingAction_ = action;
    performPendingAction();
}

void ScenarioEditor::performPendingAction() {
    const PendingAction action = pendingAction_;
    pendingAction_ = PendingAction::None;
    switch (action) {
    case PendingAction::NewBlank:
        createTemplate(ScenarioTemplate::BlankLand);
        break;
    case PendingAction::NewIslands:
        createTemplate(ScenarioTemplate::Islands);
        break;
    case PendingAction::NewSkirmishBase:
        createTemplate(ScenarioTemplate::SkirmishBase);
        break;
    case PendingAction::NewTriggerTutorial:
        createTemplate(
            ScenarioTemplate::TriggerTutorial);
        break;
    case PendingAction::Recover:
        recoverAutosave();
        break;
    case PendingAction::Import:
        importScx(joinScenarioPath(
            importDirectory_, "import.scx"));
        break;
    case PendingAction::SaveOverwrite:
        saveNative(false);
        break;
    default: break;
    }
}

void ScenarioEditor::updateConfirmation(
    const InputState &input) {
    const bool overwrite =
        pendingAction_ ==
        PendingAction::SaveOverwrite;
    const size_t count = overwrite ? 2 : 3;
    if (input.menuLeft || input.menuUp)
        confirmationSelection_ =
            (confirmationSelection_ + count - 1) %
            count;
    if (input.menuRight || input.menuDown)
        confirmationSelection_ =
            (confirmationSelection_ + 1) % count;
    if (input.menuBack) {
        pendingAction_ = PendingAction::None;
        status_ = "ACTION CANCELLED";
        return;
    }
    if (!input.menuActivate) return;
    if (overwrite) {
        if (confirmationSelection_ == 0) {
            pendingAction_ = PendingAction::None;
            status_ = "OVERWRITE CANCELLED";
        } else {
            performPendingAction();
        }
        return;
    }
    if (confirmationSelection_ == 0) {
        const PendingAction action = pendingAction_;
        pendingAction_ = PendingAction::None;
        if (!saveNative(false)) return;
        pendingAction_ = action;
        performPendingAction();
    } else if (confirmationSelection_ == 1) {
        performPendingAction();
    } else {
        pendingAction_ = PendingAction::None;
        status_ = "ACTION CANCELLED";
    }
}

void ScenarioEditor::updateWorkspace(
    float, const InputState &input) {
    if (input.actionTabLeft) {
        const size_t page =
            (size_t)page_ - (size_t)EditorPage::Map;
        page_ = (EditorPage)(
            (size_t)EditorPage::Map +
            (page + kPageCount - 1) % kPageCount);
        selection_ = 0;
    }
    if (input.actionTabRight) {
        const size_t page =
            (size_t)page_ - (size_t)EditorPage::Map;
        page_ = (EditorPage)(
            (size_t)EditorPage::Map +
            (page + 1) % kPageCount);
        selection_ = 0;
    }
    if (input.controlGroupAssign) {
        std::string error;
        if (undo_.undo(document_, &error)) {
            refreshValidation();
            status_ = "UNDO";
        } else {
            status_ = error.empty() ? "NOTHING TO UNDO"
                                    : "UNDO FAILED: " + error;
        }
        return;
    }
    if (input.toggleCheatMenu) {
        std::string error;
        if (undo_.redo(document_, &error)) {
            refreshValidation();
            status_ = "REDO";
        } else {
            status_ = error.empty() ? "NOTHING TO REDO"
                                    : "REDO FAILED: " + error;
        }
        return;
    }
    if (input.pausePressed) {
        refreshValidation();
        if (validation_.hasErrors()) {
            page_ = EditorPage::Validate;
            selection_ = 0;
            status_ =
                "PLAYTEST BLOCKED BY VALIDATION ERRORS";
        } else {
            if (saveNative(true))
                status_ = "__PLAYTEST__";
            else
                status_ =
                    "PLAYTEST BLOCKED: RECOVERY SAVE FAILED";
        }
        return;
    }
    switch (page_) {
    case EditorPage::Map: updateMap(input); break;
    case EditorPage::Objects: updateObjects(input); break;
    case EditorPage::Players: updatePlayers(input); break;
    case EditorPage::Triggers: updateTriggers(input); break;
    case EditorPage::Scenario: updateScenario(input); break;
    case EditorPage::Validate: updateValidation(input); break;
    case EditorPage::Files: updateFiles(input); break;
    default: break;
    }
}

bool ScenarioEditor::screenToTile(
    float x, float y, int &tileX,
    int &tileY) const {
    if (x < kMapX || y < kMapY ||
        x >= kMapX + kMapW ||
        y >= kMapY + kMapH ||
        document_.map.width == 0 ||
        document_.map.height == 0)
        return false;
    const float base =
        std::min(
            kMapW / document_.map.width,
            kMapH / document_.map.height) *
        mapZoom_;
    tileX = (int)((x - kMapX) / base + mapPanX_);
    tileY = (int)((y - kMapY) / base + mapPanY_);
    return tileX >= 0 && tileY >= 0 &&
           (uint32_t)tileX < document_.map.width &&
           (uint32_t)tileY < document_.map.height;
}

bool ScenarioEditor::applyBrushForTesting(
    int tileX, int tileY, EditorMapTool tool,
    uint8_t value, int size, bool circular,
    std::string *error) {
    if (document_.map.width == 0 ||
        document_.map.height == 0 ||
        document_.map.tiles.size() !=
            (size_t)document_.map.width *
                document_.map.height) {
        if (error) *error = "document map is invalid";
        return false;
    }
    if (tileX < 0 || tileY < 0 ||
        (uint32_t)tileX >= document_.map.width ||
        (uint32_t)tileY >= document_.map.height) {
        if (error) *error = "brush is outside the map";
        return false;
    }
    const auto tileAt = [&](int x, int y)
        -> ScenarioTile & {
        return document_.map.tiles[
            (size_t)y * document_.map.width + x];
    };
    if (tool == EditorMapTool::Fill) {
        const uint8_t source =
            tileAt(tileX, tileY).terrain;
        if (source == value) return true;
        std::vector<std::pair<int, int>> pending{
            {tileX, tileY}};
        tileAt(tileX, tileY).terrain = value;
        for (size_t index = 0;
             index < pending.size(); ++index) {
            const auto current = pending[index];
            static constexpr int dx[] = {1, -1, 0, 0};
            static constexpr int dy[] = {0, 0, 1, -1};
            for (int side = 0; side < 4; ++side) {
                const int x = current.first + dx[side];
                const int y = current.second + dy[side];
                if (x < 0 || y < 0 ||
                    (uint32_t)x >= document_.map.width ||
                    (uint32_t)y >= document_.map.height ||
                    tileAt(x, y).terrain != source)
                    continue;
                tileAt(x, y).terrain = value;
                pending.push_back({x, y});
            }
        }
        return true;
    }
    if (tool == EditorMapTool::Replace) {
        const uint8_t source =
            tileAt(tileX, tileY).terrain;
        for (ScenarioTile &tile : document_.map.tiles)
            if (tile.terrain == source)
                tile.terrain = value;
        return true;
    }
    size = std::clamp(size, 1, 9);
    const int radius = size / 2;
    for (int y = tileY - radius;
         y <= tileY + radius; ++y)
        for (int x = tileX - radius;
             x <= tileX + radius; ++x) {
            if (x < 0 || y < 0 ||
                (uint32_t)x >= document_.map.width ||
                (uint32_t)y >= document_.map.height)
                continue;
            if (circular &&
                (x - tileX) * (x - tileX) +
                        (y - tileY) * (y - tileY) >
                    radius * radius + radius)
                continue;
            ScenarioTile &tile = tileAt(x, y);
            if (tool == EditorMapTool::Elevation ||
                tool == EditorMapTool::Cliff)
                tile.elevation =
                    clampByte(value, 7);
            else if (tool == EditorMapTool::Water)
                tile.terrain = 2;
            else if (tool ==
                     EditorMapTool::Foundation)
                tile.terrain = value;
            else
                tile.terrain = value;
        }
    return true;
}

void ScenarioEditor::updateMap(
    const InputState &input) {
    if (input.menuBack) {
        page_ = EditorPage::Hub;
        selection_ = 0;
        status_ =
            undo_.dirty()
                ? "UNSAVED CHANGES RETAINED IN EDITOR"
                : "EDITOR DOCUMENT RETAINED";
        return;
    }
    if (input.menuUp)
        mapTool_ = (EditorMapTool)(
            ((int)mapTool_ + 9) % 10);
    if (input.menuDown)
        mapTool_ = (EditorMapTool)(
            ((int)mapTool_ + 1) % 10);
    if (input.menuLeft) {
        if (mapTool_ == EditorMapTool::Terrain ||
            mapTool_ == EditorMapTool::Water ||
            mapTool_ == EditorMapTool::Foundation ||
            mapTool_ == EditorMapTool::Fill ||
            mapTool_ == EditorMapTool::Replace)
            brushValue_--;
        else
            brushValue_ = clampByte(
                (int)brushValue_ - 1, 7);
    }
    if (input.menuRight) {
        if (mapTool_ == EditorMapTool::Terrain ||
            mapTool_ == EditorMapTool::Water ||
            mapTool_ == EditorMapTool::Foundation ||
            mapTool_ == EditorMapTool::Fill ||
            mapTool_ == EditorMapTool::Replace)
            brushValue_++;
        else
            brushValue_ = clampByte(
                (int)brushValue_ + 1, 7);
    }
    if (input.cycleAttackMode)
        circularBrush_ = !circularBrush_;
    if (input.commandPressed)
        brushSize_ = brushSize_ == 9
                         ? 1
                         : brushSize_ + 2;
    if (input.dragX != 0 || input.dragY != 0) {
        const float tilePixels =
            std::min(
                kMapW / document_.map.width,
                kMapH / document_.map.height) *
            mapZoom_;
        mapPanX_ = std::clamp(
            mapPanX_ - input.dragX / tilePixels,
            0.0f,
            std::max(
                0.0f,
                document_.map.width -
                    kMapW / tilePixels));
        mapPanY_ = std::clamp(
            mapPanY_ - input.dragY / tilePixels,
            0.0f,
            std::max(
                0.0f,
                document_.map.height -
                    kMapH / tilePixels));
    }
    if (input.zoomStep != 0 &&
        !input.actionTabLeft &&
        !input.actionTabRight)
        mapZoom_ = std::clamp(
            mapZoom_ *
                (input.zoomStep > 0 ? 1.25f : 0.8f),
            0.5f, 4.0f);
    int x = 0, y = 0;
    if (!(input.pointerTap || input.menuActivate) ||
        !screenToTile(
            input.pointerX, input.pointerY, x, y))
        return;
    EditableScenarioDocument before = document_;
    if (mapTool_ == EditorMapTool::Camera) {
        document_.camera.x = x + 0.5f;
        document_.camera.y = y + 0.5f;
        markChanged(before, "CAMERA MARKER MOVED");
        return;
    }
    if (mapTool_ == EditorMapTool::PlayerStart) {
        if (document_.players.empty()) {
            status_ = "NO ACTIVE PLAYER";
            return;
        }
        EditorPlayer &player =
            document_.players[
                selectedPlayer_ %
                document_.players.size()];
        player.scenario.cameraX = x + 0.5f;
        player.scenario.cameraY = y + 0.5f;
        markChanged(before, "PLAYER START/CAMERA MOVED");
        return;
    }
    if (mapTool_ == EditorMapTool::Area) {
        EditorArea area;
        area.id = document_.areas.empty()
                      ? 1
                      : document_.areas.back().id + 1;
        area.name =
            "Area " + std::to_string(area.id);
        const int radius = brushSize_ / 2;
        area.left = std::max(0, x - radius);
        area.top = std::max(0, y - radius);
        area.right = std::min(
            (int)document_.map.width - 1,
            x + radius);
        area.bottom = std::min(
            (int)document_.map.height - 1,
            y + radius);
        document_.areas.push_back(area);
        markChanged(before, "AREA CREATED");
        return;
    }
    std::string error;
    if (!applyBrushForTesting(
            x, y, mapTool_, brushValue_,
            brushSize_, circularBrush_, &error)) {
        status_ = "BRUSH FAILED: " + error;
        return;
    }
    markChanged(before, "MAP UPDATED");
}

const dat::Unit *ScenarioEditor::unitDefinition(
    uint16_t id, uint8_t player) const {
    const dat::DatFile &data = assets_.dat();
    size_t civilization = 0;
    if (player > 0 &&
        player <= document_.players.size())
        civilization =
            document_.players[player - 1]
                .scenario.civilization;
    if (civilization >= data.civs.size())
        civilization =
            data.civs.size() > 1 ? 1 : 0;
    if (civilization >= data.civs.size() ||
        id >= data.civs[civilization].units.size())
        return nullptr;
    const dat::Unit &unit =
        data.civs[civilization].units[id];
    return unit.exists ? &unit : nullptr;
}

std::string ScenarioEditor::objectName(
    uint16_t id) const {
    const dat::Unit *unit =
        unitDefinition(id, objectPlayer_);
    if (!unit) return "INVALID UNIT " + std::to_string(id);
    const std::string &localized =
        assets_.localizedString(
            unit->languageDllName);
    return localized.empty()
               ? !unit->name2.empty()
                     ? unit->name2
                     : unit->name
               : localized;
}

uint16_t ScenarioEditor::nextPlaceableUnit(
    uint16_t current, int direction) const {
    constexpr int maximum = 32767;
    int candidate = current;
    for (int checked = 0; checked < maximum;
         ++checked) {
        candidate =
            (candidate + maximum +
             (direction < 0 ? -1 : 1)) %
            maximum;
        const dat::Unit *unit =
            unitDefinition(
                (uint16_t)candidate,
                objectPlayer_);
        if (unit && !unit->hideInEditor &&
            unit->standingGraphic[0] >= 0)
            return (uint16_t)candidate;
    }
    return current;
}

bool ScenarioEditor::objectFits(
    const dat::Unit &unit, float x, float y,
    uint32_t ignoreSpawnId,
    std::string *error) const {
    if (!finite(x) || !finite(y)) {
        if (error) *error = "object transform is not finite";
        return false;
    }
    const float width =
        std::max(
            0.2f,
            std::max(
                std::fabs(unit.clearanceSize[0]),
                std::fabs(unit.collisionSize[0])));
    const float height =
        std::max(
            0.2f,
            std::max(
                std::fabs(unit.clearanceSize[1]),
                std::fabs(unit.collisionSize[1])));
    if (x - width * 0.5f < 0 ||
        y - height * 0.5f < 0 ||
        x + width * 0.5f >= document_.map.width ||
        y + height * 0.5f >= document_.map.height) {
        if (error) *error = "object footprint crosses map edge";
        return false;
    }
    for (const EditorObject &other : document_.objects) {
        if (other.scenario.spawnId == ignoreSpawnId)
            continue;
        const dat::Unit *otherUnit =
            unitDefinition(
                other.scenario.unitId,
                other.scenario.player);
        if (!otherUnit) continue;
        const float otherWidth =
            std::max(
                0.2f,
                std::max(
                    std::fabs(otherUnit->clearanceSize[0]),
                    std::fabs(otherUnit->collisionSize[0])));
        const float otherHeight =
            std::max(
                0.2f,
                std::max(
                    std::fabs(otherUnit->clearanceSize[1]),
                    std::fabs(otherUnit->collisionSize[1])));
        if (std::fabs(x - other.scenario.x) <
                (width + otherWidth) * 0.5f &&
            std::fabs(y - other.scenario.y) <
                (height + otherHeight) * 0.5f) {
            if (error) *error = "object footprint overlaps another object";
            return false;
        }
    }
    return true;
}

bool ScenarioEditor::placeObjectForTesting(
    uint16_t unitId, uint8_t player,
    float x, float y, std::string *error) {
    const dat::Unit *unit =
        unitDefinition(unitId, player);
    if (!unit || unit->hideInEditor) {
        if (error)
            *error = "unit ID is invalid or hidden from the editor";
        return false;
    }
    if (!objectFits(*unit, x, y, 0, error))
        return false;
    uint32_t nextId = 1;
    for (const EditorObject &object : document_.objects)
        nextId = std::max(
            nextId, object.scenario.spawnId + 1);
    EditorObject object;
    object.scenario.spawnId = nextId;
    object.scenario.unitId = unitId;
    object.scenario.player = player;
    object.scenario.x = x;
    object.scenario.y = y;
    object.scenario.z = 0;
    object.hitPoints =
        unit->hitPoints > 0
            ? (float)unit->hitPoints
            : -1.0f;
    document_.objects.push_back(std::move(object));
    selectedObject_ = document_.objects.size() - 1;
    return true;
}

void ScenarioEditor::updateObjects(
    const InputState &input) {
    if (input.menuBack) {
        page_ = EditorPage::Map;
        return;
    }
    if (input.menuLeft)
        paletteUnit_ =
            nextPlaceableUnit(paletteUnit_, -1);
    if (input.menuRight)
        paletteUnit_ =
            nextPlaceableUnit(paletteUnit_, 1);
    if (input.menuUp)
        objectPlayer_ =
            (uint8_t)(objectPlayer_ <= 1
                          ? std::max<size_t>(
                                1,
                                document_.players.size())
                          : objectPlayer_ - 1);
    if (input.menuDown)
        objectPlayer_ =
            (uint8_t)(objectPlayer_ %
                          std::max<size_t>(
                              1,
                              document_.players.size()) +
                      1);
    if (input.cycleAttackMode &&
        !document_.objects.empty()) {
        EditableScenarioDocument before = document_;
        selectedObject_ = std::min(
            selectedObject_,
            document_.objects.size() - 1);
        document_.objects.erase(
            document_.objects.begin() +
            (ptrdiff_t)selectedObject_);
        if (selectedObject_ >= document_.objects.size() &&
            selectedObject_ > 0)
            --selectedObject_;
        markChanged(before, "OBJECT DELETED");
        return;
    }
    int x = 0, y = 0;
    if (!(input.pointerTap || input.menuActivate) ||
        !screenToTile(
            input.pointerX, input.pointerY, x, y))
        return;
    EditableScenarioDocument before = document_;
    size_t nearest = document_.objects.size();
    float nearestDistance = 1.2f;
    for (size_t index = 0;
         index < document_.objects.size(); ++index) {
        const EditorObject &object =
            document_.objects[index];
        const float dx = object.scenario.x - x - 0.5f;
        const float dy = object.scenario.y - y - 0.5f;
        const float distance =
            std::sqrt(dx * dx + dy * dy);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearest = index;
        }
    }
    if (nearest < document_.objects.size()) {
        selectedObject_ = nearest;
        status_ =
            "SELECTED " +
            objectName(
                document_.objects[nearest]
                    .scenario.unitId);
        return;
    }
    std::string error;
    if (!placeObjectForTesting(
            paletteUnit_, objectPlayer_,
            x + 0.5f, y + 0.5f, &error)) {
        status_ = "PLACEMENT BLOCKED: " + error;
        return;
    }
    markChanged(before, "OBJECT PLACED");
}

void ScenarioEditor::updatePlayers(
    const InputState &input) {
    if (document_.players.empty()) return;
    if (playerTechnologyPanel_) {
        constexpr size_t rows = 5;
        if (input.menuBack) {
            playerTechnologyPanel_ = false;
            selection_ = 12;
            status_ = "PLAYER SETTINGS";
            return;
        }
        if (input.menuUp)
            selection_ =
                (selection_ + rows - 1) % rows;
        if (input.menuDown)
            selection_ =
                (selection_ + 1) % rows;
        const int direction =
            input.menuLeft ? -1
            : (input.menuRight ||
               input.menuActivate)
                ? 1
                : 0;
        if (!direction) return;
        EditorPlayer &player =
            document_.players[selectedPlayer_];
        if (selection_ == 0) {
            playerTechnologyKind_ =
                (uint8_t)(
                    (playerTechnologyKind_ + 2 +
                     (direction < 0 ? 1 : 1)) %
                    2);
            playerTechnologyId_ = 0;
            if (playerTechnologyKind_ == 1)
                for (uint32_t id = 0;
                     id < 32767; ++id)
                    if (unitDefinition(
                            (uint16_t)id,
                            (uint8_t)selectedPlayer_ +
                                1)) {
                        playerTechnologyId_ = id;
                        break;
                    }
            status_ = "FILTER CHANGED";
            return;
        }
        if (selection_ == 1) {
            if (playerTechnologyKind_ == 0) {
                const size_t count =
                    assets_.dat().techs.size();
                if (count)
                    playerTechnologyId_ =
                        (uint32_t)(
                            (playerTechnologyId_ +
                             count +
                             (direction < 0
                                  ? count - 1
                                  : 1)) %
                            count);
            } else {
                uint16_t candidate =
                    (uint16_t)playerTechnologyId_;
                for (size_t checked = 0;
                     checked < 32767; ++checked) {
                    candidate =
                        (uint16_t)(
                            (candidate + 32767 +
                             (direction < 0
                                  ? 32766
                                  : 1)) %
                            32767);
                    if (unitDefinition(
                            candidate,
                            (uint8_t)selectedPlayer_ +
                                1)) {
                        playerTechnologyId_ =
                            candidate;
                        break;
                    }
                }
            }
            status_ = "ITEM SELECTED";
            return;
        }
        if (selection_ == 4) {
            playerTechnologyPanel_ = false;
            selection_ = 12;
            return;
        }
        EditableScenarioDocument before =
            document_;
        auto toggle =
            [](std::vector<uint32_t> &values,
               uint32_t id) {
                const auto found = std::find(
                    values.begin(), values.end(),
                    id);
                if (found == values.end())
                    values.push_back(id);
                else
                    values.erase(found);
            };
        const auto erase =
            [](std::vector<uint32_t> &values,
               uint32_t id) {
                values.erase(
                    std::remove(
                        values.begin(), values.end(),
                        id),
                    values.end());
            };
        if (playerTechnologyKind_ == 0) {
            if (selection_ == 2) {
                erase(
                    player.researchedTechnologies,
                    playerTechnologyId_);
                toggle(
                    player.scenario
                        .disabledTechnologies,
                    playerTechnologyId_);
            } else {
                erase(
                    player.scenario
                        .disabledTechnologies,
                    playerTechnologyId_);
                toggle(
                    player.researchedTechnologies,
                    playerTechnologyId_);
            }
        } else {
            const dat::Unit *unit =
                unitDefinition(
                    (uint16_t)playerTechnologyId_,
                    (uint8_t)selectedPlayer_ + 1);
            const bool building =
                unit &&
                unit->type == dat::UT_Building;
            std::vector<uint32_t> &disabled =
                building
                    ? player.scenario
                          .disabledBuildings
                    : player.scenario
                          .disabledUnits;
            std::vector<uint32_t> &enabled =
                building
                    ? player.researchedBuildings
                    : player.researchedUnits;
            if (selection_ == 2) {
                erase(
                    enabled,
                    playerTechnologyId_);
                toggle(
                    disabled,
                    playerTechnologyId_);
            } else {
                erase(
                    disabled,
                    playerTechnologyId_);
                toggle(
                    enabled,
                    playerTechnologyId_);
            }
        }
        markChanged(
            before,
            selection_ == 2
                ? "AVAILABILITY UPDATED"
                : "RESEARCH STATE UPDATED");
        return;
    }
    if (input.menuBack) {
        page_ = EditorPage::Map;
        return;
    }
    constexpr size_t rows = 13;
    if (input.actionTabLeft || input.actionTabRight)
        return;
    if (input.menuUp)
        selection_ = (selection_ + rows - 1) % rows;
    if (input.menuDown)
        selection_ = (selection_ + 1) % rows;
    int direction = 0;
    if (input.menuLeft) direction = -1;
    if (input.menuRight || input.menuActivate) direction = 1;
    if (!direction) return;
    EditableScenarioDocument before = document_;
    if (selection_ == 0) {
        selectedPlayer_ =
            (selectedPlayer_ +
             document_.players.size() +
             (direction < 0
                  ? document_.players.size() - 1
                  : 1)) %
            document_.players.size();
        status_ = "PLAYER SELECTED";
        return;
    }
    EditorPlayer &player =
        document_.players[selectedPlayer_];
    switch (selection_) {
    case 1:
        player.scenario.civilization =
            (uint32_t)std::clamp(
                (int)player.scenario.civilization +
                    direction,
                1,
                std::max(
                    1,
                    (int)assets_.dat().civs.size() - 1));
        break;
    case 2:
        player.scenario.color =
            (player.scenario.color + 8 +
             (direction < 0 ? 7 : 1)) %
            8;
        break;
    case 3:
        player.team =
            std::clamp(
                player.team + direction, 0, 8);
        break;
    case 4:
        player.scenario.resources[0] =
            std::max(
                0.0f,
                player.scenario.resources[0] +
                    direction * 100.0f);
        break;
    case 5:
        player.scenario.resources[1] =
            std::max(
                0.0f,
                player.scenario.resources[1] +
                    direction * 100.0f);
        break;
    case 6:
        player.scenario.populationLimit =
            std::clamp(
                player.scenario.populationLimit +
                    direction * 25.0f,
                1.0f, 500.0f);
        break;
    case 7:
        player.scenario.startingAge =
            std::clamp(
                player.scenario.startingAge +
                    direction,
                0, 3);
        break;
    case 8:
        player.difficulty =
            std::clamp(
                player.difficulty + direction,
                0, 4);
        break;
    case 9:
        player.scenario.alliedVictory =
            !player.scenario.alliedVictory;
        break;
    case 10:
        selectedDiplomacyTarget_ =
            (selectedDiplomacyTarget_ +
             document_.players.size() +
             (direction < 0
                  ? document_.players.size() - 1
                  : 1)) %
            document_.players.size();
        status_ = "DIPLOMACY TARGET SELECTED";
        return;
    case 11: {
        const size_t target =
            std::min(
                selectedDiplomacyTarget_,
                document_.players.size() - 1);
        uint32_t &stance =
            player.scenario.diplomacy[target + 1];
        stance =
            (stance + 4 +
             (direction < 0 ? 3 : 1)) %
            4;
        break;
    }
    case 12:
        playerTechnologyPanel_ = true;
        selection_ = 0;
        status_ = "TECHNOLOGY / UNIT STATES";
        return;
    default: return;
    }
    markChanged(before, "PLAYER SETTINGS UPDATED");
}

std::string ScenarioEditor::triggerSummary(
    const EditorTrigger &trigger) const {
    return trigger.name.empty()
               ? "Unnamed trigger"
               : trigger.name;
}

void ScenarioEditor::updateTriggers(
    const InputState &input) {
    if (input.menuBack) {
        page_ = EditorPage::Map;
        return;
    }
    if (document_.triggers.empty()) {
        if (input.menuActivate ||
            input.cycleAttackMode) {
            EditableScenarioDocument before = document_;
            EditorTrigger trigger;
            trigger.name = "New Trigger";
            trigger.enabled = true;
            document_.triggers.push_back(trigger);
            document_.triggerOrder.push_back(0);
            selectedTrigger_ = 0;
            markChanged(before, "TRIGGER CREATED");
        }
        return;
    }
    selectedTrigger_ =
        std::min(
            selectedTrigger_,
            document_.triggers.size() - 1);
    constexpr size_t rows = 8;
    if (input.menuUp)
        selection_ = (selection_ + rows - 1) % rows;
    if (input.menuDown)
        selection_ = (selection_ + 1) % rows;
    if (input.cycleAttackMode) {
        EditableScenarioDocument before = document_;
        EditorTrigger copy =
            document_.triggers[selectedTrigger_];
        copy.name += " Copy";
        const uint32_t newIndex =
            (uint32_t)document_.triggers.size();
        for (EditorTriggerEffect &effect :
             copy.effects)
            if ((effect.scenario.type == 8 ||
                 effect.scenario.type == 9) &&
                effect.scenario.fields.size() > 13 &&
                effect.scenario.fields[13] ==
                    (int32_t)selectedTrigger_)
                effect.scenario.fields[13] =
                    (int32_t)newIndex;
        document_.triggers.push_back(
            std::move(copy));
        auto ordered = std::find(
            document_.triggerOrder.begin(),
            document_.triggerOrder.end(),
            (uint32_t)selectedTrigger_);
        document_.triggerOrder.insert(
            ordered == document_.triggerOrder.end()
                ? document_.triggerOrder.end()
                : ordered + 1,
            newIndex);
        selectedTrigger_ = newIndex;
        markChanged(before, "TRIGGER DUPLICATED");
        return;
    }
    int direction =
        input.menuLeft ? -1
        : (input.menuRight || input.menuActivate) ? 1
                                                 : 0;
    if (!direction) return;
    EditableScenarioDocument before = document_;
    EditorTrigger &trigger =
        document_.triggers[selectedTrigger_];
    switch (selection_) {
    case 0: {
        if (document_.triggerOrder.empty()) {
            selectedTrigger_ =
                (selectedTrigger_ +
                 document_.triggers.size() +
                 (direction < 0
                      ? document_.triggers.size() -
                            1
                      : 1)) %
                document_.triggers.size();
            status_ = "TRIGGER SELECTED";
            return;
        }
        auto ordered = std::find(
            document_.triggerOrder.begin(),
            document_.triggerOrder.end(),
            (uint32_t)selectedTrigger_);
        size_t position =
            ordered == document_.triggerOrder.end()
                ? 0
                : (size_t)(ordered -
                           document_.triggerOrder.begin());
        position =
            (position +
             document_.triggerOrder.size() +
             (direction < 0
                  ? document_.triggerOrder.size() - 1
                  : 1)) %
            document_.triggerOrder.size();
        selectedTrigger_ =
            document_.triggerOrder[position];
        status_ = "TRIGGER SELECTED";
        return;
    }
    case 1:
        trigger.enabled = !trigger.enabled;
        break;
    case 2:
        trigger.looping = !trigger.looping;
        break;
    case 3:
        trigger.objective = !trigger.objective;
        break;
    case 4: {
        EditorTriggerCondition condition;
        condition.scenario.type = 10;
        condition.scenario.fields.assign(16, -1);
        condition.scenario.fields[7] = 1;
        trigger.conditions.push_back(condition);
        trigger.conditionOrder.push_back(
            (int32_t)trigger.conditions.size() - 1);
        break;
    }
    case 5: {
        EditorTriggerEffect effect;
        effect.scenario.type = 3;
        effect.scenario.fields.assign(23, -1);
        effect.scenario.message =
            "Objective updated";
        trigger.effects.push_back(effect);
        trigger.effectOrder.push_back(
            (int32_t)trigger.effects.size() - 1);
        break;
    }
    case 6: {
        auto ordered = std::find(
            document_.triggerOrder.begin(),
            document_.triggerOrder.end(),
            (uint32_t)selectedTrigger_);
        if (ordered != document_.triggerOrder.end() &&
            ordered != document_.triggerOrder.begin())
            std::iter_swap(ordered, ordered - 1);
        break;
    }
    case 7:
        if (document_.triggers.size() > 1) {
            bool referenced = false;
            for (size_t candidateIndex = 0;
                 candidateIndex <
                 document_.triggers.size();
                 ++candidateIndex) {
                if (candidateIndex ==
                    selectedTrigger_)
                    continue;
                const EditorTrigger &candidate =
                    document_.triggers[
                        candidateIndex];
                for (const EditorTriggerEffect &effect :
                     candidate.effects)
                    if ((effect.scenario.type == 8 ||
                         effect.scenario.type == 9) &&
                        effect.scenario.fields.size() >
                            13 &&
                        effect.scenario.fields[13] ==
                            (int32_t)selectedTrigger_)
                        referenced = true;
            }
            if (referenced) {
                status_ =
                    "DELETE BLOCKED: TRIGGER IS REFERENCED";
                return;
            }
            document_.triggers.erase(
                document_.triggers.begin() +
                (ptrdiff_t)selectedTrigger_);
            document_.triggerOrder.erase(
                std::remove(
                    document_.triggerOrder.begin(),
                    document_.triggerOrder.end(),
                    (uint32_t)selectedTrigger_),
                document_.triggerOrder.end());
            for (uint32_t &index :
                 document_.triggerOrder)
                if (index > selectedTrigger_) --index;
            for (EditorTrigger &candidate :
                 document_.triggers)
                for (EditorTriggerEffect &effect :
                     candidate.effects)
                    if ((effect.scenario.type == 8 ||
                         effect.scenario.type == 9) &&
                        effect.scenario.fields.size() >
                            13 &&
                        effect.scenario.fields[13] >
                            (int32_t)selectedTrigger_)
                        --effect.scenario.fields[13];
            selectedTrigger_ = std::min(
                selectedTrigger_,
                document_.triggers.size() - 1);
        }
        break;
    default: return;
    }
    markChanged(before, "TRIGGER UPDATED");
}

void ScenarioEditor::updateScenario(
    const InputState &input) {
    if (input.menuBack) {
        page_ = EditorPage::Map;
        return;
    }
    constexpr size_t rows = 9;
    if (input.menuUp)
        selection_ = (selection_ + rows - 1) % rows;
    if (input.menuDown)
        selection_ = (selection_ + 1) % rows;
    int direction =
        input.menuLeft ? -1
        : (input.menuRight || input.menuActivate) ? 1
                                                 : 0;
    if (!direction) return;
    EditableScenarioDocument before = document_;
    switch (selection_) {
    case 0:
        document_.victory.type =
            (document_.victory.type + 6 +
             (direction < 0 ? 5 : 1)) %
            6;
        break;
    case 1:
        document_.victory.scenario.conquestRequired =
            !document_.victory.scenario.conquestRequired;
        break;
    case 2:
        document_.victory.scenario.requiredHolocrons =
            (uint32_t)std::max(
                0,
                (int)document_.victory.scenario
                        .requiredHolocrons +
                    direction);
        break;
    case 3:
        document_.victory.scenario.requiredScore =
            (uint32_t)std::max(
                0,
                (int)document_.victory.scenario
                        .requiredScore +
                    direction * 100);
        break;
    case 4:
        document_.victory.scenario.timeLimit =
            (uint32_t)std::max(
                0,
                (int)document_.victory.scenario
                        .timeLimit +
                    direction * 60);
        break;
    case 5:
        document_.camera.x = std::clamp(
            document_.camera.x + direction,
            0.0f,
            (float)document_.map.width - 1);
        break;
    case 6:
        document_.camera.y = std::clamp(
            document_.camera.y + direction,
            0.0f,
            (float)document_.map.height - 1);
        break;
    case 7:
        document_.messages.instructions =
            document_.messages.instructions.empty()
                ? "Complete the scenario objectives."
                : "";
        break;
    case 8:
        document_.messages.objectives =
            document_.messages.objectives.empty()
                ? "Defeat the hostile force."
                : "";
        break;
    default: return;
    }
    markChanged(before, "SCENARIO SETTINGS UPDATED");
}

void ScenarioEditor::updateValidation(
    const InputState &input) {
    if (input.menuBack) {
        page_ = EditorPage::Map;
        return;
    }
    if (input.menuActivate) {
        refreshValidation();
        status_ =
            validation_.hasErrors()
                ? "VALIDATION FOUND ERRORS"
                : "VALIDATION PASSED";
    }
    if (validation_.issues.empty()) return;
    if (input.menuUp)
        selectedIssue_ =
            (selectedIssue_ +
             validation_.issues.size() - 1) %
            validation_.issues.size();
    if (input.menuDown)
        selectedIssue_ =
            (selectedIssue_ + 1) %
            validation_.issues.size();
    if (input.menuRight) {
        const ValidationFocus &focus =
            validation_.issues[selectedIssue_].focus;
        if (focus.kind == ValidationFocusKind::Map ||
            focus.kind == ValidationFocusKind::Tile ||
            focus.kind == ValidationFocusKind::Area)
            page_ = EditorPage::Map;
        else if (
            focus.kind == ValidationFocusKind::Object)
            page_ = EditorPage::Objects;
        else if (
            focus.kind == ValidationFocusKind::Player)
            page_ = EditorPage::Players;
        else if (
            focus.kind == ValidationFocusKind::Trigger ||
            focus.kind ==
                ValidationFocusKind::TriggerCondition ||
            focus.kind ==
                ValidationFocusKind::TriggerEffect)
            page_ = EditorPage::Triggers;
        status_ = "JUMPED TO " + focus.text;
    }
}

void ScenarioEditor::updateFiles(
    const InputState &input) {
    if (input.menuBack) {
        page_ = EditorPage::Map;
        return;
    }
    constexpr size_t rows = 7;
    if (input.menuUp)
        selection_ = (selection_ + rows - 1) % rows;
    if (input.menuDown)
        selection_ = (selection_ + 1) % rows;
    if (!input.menuActivate) return;
    switch (selection_) {
    case 0:
        requestAction(PendingAction::SaveOverwrite);
        break;
    case 1: saveNative(true); break;
    case 2:
        requestAction(PendingAction::Recover);
        break;
    case 3: exportScx(); break;
    case 4:
        requestAction(PendingAction::Import);
        break;
    case 5:
        refreshValidation();
        if (validation_.hasErrors()) {
            page_ = EditorPage::Validate;
            status_ =
                "PLAYTEST BLOCKED BY VALIDATION ERRORS";
        } else {
            if (saveNative(true))
                status_ = "__PLAYTEST__";
            else
                status_ =
                    "PLAYTEST BLOCKED: RECOVERY SAVE FAILED";
        }
        break;
    default:
        page_ = EditorPage::Hub;
        selection_ = 0;
        break;
    }
}

bool ScenarioEditor::buildPlaytestScenario(
    Scenario &scenario, std::string *error) const {
    const ValidationReport report =
        validateEditableScenario(document_, assets_);
    if (report.hasErrors()) {
        if (error)
            *error =
                "scenario has validation errors; open Validate";
        return false;
    }
    scenario = {};
    scenario.version =
        document_.scx.version.empty()
            ? "1.21"
            : document_.scx.version;
    scenario.saveType = document_.scx.saveType;
    scenario.lastSaveTime =
        document_.scx.lastSaveTime;
    scenario.instructions =
        document_.messages.instructions;
    scenario.victoryType = document_.victory.type;
    scenario.enabledPlayerCount =
        (uint32_t)document_.players.size();
    scenario.nextUnitId = 1;
    scenario.playerDataVersion =
        document_.scx.playerDataVersion > 0
            ? document_.scx.playerDataVersion
            : 1.24f;
    scenario.originalFilename =
        document_.metadata.title;
    scenario.hints = document_.messages.hints;
    scenario.victoryMessage =
        document_.messages.victory;
    scenario.lossMessage =
        document_.messages.loss;
    scenario.history = document_.messages.history;
    scenario.scouts = document_.messages.scouts;
    scenario.pregameCinematic =
        document_.messages.pregameCinematic;
    scenario.victoryCinematic =
        document_.messages.victoryCinematic;
    scenario.lossCinematic =
        document_.messages.lossCinematic;
    scenario.background =
        document_.messages.background;
    scenario.cameraX = document_.camera.x;
    scenario.cameraY = document_.camera.y;
    scenario.mapCameraX = document_.camera.mapX;
    scenario.mapCameraY = document_.camera.mapY;
    scenario.victory = document_.victory.scenario;
    scenario.allTechnologies =
        document_.scx.allTechnologies;
    scenario.map = document_.map;
    for (size_t index = 0;
         index < document_.players.size() &&
         index < 8;
         ++index) {
        scenario.players[index] =
            document_.players[index].scenario;
        scenario.players[index]
            .researchedTechnologies =
            document_.players[index]
                .researchedTechnologies;
        scenario.players[index].researchedUnits =
            document_.players[index].researchedUnits;
        scenario.players[index].researchedBuildings =
            document_.players[index]
                .researchedBuildings;
        scenario.civilizations[index] =
            scenario.players[index].civilization;
    }
    for (size_t source = 0;
         source < document_.players.size();
         ++source)
        for (size_t target = 0;
             target < document_.players.size();
             ++target) {
            const int left =
                document_.players[source].team;
            const int right =
                document_.players[target].team;
            if (source == target ||
                (left > 0 && left == right))
                scenario.players[source]
                    .diplomacy[target + 1] = 0u;
        }
    for (const EditorObject &object :
         document_.objects) {
        scenario.units.push_back(object.scenario);
        scenario.nextUnitId = std::max(
            scenario.nextUnitId,
            object.scenario.spawnId + 1);
    }
    for (const EditorTrigger &source :
         document_.triggers) {
        ScenarioTrigger trigger;
        trigger.enabled = source.enabled;
        trigger.looping = source.looping;
        trigger.objective = source.objective;
        trigger.objectiveOrder =
            source.objectiveOrder;
        trigger.objectiveStringId =
            source.objectiveStringId;
        trigger.description = source.description;
        trigger.name = source.name;
        for (const EditorTriggerEffect &effect :
             source.effects)
            trigger.effects.push_back(
                effect.scenario);
        trigger.effectOrder = source.effectOrder;
        for (const EditorTriggerCondition &condition :
             source.conditions)
            trigger.conditions.push_back(
                condition.scenario);
        trigger.conditionOrder =
            source.conditionOrder;
        scenario.triggers.push_back(
            std::move(trigger));
    }
    scenario.triggerOrder = document_.triggerOrder;
    scenario.triggerSystemVersion =
        document_.scx.triggerSystemVersion > 0
            ? document_.scx.triggerSystemVersion
            : 1.6;
    scenario.objectiveState =
        document_.scx.objectiveState;
    return true;
}

int ScenarioEditor::playtestDifficulty() const {
    for (const EditorPlayer &player :
         document_.players)
        if (player.scenario.active &&
            !player.scenario.human)
            return std::clamp(
                player.difficulty, 0, 4);
    return 2;
}

void ScenarioEditor::playtestFinished(
    bool completed,
    const std::string &diagnostic) {
    playtestDiagnostic_ =
        diagnostic.empty()
            ? completed
                  ? "PLAYTEST COMPLETED"
                  : "PLAYTEST RETURNED"
            : diagnostic;
    status_ = playtestDiagnostic_;
    page_ = EditorPage::Validate;
    refreshValidation();
}

void ScenarioEditor::render(
    Renderer &renderer, int screenWidth,
    int screenHeight) const {
    renderer.beginFrame(
        screenWidth, screenHeight, 1.0f,
        2, 6, 15);
    renderer.fillRect(
        0, 0, (float)screenWidth,
        (float)screenHeight,
        2, 6, 15, 255);
    renderer.fillRect(
        0, 0, (float)screenWidth, 60,
        8, 24, 43, 255);
    renderer.fillRect(
        0, 58, (float)screenWidth, 2,
        202, 168, 74, 255);
    if (page_ == EditorPage::Hub)
        renderHub(
            renderer, screenWidth, screenHeight);
    else
        renderWorkspace(
            renderer, screenWidth, screenHeight);
    if (pendingAction_ != PendingAction::None)
        renderConfirmation(renderer);
    renderer.endFrame();
}

void ScenarioEditor::renderConfirmation(
    Renderer &renderer) const {
    renderer.fillRect(
        0, 0, 960, 544, 0, 0, 0, 190);
    drawModernPanel(
        renderer, 206, 154, 548, 236);
    const bool overwrite =
        pendingAction_ ==
        PendingAction::SaveOverwrite;
    drawUiText(
        renderer,
        {overwrite ? "OVERWRITE EXISTING SCENARIO?"
                   : "UNSAVED CHANGES"},
        234, 179, 1.18f, 235, 213, 145);
    drawUiText(
        renderer,
        {overwrite
             ? "The existing native file will be atomically replaced."
             : "Save the current document before replacing it?"},
        234, 218, 0.77f, 196, 211, 222);
    const std::vector<std::string> rows =
        overwrite
            ? std::vector<std::string>{
                  "CANCEL", "OVERWRITE"}
            : std::vector<std::string>{
                  "SAVE THEN CONTINUE",
                  "DISCARD CHANGES", "CANCEL"};
    for (size_t row = 0; row < rows.size(); ++row)
        drawModernMenuRow(
            renderer, rows[row], "",
            234, 258 + row * 34.0f, 492,
            row == confirmationSelection_);
}

void ScenarioEditor::renderHub(
    Renderer &renderer, int screenWidth,
    int) const {
    title(renderer, "SCENARIO EDITOR", screenWidth);
    drawModernPanel(
        renderer, 90, 79, 780, 418);
    const std::vector<std::pair<std::string, std::string>>
        entries{
            {"NEW SCENARIO", "BLANK LAND"},
            {"NEW SCENARIO", "ISLANDS"},
            {"NEW SCENARIO", "SKIRMISH BASE"},
            {"NEW SCENARIO", "TRIGGER TUTORIAL"},
            {"MAP SIZE", std::to_string(templateSize_) +
                             " x " +
                             std::to_string(templateSize_)},
            {"PLAYERS", std::to_string(templatePlayers_)},
            {"SEED", std::to_string(templateSeed_)},
            {"LOAD / EDIT RECENT",
             recentPath_.empty() ? "NONE"
                                 : shortPath(recentPath_)},
            {"RECOVER AUTOSAVE",
             shortPath(paths_.recovery)},
            {"IMPORT STOCK SCX",
             shortPath(joinScenarioPath(
                 importDirectory_, "import.scx"))},
            {"BACK TO MAIN MENU", ""}};
    for (size_t row = 0; row < entries.size();
         ++row)
        drawModernMenuRow(
            renderer, entries[row].first,
            entries[row].second,
            112, 100 + row * 34.0f, 736,
            row == selection_,
            (row == 7 && recentPath_.empty()));
    drawModernTooltip(
        renderer,
        "D-PAD/STICK NAVIGATE  LEFT/RIGHT ADJUST  X SELECT  O BACK",
        90, 507, 780);
    if (!status_.empty())
        drawModernStatusPill(
            renderer, status_, 108, 68,
            status_.find("FAILED") != std::string::npos);
}

void ScenarioEditor::renderWorkspace(
    Renderer &renderer, int screenWidth,
    int) const {
    title(
        renderer,
        document_.metadata.title.empty()
            ? "UNTITLED SCENARIO"
            : document_.metadata.title,
        screenWidth);
    drawModernTabs(
        renderer, kWorkspaceTabs,
        (size_t)page_ - (size_t)EditorPage::Map,
        14, 59, 932);
    if (currentPath_.empty() || undo_.dirty())
        drawModernStatusPill(
            renderer,
            currentPath_.empty() ? "UNSAVED"
                                 : "MODIFIED",
            835, 18, true);
    else
        drawModernStatusPill(
            renderer, "SAVED", 866, 18);
    switch (page_) {
    case EditorPage::Map: renderMap(renderer); break;
    case EditorPage::Objects: renderObjects(renderer); break;
    case EditorPage::Players: renderPlayers(renderer); break;
    case EditorPage::Triggers: renderTriggers(renderer); break;
    case EditorPage::Scenario: renderScenario(renderer); break;
    case EditorPage::Validate:
        renderValidation(renderer);
        break;
    case EditorPage::Files: renderFiles(renderer); break;
    default: break;
    }
    drawModernTooltip(
        renderer,
        status_.empty()
            ? "L/R PANEL  START PLAYTEST  SELECT+SQUARE UNDO  SELECT+L+R REDO"
            : status_,
        14, 513, 932);
}

void ScenarioEditor::renderMap(
    Renderer &renderer) const {
    drawModernPanel(
        renderer, kMapX - 5, kMapY - 5,
        kMapW + 10, kMapH + 10);
    if (document_.map.width &&
        document_.map.height &&
        document_.map.tiles.size() ==
            (size_t)document_.map.width *
                document_.map.height) {
        const float tilePixels =
            std::min(
                kMapW / document_.map.width,
                kMapH / document_.map.height) *
            mapZoom_;
        const int firstX = (int)mapPanX_;
        const int firstY = (int)mapPanY_;
        const int lastX = std::min(
            (int)document_.map.width,
            firstX + (int)std::ceil(
                         kMapW / tilePixels) +
                1);
        const int lastY = std::min(
            (int)document_.map.height,
            firstY + (int)std::ceil(
                         kMapH / tilePixels) +
                1);
        for (int y = firstY; y < lastY; ++y) {
            int run = firstX;
            while (run < lastX) {
                const ScenarioTile &tile =
                    document_.map.tiles[
                        (size_t)y *
                            document_.map.width +
                        run];
                int end = run + 1;
                while (end < lastX) {
                    const ScenarioTile &next =
                        document_.map.tiles[
                            (size_t)y *
                                document_.map.width +
                            end];
                    if (next.terrain != tile.terrain ||
                        next.elevation != tile.elevation)
                        break;
                    ++end;
                }
                uint8_t red, green, blue;
                terrainColor(
                    tile.terrain, tile.elevation,
                    red, green, blue);
                renderer.fillRect(
                    kMapX +
                        (run - mapPanX_) *
                            tilePixels,
                    kMapY +
                        (y - mapPanY_) *
                            tilePixels,
                    (end - run) * tilePixels + 0.5f,
                    tilePixels + 0.5f,
                    red, green, blue, 255);
                run = end;
            }
        }
        for (const EditorArea &area :
             document_.areas) {
            const float x =
                kMapX +
                (area.left - mapPanX_) * tilePixels;
            const float y =
                kMapY +
                (area.top - mapPanY_) * tilePixels;
            const float w =
                (area.right - area.left + 1) *
                tilePixels;
            const float h =
                (area.bottom - area.top + 1) *
                tilePixels;
            renderer.drawLine(
                x, y, x + w, y, 2,
                255, 220, 89, 220);
            renderer.drawLine(
                x + w, y, x + w, y + h, 2,
                255, 220, 89, 220);
            renderer.drawLine(
                x + w, y + h, x, y + h, 2,
                255, 220, 89, 220);
            renderer.drawLine(
                x, y + h, x, y, 2,
                255, 220, 89, 220);
        }
        for (const EditorObject &object :
             document_.objects) {
            const float x =
                kMapX +
                (object.scenario.x - mapPanX_) *
                    tilePixels;
            const float y =
                kMapY +
                (object.scenario.y - mapPanY_) *
                    tilePixels;
            if (x < kMapX || y < kMapY ||
                x > kMapX + kMapW ||
                y > kMapY + kMapH)
                continue;
            const uint8_t playerColor =
                object.scenario.player;
            renderer.fillRect(
                x - 3, y - 3, 7, 7,
                (uint8_t)(80 +
                          playerColor * 21u),
                (uint8_t)(220 -
                          playerColor * 13u),
                (uint8_t)(75 +
                          playerColor * 17u),
                255);
        }
        if (document_.camera.x >= 0 &&
            document_.camera.y >= 0) {
            const float x =
                kMapX +
                (document_.camera.x - mapPanX_) *
                    tilePixels;
            const float y =
                kMapY +
                (document_.camera.y - mapPanY_) *
                    tilePixels;
            renderer.drawLine(
                x - 6, y, x + 6, y, 2,
                255, 255, 255, 255);
            renderer.drawLine(
                x, y - 6, x, y + 6, 2,
                255, 255, 255, 255);
        }
    }
    drawModernPanel(renderer, kSideX, 88, kSideW, 405);
    drawUiText(
        renderer, {"MAP TOOLS"}, kSideX + 14,
        104, 0.9f, 235, 213, 145);
    const std::vector<std::pair<std::string, std::string>>
        rows{
            {"Tool", toolName(mapTool_)},
            {"Value", std::to_string(brushValue_)},
            {"Brush", std::to_string(brushSize_)},
            {"Shape", circularBrush_ ? "ROUND" : "SQUARE"},
            {"Zoom", number(mapZoom_)},
            {"Map", std::to_string(document_.map.width) +
                        " x " +
                        std::to_string(document_.map.height)},
            {"Areas", std::to_string(document_.areas.size())},
            {"Objects", std::to_string(document_.objects.size())}};
    for (size_t row = 0; row < rows.size(); ++row)
        drawModernMenuRow(
            renderer, rows[row].first,
            rows[row].second,
            kSideX + 10, 139 + row * 36,
            kSideW - 20, row == 0);
    drawUiText(
        renderer,
        {"UP/DOWN TOOL",
         "LEFT/RIGHT VALUE",
         "O BRUSH SIZE",
         "TRIANGLE SHAPE",
         "TOUCH/X PAINT"},
        kSideX + 18, 426, 0.62f,
        151, 177, 199);
}

void ScenarioEditor::renderObjects(
    Renderer &renderer) const {
    renderMap(renderer);
    renderer.fillRect(
        kSideX + 4, 92, kSideW - 8, 397,
        7, 17, 31, 255);
    drawUiText(
        renderer, {"OBJECT PALETTE"},
        kSideX + 16, 105, 0.85f,
        235, 213, 145);
    drawModernMenuRow(
        renderer, "Unit",
        std::to_string(paletteUnit_),
        kSideX + 10, 139, kSideW - 20, true);
    drawModernMenuRow(
        renderer, "Owner",
        std::to_string(objectPlayer_),
        kSideX + 10, 176, kSideW - 20, false);
    std::string name = objectName(paletteUnit_);
    if (name.size() > 29) name.resize(29);
    drawUiText(
        renderer, {name}, kSideX + 18, 215,
        0.68f, 205, 218, 229);
    drawUiText(
        renderer,
        {"LEFT/RIGHT SEARCH DAT",
         "UP/DOWN OWNER",
         "X/TOUCH MAP PLACE",
         "X EXISTING SELECT",
         "TRIANGLE DELETE",
         "",
         "Placement validates:",
         "DAT ID / visibility",
         "map footprint",
         "object collision"},
        kSideX + 18, 258, 0.61f,
        151, 177, 199);
    if (!document_.objects.empty()) {
        const EditorObject &object =
            document_.objects[
                std::min(
                    selectedObject_,
                    document_.objects.size() - 1)];
        drawModernStatusPill(
            renderer,
            "#" +
                std::to_string(
                    object.scenario.spawnId) +
                " " +
                objectName(
                    object.scenario.unitId),
            kSideX + 12, 458);
    }
}

void ScenarioEditor::renderPlayers(
    Renderer &renderer) const {
    drawModernPanel(renderer, 58, 104, 844, 379);
    if (document_.players.empty()) return;
    const EditorPlayer &player =
        document_.players[
            std::min(
                selectedPlayer_,
                document_.players.size() - 1)];
    if (playerTechnologyPanel_) {
        const bool technology =
            playerTechnologyKind_ == 0;
        std::string itemName;
        bool disabled = false;
        bool researched = false;
        const auto contains =
            [](const std::vector<uint32_t> &values,
               uint32_t value) {
                return std::find(
                           values.begin(),
                           values.end(), value) !=
                       values.end();
            };
        if (technology) {
            if (playerTechnologyId_ <
                assets_.dat().techs.size()) {
                const dat::Tech &tech =
                    assets_.dat().techs[
                        playerTechnologyId_];
                itemName =
                    assets_.localizedString(
                        tech.languageDllName);
                if (itemName.empty())
                    itemName =
                        !tech.name2.empty()
                            ? tech.name2
                            : tech.name;
            }
            disabled = contains(
                player.scenario
                    .disabledTechnologies,
                playerTechnologyId_);
            researched = contains(
                player.researchedTechnologies,
                playerTechnologyId_);
        } else {
            const dat::Unit *unit =
                unitDefinition(
                    (uint16_t)playerTechnologyId_,
                    (uint8_t)selectedPlayer_ + 1);
            if (unit) {
                itemName =
                    assets_.localizedString(
                        unit->languageDllName);
                if (itemName.empty())
                    itemName =
                        !unit->name2.empty()
                            ? unit->name2
                            : unit->name;
                const bool building =
                    unit->type ==
                    dat::UT_Building;
                disabled = contains(
                    building
                        ? player.scenario
                              .disabledBuildings
                        : player.scenario
                              .disabledUnits,
                    playerTechnologyId_);
                researched = contains(
                    building
                        ? player
                              .researchedBuildings
                        : player.researchedUnits,
                    playerTechnologyId_);
            }
        }
        if (itemName.empty())
            itemName = "UNNAMED";
        if (itemName.size() > 27)
            itemName.resize(27);
        const std::vector<
            std::pair<std::string, std::string>>
            rows{
                {"Filter",
                 technology
                     ? "TECHNOLOGIES"
                     : "UNITS / BUILDINGS"},
                {"Item",
                 itemName + " [" +
                     std::to_string(
                         playerTechnologyId_) +
                     "]"},
                {"Availability",
                 disabled ? "DISABLED"
                          : "ENABLED"},
                {technology
                     ? "Initial State"
                     : "Enable Override",
                 researched
                     ? technology
                           ? "RESEARCHED"
                           : "ENABLED"
                     : "DEFAULT"},
                {"Back", "PLAYER SETTINGS"}};
        for (size_t row = 0;
             row < rows.size(); ++row)
            drawModernMenuRow(
                renderer, rows[row].first,
                rows[row].second,
                90, 142 + row * 48, 780,
                row == selection_);
        drawUiText(
            renderer,
            {"Localized DAT-backed lists",
             "LEFT/RIGHT or X changes the selected value.",
             "Unknown imported IDs remain preserved."},
            108, 399, 0.72f,
            174, 198, 215);
        return;
    }
    const size_t diplomacyTarget =
        std::min(
            selectedDiplomacyTarget_,
            document_.players.size() - 1);
    const uint32_t diplomacy =
        player.scenario
            .diplomacy[diplomacyTarget + 1];
    static const char *stanceNames[] = {
        "ALLY", "NEUTRAL", "UNKNOWN", "ENEMY"};
    const std::vector<std::pair<std::string, std::string>>
        rows{
            {"Player", std::to_string(selectedPlayer_ + 1) +
                           " / " +
                           std::to_string(document_.players.size())},
            {"Civilization",
             std::to_string(player.scenario.civilization)},
            {"Color", std::to_string(player.scenario.color + 1)},
            {"Team", player.team ? std::to_string(player.team)
                                  : "NONE"},
            {"Food", number(player.scenario.resources[0])},
            {"Carbon", number(player.scenario.resources[1])},
            {"Population", number(player.scenario.populationLimit)},
            {"Starting Age",
             std::to_string(player.scenario.startingAge + 1)},
            {"AI Difficulty",
             std::to_string(player.difficulty)},
            {"Allied Victory",
             player.scenario.alliedVictory ? "YES" : "NO"},
            {"Diplomacy Target",
             "PLAYER " +
                 std::to_string(
                     diplomacyTarget + 1)},
            {"Relationship",
             stanceNames[
                 std::min<uint32_t>(
                     diplomacy, 3)]},
            {"Technology / Units",
             "OPEN EDITOR"}};
    for (size_t row = 0; row < rows.size(); ++row)
        drawModernMenuRow(
            renderer, rows[row].first,
            rows[row].second,
            90, 118 + row * 28, 500,
            row == selection_);
    drawUiText(
        renderer,
        {"PLAYER / DIPLOMACY / TECHNOLOGY",
         "",
         "1-8 active editor players",
         "Civilization and localized DAT",
         "names are used by Objects.",
         "",
         "Disabled technologies: " +
             std::to_string(
                 player.scenario
                     .disabledTechnologies.size()),
         "Disabled units: " +
             std::to_string(
                 player.scenario.disabledUnits.size()),
         "Researched technologies: " +
             std::to_string(
                 player.researchedTechnologies.size()),
         "",
         "Relationship matrix edits are",
         "directional; validation reports",
         "contradictory asymmetric stances."},
        625, 137, 0.67f,
        174, 198, 215);
}

void ScenarioEditor::renderTriggers(
    Renderer &renderer) const {
    drawModernPanel(renderer, 32, 101, 896, 386);
    if (document_.triggers.empty()) {
        drawUiText(
            renderer,
            {"NO TRIGGERS", "",
             "Press X or Triangle to create one.",
             "Imported unknown forms remain read-only",
             "and are preserved in the native document."},
            92, 160, 1.0f,
            196, 211, 225);
        return;
    }
    const EditorTrigger &trigger =
        document_.triggers[
            std::min(
                selectedTrigger_,
                document_.triggers.size() - 1)];
    const auto ordered = std::find(
        document_.triggerOrder.begin(),
        document_.triggerOrder.end(),
        (uint32_t)selectedTrigger_);
    const size_t orderPosition =
        ordered == document_.triggerOrder.end()
            ? selectedTrigger_
            : (size_t)(ordered -
                       document_.triggerOrder.begin());
    const std::vector<std::pair<std::string, std::string>>
        rows{
            {"Trigger",
             std::to_string(orderPosition + 1) +
                 " / " +
                 std::to_string(document_.triggers.size())},
            {"Enabled", trigger.enabled ? "YES" : "NO"},
            {"Looping", trigger.looping ? "YES" : "NO"},
            {"Objective", trigger.objective ? "YES" : "NO"},
            {"Add Condition", "TIMER"},
            {"Add Effect", "DISPLAY INSTRUCTION"},
            {"Move Earlier", ""},
            {"Delete", ""}};
    for (size_t row = 0; row < rows.size(); ++row)
        drawModernMenuRow(
            renderer, rows[row].first,
            rows[row].second,
            54, 129 + row * 38, 430,
            row == selection_);
    drawUiText(
        renderer,
        {triggerSummary(trigger),
         trigger.description,
         "",
         "Conditions: " +
             std::to_string(trigger.conditions.size()),
         "Effects: " +
             std::to_string(trigger.effects.size()),
         "Condition order: " +
             std::to_string(trigger.conditionOrder.size()),
         "Effect order: " +
             std::to_string(trigger.effectOrder.size()),
         "",
         "Triangle duplicates trigger.",
         "Unsupported imported records are",
         "visible, read-only, and preserved.",
         "Validate checks object/area/type refs."},
        528, 132, 0.72f,
        186, 207, 222);
}

void ScenarioEditor::renderScenario(
    Renderer &renderer) const {
    drawModernPanel(renderer, 78, 108, 804, 370);
    const std::vector<std::pair<std::string, std::string>>
        rows{
            {"Victory Type",
             std::to_string(document_.victory.type)},
            {"Conquest Required",
             document_.victory.scenario.conquestRequired
                 ? "YES"
                 : "NO"},
            {"Required Holocrons",
             std::to_string(
                 document_.victory.scenario
                     .requiredHolocrons)},
            {"Required Score",
             std::to_string(
                 document_.victory.scenario.requiredScore)},
            {"Time Limit (sec)",
             std::to_string(
                 document_.victory.scenario.timeLimit)},
            {"Camera X", number(document_.camera.x)},
            {"Camera Y", number(document_.camera.y)},
            {"Instructions",
             document_.messages.instructions.empty()
                 ? "EMPTY"
                 : "SET"},
            {"Objectives",
             document_.messages.objectives.empty()
                 ? "EMPTY"
                 : "SET"}};
    for (size_t row = 0; row < rows.size(); ++row)
        drawModernMenuRow(
            renderer, rows[row].first,
            rows[row].second,
            105, 135 + row * 35, 510,
            row == selection_);
    drawUiText(
        renderer,
        {"SCENARIO METADATA",
         document_.metadata.author.empty()
             ? "Author: unset"
             : "Author: " + document_.metadata.author,
         "Template: " +
             document_.metadata.templateName,
         "Areas: " +
             std::to_string(document_.areas.size()),
         "AI references: " +
             std::to_string(document_.ai.size()),
         "Opaque records: " +
             std::to_string(
                 document_.unknownRecords.size()),
         "",
         "Text fields use safe defaults here;",
         "full values round-trip in native files."},
        650, 139, 0.67f,
        174, 198, 215);
}

void ScenarioEditor::renderValidation(
    Renderer &renderer) const {
    drawModernPanel(renderer, 38, 104, 884, 382);
    size_t errors = 0;
    for (const ValidationIssue &issue :
         validation_.issues)
        if (issue.severity ==
            ValidationSeverity::Error)
            ++errors;
    drawModernStatusPill(
        renderer,
        errors
            ? std::to_string(errors) + " ERRORS"
            : "NO ERRORS",
        55, 118, errors != 0);
    drawModernStatusPill(
        renderer,
        std::to_string(
            validation_.issues.size() - errors) +
            " WARNINGS",
        180, 118,
        validation_.issues.size() > errors);
    if (validation_.issues.empty()) {
        drawUiText(
            renderer,
            {"SCENARIO VALIDATION PASSED",
             "",
             "START: snapshot and playtest",
             "X: run validation again"},
            105, 194, 1.1f,
            196, 225, 203);
    } else {
        const size_t first =
            selectedIssue_ > 6
                ? selectedIssue_ - 6
                : 0;
        for (size_t row = 0;
             row < 11 &&
             first + row < validation_.issues.size();
             ++row) {
            const size_t index = first + row;
            const ValidationIssue &issue =
                validation_.issues[index];
            std::string message =
                (issue.severity ==
                         ValidationSeverity::Error
                     ? "ERROR "
                     : "WARN  ") +
                issue.code + ": " + issue.message;
            if (message.size() > 108)
                message.resize(108);
            drawModernMenuRow(
                renderer, message, "",
                56, 160 + row * 27, 846,
                index == selectedIssue_);
        }
    }
    if (!playtestDiagnostic_.empty())
        drawModernStatusPill(
            renderer, playtestDiagnostic_,
            520, 118,
            playtestDiagnostic_.find("FAILED") !=
                std::string::npos);
}

void ScenarioEditor::renderFiles(
    Renderer &renderer) const {
    drawModernPanel(renderer, 54, 108, 852, 369);
    const bool exactExport =
        document_.hasImportFingerprint &&
        editableScenarioSemanticFingerprint(document_) ==
            document_.importSemanticFingerprint;
    const std::vector<std::pair<std::string, std::string>>
        rows{
            {"SAVE NATIVE",
             currentPath_.empty()
                 ? shortPath(paths_.userCreated)
                 : shortPath(currentPath_)},
            {"WRITE AUTOSAVE",
             shortPath(paths_.autosave)},
            {"RECOVER AUTOSAVE",
             shortPath(paths_.recovery)},
            {"EXPORT STOCK SCX",
             exactExport ? "BYTE-EXACT SOURCE"
                         : "UNAVAILABLE AFTER EDIT"},
            {"IMPORT SCX",
             shortPath(joinScenarioPath(
                 importDirectory_, "import.scx"))},
            {"VALIDATE + PLAYTEST", "START"},
            {"CLOSE DOCUMENT", "EDITOR HUB"}};
    for (size_t row = 0; row < rows.size(); ++row)
        drawModernMenuRow(
            renderer, rows[row].first,
            rows[row].second,
            78, 137 + row * 43, 804,
            row == selection_,
            row == 3 && !exactExport);
    drawUiText(
        renderer,
        {"Native .swscenario files are versioned, bounded,",
         "checksummed, and atomically replaced.",
         "Modified/generated documents are never mislabeled SCX.",
         "Original imported SCX bytes remain embedded in the sidecar."},
        95, 438, 0.64f,
        151, 177, 199);
}

} // namespace swgb
