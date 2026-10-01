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
};

struct ScenarioCondition {
    int32_t type = 0;
    std::vector<int32_t> fields;
};

struct ScenarioTrigger {
    bool enabled = false;
    bool looping = false;
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

    bool load(const std::vector<uint8_t> &scx, std::string *err = nullptr);
};

} // namespace swgb
