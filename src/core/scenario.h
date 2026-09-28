// SPDX-License-Identifier: GPL-3.0-or-later
// SWGB scenario header and terrain-map reader.
#pragma once

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
    int32_t cameraX = -1;
    int32_t cameraY = -1;
    ScenarioMap map;

    bool load(const std::vector<uint8_t> &scx, std::string *err = nullptr);
};

} // namespace swgb
