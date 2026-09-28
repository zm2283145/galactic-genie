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
    std::string scouts;
    float cameraX = -1;
    float cameraY = -1;
    std::array<uint32_t, 16> civilizations{};
    ScenarioMap map;
    std::vector<ScenarioUnit> units;

    bool load(const std::vector<uint8_t> &scx, std::string *err = nullptr);
};

} // namespace swgb
