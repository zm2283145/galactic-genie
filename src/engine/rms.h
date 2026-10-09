// SPDX-License-Identifier: GPL-3.0-or-later
// The original random map generator (RGE_RMM_* in battlegrounds_x1.exe): the
// RMS script interpreter and its land, elevation, cliff, terrain,
// connection and object modules, reimplemented from the exe (see
// claude/swgb-rms-algorithms.txt and claude/swgb-rms-engine-helpers.txt in
// the project docs). It works on its own copy of the map and object list;
// the game applies the result.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace swgb {
namespace dat {
struct DatFile;
}

struct RmsPlayer {
    int civilization = 1;
    int team = 0; // >0: allied with every player of the same team
    float startingWorkers = 3.0f; // player attribute 84
};

struct RmsSettings {
    int width = 144, height = 144;
    int mapSizeIndex = 1;        // 0 tiny .. 5 giant
    int gameType = 0;            // options+0x1445 (1 Terminate, 2 Death Match)
    int resourceLevel = 0;       // 0 standard, 1 low, 2 medium, 3 high
    bool fixedPositions = true;  // "Team Together" (FIXED_POSITIONS)
    bool customScript = false;   // map type 51: #include disabled
    int32_t seed = 1;
    std::vector<RmsPlayer> players; // players 1..n
};

struct RmsObject {
    int unitId = -1;
    int player = 0;
    float x = 0, y = 0;
    int facet = -1;  // cliff pieces: sprite facet
    bool alive = true;
};

struct RmsResult {
    int width = 0, height = 0;
    std::vector<uint8_t> terrain;   // [y*w+x]
    std::vector<uint8_t> elevation; // [y*w+x]
    std::vector<RmsObject> objects; // alive ones only
    // Player land origins (index = player, 0 unused); -1 when none.
    std::vector<std::array<float, 2>> starts;
    std::string error;
};

// Loads the text of a DRS 'bina' resource (drsId >= 0) or a script file
// from the game folder (drsId < 0, by name). Returns false when missing.
using RmsLoader = std::function<bool(int drsId, const std::string &name, std::string &text)>;

// The script for an original map type (9..61): its DRS id, or 0.
int rmsScriptForMapType(int mapType);
// Tiles per side for a map size index and map type (0x5eec80).
int rmsMapWidth(int mapType, int sizeIndex);
// The built-in random map names by map type, for the setup screen
// (empty for unused ids).
const char *rmsMapName(int mapType);

bool generateRandomMap(const dat::DatFile &dat, const RmsSettings &settings,
                       const std::string &script, const RmsLoader &loader,
                       RmsResult &result);

} // namespace swgb
