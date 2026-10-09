// SPDX-License-Identifier: GPL-3.0-or-later
// SWGB scenario header and terrain-map reader.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace swgb {

struct ScenarioTile {
    uint8_t terrain = 0;
    uint8_t elevation = 0;
};

struct ScenarioMap {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<ScenarioTile> tiles;
};

struct ScenarioUnit {
    float x = 0;
    float y = 0;
    float z = 0;
    uint32_t spawnId = 0;
    uint16_t unitId = 0;
    uint8_t state = 0;
    float rotation = 0;
    uint16_t initialFrame = 0;
    int32_t garrisonedInId = -1;
    uint8_t player = 0;
};

struct ScenarioPlayer {
    std::string name;
    std::string aiName;
    std::string cityName;
    std::string personalityName;
    std::string aiFilename;
    std::string cityFilename;
    std::string personality;
    uint32_t civilization = 0;
    uint32_t color = 0;
    uint8_t aiType = 2;
    bool active = false;
    bool human = false;
    bool alliedVictory = false;
    float cameraX = -1;
    float cameraY = -1;
    std::array<float, 6> resources{};
    std::array<uint32_t, 16> diplomacy{};
    std::vector<uint32_t> disabledTechnologies;
    std::vector<uint32_t> disabledUnits;
    std::vector<uint32_t> disabledBuildings;
    std::vector<uint32_t> researchedTechnologies;
    std::vector<uint32_t> researchedUnits;
    std::vector<uint32_t> researchedBuildings;
    // As stored, for saving: the player-data diplomacy block, the name string
    // id and the secondary record's name (the game uses `diplomacy`, which the
    // secondary records override).
    std::array<uint32_t, 16> primaryDiplomacy{};
    int32_t nameStringId = -1;
    std::string recordName;
    std::array<int16_t, 2> recordView{};
    std::array<uint32_t, 16> recordDiplomacy{};
    std::array<uint32_t, 6> primaryResources{};
    std::vector<uint8_t> recordTail; // version 2.0 record tail, as stored
    int32_t startingAge = -1;
    float populationLimit = 0;
};

struct ScenarioVictory {
    bool conquestRequired = false;
    uint32_t requiredHolocrons = 0;
    uint32_t requiredExploredPercent = 0;
    bool allConditionsRequired = false;
    uint32_t mode = 4;
    uint32_t requiredScore = 0;
    uint32_t timeLimit = 0;
};

struct ScenarioEffect {
    int32_t type = 0;
    std::vector<int32_t> fields;
    std::string message;
    std::string sound;
    std::vector<uint32_t> selectedUnitIds;
    // An empty string stored as a lone NUL (size 1) rather than size 0.
    bool emptyMessageTerminated = false, emptySoundTerminated = false;
};

struct ScenarioCondition {
    int32_t type = 0;
    std::vector<int32_t> fields;
};

struct ScenarioTrigger {
    bool enabled = false;
    bool looping = false;
    int32_t descriptionStringId = -1;
    bool objective = false;
    int32_t objectiveOrder = -1;
    int32_t objectiveStringId = -1;
    std::string description;
    std::string name;
    std::vector<ScenarioEffect> effects;
    std::vector<int32_t> effectOrder;
    std::vector<ScenarioCondition> conditions;
    std::vector<int32_t> conditionOrder;
};

struct Scenario {
    std::string version;
    int32_t saveType = 0;
    uint32_t lastSaveTime = 0;
    std::string instructions;
    int32_t victoryType = 0;
    uint32_t enabledPlayerCount = 0;
    uint32_t nextUnitId = 0;
    float playerDataVersion = 0;
    std::string originalFilename;
    std::string hints;
    std::string victoryMessage;
    std::string lossMessage;
    std::string history;
    std::string scouts;
    std::string pregameCinematic;
    std::string victoryCinematic;
    std::string lossCinematic;
    std::string background;
    float cameraX = -1;
    float cameraY = -1;
    float mapCameraX = -1;
    float mapCameraY = -1;
    std::array<uint32_t, 16> civilizations{};
    std::array<ScenarioPlayer, 16> players{};
    ScenarioVictory victory;
    bool allTechnologies = false;
    ScenarioMap map;
    std::vector<ScenarioUnit> units;
    double triggerSystemVersion = 0;
    uint8_t objectiveState = 0;
    std::vector<ScenarioTrigger> triggers;
    std::vector<uint32_t> triggerOrder;
    // Files stored with the scenario (custom AI scripts) and the
    // compatibility block, kept for saving.
    struct IncludedFile {
        std::string name;
        std::string data;
    };
    std::vector<IncludedFile> includedFiles;
    std::vector<uint8_t> compatibilityBlock;
    // Raw player-data values kept for saving.
    uint8_t playerDataByte = 1;
    uint32_t messageTerminatedMask = 0x3f; // messages NUL-terminated, file names not
    std::array<int32_t, 6> messageStringIds{{-1, -1, -1, -1, -1, -1}};
    float timelineValue = -1.0f;
    int32_t headerCameraX = 0, headerCameraY = 0, headerCameraExtra = 0;

    bool load(const std::vector<uint8_t> &scx, std::string *err = nullptr);
    // Writes an SCX 1.21 file (player data 1.22, as SWGB's editor saves).
    // Fields the reader skips are written with the editor's defaults.
    bool save(std::vector<uint8_t> &scx, std::string *err = nullptr) const;
};

} // namespace swgb
