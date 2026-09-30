// SPDX-License-Identifier: GPL-3.0-or-later
#include "game.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <sstream>

namespace swgb {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr uint8_t kSequenceAnimated = 0x1;
constexpr int kCursorSlp = 51000;
constexpr size_t kCursorNormal = 0;
constexpr size_t kCursorCommand = 3;
constexpr size_t kCursorAttack = 4;
constexpr size_t kCursorMove = 11;
constexpr size_t kCursorGarrison = 13;
constexpr size_t kCursorGather = 7;
constexpr size_t kCursorRepair = 8;
constexpr int kCommandIconSlp = 50721;
constexpr size_t kCommandGarrisonIcon = 1;
constexpr size_t kCommandEjectIcon = 2;
constexpr size_t kCommandRepairIcon = 28; // exe: repair button (help 4927)
constexpr size_t kCommandDestroyIcon = 59; // exe: kill/destroy button (help 4941)
constexpr size_t kCommandLockGateIcon = 47;
constexpr size_t kCommandUnlockGateIcon = 48;
constexpr size_t kCommandPowerStatusIcon = 49;
constexpr size_t kCommandShieldStatusIcon = 10;
// Stance buttons (exe 0x503800): Aggressive, Defensive, Stand Ground, No
// Attack. The pressed (active) variants are drawn for the current stance.
constexpr size_t kStanceIcons[4] = {9, 10, 11, 50};
constexpr size_t kStanceActiveIcons[4] = {53, 52, 51, 54};
constexpr int kStanceNameStrings[4] = {4133, 4134, 4122, 4135};
constexpr int kStanceHelpStrings[4] = {4933, 4934, 4922, 4935};
constexpr int kSuperconductingShieldsTech = 570;
constexpr int kShieldWallTech = 484;
constexpr int kBuildingIconSlpBase = 53241;
constexpr int kUnitIconSlpBase = 53251;
constexpr int kTechnologyIconSlpBase = 53261;
constexpr float kSelectionPanelHeight = 112.0f;
constexpr float kFormationButtonX = 14.0f;
constexpr float kFormationButtonY = 12.0f;
constexpr float kFormationButtonSize = 34.0f;
constexpr float kFormationButtonStepX = 40.0f;
constexpr float kFormationButtonStepY = 44.0f;
constexpr float kGroupPortraitX = 100.0f;
constexpr float kGroupPortraitY = 10.0f;
constexpr float kGroupPortraitSize = 36.0f;
constexpr float kGroupPortraitStepX = 41.0f;
constexpr float kGroupPortraitStepY = 46.0f;
constexpr float kActionMenuX = 176.0f;
constexpr float kActionMenuTop = 118.0f;
constexpr float kActionMenuY = 164.0f;
constexpr float kActionMenuWidth = 760.0f;
constexpr float kActionMenuHeight = 306.0f;
constexpr float kActionMenuCell = 52.0f;
constexpr float kActionMenuIconSize = 44.0f;
constexpr size_t kActionMenuColumns = 5;
constexpr size_t kActionMenuRows = 3;
constexpr size_t kActionMenuVisibleItems =
    kActionMenuColumns * kActionMenuRows;
constexpr float kActionMenuRowHeight = 42.0f;
constexpr size_t kActionMenuMaxRows = 10;
constexpr int kPowerRadiusGraphic = 5025;
constexpr int kShieldRadiusGraphic = 5034;
constexpr int kPowerIndicatorSlpFirst = 2695;
constexpr int kPowerIndicatorSlpLast = 2706;
constexpr float kCheatMenuX = 170.0f;
constexpr float kCheatMenuY = 58.0f;
constexpr float kCheatMenuWidth = 620.0f;
constexpr float kCheatMenuRowHeight = 34.0f;
constexpr size_t kCheatMenuVisibleRows = 11;

void snapAdjacentBuildingPosition(
    const dat::Unit &unit, float &x, float &y) {
    if (!unit.adjacentMode) return;
    const auto halfTile = [](float value) {
        return std::floor(value) + 0.5f;
    };
    if (unit.name.rfind("BLDG-ENTRYA", 0) == 0) {
        x = std::round(x);
        y = halfTile(y);
    } else if (
        unit.name.rfind("BLDG-ENTRYB", 0) == 0) {
        x = halfTile(x);
        y = std::round(y);
    } else if (
        unit.name.rfind("BLDG-ENTRYC", 0) == 0 ||
        unit.name.rfind("BLDG-ENTRYD", 0) == 0) {
        x = std::round(x);
        y = std::round(y);
    } else {
        x = halfTile(x);
        y = halfTile(y);
    }
}

enum class CheatAction {
    Food,
    Carbon,
    Nova,
    Ore,
    ForceBuild,
    ForceTech,
    ForceSight,
    ForceExplore,
    Spawn,
    Tarkin,
    Skywalker,
    Darkside,
    Technology,
};

struct CheatEntry {
    const char *code;
    const char *effect;
    CheatAction action;
    int value;
    bool requireWater;
};

constexpr CheatEntry kCheats[] = {
    {"FORCEFOOD", "+1000 FOOD", CheatAction::Food, 0, false},
    {"FORCECARBON", "+1000 CARBON", CheatAction::Carbon, 0, false},
    {"FORCENOVA", "+1000 NOVA", CheatAction::Nova, 0, false},
    {"FORCEORE", "+1000 ORE", CheatAction::Ore, 0, false},
    {"FORCEBUILD", "INSTANT PRODUCTION", CheatAction::ForceBuild, 0, false},
    {"FORCETECH", "REMOVE CAMPAIGN TECH LIMITS", CheatAction::ForceTech, 0, false},
    {"FORCESIGHT", "REMOVE FOG OF WAR", CheatAction::ForceSight, 0, false},
    {"FORCEEXPLORE", "EXPLORE THE MAP", CheatAction::ForceExplore, 0, false},
    {"SIMONSAYS", "SPAWN KILLER EWOK", CheatAction::Spawn, 1204, false},
    {"SCARYNEIGHBOR", "SPAWN BONGO MARAUDER", CheatAction::Spawn, 1314, true},
    {"IMPERIAL ENTANGLEMENTS", "SPAWN STAR DESTROYER", CheatAction::Spawn, 1586, false},
    {"THAT'S NO MOON", "SPAWN DEATH STAR", CheatAction::Spawn, 1587, false},
    {"TANTIVE IV", "SPAWN BLOCKADE RUNNER", CheatAction::Spawn, 1580, false},
    {"GALACTIC UPHEAVAL", "SPAWN DECIMATOR", CheatAction::Spawn, 545, false},
    {"SUDDENLY SILENCED", "FASTER ATTACKS", CheatAction::Technology, 592, false},
    {"THE FORCE IS STRONG WITH THIS ONE", "STRONGER JEDI", CheatAction::Technology, 588, false},
    {"MOST POWERFUL JEDI", "IMPROVE JEDI", CheatAction::Technology, 591, false},
    {"INTENSIFY FORWARD FIRE POWER", "IMPROVE DEFENSES", CheatAction::Technology, 590, false},
    {"THE FIGHTERS ARE COMING IN TOO FAST", "FASTER AIRCRAFT", CheatAction::Technology, 589, false},
    {"TARKIN", "DESTROY ALL ENEMIES", CheatAction::Tarkin, 0, false},
    {"SKYWALKER", "WIN SCENARIO", CheatAction::Skywalker, 0, false},
    {"DARKSIDE2", "DESTROY PLAYER 2", CheatAction::Darkside, 2, false},
    {"DARKSIDE3", "DESTROY PLAYER 3", CheatAction::Darkside, 3, false},
    {"DARKSIDE4", "DESTROY PLAYER 4", CheatAction::Darkside, 4, false},
    {"DARKSIDE5", "DESTROY PLAYER 5", CheatAction::Darkside, 5, false},
    {"DARKSIDE6", "DESTROY PLAYER 6", CheatAction::Darkside, 6, false},
    {"DARKSIDE7", "DESTROY PLAYER 7", CheatAction::Darkside, 7, false},
    {"DARKSIDE8", "DESTROY PLAYER 8", CheatAction::Darkside, 8, false},
};

// Terrain ids from genie_x1.dat's terrain table.
enum : uint8_t {
    T_GRASS1 = 0, T_WATER1 = 1, T_SHORE = 2, T_DIRT3 = 3, T_DIRT1 = 6, T_GRASS3 = 9,
    T_FARM = 7, T_DIRT2 = 11, T_GRASS2 = 12, T_SAND = 14, T_WATER2 = 22, T_WATER3 = 23,
    T_FARM_GUNGAN = 48,
};

struct SpriteDraw {
    int64_t key;
    Texture *tex;
    Quad q;
    Texture *outlineTex = nullptr;
    uint32_t ownerId = 0;
    bool outlineCandidate = false;
    bool occludes = true;
};

Quad clippedQuad(const Quad &quad, float left, float top,
                 float right, float bottom) {
    const float x0 = (left - quad.x) / quad.w;
    const float y0 = (top - quad.y) / quad.h;
    const float x1 = (right - quad.x) / quad.w;
    const float y1 = (bottom - quad.y) / quad.h;
    return {
        left, top, right - left, bottom - top,
        quad.u0 + (quad.u1 - quad.u0) * x0,
        quad.v0 + (quad.v1 - quad.v0) * y0,
        quad.u0 + (quad.u1 - quad.u0) * x1,
        quad.v0 + (quad.v1 - quad.v0) * y1,
    };
}

struct TerrainInfluence {
    int terrain = 0;
    uint8_t directions = 0;
};

int blendModeFor(int here, int there) {
    static constexpr int modes[10][10] = {
        {2, 3, 2, 1, 1, 6, 5, 4, 8, 9},
        {3, 3, 3, 3, 3, 3, 3, 3, 3, 3},
        {2, 3, 2, 1, 1, 6, 1, 4, 8, 9},
        {1, 3, 1, 0, 7, 6, 5, 4, 10, 9},
        {1, 3, 1, 7, 7, 6, 5, 4, 8, 9},
        {6, 3, 6, 6, 6, 6, 5, 4, 8, 9},
        {5, 3, 1, 5, 5, 5, 5, 4, 5, 9},
        {4, 3, 4, 4, 4, 4, 4, 4, 4, 9},
        {8, 3, 8, 10, 8, 8, 5, 4, 8, 10},
        {9, 3, 9, 9, 9, 9, 9, 9, 10, 9},
    };
    here = std::max(0, std::min(9, here));
    there = std::max(0, std::min(9, there));
    return modes[here][there];
}

int blendMasksFor(uint8_t bits, int tx, int ty, std::array<int, 5> &masks) {
    int count = 0, edge = -1;
    switch (bits & 0xAA) {
    case 0x08: edge = 0; break;
    case 0x02: edge = 4; break;
    case 0x20: edge = 8; break;
    case 0x80: edge = 12; break;
    case 0x22: edge = 20; break;
    case 0x88: edge = 21; break;
    case 0xA0: edge = 22; break;
    case 0x82: edge = 23; break;
    case 0x28: edge = 24; break;
    case 0x0A: edge = 25; break;
    case 0x2A: edge = 26; break;
    case 0xA8: edge = 27; break;
    case 0xA2: edge = 28; break;
    case 0x8A: edge = 29; break;
    case 0xAA: edge = 30; break;
    }
    if (edge >= 0) {
        if (edge <= 12) edge += (tx + ty) & 3;
        masks[count++] = edge;
    }
    static constexpr int cornerMasks[4] = {18, 16, 17, 19};
    for (int i = 0; i < 4; i++)
        if (bits & (1 << (i * 2))) masks[count++] = cornerMasks[i];
    return count;
}

// Cheap deterministic value noise for map generation.
float hash2(int x, int y, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}

float valueNoise(float x, float y, uint32_t seed) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    float a = hash2(xi, yi, seed), b = hash2(xi + 1, yi, seed);
    float c = hash2(xi, yi + 1, seed), d = hash2(xi + 1, yi + 1, seed);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

float fbm(float x, float y, uint32_t seed) {
    return valueNoise(x, y, seed) * 0.6f + valueNoise(x * 2.1f, y * 2.1f, seed + 17) * 0.3f +
           valueNoise(x * 4.3f, y * 4.3f, seed + 41) * 0.1f;
}

uint8_t slopeForCorners(uint8_t north, uint8_t east, uint8_t south, uint8_t west, uint8_t &base) {
    base = std::min(std::min(north, east), std::min(south, west));
    const uint8_t high = std::max(std::max(north, east), std::max(south, west));
    if (high == base) return 0;
    if (high != base + 1) return 0;
    uint8_t bits = (north > base ? 1 : 0) | (east > base ? 2 : 0) |
                   (south > base ? 4 : 0) | (west > base ? 8 : 0);
    static constexpr uint8_t slopes[16] = {
        0, 2, 4, 8, 1, 10, 7, 15, 3, 6, 12, 14, 5, 16, 13, 0,
    };
    return slopes[bits];
}

const dat::Terrain &drawTerrain(const std::vector<dat::Terrain> &terrains, int id) {
    const dat::Terrain *terrain = &terrains[(size_t)id];
    for (size_t depth = 0; depth < terrains.size(); depth++) {
        const int alias = terrain->terrainToDraw;
        if (alias < 0 || (size_t)alias >= terrains.size() || &terrains[(size_t)alias] == terrain) break;
        terrain = &terrains[(size_t)alias];
    }
    return *terrain;
}

inline void toScreen(float x, float y, float &sx, float &sy) {
    sx = (x - y) * Game::kTileHalfW;
    sy = (x + y) * Game::kTileHalfH;
}

} // namespace

bool Game::init(uint32_t seed, int mapSize, std::string *err) {
    rng_.seed(seed);
    players_ = {};
    resources_ = {};
    researchedTechs_ = {};
    disabledTechs_ = {};
    disabledUnits_ = {};
    triggers_.clear();
    triggerOrder_.clear();
    triggerRuntime_.clear();
    instructions_.clear();
    currentInstruction_.clear();
    instructionTime_ = 0;
    cursorVisible_ = false;
    boxSelectActive_ = false;
    commandMarkerTime_ = 0;
    attackOrdersIssued_ = attacksLanded_ = unitsKilled_ = 0;
    projectilesLaunched_ = attackPathsComputed_ = attackApproachRetries_ = 0;
    automaticTargetsAcquired_ = retaliationOrders_ = armedBuildingsEngaged_ = 0;
    attackModeChanges_ = 0;
    objects_.clear();
    selectionOrder_.clear();
    actionMenuOpen_ = false;
    actionMenuObjectId_ = 0;
    actionMenuTab_ = ActionMenuTab::Units;
    actionMenuSelection_ = 0;
    actionMenuScroll_ = 0;
    placementUnit_ = nullptr;
    placementBuilderId_ = 0;
    cheatMenuOpen_ = false;
    cheatMenuSelection_ = 0;
    forceBuildCheat_ = false;
    fullTechTreeCheat_ = false;
    ambienceTime_ = 2.0f;
    ambienceSequence_ = 0;
    forceExploreCheat_ = false;
    forceSightCheat_ = false;
    garrisonCursorActive_ = false;
    repairCursorActive_ = false;
    statusMessage_.clear();
    statusTime_ = 0;
    objectIndices_.clear();
    projectiles_.clear();
    remains_.clear();
    combatObjectIndices_.clear();
    combatObjectCells_.clear();
    selectionClickAge_ = 1000.0f;
    lastSelectionUnitId_ = -1;
    nextSpawnId_ = 1;
    victoryState_ = -1;
    warnedEffects_.clear();
    warnedConditions_.clear();
    for (size_t i = 0; i < players_.size(); i++) players_[i].color = (uint32_t)i;
    players_[0].active = true;
    players_[0].human = true;
    players_[0].civilization = 3;
    players_[0].populationLimit = 200;
    players_[0].diplomacy[2] = 3;
    players_[1].active = true;
    players_[1].civilization = 1;
    players_[1].diplomacy[1] = 3;
    localPlayer_ = 1;
    mapSize_ = mapSize;
    generateTerrain(mapSize);

    // Two bases: Rebel Alliance (civ 3) vs Galactic Empire (civ 1).
    spawnBase(1, 3, 'R', mapSize * 0.30f, mapSize * 0.35f);
    spawnBase(2, 1, 'E', mapSize * 0.62f, mapSize * 0.60f);
    // Keep the generated sandbox quiet until its opposing bases are engaged.
    for (Object &object : objects_)
        if (canAttack(object))
            object.attackMode = AttackMode::Defensive;

    // Some wildlife for gaia.
    std::uniform_real_distribution<float> pos(4.0f, mapSize - 4.0f);
    for (int i = 0; i < 8; i++) {
        float x = pos(rng_), y = pos(rng_);
        if (terrainAt((int)x, (int)y) == T_WATER1 || terrainAt((int)x, (int)y) == T_WATER2) continue;
        if (Object *o = spawn(0, i % 2 ? "ANIMAL-NERF" : "ANIMAL-BANTHA", 0, x, y, 0)) (void)o;
    }
    if (objects_.empty()) {
        if (err) *err = "no units could be spawned (dat/graphics mismatch?)";
        return false;
    }
    rebuildAdjacency();
    refreshAllAutomaticTechnologies();
    lookAt(mapSize * 0.30f + 2, mapSize * 0.35f + 2);
    return true;
}

bool Game::initCompactTestMap(
    uint32_t seed, int mapSize,
    std::string *err) {
    if (!init(seed, mapSize, err))
        return false;
    const float testX = mapSize * 0.30f;
    const float testY = mapSize * 0.35f;
    spawn(3, "BLDG-TRAINFOOT1", 1,
          testX + 10.0f, testY + 7.0f, 0);
    spawn(3, "BLDG-SHLDGEN1", 1,
          testX + 15.0f, testY + 7.0f, 0);
    float gateX = testX + 2.0f;
    float gateY = testY + 12.0f;
    const dat::Unit *gateUnit =
        findUnit(3, "BLDG-ENTRYA1CLOS");
    if (gateUnit)
        snapAdjacentBuildingPosition(
            *gateUnit, gateX, gateY);
    spawn(3, "BLDG-ENTRYA1CLOS", 1,
          gateX, gateY, 0);
    if (gateUnit) {
        for (const dat::BuildingAnnex &annex :
             gateUnit->annexes) {
            const dat::Unit *part =
                findUnit(3, annex.unitId);
            if (!part) continue;
            Object *created = addObject(
                part, 1,
                gateX + annex.misplacementY,
                gateY - annex.misplacementX,
                0.0f, 0, 0, false, -1, false);
            if (created) created->wander = false;
        }
    }
    for (int side : {-1, 1})
        for (int segment = 0; segment < 5;
             segment++)
            spawn(3, "BLDG-FENCE", 1,
                  gateX,
                  gateY +
                      side * (2.5f + segment),
                  0);
    spawn(3, "BLDG-DWELLING1", 1,
          testX + 13.0f, testY + 7.0f, 0);
    spawn(3, "BLDG-DWELLING1", 1,
          testX + 26.0f, testY + 7.0f, 0);
    spawn(0, "ANIMAL-NERF", 0,
          testX + 8.0f, testY + 12.0f, 0);
    static constexpr const char *resources[] = {
        "OBJ-VEGETABLE", "OBJ-BULLION",
        "OBJ-MINERAL", "OBJ-TIMBERA",
    };
    for (size_t type = 0;
         type < std::size(resources); type++)
        for (int index = 0; index < 5; index++)
            spawn(
                0, resources[type], 0,
                testX - 9.0f + type * 2.25f,
                testY + 8.0f + index * 1.1f,
                0);
    rebuildAdjacency();
    refreshAllAutomaticTechnologies();
    lookAt(testX + 2.0f, testY + 5.0f);
    return true;
}

bool Game::initScenario(const Scenario &scenario, std::string *err) {
    if (!scenario.map.width || scenario.map.width != scenario.map.height) {
        if (err) *err = "only square scenario maps are currently supported";
        return false;
    }
    if (scenario.map.tiles.size() != (size_t)scenario.map.width * scenario.map.height) {
        if (err) *err = "scenario map tile count does not match its dimensions";
        return false;
    }
    const auto &terrains = assets_.dat().terrainBlock.terrains;
    for (const ScenarioTile &tile : scenario.map.tiles) {
        if ((size_t)tile.terrain >= terrains.size()) {
            if (err) *err = "scenario references terrain " + std::to_string(tile.terrain) +
                            ", but the dat only contains " + std::to_string(terrains.size());
            return false;
        }
    }

    rng_.seed(1);
    players_ = scenario.players;
    resources_ = {};
    researchedTechs_ = {};
    disabledTechs_ = {};
    disabledUnits_ = {};
    for (size_t i = 0; i < players_.size(); i++) {
        const int player = (int)i + 1;
        resources_[(size_t)player][0] = players_[i].resources[0]; // food
        resources_[(size_t)player][1] = players_[i].resources[1]; // wood
        resources_[(size_t)player][2] = players_[i].resources[3]; // stone
        resources_[(size_t)player][3] = players_[i].resources[2]; // gold
        resources_[(size_t)player][4] = players_[i].resources[4];
    }
    triggers_ = scenario.triggers;
    triggerOrder_ = scenario.triggerOrder;
    triggerRuntime_.resize(triggers_.size());
    for (size_t i = 0; i < triggers_.size(); i++)
        triggerRuntime_[i].enabled = triggers_[i].enabled;
    instructions_.clear();
    currentInstruction_.clear();
    instructionTime_ = 0;
    cursorVisible_ = false;
    boxSelectActive_ = false;
    commandMarkerTime_ = 0;
    attackOrdersIssued_ = attacksLanded_ = unitsKilled_ = 0;
    projectilesLaunched_ = attackPathsComputed_ = attackApproachRetries_ = 0;
    automaticTargetsAcquired_ = retaliationOrders_ = armedBuildingsEngaged_ = 0;
    attackModeChanges_ = 0;
    objectIndices_.clear();
    projectiles_.clear();
    remains_.clear();
    combatObjectIndices_.clear();
    combatObjectCells_.clear();
    selectionClickAge_ = 1000.0f;
    lastSelectionUnitId_ = -1;
    nextSpawnId_ = scenario.nextUnitId;
    victoryState_ = -1;
    warnedEffects_.clear();
    warnedConditions_.clear();
    localPlayer_ = 0;
    for (size_t i = 0; i < players_.size(); i++) {
        if (players_[i].active && players_[i].human) {
            localPlayer_ = (int)i + 1;
            break;
        }
    }
    mapSize_ = (int)scenario.map.width;
    terrain_.resize(scenario.map.tiles.size());
    // Rotate scenario world coordinates 90 degrees counterclockwise. Transforming
    // the source data keeps slope geometry, objects, facings, and art aligned.
    for (int y = 0; y < mapSize_; y++)
        for (int x = 0; x < mapSize_; x++)
            terrain_[(size_t)y * mapSize_ + x] =
                scenario.map.tiles[(size_t)x * mapSize_ + (mapSize_ - 1 - y)].terrain;

    // Player-unit block 0 is Gaia; block N belongs to player N and uses
    // player-info entry N-1. Gungans are the only civilization with their
    // own garden terrain. Scenario saves can retain TERR-FARMG for every
    // garden, so normalize it when the map has no Gungan player.
    auto civilizationFor = [&](uint8_t player) -> int {
        if (player == 0) return 0;
        const size_t index = (size_t)player - 1;
        return index < scenario.civilizations.size() ? (int)scenario.civilizations[index] : -1;
    };
    for (const ScenarioUnit &unit : scenario.units) {
        if (unit.unitId != 50) continue; // BLDG-GARDEN
        const uint8_t farmTerrain = civilizationFor(unit.player) == 2 ? T_FARM_GUNGAN : T_FARM;
        const int centerX = (int)std::floor(unit.y);
        const int centerY = (int)std::floor(mapSize_ - unit.x);
        for (int y = centerY - 1; y <= centerY + 1; y++)
            for (int x = centerX - 1; x <= centerX + 1; x++) {
                if (x < 0 || y < 0 || x >= mapSize_ || y >= mapSize_) continue;
                uint8_t &terrain = terrain_[(size_t)y * mapSize_ + x];
                if (terrain == T_FARM || terrain == T_FARM_GUNGAN) terrain = farmTerrain;
            }
    }

    const size_t stride = (size_t)mapSize_ + 1;
    cornerElevation_.resize(stride * stride);
    for (int y = 0; y <= mapSize_; y++) {
        for (int x = 0; x <= mapSize_; x++) {
            const int sourceX = std::max(0, std::min(mapSize_ - 1, mapSize_ - y));
            const int sourceY = std::min(x, mapSize_ - 1);
            cornerElevation_[(size_t)y * stride + x] =
                scenario.map.tiles[(size_t)sourceY * mapSize_ + sourceX].elevation;
        }
    }
    buildTileElevation();
    std::set<std::pair<int32_t, uint8_t>> canonicalSlopes;
    for (size_t i = 0; i < terrain_.size(); i++) {
        const uint8_t slope = tileSlope_[i];
        if (!slope) continue;
        const dat::Terrain &draw = drawTerrain(terrains, terrain_[i]);
        canonicalSlopes.insert({draw.slp, slope});
    }
    std::array<int8_t, 8> canonicalNeighbors;
    canonicalNeighbors.fill(0);
    for (const auto &entry : canonicalSlopes) {
        const SpriteSheet *sheet = assets_.terrainSheet(entry.first);
        const size_t variants = sheet && !sheet->frames.empty() ? 1 : 0;
        for (size_t frame = 0; frame < variants; frame++)
            assets_.terrainSlopeFrame(entry.first, entry.second, frame, canonicalNeighbors);
    }

    objects_.clear();
    selectionOrder_.clear();
    actionMenuOpen_ = false;
    actionMenuObjectId_ = 0;
    actionMenuTab_ = ActionMenuTab::Units;
    actionMenuSelection_ = 0;
    actionMenuScroll_ = 0;
    placementUnit_ = nullptr;
    placementBuilderId_ = 0;
    cheatMenuOpen_ = false;
    cheatMenuSelection_ = 0;
    forceBuildCheat_ = false;
    fullTechTreeCheat_ = false;
    ambienceTime_ = 2.0f;
    ambienceSequence_ = 0;
    forceExploreCheat_ = false;
    forceSightCheat_ = false;
    garrisonCursorActive_ = false;
    repairCursorActive_ = false;
    statusMessage_.clear();
    statusTime_ = 0;
    objects_.reserve(scenario.units.size() * 2);
    for (const ScenarioUnit &source : scenario.units) {
        const int civilization = civilizationFor(source.player);
        const dat::Unit *unit = findUnit(civilization, source.unitId);
        if (!unit) continue;
        const float x = source.y;
        const float y = mapSize_ - source.x;
        auto addScenarioObject = [&](const dat::Unit *part, float partX, float partY,
                                     uint16_t initialFrame, uint32_t spawnId, bool hidden) {
            const dat::Graphic *graphic = assets_.dat().graphic(part->standingGraphic[0]);
            const float facing = part->adjacentMode && graphic && graphic->angleCount == 5
                                     ? source.rotation
                                     : source.rotation - kPi * 0.5f;
            Object *object = addObject(part, source.player, partX, partY, facing, spawnId,
                                       initialFrame, hidden, source.garrisonedInId, spawnId != 0);
            if (object) {
                object->wander = false;
                // Its detached layer-5 silhouette reads as a second ship over the landed prop.
                object->drawShadows = part->name != "BLDG-LLAMBDASH";
            }
        };
        const bool hidden = source.garrisonedInId >= 0;
        addScenarioObject(unit, x, y, source.initialFrame, source.spawnId, hidden);
        if (!hidden && unit->type == dat::UT_Building) {
            for (const dat::BuildingAnnex &annex : unit->annexes) {
                if (annex.unitId < 0) continue;
                const dat::Unit *part = findUnit(civilization, annex.unitId);
                if (!part) continue;
                addScenarioObject(part, x + annex.misplacementY, y - annex.misplacementX, 0, 0, false);
            }
        }
    }
    for (Object &object : objects_)
        configureGate(object);
    for (const ScenarioTrigger &trigger : triggers_)
        for (const ScenarioEffect &effect : trigger.effects)
            if (effect.type == 6 || effect.type == 7)
                for (uint32_t spawnId : effect.selectedUnitIds)
                    if (Object *object = findObject(spawnId))
                        configureGate(*object);
    rebuildAdjacency();
    refreshAllAutomaticTechnologies();
    const Object *hero = nullptr;
    size_t heroCount = 0;
    for (const Object &object : objects_) {
        if (!object.active || object.hidden || object.player != localPlayer_ ||
            !object.triggerAddressable || !object.unit->heroMode)
            continue;
        hero = &object;
        heroCount++;
    }
    if (heroCount == 1) {
        lookAt(hero->x, hero->y);
        log("camera focused on human player hero " + hero->unit->name);
    } else if (scenario.cameraX >= 0 && scenario.cameraY >= 0) {
        lookAt(scenario.cameraY, mapSize_ - scenario.cameraX);
    } else {
        lookAt(mapSize_ * 0.5f, mapSize_ * 0.5f);
    }
    return true;
}

void Game::generateTerrain(int size) {
    terrain_.assign((size_t)size * size, T_GRASS1);
    cornerElevation_.assign((size_t)(size + 1) * (size + 1), 0);
    uint32_t seed = rng_();
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float h = fbm(x / 9.0f, y / 9.0f, seed);
            float m = fbm(x / 6.0f + 100, y / 6.0f, seed + 99);
            uint8_t t;
            if (h < 0.28f) t = h < 0.22f ? T_WATER2 : T_WATER1;
            else if (h < 0.31f) t = T_SAND;
            else if (m > 0.66f) t = T_DIRT1;
            else if (m > 0.58f) t = T_DIRT2;
            else if (m < 0.30f) t = T_GRASS2;
            else t = T_GRASS1;
            terrain_[(size_t)y * size + x] = t;
        }
    }

    // A deterministic two-level hill exercises every elevation-aware path in
    // the prototype while leaving the generated terrain and base layout intact.
    const float hillX = size * 0.30f - 7.0f;
    const float hillY = size * 0.35f - 7.0f;
    for (int y = 0; y <= size; y++) {
        for (int x = 0; x <= size; x++) {
            const float dx = x - hillX, dy = y - hillY;
            const float distance = std::sqrt(dx * dx + dy * dy);
            cornerElevation_[(size_t)y * (size + 1) + x] =
                distance < 3.25f ? 2 : distance < 5.75f ? 1 : 0;
        }
    }
    buildTileElevation();
}

void Game::buildTileElevation() {
    tileElevation_.assign((size_t)mapSize_ * mapSize_, 0);
    tileSlope_.assign((size_t)mapSize_ * mapSize_, 0);
    for (int y = 0; y < mapSize_; y++) {
        for (int x = 0; x < mapSize_; x++) {
            const uint8_t north = cornerElevation_[(size_t)y * (mapSize_ + 1) + x];
            const uint8_t east = cornerElevation_[(size_t)y * (mapSize_ + 1) + x + 1];
            const uint8_t south = cornerElevation_[(size_t)(y + 1) * (mapSize_ + 1) + x + 1];
            const uint8_t west = cornerElevation_[(size_t)(y + 1) * (mapSize_ + 1) + x];
            uint8_t &elevation = tileElevation_[(size_t)y * mapSize_ + x];
            tileSlope_[(size_t)y * mapSize_ + x] =
                slopeForCorners(north, east, south, west, elevation);
        }
    }
}

float Game::elevationAt(float x, float y) const {
    if (cornerElevation_.empty()) return 0;
    int tx = std::max(0, std::min(mapSize_ - 1, (int)std::floor(x)));
    int ty = std::max(0, std::min(mapSize_ - 1, (int)std::floor(y)));
    float fx = std::max(0.0f, std::min(1.0f, x - tx));
    float fy = std::max(0.0f, std::min(1.0f, y - ty));
    const size_t stride = (size_t)mapSize_ + 1;
    float north = cornerElevation_[(size_t)ty * stride + tx];
    float east = cornerElevation_[(size_t)ty * stride + tx + 1];
    float south = cornerElevation_[(size_t)(ty + 1) * stride + tx + 1];
    float west = cornerElevation_[(size_t)(ty + 1) * stride + tx];
    return north * (1 - fx) * (1 - fy) + east * fx * (1 - fy) +
           south * fx * fy + west * (1 - fx) * fy;
}

const dat::Unit *Game::findUnit(int civ, const std::string &name) const {
    const auto &civs = assets_.dat().civs;
    if (civ < 0 || (size_t)civ >= civs.size()) return nullptr;
    for (const auto &u : civs[civ].units)
        if (u.exists && u.name == name) return &u;
    // Fall back to a substring match (gaia names differ between releases).
    for (const auto &u : civs[civ].units)
        if (u.exists && u.name.find(name) != std::string::npos) return &u;
    return nullptr;
}

const dat::Unit *Game::findUnit(int civ, int id) const {
    const auto &civs = assets_.dat().civs;
    if (civ < 0 || (size_t)civ >= civs.size()) return nullptr;
    const auto &units = civs[(size_t)civ].units;
    return id >= 0 && (size_t)id < units.size() && units[(size_t)id].exists ? &units[(size_t)id] : nullptr;
}

Game::Object *Game::addObject(const dat::Unit *unit, int player, float x, float y, float facing,
                              uint32_t spawnId, uint16_t initialFrame, bool hidden,
                              int32_t garrisonedInId, bool triggerAddressable) {
    if (!unit) return nullptr;
    Object object;
    object.unit = unit;
    object.player = player;
    object.x = object.homeX = object.moveAnchorX = object.targetX = x;
    object.y = object.homeY = object.moveAnchorY = object.targetY = y;
    object.hitPoints = object.maxHitPoints = std::max(
        1, (int)std::lround(modifiedUnitAttribute(
               object, 0, unit->hitPoints)));
    for (const dat::ResourceStorage &storage :
         unit->resourceStorages)
        if (storage.type >= 0 && storage.type <= 3 &&
            storage.amount > 0) {
            object.resourceType = storage.type;
            object.resourceAmount = storage.amount;
            break;
        }
    object.facing = facing;
    object.spawnId = spawnId;
    object.autoAcquireTime = 0.1f + (spawnId % 8) * 0.05f;
    if (unit->type == dat::UT_Building || unit->speed <= 0)
        object.attackMode = AttackMode::StandGround;
    object.initialFrame = initialFrame;
    object.hidden = hidden;
    object.draw = unit->name.rfind("ENGINE-", 0) != 0 &&
                  unit->name.rfind("OBJ-FLAG", 0) != 0;
    object.garrisonedInId = garrisonedInId;
    object.triggerAddressable = triggerAddressable;
    objects_.push_back(object);
    if (spawnId) objectIndices_[spawnId] = objects_.size() - 1;
    return &objects_.back();
}

bool Game::configureGate(Object &object) {
    if (!object.unit) return false;
    const std::string &name = object.unit->name;
    const size_t closed = name.rfind("CLOS");
    if (closed == std::string::npos ||
        closed + 4 != name.size())
        return false;
    const std::string prefix = name.substr(0, closed);
    const int civilization =
        civilizationForPlayer(object.player);
    const dat::Unit *open =
        findUnit(civilization, prefix + "OPEN");
    const dat::Unit *end =
        findUnit(civilization, prefix + "END");
    if (!open || !end) return false;
    object.gate = true;
    object.gateClosedUnit = object.unit;
    object.gateOpenUnit = open;
    object.gateEndUnit = end;
    return true;
}

Game::Object *Game::spawn(int civ, const std::string &name, int player, float x, float y, float facing) {
    const dat::Unit *u = findUnit(civ, name);
    if (!u) return nullptr;
    Object *o = addObject(u, player, x, y, facing, nextSpawnId_++);
    if (!o) return nullptr;
    configureGate(*o);
    std::uniform_real_distribution<float> r01(0, 1);
    o->animTime = r01(rng_) * 3.0f;
    o->stateTime = 1.0f + r01(rng_) * 4.0f;
    return o;
}

Game::Object *Game::findObject(uint32_t spawnId) {
    if (!spawnId) return nullptr;
    const auto found = objectIndices_.find(spawnId);
    return found != objectIndices_.end() && found->second < objects_.size()
               ? &objects_[found->second]
               : nullptr;
}

const Game::Object *Game::findObject(uint32_t spawnId) const {
    if (!spawnId) return nullptr;
    const auto found = objectIndices_.find(spawnId);
    return found != objectIndices_.end() && found->second < objects_.size()
               ? &objects_[found->second]
               : nullptr;
}

int Game::civilizationForPlayer(int player) const {
    if (player == 0) return 0;
    const size_t index = (size_t)player - 1;
    return index < players_.size() ? (int)players_[index].civilization : -1;
}

void Game::rebuildAdjacency() {
    pathGridCache_.clear();
    std::vector<Object *> adjacentObjects, adjacentWalls;
    for (Object &object : objects_) {
        if (!object.active || object.hidden || !object.unit->adjacentMode) continue;
        adjacentObjects.push_back(&object);
        const dat::Graphic *graphic = assets_.dat().graphic(object.unit->standingGraphic[0]);
        if (graphic && graphic->angleCount == 5) adjacentWalls.push_back(&object);
    }
    for (Object *object : adjacentWalls) {
        bool north = false, east = false, south = false, west = false;
        for (const Object *neighbor : adjacentObjects) {
            if (neighbor == object || neighbor->player != object->player) continue;
            const float dx = neighbor->x - object->x;
            const float dy = neighbor->y - object->y;
            if (std::fabs(dx) < 0.01f && std::fabs(dy + 1.0f) < 0.01f) north = true;
            if (std::fabs(dx - 1.0f) < 0.01f && std::fabs(dy) < 0.01f) east = true;
            if (std::fabs(dx) < 0.01f && std::fabs(dy - 1.0f) < 0.01f) south = true;
            if (std::fabs(dx + 1.0f) < 0.01f && std::fabs(dy) < 0.01f) west = true;
        }
        const int horizontal = (east ? 1 : 0) + (west ? 1 : 0);
        const int vertical = (north ? 1 : 0) + (south ? 1 : 0);
        int connectionFrame = 2;
        if (horizontal && !vertical) connectionFrame = horizontal == 2 ? 1 : 4;
        if (vertical && !horizontal) connectionFrame = vertical == 2 ? 0 : 3;
        object->facing = connectionFrame * 2.0f * kPi / 5.0f;
    }

    mobileObjectIndices_.clear();
    combatObjectIndices_.clear();
    staticObstructionIndices_.clear();
    staticObstructionCells_.assign((size_t)mapSize_ * mapSize_, {});
    for (size_t index = 0; index < objects_.size(); index++) {
        const Object &object = objects_[index];
        if (!object.active || object.hidden) continue;
        const dat::Unit *obstructionUnit = object.unit;
        if (object.felled) continue;
        if (object.draw && object.spawnId &&
            object.unit->type >= dat::UT_Combatant)
            combatObjectIndices_.push_back((uint32_t)index);
        const bool mobile = object.unit->speed > 0 && object.unit->type != dat::UT_Building;
        if (mobile) {
            mobileObjectIndices_.push_back((uint32_t)index);
            continue;
        }
        if (obstructionUnit->obstructionType == 0)
            continue;
        staticObstructionIndices_.push_back((uint32_t)index);
        const float halfX =
            std::max(0.05f, obstructionUnit->collisionSize[0]);
        const float halfY =
            std::max(0.05f, obstructionUnit->collisionSize[1]);
        const int minX = std::max(0, (int)std::floor(object.x - halfX));
        const int maxX = std::min(mapSize_ - 1, (int)std::floor(object.x + halfX));
        const int minY = std::max(0, (int)std::floor(object.y - halfY));
        const int maxY = std::min(mapSize_ - 1, (int)std::floor(object.y + halfY));
        for (int y = minY; y <= maxY; y++)
            for (int x = minX; x <= maxX; x++)
                staticObstructionCells_[(size_t)y * mapSize_ + x].push_back((uint32_t)index);
    }
}

void Game::rebuildMobileOccupancy() {
    constexpr float cellSize = 4.0f;
    mobileObjectGridWidth_ = std::max(1, (mapSize_ + 3) / 4);
    const size_t cellCount = (size_t)mobileObjectGridWidth_ * mobileObjectGridWidth_;
    if (mobileObjectCells_.size() != cellCount)
        mobileObjectCells_.assign(cellCount, {});
    else
        for (auto &cell : mobileObjectCells_) cell.clear();
    if (combatObjectCells_.size() != cellCount)
        combatObjectCells_.assign(cellCount, {});
    else
        for (auto &cell : combatObjectCells_) cell.clear();
    maxMobileCollisionRadius_ = 0;
    for (uint32_t index : mobileObjectIndices_) {
        const Object &object = objects_[(size_t)index];
        if (!object.active || object.hidden) continue;
        maxMobileCollisionRadius_ =
            std::max(maxMobileCollisionRadius_, collisionRadius(object));
        const int x = std::max(
            0, std::min(mobileObjectGridWidth_ - 1, (int)std::floor(object.x / cellSize)));
        const int y = std::max(
            0, std::min(mobileObjectGridWidth_ - 1, (int)std::floor(object.y / cellSize)));
        mobileObjectCells_[(size_t)y * mobileObjectGridWidth_ + x].push_back(index);
    }
    for (uint32_t index : combatObjectIndices_) {
        const Object &object = objects_[(size_t)index];
        if (!object.active || object.hidden || !object.draw || !object.spawnId) continue;
        const int x = std::max(
            0, std::min(mobileObjectGridWidth_ - 1, (int)std::floor(object.x / cellSize)));
        const int y = std::max(
            0, std::min(mobileObjectGridWidth_ - 1, (int)std::floor(object.y / cellSize)));
        combatObjectCells_[(size_t)y * mobileObjectGridWidth_ + x].push_back(index);
    }
}

void Game::updateLivestockOwnership() {
    constexpr float captureRadius = 4.0f;
    for (Object &livestock : objects_) {
        if (!livestock.active || livestock.hidden ||
            !livestock.unit ||
            livestock.unit->cls != 1 ||
            livestock.unit->name.rfind(
                "ANIMAL-CAPTURE", 0) != 0)
            continue;
        int closestPlayer = 0;
        float closestDistance =
            captureRadius * captureRadius;
        for (uint32_t index : mobileObjectIndices_) {
            const Object &unit =
                objects_[(size_t)index];
            if (!unit.active || unit.hidden ||
                unit.player <= 0 || !unit.unit ||
                unit.unit->cls == 1 ||
                unit.unit->type <
                    dat::UT_Combatant)
                continue;
            const float dx = unit.x - livestock.x;
            const float dy = unit.y - livestock.y;
            const float distance =
                dx * dx + dy * dy;
            if (distance < closestDistance) {
                closestDistance = distance;
                closestPlayer = unit.player;
            }
        }
        if (closestPlayer > 0 &&
            closestPlayer != livestock.player)
            livestock.player = closestPlayer;
    }
}

void Game::spawnBase(int player, int civ, char L, float cx, float cy) {
    // Clear the base area of water so buildings sit on land.
    for (int y = (int)cy - 8; y <= (int)cy + 12; y++)
        for (int x = (int)cx - 8; x <= (int)cx + 16; x++)
            if (x >= 0 && y >= 0 && x < mapSize_ && y < mapSize_) {
                uint8_t &t = terrain_[(size_t)y * mapSize_ + x];
                if (t == T_WATER1 || t == T_WATER2 || t == T_SAND) t = T_GRASS1;
            }
    const std::string sL(1, L);
    spawn(civ, "BLDG-MAIN1", player, cx + 2.0f, cy + 2.0f, 0);
    spawn(civ, "BLDG-DWELLING1", player, cx - 3.0f, cy + 1.0f, 0);
    spawn(civ, "BLDG-DWELLING1", player, cx - 3.0f, cy + 4.0f, 0);
    spawn(civ, "BLDG-TRAINRANGE1", player, cx + 5.5f, cy - 2.5f, 0);
    spawn(civ, "BLDG-DEFENSEA1", player, cx + 6.0f, cy + 5.0f, 0);

    std::uniform_real_distribution<float> off(-3.0f, 3.0f);
    std::uniform_real_distribution<float> ang(0, 2 * kPi);
    for (int i = 0; i < 5; i++)
        spawn(civ, "UNIT-WORKERA1", player, cx + off(rng_), cy + 6 + off(rng_) * 0.5f, ang(rng_));
    for (int i = 0; i < 4; i++)
        spawn(civ, "UNIT-TTRP" + sL, player, cx + 8 + i * 0.9f, cy + 1 + (i % 2) * 0.9f, ang(rng_));
    spawn(civ, "UNIT-JKNT" + sL, player, cx + 1, cy - 2, ang(rng_));
    spawn(civ, "UNIT-M1TR" + sL, player, cx + 9, cy + 4, ang(rng_));
    spawn(civ, "UNIT-TAAT" + sL, player, cx + 11, cy + 2, ang(rng_));
}

namespace {

int triggerField(const std::vector<int32_t> &fields, size_t index) {
    return index < fields.size() ? fields[index] : -1;
}

const char *effectLabel(int type) {
    static const char *labels[] = {
        "None", "Change Diplomacy", "Research Technology", "Send Chat", "Play Sound",
        "Send Tribute", "Unlock Gate", "Lock Gate", "Activate Trigger", "Deactivate Trigger",
        "AI Script Goal", "Create Object", "Task Object", "Declare Victory", "Kill Object",
        "Remove Object", "Change View", "Unload", "Change Ownership", "Patrol",
        "Display Instructions", "Clear Instructions", "Freeze Unit", "Advanced Buttons",
        "Damage Object", "Place Foundation", "Change Object Name", "Change Object HP",
        "Change Object Attack", "Stop Unit", "Snap View", "Unknown", "Enable Tech",
        "Disable Tech", "Enable Unit", "Disable Unit", "Flash Objects",
    };
    return type >= 0 && (size_t)type < std::size(labels) ? labels[type] : "Unknown";
}

const char *conditionLabel(int type) {
    static const char *labels[] = {
        "None", "Bring Object to Area", "Bring Object to Object", "Own Objects",
        "Own Fewer Objects", "Objects in Area", "Destroy Object", "Capture Object",
        "Accumulate Attribute", "Research Technology", "Timer", "Object Selected",
        "AI Signal", "Player Defeated", "Object Has Target", "Object Visible",
        "Object Not Visible", "Researching Technology", "Units Garrisoned",
        "Difficulty Level", "Own Fewer Foundations", "Selected Objects in Area",
        "Powered Objects in Area", "Units Queued Past Pop Cap",
    };
    return type >= 0 && (size_t)type < std::size(labels) ? labels[type] : "Unknown";
}

uint64_t glyphBits(char c) {
#define GLYPH(a, b, c, d, e, f, g) \
    ((uint64_t)(a) | (uint64_t)(b) << 5 | (uint64_t)(c) << 10 | (uint64_t)(d) << 15 | \
     (uint64_t)(e) << 20 | (uint64_t)(f) << 25 | (uint64_t)(g) << 30)
    c = (char)std::toupper((unsigned char)c);
    switch (c) {
    case 'A': return GLYPH(14, 17, 17, 31, 17, 17, 17);
    case 'B': return GLYPH(30, 17, 17, 30, 17, 17, 30);
    case 'C': return GLYPH(14, 17, 16, 16, 16, 17, 14);
    case 'D': return GLYPH(30, 17, 17, 17, 17, 17, 30);
    case 'E': return GLYPH(31, 16, 16, 30, 16, 16, 31);
    case 'F': return GLYPH(31, 16, 16, 30, 16, 16, 16);
    case 'G': return GLYPH(14, 17, 16, 23, 17, 17, 15);
    case 'H': return GLYPH(17, 17, 17, 31, 17, 17, 17);
    case 'I': return GLYPH(14, 4, 4, 4, 4, 4, 14);
    case 'J': return GLYPH(7, 2, 2, 2, 2, 18, 12);
    case 'K': return GLYPH(17, 18, 20, 24, 20, 18, 17);
    case 'L': return GLYPH(16, 16, 16, 16, 16, 16, 31);
    case 'M': return GLYPH(17, 27, 21, 21, 17, 17, 17);
    case 'N': return GLYPH(17, 25, 21, 19, 17, 17, 17);
    case 'O': return GLYPH(14, 17, 17, 17, 17, 17, 14);
    case 'P': return GLYPH(30, 17, 17, 30, 16, 16, 16);
    case 'Q': return GLYPH(14, 17, 17, 17, 21, 18, 13);
    case 'R': return GLYPH(30, 17, 17, 30, 20, 18, 17);
    case 'S': return GLYPH(15, 16, 16, 14, 1, 1, 30);
    case 'T': return GLYPH(31, 4, 4, 4, 4, 4, 4);
    case 'U': return GLYPH(17, 17, 17, 17, 17, 17, 14);
    case 'V': return GLYPH(17, 17, 17, 17, 17, 10, 4);
    case 'W': return GLYPH(17, 17, 17, 21, 21, 21, 10);
    case 'X': return GLYPH(17, 17, 10, 4, 10, 17, 17);
    case 'Y': return GLYPH(17, 17, 10, 4, 4, 4, 4);
    case 'Z': return GLYPH(31, 1, 2, 4, 8, 16, 31);
    case '0': return GLYPH(14, 17, 19, 21, 25, 17, 14);
    case '1': return GLYPH(4, 12, 4, 4, 4, 4, 14);
    case '2': return GLYPH(14, 17, 1, 2, 4, 8, 31);
    case '3': return GLYPH(30, 1, 1, 14, 1, 1, 30);
    case '4': return GLYPH(2, 6, 10, 18, 31, 2, 2);
    case '5': return GLYPH(31, 16, 16, 30, 1, 1, 30);
    case '6': return GLYPH(14, 16, 16, 30, 17, 17, 14);
    case '7': return GLYPH(31, 1, 2, 4, 8, 8, 8);
    case '8': return GLYPH(14, 17, 17, 14, 17, 17, 14);
    case '9': return GLYPH(14, 17, 17, 15, 1, 1, 14);
    case '.': return GLYPH(0, 0, 0, 0, 0, 6, 6);
    case ',': return GLYPH(0, 0, 0, 0, 6, 6, 4);
    case ':': return GLYPH(0, 6, 6, 0, 6, 6, 0);
    case ';': return GLYPH(0, 6, 6, 0, 6, 6, 4);
    case '!': return GLYPH(4, 4, 4, 4, 4, 0, 4);
    case '?': return GLYPH(14, 17, 1, 2, 4, 0, 4);
    case '\'': return GLYPH(4, 4, 2, 0, 0, 0, 0);
    case '"': return GLYPH(10, 10, 0, 0, 0, 0, 0);
    case '-': return GLYPH(0, 0, 0, 31, 0, 0, 0);
    case '_': return GLYPH(0, 0, 0, 0, 0, 0, 31);
    case '/': return GLYPH(1, 2, 2, 4, 8, 8, 16);
    case '(': return GLYPH(2, 4, 8, 8, 8, 4, 2);
    case ')': return GLYPH(8, 4, 2, 2, 2, 4, 8);
    case '+': return GLYPH(0, 4, 4, 31, 4, 4, 0);
    case '=': return GLYPH(0, 0, 31, 0, 31, 0, 0);
    case ' ': return 0;
    default: return GLYPH(31, 17, 2, 4, 0, 4, 0);
    }
#undef GLYPH
}

std::vector<std::string> wrapText(const std::string &text, size_t columns) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string word, line;
    while (stream >> word) {
        if (!line.empty() && line.size() + 1 + word.size() > columns) {
            lines.push_back(line);
            line.clear();
        }
        if (!line.empty()) line.push_back(' ');
        line += word;
    }
    if (!line.empty()) lines.push_back(line);
    if (lines.empty()) lines.emplace_back();
    return lines;
}

std::string displayUnitName(const dat::Unit &unit,
                            const std::string &localizedName) {
    std::string name =
        localizedName.empty()
            ? (unit.name2.empty() ? unit.name : unit.name2)
            : localizedName;
    if (name.rfind("UNIT-", 0) == 0) name.erase(0, 5);
    for (char &c : name)
        if (c == '-' || c == '_') c = ' ';
    std::string cleaned;
    cleaned.reserve(name.size());
    bool previousSpace = true;
    for (char c : name) {
        const bool space = std::isspace((unsigned char)c) != 0;
        if (space && previousSpace) continue;
        cleaned.push_back(space ? ' ' : c);
        previousSpace = space;
    }
    while (!cleaned.empty() && cleaned.back() == ' ') cleaned.pop_back();
    if (cleaned.empty()) cleaned = "UNIT " + std::to_string(unit.id);
    return cleaned;
}

std::string displayDecimal(float value) {
    std::ostringstream text;
    text << std::fixed << std::setprecision(
                std::abs(value - std::round(value)) < 0.05f ? 0 : 1)
         << value;
    return text.str();
}

void drawBitmapText(Renderer &renderer, const std::vector<std::string> &lines,
                    float x, float y, float pixel, uint8_t red = 255,
                    uint8_t green = 255, uint8_t blue = 255) {
    auto drawPass =
        [&](float offsetX, float offsetY,
            uint8_t passRed, uint8_t passGreen,
            uint8_t passBlue) {
            for (size_t line = 0;
                 line < lines.size(); line++) {
                for (size_t column = 0;
                     column < lines[line].size();
                     column++) {
                    const uint64_t bits =
                        glyphBits(lines[line][column]);
                    for (int row = 0; row < 7; row++) {
                        const uint8_t rowBits =
                            (uint8_t)((bits >> (row * 5)) &
                                      31);
                        for (int bit = 0; bit < 5;) {
                            if (!(rowBits &
                                  (1 << (4 - bit)))) {
                                bit++;
                                continue;
                            }
                            const int start = bit;
                            while (bit < 5 &&
                                   (rowBits &
                                    (1 << (4 - bit))))
                                bit++;
                            const float left = std::round(
                                x + offsetX +
                                (column * 6 + start) *
                                    pixel);
                            const float top = std::round(
                                y + offsetY +
                                (line * 9 + row) * pixel);
                            const float right = std::max(
                                left + 1.0f,
                                std::round(
                                    x + offsetX +
                                    (column * 6 + bit) *
                                        pixel));
                            const float bottom = std::max(
                                top + 1.0f,
                                std::round(
                                    y + offsetY +
                                    (line * 9 + row + 1) *
                                        pixel));
                            renderer.fillRect(
                                left, top,
                                right - left,
                                bottom - top,
                                passRed, passGreen,
                                passBlue, 255);
                        }
                    }
                }
            }
        };
    drawPass(1.0f, 1.0f, 0, 0, 0);
    drawPass(0.0f, 0.0f, red, green, blue);
}

} // namespace

void Game::log(const std::string &message) const {
    if (log_) log_(message);
}

bool Game::triggerEnabled(size_t id) const {
    return id < triggerRuntime_.size() && triggerRuntime_[id].enabled;
}

bool Game::triggerFired(size_t id) const {
    return id < triggerRuntime_.size() && triggerRuntime_[id].fired;
}

bool Game::gateLocked(uint32_t spawnId) const {
    const Object *object = findObject(spawnId);
    return object && object->active && object->locked;
}

size_t Game::gateCount() const {
    return (size_t)std::count_if(
        objects_.begin(), objects_.end(),
        [](const Object &object) {
            return object.active && object.gate &&
                   object.gateClosedUnit &&
                   object.gateOpenUnit &&
                   object.gateEndUnit;
        });
}

size_t Game::garrisonedCount(uint32_t spawnId) const {
    const Object *building = findObject(spawnId);
    return building ? garrisonedCount(*building, false) : 0;
}

bool Game::objectActive(uint32_t spawnId) const {
    const Object *object = findObject(spawnId);
    return object && object->active;
}

float Game::resource(int player, int resourceId) const {
    if (player < 0 || (size_t)player >= resources_.size()) return 0;
    const auto found = resources_[(size_t)player].find(resourceId);
    return found == resources_[(size_t)player].end() ? 0 : found->second;
}

bool Game::researchTechnology(int player, int technologyId) {
    if (player < 0 ||
        (size_t)player >= researchedTechs_.size() ||
        technologyId < 0 ||
        (size_t)technologyId >= assets_.dat().techs.size())
        return false;
    researchedTechs_[(size_t)player].insert(technologyId);
    refreshAutomaticTechnologies(player);
    return true;
}

size_t Game::activeObjectCount() const {
    return (size_t)std::count_if(objects_.begin(), objects_.end(),
                                 [](const Object &object) { return object.active; });
}

size_t Game::underConstructionObjectCount() const {
    return (size_t)std::count_if(
        objects_.begin(), objects_.end(),
        [](const Object &object) {
            return object.active && object.underConstruction;
        });
}

size_t Game::selectedObjectCount() const {
    return (size_t)std::count_if(objects_.begin(), objects_.end(),
                                 [](const Object &object) {
                                     return object.active && object.selected;
                                 });
}

std::vector<int> Game::productionOptionIds(
    uint32_t spawnId) const {
    std::vector<int> ids;
    const Object *object = findObject(spawnId);
    if (!object) return ids;
    for (const dat::Unit *unit :
         productionOptions(*object))
        ids.push_back(unit->id);
    return ids;
}

std::vector<int> Game::researchOptionIds(
    uint32_t spawnId) const {
    const Object *object = findObject(spawnId);
    return object ? researchOptions(*object)
                  : std::vector<int>{};
}

std::vector<int> Game::buildingOptionIds(
    uint32_t spawnId) const {
    std::vector<int> ids;
    const Object *object = findObject(spawnId);
    if (!object) return ids;
    for (ActionMenuTab category :
         {ActionMenuTab::Economy,
          ActionMenuTab::Military,
          ActionMenuTab::Defense})
        for (const dat::Unit *unit :
             buildingOptions(*object, category))
            ids.push_back(unit->id);
    return ids;
}

std::vector<int> Game::buildingOptionIds(
    uint32_t spawnId, int interfaceKind) const {
    std::vector<int> ids;
    const Object *object = findObject(spawnId);
    if (!object) return ids;
    const ActionMenuTab category =
        interfaceKind == 10
            ? ActionMenuTab::Military
            : interfaceKind == 11
                  ? ActionMenuTab::Defense
                  : ActionMenuTab::Economy;
    for (const dat::Unit *unit :
         buildingOptions(*object, category))
        ids.push_back(unit->id);
    return ids;
}

std::vector<uint32_t>
Game::underConstructionObjectIds() const {
    std::vector<uint32_t> ids;
    for (const Object &object : objects_)
        if (object.active && object.underConstruction)
            ids.push_back(object.spawnId);
    return ids;
}

uint32_t Game::constructionBuilderId(
    uint32_t spawnId) const {
    const Object *object = findObject(spawnId);
    return object ? object->constructionBuilderId : 0;
}

bool Game::objectIsBuilder(uint32_t spawnId) const {
    const Object *object = findObject(spawnId);
    return object && object->active &&
           object->state == State::Build;
}

bool Game::objectGatheringTarget(
    uint32_t spawnId, uint32_t targetId) const {
    const Object *object = findObject(spawnId);
    return object && object->active &&
           object->gatherTargetId != 0 &&
           (targetId == 0 ||
            object->gatherTargetId == targetId);
}

bool Game::objectBuildingTarget(
    uint32_t spawnId, uint32_t targetId) const {
    const Object *object = findObject(spawnId);
    return object && object->active &&
           object->constructionTargetId ==
               targetId;
}

float Game::objectCarriedAmount(
    uint32_t spawnId) const {
    const Object *object = findObject(spawnId);
    return object ? object->carriedAmount : 0.0f;
}

float Game::objectResourceAmount(
    uint32_t spawnId) const {
    const Object *object = findObject(spawnId);
    return object ? object->resourceAmount : 0.0f;
}

std::array<float, 2> Game::objectPosition(
    uint32_t spawnId) const {
    const Object *object = findObject(spawnId);
    return object
               ? std::array<float, 2>{
                     object->x, object->y}
               : std::array<float, 2>{0, 0};
}

size_t Game::selectedMovingObjectCount() const {
    return (size_t)std::count_if(objects_.begin(), objects_.end(),
                                 [](const Object &object) {
                                     return object.active && object.selected &&
                                            object.state == State::Walk;
                                 });
}

MovementStats Game::movementStats() const {
    MovementStats stats;
    for (size_t i = 0; i < objects_.size(); i++) {
        const Object &object = objects_[i];
        if (!object.active || object.hidden || object.unit->speed <= 0 ||
            object.unit->type == dat::UT_Building)
            continue;
        if (object.state == State::Walk) stats.pathingObjects++;
        if (object.moveGoalActive) {
            stats.pendingMoveGoals++;
            if (object.selected) stats.selectedPendingMoveGoals++;
        }
        if (!terrainPassable(object, object.x, object.y)) stats.terrainViolations++;
        else if (!positionPassable(object, object.x, object.y, false))
            stats.staticObstructionViolations++;
        for (size_t j = i + 1; j < objects_.size(); j++) {
            const Object &other = objects_[j];
            if (!other.active || other.hidden || other.unit->speed <= 0 ||
                other.unit->type == dat::UT_Building ||
                isAirUnit(object) != isAirUnit(other))
                continue;
            const float dx = object.x - other.x, dy = object.y - other.y;
            const float separation = collisionRadius(object) + collisionRadius(other) + 0.04f;
            if (dx * dx + dy * dy < separation * separation) {
                if (stats.overlappingPairs == 0) {
                    stats.firstOverlapObject = object.spawnId;
                    stats.secondOverlapObject = other.spawnId;
                    stats.firstOverlapUnit = object.unit->id;
                    stats.secondOverlapUnit = other.unit->id;
                    stats.firstOverlapSelected = object.selected;
                    stats.secondOverlapSelected = other.selected;
                }
                stats.overlappingPairs++;
            }
        }
    }
    return stats;
}

CombatStats Game::combatStats() const {
    CombatStats stats;
    stats.ordersIssued = attackOrdersIssued_;
    stats.attacksLanded = attacksLanded_;
    stats.unitsKilled = unitsKilled_;
    stats.projectilesLaunched = projectilesLaunched_;
    stats.activeProjectiles = projectiles_.size();
    stats.activeRemains = remains_.size();
    stats.attackPathsComputed = attackPathsComputed_;
    stats.attackApproachRetries = attackApproachRetries_;
    stats.automaticTargetsAcquired = automaticTargetsAcquired_;
    stats.retaliationOrders = retaliationOrders_;
    stats.armedBuildingsEngaged = armedBuildingsEngaged_;
    stats.attackModeChanges = attackModeChanges_;
    for (const Object &object : objects_)
        if (object.active && object.attackTargetId) stats.activeOrders++;
    return stats;
}

float Game::objectHitPoints(uint32_t spawnId) const {
    const Object *object = findObject(spawnId);
    return object && object->active ? object->hitPoints : 0.0f;
}

float Game::objectMaxHitPoints(uint32_t spawnId) const {
    const Object *object = findObject(spawnId);
    return object ? object->maxHitPoints : 0.0f;
}

float Game::objectShieldPoints(uint32_t spawnId) const {
    const Object *object = findObject(spawnId);
    return object ? object->shieldPoints : 0.0f;
}

float Game::objectMaxShieldPoints(uint32_t spawnId) const {
    const Object *object = findObject(spawnId);
    return object ? object->maxShieldPoints : 0.0f;
}

bool Game::objectShielded(uint32_t spawnId) const {
    const Object *object = findObject(spawnId);
    return object && isShielded(*object);
}

uint32_t Game::spawnObjectForTesting(
    int civilization, int unitId, int player,
    float x, float y) {
    const dat::Unit *unit =
        findUnit(civilization, unitId);
    if (!unit) return 0;
    const uint32_t spawnId = nextSpawnId_++;
    Object *object = addObject(
        unit, player, x, y, 0, spawnId);
    if (!object) return 0;
    configureGate(*object);
    object->wander = false;
    rebuildAdjacency();
    return spawnId;
}

uint32_t Game::spawnFoundationForTesting(
    int civilization, int unitId, int player,
    float x, float y,
    const std::vector<uint32_t> &builderIds) {
    const dat::Unit *unit =
        findUnit(civilization, unitId);
    if (!unit) return 0;
    const uint32_t spawnId = nextSpawnId_++;
    Object *building = addObject(
        unit, player, x, y, 0, spawnId);
    if (!building) return 0;
    building->wander = false;
    building->underConstruction = true;
    building->constructionTotal =
        std::max(1.0f, (float)unit->trainTime);
    building->constructionRemaining =
        building->constructionTotal;
    building->hitPoints = 1.0f;
    rebuildAdjacency();
    for (uint32_t builderId : builderIds) {
        Object *worker = findObject(builderId);
        if (worker)
            assignBuilder(*worker, *building);
    }
    return spawnId;
}

bool Game::completeFoundationForTesting(
    uint32_t spawnId) {
    Object *building = findObject(spawnId);
    if (!building ||
        !building->underConstruction)
        return false;
    building->constructionRemaining = 0.0f;
    return true;
}

bool Game::setConstructionProgressForTesting(
    uint32_t spawnId, float progress) {
    Object *building = findObject(spawnId);
    if (!building ||
        !building->underConstruction)
        return false;
    progress = std::max(
        0.0f, std::min(0.999f, progress));
    building->constructionRemaining =
        building->constructionTotal *
        (1.0f - progress);
    building->hitPoints = std::max(
        1.0f,
        building->maxHitPoints * progress);
    return true;
}

bool Game::damageObjectForTesting(
    uint32_t spawnId, int damage) {
    Object *object = findObject(spawnId);
    if (!object) return false;
    damageObject(*object, damage, 0);
    return true;
}

bool Game::moveObjectForTesting(
    uint32_t spawnId, float x, float y) {
    Object *object = findObject(spawnId);
    if (!object) return false;
    object->x = object->homeX =
        object->moveAnchorX = object->targetX = x;
    object->y = object->homeY =
        object->moveAnchorY = object->targetY = y;
    object->path.clear();
    object->state = State::Idle;
    object->moveGoalActive = false;
    rebuildMobileOccupancy();
    return true;
}

int Game::objectPlayer(uint32_t spawnId) const {
    const Object *object = findObject(spawnId);
    return object && object->active
               ? object->player
               : -1;
}

bool Game::setGateLockedForTesting(
    uint32_t spawnId, bool locked) {
    Object *gate = findObject(spawnId);
    if (!gate || !gate->gate) return false;
    setGateLocked(*gate, locked);
    return true;
}

bool Game::positionPassableForTesting(
    uint32_t spawnId, float x, float y) const {
    const Object *object = findObject(spawnId);
    return object &&
           positionPassable(*object, x, y, false);
}

bool Game::issueRepairForTesting(
    uint32_t workerId, uint32_t targetId) {
    Object *worker = findObject(workerId);
    Object *target = findObject(targetId);
    return worker && target &&
           issueRepairCommand(*worker, *target);
}

bool Game::issueGatherForTesting(
    uint32_t workerId, uint32_t targetId) {
    Object *worker = findObject(workerId);
    Object *target = findObject(targetId);
    return worker && target &&
           issueGatherCommand(*worker, *target);
}

bool Game::issueDropOffForTesting(
    uint32_t workerId, uint32_t buildingId) {
    Object *worker = findObject(workerId);
    Object *building = findObject(buildingId);
    return worker && building &&
           issueDropOffCommand(
               *worker, *building);
}

bool Game::objectPoweredForTesting(
    uint32_t spawnId) const {
    const Object *object = findObject(spawnId);
    return object && isPowered(*object);
}

bool Game::destroyLastSelectedForTesting() {
    return destroyLastSelected();
}

bool Game::ejectGarrisonedUnitForTesting(
    uint32_t buildingId, uint32_t unitId) {
    Object *building = findObject(buildingId);
    Object *unit = findObject(unitId);
    return building && unit &&
           ejectGarrisonedUnit(
               *building, *unit);
}

void Game::setResourceForTesting(
    int player, int resourceType,
    float amount) {
    if (player < 0 ||
        (size_t)player >= resources_.size() ||
        resourceType < 0)
        return;
    resources_[(size_t)player][resourceType] =
        std::max(0.0f, amount);
}

void Game::setDiplomacyForTesting(
    int sourcePlayer, int targetPlayer,
    uint32_t stance) {
    if (sourcePlayer <= 0 ||
        (size_t)sourcePlayer > players_.size() ||
        targetPlayer < 0 || targetPlayer >= 16)
        return;
    players_[(size_t)sourcePlayer - 1]
        .diplomacy[(size_t)targetPlayer] =
        stance;
}

int Game::objectUnitId(uint32_t spawnId) const {
    const Object *object = findObject(spawnId);
    return object && object->active && object->unit
               ? object->unit->id
               : -1;
}

int Game::objectAttackDamage(uint32_t sourceId,
                             uint32_t targetId) const {
    const Object *source = findObject(sourceId);
    const Object *target = findObject(targetId);
    return source && target
               ? attackDamage(*source, *target)
               : 0;
}

bool Game::objectSelected(uint32_t spawnId) const {
    const Object *object = findObject(spawnId);
    return object && object->active && object->selected;
}

std::vector<uint32_t> Game::selectedObjectIds() const {
    std::vector<uint32_t> result;
    result.reserve(selectionOrder_.size());
    for (uint32_t spawnId : selectionOrder_) {
        const Object *object = findObject(spawnId);
        if (object && object->active && object->selected)
            result.push_back(spawnId);
    }
    for (const Object &object : objects_)
        if (object.active && object.selected &&
            std::find(result.begin(), result.end(), object.spawnId) ==
                result.end())
            result.push_back(object.spawnId);
    return result;
}

bool Game::objectScreenPosition(uint32_t spawnId, int screenW, int screenH,
                                float &screenX, float &screenY) const {
    const Object *object = findObject(spawnId);
    if (!object || !object->active) return false;
    objectScreenPosition(*object, screenW, screenH, screenX, screenY);
    return true;
}

std::vector<MovingObjectInfo> Game::movingObjects() const {
    std::vector<MovingObjectInfo> result;
    for (const Object &object : objects_) {
        if (!object.active || object.hidden ||
            (object.state != State::Walk && !object.moveGoalActive))
            continue;
        MovingObjectInfo info{object.spawnId, object.unit->id, object.player, object.x, object.y,
                              object.targetX, object.targetY};
        if (object.pathIndex < object.path.size()) {
            info.waypointX = object.path[object.pathIndex][0];
            info.waypointY = object.path[object.pathIndex][1];
        }
        info.blockedTime = object.blockedTime;
        info.moveGoalActive = object.moveGoalActive;
        info.selected = object.selected;
        result.push_back(info);
    }
    return result;
}

bool Game::objectMatches(const Object &object, int unitId, int player, int group, int type) const {
    if (!object.active || !object.triggerAddressable) return false;
    if (unitId >= 0 && object.unit->id != unitId) return false;
    if (player >= 0 && object.player != player) return false;
    if (group >= 0 && object.unit->cls != group) return false;
    if (type < 0) return true;

    const bool building = object.unit->type == dat::UT_Building;
    const bool civilian = object.unit->cls == 4 ||
                          object.unit->name.find("WORKER") != std::string::npos;
    const bool military = !building && !civilian && object.unit->type >= dat::UT_Combatant;
    if (type == 2) return building;
    if (type == 3) return civilian;
    if (type == 4) return military;
    if (type == 1) return !building && !civilian && !military;
    return false;
}

bool Game::inSourceArea(const Object &object, int x1, int y1, int x2, int y2) const {
    if (x1 < 0 || y1 < 0 || x2 < 0 || y2 < 0 || object.hidden) return false;
    const float sourceX = mapSize_ - object.y;
    const float sourceY = object.x;
    const int minX = std::min(x1, x2), maxX = std::max(x1, x2);
    const int minY = std::min(y1, y2), maxY = std::max(y1, y2);
    return sourceX >= minX && sourceX <= maxX + 1.0f &&
           sourceY >= minY && sourceY <= maxY + 1.0f;
}

std::vector<Game::Object *> Game::effectTargets(const ScenarioEffect &effect) {
    std::vector<Object *> targets;
    if (!effect.selectedUnitIds.empty()) {
        for (uint32_t id : effect.selectedUnitIds) {
            Object *object = findObject(id);
            if (object && object->active) targets.push_back(object);
        }
        return targets;
    }

    const int unitId = triggerField(effect.fields, 6);
    const int player = triggerField(effect.fields, 7);
    const int x1 = triggerField(effect.fields, 16), y1 = triggerField(effect.fields, 17);
    const int x2 = triggerField(effect.fields, 18), y2 = triggerField(effect.fields, 19);
    const int group = triggerField(effect.fields, 20);
    const int type = triggerField(effect.fields, 21);
    const bool hasArea = x1 >= 0 && y1 >= 0 && x2 >= 0 && y2 >= 0;
    if (!hasArea && unitId < 0 && group < 0 && type < 0) return targets;
    for (Object &object : objects_) {
        if (!objectMatches(object, unitId, player, group, type)) continue;
        if (hasArea && !inSourceArea(object, x1, y1, x2, y2)) continue;
        targets.push_back(&object);
    }
    return targets;
}

bool Game::isAirUnit(const Object &object) const {
    return object.unit->flyMode != 0;
}

bool Game::isEnemy(const Object &source, const Object &target) const {
    if (!target.active || target.hidden || !target.draw || !target.spawnId ||
        target.hitPoints <= 0 ||
        target.unit->type < dat::UT_Combatant || source.player <= 0 ||
        source.player == target.player)
        return false;
    const size_t sourceIndex = (size_t)source.player - 1;
    return sourceIndex < players_.size() && target.player >= 0 && target.player < 16 &&
           players_[sourceIndex].diplomacy[(size_t)target.player] == 3;
}

bool Game::canAttack(const Object &object) const {
    return object.active && object.unit->type >= dat::UT_Combatant &&
           !object.unit->attacks.empty() && object.unit->attackGraphic >= 0;
}

float Game::attackRange(const Object &source, const Object &target) const {
    const float contact = collisionRadius(source) + collisionRadius(target) + 0.08f;
    return std::max(contact, source.unit->maxRange);
}

bool Game::technologyCommandApplies(
    const dat::EffectCommand &command,
    const Object &object) const {
    return (command.a < 0 || command.a == object.unit->id) &&
           (command.b < 0 || command.b == object.unit->cls);
}

float Game::modifiedUnitAttribute(const Object &object,
                                  int attribute,
                                  float baseValue) const {
    float value = baseValue;
    if (object.player < 0 ||
        (size_t)object.player >= researchedTechs_.size())
        return value;
    for (int technologyId :
         researchedTechs_[(size_t)object.player]) {
        if (technologyId < 0 ||
            (size_t)technologyId >= assets_.dat().techs.size())
            continue;
        const dat::Tech &technology =
            assets_.dat().techs[(size_t)technologyId];
        if (technology.effectId < 0 ||
            (size_t)technology.effectId >=
                assets_.dat().effects.size())
            continue;
        for (const dat::EffectCommand &command :
             assets_.dat()
                 .effects[(size_t)technology.effectId]
                 .commands) {
            if (command.c != attribute ||
                !technologyCommandApplies(command, object))
                continue;
            if (command.type == 0)
                value = command.d;
            else if (command.type == 4)
                value += command.d;
            else if (command.type == 5)
                value *= command.d;
        }
    }
    return value;
}

int Game::modifiedAttackAmount(
    const Object &source,
    const dat::AttackOrArmor &attack) const {
    float amount = attack.amount;
    if (source.player < 0 ||
        (size_t)source.player >= researchedTechs_.size())
        return attack.amount;
    for (int technologyId :
         researchedTechs_[(size_t)source.player]) {
        if (technologyId < 0 ||
            (size_t)technologyId >= assets_.dat().techs.size())
            continue;
        const dat::Tech &technology =
            assets_.dat().techs[(size_t)technologyId];
        if (technology.effectId < 0 ||
            (size_t)technology.effectId >=
                assets_.dat().effects.size())
            continue;
        for (const dat::EffectCommand &command :
             assets_.dat()
                 .effects[(size_t)technology.effectId]
                 .commands) {
            if (command.c != 9 ||
                !technologyCommandApplies(command, source))
                continue;
            if (command.type == 5) {
                amount *= command.d;
                continue;
            }
            if (command.type != 0 && command.type != 4)
                continue;
            const int packed = (int)std::lround(command.d);
            if (((packed >> 8) & 0xFF) != attack.cls)
                continue;
            const int value =
                (int)(int8_t)(packed & 0xFF);
            amount = command.type == 0
                         ? (float)value
                         : amount + value;
        }
    }
    return (int)std::lround(amount);
}

int Game::modifiedArmourAmount(const Object &target,
                               int armourClass,
                               bool &present) const {
    float amount = target.unit->baseArmor;
    present = false;
    for (const dat::AttackOrArmor &armour :
         target.unit->armours)
        if (armour.cls == armourClass) {
            amount = armour.amount;
            present = true;
            break;
        }
    if (target.player < 0 ||
        (size_t)target.player >= researchedTechs_.size())
        return (int)std::lround(amount);
    for (int technologyId :
         researchedTechs_[(size_t)target.player]) {
        if (technologyId < 0 ||
            (size_t)technologyId >= assets_.dat().techs.size())
            continue;
        const dat::Tech &technology =
            assets_.dat().techs[(size_t)technologyId];
        if (technology.effectId < 0 ||
            (size_t)technology.effectId >=
                assets_.dat().effects.size())
            continue;
        for (const dat::EffectCommand &command :
             assets_.dat()
                 .effects[(size_t)technology.effectId]
                 .commands) {
            if (command.c != 8 ||
                !technologyCommandApplies(command, target))
                continue;
            if (command.type == 5) {
                if (present) amount *= command.d;
                continue;
            }
            if (command.type != 0 && command.type != 4)
                continue;
            const int packed = (int)std::lround(command.d);
            if (((packed >> 8) & 0xFF) != armourClass)
                continue;
            const int value =
                (int)(int8_t)(packed & 0xFF);
            if (!present) {
                amount = 0;
                present = true;
            }
            amount = command.type == 0
                         ? (float)value
                         : amount + value;
        }
    }
    return (int)std::lround(amount);
}

int Game::attackDamage(const Object &source,
                       const Object &target) const {
    int damage = 0;
    for (const dat::AttackOrArmor &attack :
         source.unit->attacks) {
        bool armourPresent = false;
        const int armour =
            modifiedArmourAmount(target, attack.cls,
                                 armourPresent);
        const int attackAmount =
            modifiedAttackAmount(source, attack);
        damage += std::max(
            attackAmount -
                (armourPresent ? armour
                               : target.unit->baseArmor),
            0);
    }
    return source.unit->attacks.empty()
               ? 0
               : std::max(1, damage);
}

int Game::graphicSound(int graphicId) const {
    const dat::Graphic *graphic = assets_.dat().graphic(graphicId);
    if (!graphic) return -1;
    if (graphic->soundId >= 0) return graphic->soundId;
    for (const dat::GraphicAngleSound &angle : graphic->angleSounds)
        for (int sound : angle.sound)
            if (sound >= 0) return sound;
    return -1;
}

float Game::collisionRadius(const Object &object) const {
    return std::max(0.1f, std::max(object.unit->collisionSize[0],
                                    object.unit->collisionSize[1]));
}

void Game::interactionPoint(const Object &source, const Object &target,
                            float clearance, float &x, float &y) const {
    float dx = source.x - target.x;
    float dy = source.y - target.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length > 0.001f) {
        dx /= length;
        dy /= length;
    } else {
        dx = 1.0f;
        dy = 0.0f;
    }
    const float halfWidth =
        std::max(0.1f, target.unit->collisionSize[0]) +
        collisionRadius(source) + clearance;
    const float halfHeight =
        std::max(0.1f, target.unit->collisionSize[1]) +
        collisionRadius(source) + clearance;
    const float xScale =
        std::abs(dx) > 0.001f
            ? halfWidth / std::abs(dx)
            : std::numeric_limits<float>::max();
    const float yScale =
        std::abs(dy) > 0.001f
            ? halfHeight / std::abs(dy)
            : std::numeric_limits<float>::max();
    const float scale = std::min(xScale, yScale);
    x = target.x + dx * scale;
    y = target.y + dy * scale;
}

bool Game::withinInteractionRange(const Object &source,
                                  const Object &target,
                                  float clearance) const {
    const float dx = std::max(
        0.0f, std::abs(source.x - target.x) -
                  std::max(0.1f, target.unit->collisionSize[0]));
    const float dy = std::max(
        0.0f, std::abs(source.y - target.y) -
                  std::max(0.1f, target.unit->collisionSize[1]));
    // Interaction paths terminate on a quantized path node. Keep a
    // small acceptance margin so workers do not idle forever on the
    // exact build/gather/repair boundary due to float rounding.
    const float range =
        collisionRadius(source) + clearance +
        0.04f;
    return dx * dx + dy * dy <= range * range;
}

bool Game::isInspectable(const Object &object) const {
    return object.active && !object.hidden && object.draw && object.spawnId &&
           object.player > 0 && object.unit->type >= dat::UT_Combatant;
}

bool Game::isSelectable(const Object &object) const {
    return isInspectable(object) && object.player == localPlayer_ &&
           object.unit->speed > 0 && object.unit->type != dat::UT_Building;
}

bool Game::hasSelectedUnit() const {
    for (const Object &object : objects_)
        if (object.selected && isSelectable(object)) return true;
    return false;
}

bool Game::hasSelectedAttacker() const {
    for (const Object &object : objects_)
        if (object.selected && isSelectable(object) && canAttack(object)) return true;
    return false;
}

void Game::clearSelection() {
    for (Object &object : objects_) object.selected = false;
    selectionOrder_.clear();
}

void Game::selectObject(Object &object, bool first) {
    object.selected = true;
    selectionOrder_.erase(
        std::remove(selectionOrder_.begin(), selectionOrder_.end(),
                    object.spawnId),
        selectionOrder_.end());
    if (first)
        selectionOrder_.insert(selectionOrder_.begin(), object.spawnId);
    else
        selectionOrder_.push_back(object.spawnId);
}

void Game::syncSelectionOrder() {
    selectionOrder_.erase(
        std::remove_if(
            selectionOrder_.begin(), selectionOrder_.end(),
            [&](uint32_t spawnId) {
                const Object *object = findObject(spawnId);
                return !object || !object->active || !object->selected;
            }),
        selectionOrder_.end());
    for (const Object &object : objects_)
        if (object.active && object.selected &&
            std::find(selectionOrder_.begin(), selectionOrder_.end(),
                      object.spawnId) == selectionOrder_.end())
            selectionOrder_.push_back(object.spawnId);
}

std::vector<Game::Object *> Game::selectedObjectsInOrder(
    bool selectableOnly) {
    syncSelectionOrder();
    std::vector<Object *> selected;
    selected.reserve(selectionOrder_.size());
    for (uint32_t spawnId : selectionOrder_) {
        Object *object = findObject(spawnId);
        if (!object || !object->selected ||
            (selectableOnly ? !isSelectable(*object)
                            : !isInspectable(*object)))
            continue;
        selected.push_back(object);
    }
    return selected;
}

void Game::objectScreenPosition(const Object &object, int screenW, int screenH,
                                float &screenX, float &screenY) const {
    float projectedX, projectedY;
    toScreen(object.x, object.y, projectedX, projectedY);
    projectedY -= elevationAt(object.x, object.y) * assets_.dat().terrainBlock.elevHeight;
    const float viewW = screenW / zoom_, viewH = screenH / zoom_;
    const float ox = camX_ - viewW * 0.5f, oy = camY_ - viewH * 0.5f;
    screenX = (projectedX - ox) * zoom_;
    screenY = (projectedY - oy) * zoom_;
}

void Game::screenToWorld(float screenX, float screenY, int screenW, int screenH,
                         float &worldX, float &worldY) const {
    const float viewW = screenW / zoom_, viewH = screenH / zoom_;
    const float projectedX = camX_ - viewW * 0.5f + screenX / zoom_;
    const float projectedY = camY_ - viewH * 0.5f + screenY / zoom_;
    float adjustedY = projectedY;
    for (int pass = 0; pass < 2; pass++) {
        worldX = (projectedX / kTileHalfW + adjustedY / kTileHalfH) * 0.5f;
        worldY = (adjustedY / kTileHalfH - projectedX / kTileHalfW) * 0.5f;
        worldX = std::max(0.0f, std::min(mapSize_ - 0.001f, worldX));
        worldY = std::max(0.0f, std::min(mapSize_ - 0.001f, worldY));
        adjustedY = projectedY +
                    elevationAt(worldX, worldY) * assets_.dat().terrainBlock.elevHeight;
    }
}

Game::Object *Game::objectAtScreen(float screenX, float screenY,
                                   int screenW, int screenH,
                                   bool includeGatherables) {
    Object *best = nullptr;
    float bestScore = std::numeric_limits<float>::max();
    for (Object &object : objects_) {
        if (!isInspectable(object) &&
            !(includeGatherables &&
              isGatherable(object)))
            continue;
        float objectX, objectY;
        objectScreenPosition(object, screenW, screenH, objectX, objectY);
        const float radiusX = std::max(30.0f, collisionRadius(object) * 48.0f * zoom_ + 12.0f);
        const float height =
            std::max(72.0f, object.unit->outlineSize[2] * 24.0f * zoom_);
        const float dx = screenX - objectX, dy = screenY - objectY;
        if (std::abs(dx) > radiusX || dy < -height || dy > 28.0f) continue;
        const float score = dx * dx + (dy + height * 0.25f) * (dy + height * 0.25f);
        if (score < bestScore) {
            best = &object;
            bestScore = score;
        }
    }
    return best;
}

Game::Object *Game::gatherableAtScreen(
    float screenX, float screenY,
    int screenW, int screenH) {
    Object *best = nullptr;
    float bestScore =
        std::numeric_limits<float>::max();
    for (Object &object : objects_) {
        if (!isGatherable(object)) continue;
        float objectX = 0.0f;
        float objectY = 0.0f;
        objectScreenPosition(
            object, screenW, screenH,
            objectX, objectY);
        const float radiusX =
            std::max(
                30.0f,
                collisionRadius(object) *
                        48.0f * zoom_ +
                    12.0f);
        const float height =
            std::max(
                72.0f,
                object.unit->outlineSize[2] *
                    24.0f * zoom_);
        const float dx = screenX - objectX;
        const float dy = screenY - objectY;
        if (std::abs(dx) > radiusX ||
            dy < -height || dy > 28.0f)
            continue;
        const float score =
            dx * dx +
            (dy + height * 0.25f) *
                (dy + height * 0.25f);
        if (score < bestScore) {
            best = &object;
            bestScore = score;
        }
    }
    return best;
}

Game::Object *Game::enemyAtScreen(float screenX, float screenY, int screenW, int screenH) {
    const Object *source = nullptr;
    for (Object *object : selectedObjectsInOrder(true))
        if (canAttack(*object)) {
            source = object;
            break;
        }
    if (!source) return nullptr;

    Object *best = nullptr;
    float bestScore = std::numeric_limits<float>::max();
    for (Object &object : objects_) {
        if (!isEnemy(*source, object)) continue;
        float objectX, objectY;
        objectScreenPosition(object, screenW, screenH, objectX, objectY);
        const float radiusX =
            std::max(30.0f, collisionRadius(object) * 48.0f * zoom_ + 12.0f);
        const float height =
            std::max(72.0f, object.unit->outlineSize[2] * 24.0f * zoom_);
        const float dx = screenX - objectX, dy = screenY - objectY;
        if (std::abs(dx) > radiusX || dy < -height || dy > 28.0f) continue;
        const float score = dx * dx + (dy + height * 0.25f) * (dy + height * 0.25f);
        if (score < bestScore) {
            best = &object;
            bestScore = score;
        }
    }
    return best;
}

void Game::selectAtScreen(float screenX, float screenY, int screenW, int screenH) {
    Object *object = objectAtScreen(screenX, screenY, screenW, screenH);
    const float clickDx = screenX - lastSelectionX_;
    const float clickDy = screenY - lastSelectionY_;
    const bool doubleClick =
        object && isSelectable(*object) && object->unit->id == lastSelectionUnitId_ &&
        selectionClickAge_ <= 0.38f &&
        clickDx * clickDx + clickDy * clickDy <= 1600.0f;
    clearSelection();
    if (doubleClick) {
        selectObject(*object);
        for (Object &candidate : objects_) {
            if (&candidate == object || !isSelectable(candidate) ||
                candidate.unit->id != object->unit->id)
                continue;
            float candidateX, candidateY;
            objectScreenPosition(candidate, screenW, screenH, candidateX, candidateY);
            if (candidateX >= 0 && candidateY >= 0 &&
                candidateX < screenW && candidateY < screenH)
                selectObject(candidate);
        }
    } else if (object) {
        selectObject(*object);
    }
    if (!doubleClick && object && isSelectable(*object) && playUnitSound_ &&
        object->unit->selectionSound >= 0)
        playUnitSound_(object->unit->selectionSound, civilizationForPlayer(object->player));
    lastSelectionUnitId_ = object ? object->unit->id : -1;
    lastSelectionX_ = screenX;
    lastSelectionY_ = screenY;
    selectionClickAge_ = 0;
}

void Game::selectBox(float startX, float startY, float endX, float endY,
                     int screenW, int screenH) {
    const float minX = std::min(startX, endX), maxX = std::max(startX, endX);
    const float minY = std::min(startY, endY), maxY = std::max(startY, endY);
    selectionClickAge_ = 1000.0f;
    lastSelectionUnitId_ = -1;
    clearSelection();
    for (Object &object : objects_) {
        if (!isSelectable(object)) continue;
        float objectX, objectY;
        objectScreenPosition(object, screenW, screenH, objectX, objectY);
        if (objectX >= minX && objectX <= maxX && objectY >= minY && objectY <= maxY) {
            selectObject(object);
        }
    }
    Object *acknowledgement =
        selectionOrder_.empty() ? nullptr : findObject(selectionOrder_.front());
    if (acknowledgement && playUnitSound_ && acknowledgement->unit->selectionSound >= 0)
        playUnitSound_(acknowledgement->unit->selectionSound,
                       civilizationForPlayer(acknowledgement->player));
}

bool Game::handleSelectionPanelClick(float screenX, float screenY,
                                     int screenW, int screenH) {
    const float panelY = screenH - kSelectionPanelHeight;
    if (screenY < panelY) return false;
    const float localY = screenY - panelY;

    std::vector<Object *> selected = selectedObjectsInOrder(false);
    if (selected.empty()) return false;
    const bool canEnterGarrisonMode =
        std::any_of(
            selected.begin(), selected.end(),
            [](const Object *object) {
                return object && object->active &&
                       !object->hidden && object->unit &&
                       object->unit->speed > 0 &&
                       object->unit->type !=
                           dat::UT_Building &&
                       object->unit->flyMode == 0;
            });
    const bool canEnterRepairMode =
        std::any_of(
            selected.begin(), selected.end(),
            [&](const Object *object) {
                return object &&
                       isWorker(*object);
            });
    Object *selectedAttacker = nullptr;
    for (Object *object : selected)
        if (object && canAttack(*object)) {
            selectedAttacker = object;
            break;
        }
    const float commandsX =
        std::max(500.0f, screenW - 300.0f);
    if (canEnterGarrisonMode &&
        screenX >= commandsX + 12.0f &&
        screenX < commandsX + 58.0f &&
        screenY >= panelY + 18.0f &&
        screenY < panelY + 64.0f) {
        garrisonCursorActive_ =
            !garrisonCursorActive_;
        repairCursorActive_ = false;
        actionMenuOpen_ = false;
        actionMenuObjectId_ = 0;
        statusMessage_ =
            garrisonCursorActive_
                ? "SELECT A BUILDING TO GARRISON"
                : "GARRISON CANCELLED";
        statusTime_ = 3.0f;
        return true;
    }
    if (canEnterRepairMode &&
        screenX >= commandsX + 64.0f &&
        screenX < commandsX + 110.0f &&
        screenY >= panelY + 18.0f &&
        screenY < panelY + 64.0f) {
        repairCursorActive_ =
            !repairCursorActive_;
        garrisonCursorActive_ = false;
        actionMenuOpen_ = false;
        actionMenuObjectId_ = 0;
        statusMessage_ =
            repairCursorActive_
                ? "SELECT A UNIT OR BUILDING TO REPAIR"
                : "REPAIR CANCELLED";
        statusTime_ = 3.0f;
        return true;
    }
    if (screenX >= commandsX + 116.0f &&
        screenX < commandsX + 162.0f &&
        screenY >= panelY + 18.0f &&
        screenY < panelY + 64.0f) {
        garrisonCursorActive_ = false;
        repairCursorActive_ = false;
        actionMenuOpen_ = false;
        actionMenuObjectId_ = 0;
        destroyLastSelected();
        return true;
    }
    if (selectedAttacker &&
        screenX >= commandsX + 168.0f &&
        screenX < commandsX + 214.0f &&
        screenY >= panelY + 18.0f &&
        screenY < panelY + 64.0f) {
        actionMenuOpen_ = true;
        actionMenuObjectId_ =
            selectedAttacker->spawnId;
        actionMenuTab_ =
            ActionMenuTab::Stances;
        actionMenuSelection_ =
            (size_t)selectedAttacker->attackMode;
        actionMenuScroll_ = 0;
        garrisonCursorActive_ = false;
        repairCursorActive_ = false;
        return true;
    }
    if (selected.size() == 1) {
        Object *building = selected.front();
        if (building && building->unit &&
            building->unit->type ==
                dat::UT_Building) {
            size_t iconIndex = 0;
            for (Object &unit : objects_) {
                if (!unit.active ||
                    unit.garrisonedInId !=
                        (int32_t)building->spawnId)
                    continue;
                const int column =
                    (int)(iconIndex % 5);
                const int row =
                    (int)(iconIndex / 5);
                if (row >= 2) break;
                const float x =
                    460.0f + column * 38.0f;
                const float y =
                    12.0f + row * 42.0f;
                if (screenX >= x &&
                    screenX < x + 34.0f &&
                    localY >= y &&
                    localY < y + 36.0f) {
                    const std::string name =
                        unitDisplayName(*unit.unit);
                    if (ejectGarrisonedUnit(
                            *building, unit)) {
                        statusMessage_ =
                            "EJECTED " + name;
                        statusTime_ = 2.0f;
                    } else {
                        statusMessage_ =
                            "NO CLEAR EXIT";
                        statusTime_ = 3.0f;
                    }
                    return true;
                }
                iconIndex++;
            }
        }
        return true;
    }

    static constexpr FormationType formations[] = {
        FormationType::Line, FormationType::Box,
        FormationType::Staggered, FormationType::Flank,
    };
    for (size_t index = 0; index < std::size(formations); index++) {
        const float x =
            kFormationButtonX + (index % 2) * kFormationButtonStepX;
        const float y =
            kFormationButtonY + (index / 2) * kFormationButtonStepY;
        if (screenX < x || screenX >= x + kFormationButtonSize ||
            localY < y || localY >= y + kFormationButtonSize)
            continue;
        selectedFormation_ = formations[index];
        std::vector<Object *> formationUnits;
        float centroidX = 0.0f;
        float centroidY = 0.0f;
        for (Object *object : selected) {
            if (!object || !object->active ||
                object->hidden || !object->unit ||
                object->unit->speed <= 0 ||
                object->unit->type <
                    dat::UT_Combatant ||
                isWorker(*object))
                continue;
            formationUnits.push_back(object);
            centroidX += object->x;
            centroidY += object->y;
        }
        if (formationUnits.size() > 1) {
            centroidX /= formationUnits.size();
            centroidY /= formationUnits.size();
            issueGroupMove(
                std::move(formationUnits),
                centroidX, centroidY,
                selectedFormation_);
        }
        return true;
    }

    const int columns = std::max(
        1, (int)((commandsX - kGroupPortraitX - 8.0f) /
                 kGroupPortraitStepX));
    for (size_t index = 0; index < selected.size(); index++) {
        const int column = (int)(index % (size_t)columns);
        const int row = (int)(index / (size_t)columns);
        if (row >= 2) break;
        const float x = kGroupPortraitX + column * kGroupPortraitStepX;
        const float y = kGroupPortraitY + row * kGroupPortraitStepY;
        if (screenX < x || screenX >= x + kGroupPortraitSize ||
            localY < y || localY >= y + kGroupPortraitStepY)
            continue;
        Object *chosen = selected[index];
        clearSelection();
        selectObject(*chosen);
        selectionClickAge_ = 1000.0f;
        lastSelectionUnitId_ = chosen->unit->id;
        if (playUnitSound_ && chosen->unit->selectionSound >= 0)
            playUnitSound_(chosen->unit->selectionSound,
                           civilizationForPlayer(chosen->player));
        return true;
    }
    return true;
}

std::vector<const dat::Unit *> Game::productionOptions(
    const Object &building) const {
    std::vector<const dat::Unit *> options;
    if (building.gate) return options;
    const int civilization =
        civilizationForPlayer(building.player);
    const auto &civs = assets_.dat().civs;
    if (civilization < 0 ||
        (size_t)civilization >= civs.size())
        return options;
    std::set<int> added;
    for (const dat::Unit &source :
         civs[(size_t)civilization].units) {
        if (!source.exists ||
            !unitAvailable(building.player, source.id))
            continue;
        const dat::Unit *unit =
            effectiveUnitForPlayer(building.player, &source);
        if (!unit || unit->disabled || unit->hideInEditor ||
            unit->heroMode || unit->buttonId == 0 ||
            unit->type < dat::UT_Creatable ||
            unit->type == dat::UT_Building)
            continue;
        if (!buildingMatchesLocation(
                building, unit->trainLocationId))
            continue;
        if (!added.insert(unit->id).second)
            continue;
        options.push_back(unit);
    }
    std::stable_sort(
        options.begin(), options.end(),
        [](const dat::Unit *a, const dat::Unit *b) {
            if (a->buttonId != b->buttonId)
                return a->buttonId < b->buttonId;
            return a->id < b->id;
        });
    return options;
}

bool Game::buildingMatchesLocation(
    const Object &building, int locationId) const {
    if (!building.unit || locationId < 0)
        return false;
    std::set<int> lineage{
        building.unit->id,
        building.unit->baseId,
        building.unit->copyId,
    };
    lineage.erase(-1);
    bool changed = true;
    while (changed) {
        changed = false;
        for (int technologyId :
             researchedTechs_[(size_t)building.player]) {
            if (technologyId < 0 ||
                (size_t)technologyId >=
                    assets_.dat().techs.size())
                continue;
            const dat::Tech &technology =
                assets_.dat().techs[
                    (size_t)technologyId];
            if (technology.effectId < 0 ||
                (size_t)technology.effectId >=
                    assets_.dat().effects.size())
                continue;
            for (const dat::EffectCommand &command :
                 assets_.dat()
                     .effects[
                         (size_t)technology.effectId]
                     .commands)
                if (command.type == 3 &&
                    lineage.count(command.b) &&
                    lineage.insert(command.a).second)
                    changed = true;
        }
    }
    return lineage.count(locationId) != 0;
}

bool Game::technologyRequirementsMet(
    int player, const dat::Tech &technology) const {
    if (player < 0 ||
        (size_t)player >= researchedTechs_.size())
        return false;
    int satisfied = 0;
    for (int required : technology.requiredTechs)
        if (required >= 0 &&
            researchedTechs_[(size_t)player].count(required))
            satisfied++;
    return satisfied >= technology.requiredTechCount;
}

// Research buttons are shown once every *researchable* prerequisite is done.
// Hidden prerequisites (OPEN-TECH-* building requirements: no location, no
// research time, no effect) only gate pressing the button, which then reports
// string 3062 -- e.g. the next tech level stays visible in the Command Center.
bool Game::technologyVisible(
    int player, const dat::Tech &technology) const {
    if (player < 0 ||
        (size_t)player >= researchedTechs_.size())
        return false;
    int satisfied = 0;
    for (int required : technology.requiredTechs) {
        if (required < 0 ||
            (size_t)required >= assets_.dat().techs.size())
            continue;
        const dat::Tech &prerequisite =
            assets_.dat().techs[(size_t)required];
        const bool hidden = prerequisite.locationId < 0 &&
                            prerequisite.researchTime == 0 &&
                            prerequisite.effectId < 0;
        if (hidden ||
            researchedTechs_[(size_t)player].count(required))
            satisfied++;
    }
    return satisfied >= technology.requiredTechCount;
}

void Game::refreshAutomaticTechnologies(int player) {
    if (player <= 0 ||
        (size_t)player >= researchedTechs_.size())
        return;
    const int civilization = civilizationForPlayer(player);
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t id = 0; id < assets_.dat().techs.size();
             id++) {
            if (researchedTechs_[(size_t)player].count((int)id) ||
                disabledTechs_[(size_t)player].count((int)id))
                continue;
            const dat::Tech &technology =
                assets_.dat().techs[id];
            if (technology.civ >= 0 &&
                technology.civ != civilization)
                continue;
            const bool availability =
                technology.name2.rfind("AVAIL-", 0) == 0;
            const bool made =
                technology.name2.rfind("MADE-", 0) == 0;
            const bool prerequisite =
                technology.effectId < 0 &&
                technology.locationId < 0 &&
                technology.researchTime == 0 &&
                technology.requiredTechCount > 0;
            if (id != 0 && !availability && !made &&
                !prerequisite)
                continue;
            if (made) {
                const std::string unitName =
                    technology.name2.substr(5);
                bool exists = false;
                for (const Object &object : objects_)
                    if (object.active &&
                        object.player == player &&
                        object.unit->type == dat::UT_Building &&
                        object.unit->name2 == unitName) {
                        exists = true;
                        break;
                    }
                if (!exists) continue;
            } else if (!technologyRequirementsMet(
                        player, technology))
                    continue;
            researchedTechs_[(size_t)player].insert((int)id);
            changed = true;
        }
    }
    applyUnitUpgrades(player);
}

void Game::refreshAllAutomaticTechnologies() {
    for (int player = 1; player <= 8; player++)
        refreshAutomaticTechnologies(player);
}

bool Game::unitAvailable(int player, int unitId) const {
    if (player < 0 ||
        (size_t)player >= researchedTechs_.size())
        return false;
    if (!(fullTechTreeCheat_ &&
          player == localPlayer_) &&
        disabledUnits_[(size_t)player].count(unitId))
        return false;
    // Farm availability cannot depend on its MADE technology because
    // that automatic technology is only granted after a farm exists.
    if (unitId == 50) return true;
    bool available = false;
    for (int technologyId :
         researchedTechs_[(size_t)player]) {
        if (technologyId < 0 ||
            (size_t)technologyId >= assets_.dat().techs.size())
            continue;
        const dat::Tech &technology =
            assets_.dat().techs[(size_t)technologyId];
        if (technology.effectId < 0 ||
            (size_t)technology.effectId >=
                assets_.dat().effects.size())
            continue;
        for (const dat::EffectCommand &command :
             assets_.dat().effects[
                 (size_t)technology.effectId].commands)
            if (command.type == 2 &&
                command.a == unitId)
                available = command.b != 0;
    }
    return available;
}

const dat::Unit *Game::effectiveUnitForPlayer(
    int player, const dat::Unit *unit) const {
    if (!unit || player < 0 ||
        (size_t)player >= researchedTechs_.size())
        return unit;
    int unitId = unit->id;
    const int civilization = civilizationForPlayer(player);
    std::set<int> visited;
    while (visited.insert(unitId).second) {
        int upgradedId = unitId;
        for (int technologyId :
             researchedTechs_[(size_t)player]) {
            if (technologyId < 0 ||
                (size_t)technologyId >=
                    assets_.dat().techs.size())
                continue;
            const dat::Tech &technology =
                assets_.dat().techs[(size_t)technologyId];
            if (technology.effectId < 0 ||
                (size_t)technology.effectId >=
                    assets_.dat().effects.size())
                continue;
            for (const dat::EffectCommand &command :
                 assets_.dat().effects[
                     (size_t)technology.effectId].commands)
                if (command.type == 3 &&
                    command.a == unitId) {
                    upgradedId = command.b;
                    break;
                }
            if (upgradedId != unitId) break;
        }
        if (upgradedId == unitId) break;
        unitId = upgradedId;
    }
    return findUnit(civilization, unitId);
}

void Game::applyUnitUpgrades(int player) {
    bool pathingChanged = false;
    for (Object &object : objects_) {
        if (!object.active || object.player != player ||
            !object.unit)
            continue;
        const dat::Unit *upgraded =
            effectiveUnitForPlayer(player, object.unit);
        if (!upgraded || upgraded == object.unit)
            continue;
        const bool building =
            object.unit->type == dat::UT_Building ||
            upgraded->type == dat::UT_Building;
        const float health =
            object.maxHitPoints > 0.0f
                ? object.hitPoints / object.maxHitPoints
                : 1.0f;
        object.unit = upgraded;
        object.maxHitPoints =
            std::max(
                1, (int)std::lround(
                       modifiedUnitAttribute(
                           object, 0,
                           upgraded->hitPoints)));
        object.hitPoints = std::max(
            1.0f, object.maxHitPoints *
                      std::max(0.0f, std::min(1.0f, health)));
        if (building) {
            object.gate = false;
            object.gateClosedUnit = nullptr;
            object.gateOpenUnit = nullptr;
            object.gateEndUnit = nullptr;
            configureGate(object);
            pathingChanged = true;
        }
    }
    if (pathingChanged)
        rebuildAdjacency();
}

std::vector<int> Game::researchOptions(
    const Object &building) const {
    std::vector<int> options;
    if (building.gate) return options;
    const int player = building.player;
    const int civilization = civilizationForPlayer(player);
    for (size_t id = 0; id < assets_.dat().techs.size();
         id++) {
        const dat::Tech &technology =
            assets_.dat().techs[id];
        if (technology.researchTime <= 0 ||
            technology.buttonId == 0 ||
            technology.effectId < 0 ||
            (technology.civ >= 0 &&
             technology.civ != civilization) ||
            !buildingMatchesLocation(
                building, technology.locationId) ||
            researchedTechs_[(size_t)player].count((int)id) ||
            disabledTechs_[(size_t)player].count((int)id) ||
            !technologyVisible(player, technology) ||
            assets_.localizedString(
                technology.languageDllName).empty())
            continue;
        bool queued = false;
        for (const ProductionItem &item :
             building.productionQueue)
            if (item.technologyId == (int)id) {
                queued = true;
                break;
            }
        if (!queued) options.push_back((int)id);
    }
    std::stable_sort(
        options.begin(), options.end(),
        [&](int a, int b) {
            const dat::Tech &left =
                assets_.dat().techs[(size_t)a];
            const dat::Tech &right =
                assets_.dat().techs[(size_t)b];
            if (left.buttonId != right.buttonId)
                return left.buttonId < right.buttonId;
            return a < b;
        });
    return options;
}

std::string Game::technologyDisplayName(
    int technologyId) const {
    if (technologyId < 0 ||
        (size_t)technologyId >= assets_.dat().techs.size())
        return "Technology " + std::to_string(technologyId);
    const dat::Tech &technology =
        assets_.dat().techs[(size_t)technologyId];
    std::string name =
        assets_.localizedString(technology.languageDllName);
    if (!name.empty()) return name;
    name = technology.name2.empty()
               ? technology.name
               : technology.name2;
    const size_t age = name.find("AGE");
    if ((name.rfind("OPEN-TECH-", 0) == 0 ||
         name.rfind("TECH-", 0) == 0) &&
        age != std::string::npos &&
        age + 3 < name.size())
        return "Tech Level " + name.substr(age + 3);
    bool buildRequirement = false;
    static constexpr const char *prefixes[] = {
        "MADE-BLDG-", "AVAIL-", "TECH-", "UT-", "RT-",
    };
    for (const char *prefix : prefixes) {
        const size_t length = std::char_traits<char>::length(prefix);
        if (name.rfind(prefix, 0) != 0) continue;
        buildRequirement =
            std::string(prefix) == "MADE-BLDG-";
        name.erase(0, length);
        break;
    }
    for (char &character : name)
        if (character == '-' || character == '_')
            character = ' ';
        else
            character =
                (char)std::tolower((unsigned char)character);
    bool capitalize = true;
    for (char &character : name) {
        if (character == ' ') {
            capitalize = true;
            continue;
        }
        if (capitalize)
            character =
                (char)std::toupper((unsigned char)character);
        capitalize = false;
    }
    if (name.empty())
        name = "Technology " +
               std::to_string(technologyId);
    return buildRequirement ? "Build " + name : name;
}

std::vector<std::string>
Game::technologyRequirementLines(
    int player, int technologyId) const {
    std::vector<std::string> lines;
    if (technologyId < 0 ||
        (size_t)technologyId >= assets_.dat().techs.size())
        return lines;
    const dat::Tech &technology =
        assets_.dat().techs[(size_t)technologyId];
    if (technology.requiredTechCount <= 0) {
        lines.push_back("Requirements: none");
        return lines;
    }
    int listed = 0;
    for (int required : technology.requiredTechs)
        if (required >= 0) listed++;
    lines.push_back(
        "Requires " +
        std::to_string(technology.requiredTechCount) +
        (technology.requiredTechCount == listed
             ? " of:"
             : " of these " +
                   std::to_string(listed) + ":"));
    for (int required : technology.requiredTechs) {
        if (required < 0) continue;
        const bool met =
            player >= 0 &&
            (size_t)player < researchedTechs_.size() &&
            researchedTechs_[(size_t)player].count(
                required) != 0;
        lines.push_back(
            std::string(met ? "[X] " : "[ ] ") +
            technologyDisplayName(required));
    }
    return lines;
}

std::vector<std::string>
Game::technologyEffectLines(
    int technologyId) const {
    std::vector<std::string> lines;
    if (technologyId < 0 ||
        (size_t)technologyId >= assets_.dat().techs.size())
        return lines;
    const dat::Tech &technology =
        assets_.dat().techs[(size_t)technologyId];
    const std::string description =
        assets_.localizedString(
            technology.languageDllDescription);
    if (!description.empty())
        lines.push_back(description);
    if (technology.effectId < 0 ||
        (size_t)technology.effectId >=
            assets_.dat().effects.size())
        return lines;
    auto unitName = [&](int unitId) {
        for (const dat::Civ &civilization :
             assets_.dat().civs)
            if (unitId >= 0 &&
                (size_t)unitId < civilization.units.size() &&
                civilization.units[(size_t)unitId].exists)
                return unitDisplayName(
                    civilization.units[(size_t)unitId]);
        return std::string("unit ") +
               std::to_string(unitId);
    };
    static constexpr const char *attributes[] = {
        "hit points", "line of sight", "garrison capacity",
        "collision width", "collision height", "movement speed",
        "rotation speed", "unknown attribute 7", "armor",
        "attack", "reload time", "accuracy", "range",
        "work rate", "carrying capacity",
    };
    for (const dat::EffectCommand &command :
         assets_.dat().effects[
             (size_t)technology.effectId].commands) {
        std::string effect;
        if (command.type == 2 && command.a >= 0) {
            effect =
                (command.b != 0 ? "Unlocks " : "Disables ") +
                unitName(command.a);
        } else if (command.type == 3 &&
                   command.a >= 0 && command.b >= 0) {
            effect = "Upgrades " + unitName(command.a) +
                     " to " + unitName(command.b);
        } else if ((command.type == 0 ||
                    command.type == 4 ||
                    command.type == 5) &&
                   command.c >= 0) {
            const std::string attribute =
                command.c < (int)std::size(attributes)
                    ? attributes[(size_t)command.c]
                    : "attribute " +
                          std::to_string(command.c);
            effect =
                command.type == 0
                    ? "Sets " + attribute + " to "
                    : command.type == 4
                          ? "Adds " +
                                displayDecimal(command.d) +
                                " " + attribute + " to "
                          : "Multiplies " + attribute +
                                " by " +
                                displayDecimal(command.d) +
                                " for ";
            if (command.type == 0)
                effect += displayDecimal(command.d) + " for ";
            effect +=
                command.a >= 0
                    ? unitName(command.a)
                    : command.b >= 0
                          ? "class " +
                                std::to_string(command.b)
                          : "affected units";
        }
        if (effect.empty() ||
            std::find(lines.begin(), lines.end(), effect) !=
                lines.end())
            continue;
        lines.push_back(effect);
        if (lines.size() >= 5) break;
    }
    if (lines.empty())
        lines.push_back(
            "Applies DAT effect " +
            std::to_string(technology.effectId));
    return lines;
}

std::vector<const dat::Unit *> Game::buildingOptions(
    const Object &worker,
    ActionMenuTab category) const {
    std::vector<const dat::Unit *> options;
    const int civilization =
        civilizationForPlayer(worker.player);
    if (civilization < 0 ||
        (size_t)civilization >= assets_.dat().civs.size())
        return options;
    for (const dat::Unit &unit :
         assets_.dat().civs[(size_t)civilization].units) {
        if (!unit.exists || unit.type != dat::UT_Building ||
            unit.hideInEditor || unit.disabled ||
            unit.heroMode || unit.buttonId == 0 ||
            !unitAvailable(worker.player, unit.id))
            continue;
        const ActionMenuTab unitCategory =
            unit.interfaceKind == 10
                ? ActionMenuTab::Military
                : unit.interfaceKind == 11
                      ? ActionMenuTab::Defense
                      : ActionMenuTab::Economy;
        if (unitCategory != category)
            continue;
        options.push_back(&unit);
    }
    std::stable_sort(
        options.begin(), options.end(),
        [](const dat::Unit *a, const dat::Unit *b) {
            if (a->buttonId != b->buttonId)
                return a->buttonId < b->buttonId;
            return a->id < b->id;
        });
    return options;
}

std::string Game::unitDisplayName(
    const dat::Unit &unit) const {
    if (unit.name2 == "UNIT-WORKER")
        return "Worker";
    return displayUnitName(
        unit,
        assets_.localizedString(unit.languageDllName));
}

std::string Game::factionName(int civilization) const {
    static constexpr const char *names[] = {
        "Gaia",          "Galactic Empire",
        "Gungans",       "Rebel Alliance",
        "Royal Naboo",   "Trade Federation",
        "Wookiees",      "Galactic Republic",
        "Confederacy",
    };
    return civilization >= 0 &&
                   civilization < (int)std::size(names)
               ? names[civilization]
               : "Unknown Faction";
}

char Game::factionAbbreviation(int civilization) const {
    static constexpr char abbreviations[] = {
        'G', 'E', 'G', 'R', 'N', 'F', 'W', 'P', 'C',
    };
    return civilization >= 0 &&
                   civilization < (int)std::size(abbreviations)
               ? abbreviations[civilization]
               : '?';
}

int Game::civilizationGraphic(int graphicId,
                              int player) const {
    const dat::Graphic *graphic =
        assets_.dat().graphic(graphicId);
    if (!graphic || graphic->slp >= 0)
        return graphicId;
    const int civilization =
        civilizationForPlayer(player);
    const std::pair<int, int> key{
        graphicId, civilization};
    const auto cached =
        civilizationGraphicCache_.find(key);
    if (cached != civilizationGraphicCache_.end())
        return cached->second;
    const std::string expected =
        graphic->name + "-C" +
        std::to_string(civilization);
    int resolved = graphicId;
    for (size_t index = 0;
         index < assets_.dat().graphics.size();
         index++) {
        const dat::Graphic &candidate =
            assets_.dat().graphics[index];
        if (candidate.exists &&
            candidate.slp >= 0 &&
            candidate.name == expected) {
            resolved = (int)index;
            break;
        }
    }
    civilizationGraphicCache_[key] = resolved;
    return resolved;
}

std::string Game::ownershipLabel(int player) const {
    std::string relationship;
    if (player == localPlayer_) {
        relationship = "PLAYER";
    } else {
        uint32_t stance = UINT32_MAX;
        if (localPlayer_ > 0 &&
            (size_t)localPlayer_ <= players_.size() &&
            player >= 0 && player < 16)
            stance =
                players_[(size_t)localPlayer_ - 1]
                    .diplomacy[(size_t)player];
        relationship =
            stance == 0
                ? "ALLY"
                : stance == 3 ? "ENEMY" : "NEUTRAL";
    }
    if (player > 0 &&
        (size_t)player <= players_.size() &&
        !players_[(size_t)player - 1].name.empty())
        relationship += ": " +
                        players_[(size_t)player - 1]
                            .name;
    if (player > 0)
        relationship += " - " +
                        factionName(
                            civilizationForPlayer(player));
    return relationship;
}

bool Game::isWorker(const Object &object) const {
    return object.active && object.unit &&
           object.unit->type >= dat::UT_Bird &&
           object.unit->cls == 58 &&
           object.player == localPlayer_;
}

bool Game::isPowerCore(const Object &object) const {
    return object.unit &&
           (object.unit->name2 == "BLDG-POWERCORE" ||
            object.unit->name.rfind(
                "BLDG-TRAINFOOT", 0) == 0);
}

bool Game::isPowerSource(const Object &object) const {
    return isPowerCore(object) ||
           (object.unit &&
            object.unit->name2 == "UNIT-POWER");
}

bool Game::isShieldGenerator(
    const Object &object) const {
    return object.unit &&
           (object.unit->name2 == "BLDG-SHLDGEN" ||
            object.unit->name.rfind(
                "BLDG-SHLD", 0) == 0);
}

bool Game::graphicHasPowerIndicator(
    int graphicId, int depth) const {
    if (depth >= 4) return false;
    const dat::Graphic *graphic =
        assets_.dat().graphic(graphicId);
    if (!graphic) return false;
    if (graphic->frameCount == 2 &&
        graphic->slp >= kPowerIndicatorSlpFirst &&
        graphic->slp <= kPowerIndicatorSlpLast)
        return true;
    for (const dat::GraphicDelta &delta :
         graphic->deltas)
        if (delta.graphicId >= 0 &&
            graphicHasPowerIndicator(
                delta.graphicId, depth + 1))
            return true;
    return false;
}

bool Game::requiresPower(
    const Object &building) const {
    return isShieldGenerator(building) ||
           (building.unit &&
            graphicHasPowerIndicator(
                building.unit->standingGraphic[0]));
}

bool Game::isPowered(const Object &building) const {
    if (!building.active || !building.unit ||
        building.unit->type != dat::UT_Building)
        return false;
    if (isPowerSource(building))
        return !building.underConstruction;
    if (!requiresPower(building))
        return !building.underConstruction;
    for (const Object &source : objects_) {
        if (!source.active || source.hidden ||
            source.underConstruction ||
            source.player != building.player ||
            !isPowerSource(source))
            continue;
        const float dx = source.x - building.x;
        const float dy = source.y - building.y;
        const float range =
            9.0f + collisionRadius(source) +
            collisionRadius(building);
        if (dx * dx + dy * dy <= range * range)
            return true;
    }
    return false;
}

const Game::Object *Game::shieldGeneratorFor(
    const Object &object) const {
    if (!object.active || object.hidden ||
        object.underConstruction || !object.unit)
        return nullptr;
    if (object.unit->type == dat::UT_Building &&
        object.unit->adjacentMode && !object.gate &&
        (object.player < 0 ||
         (size_t)object.player >= researchedTechs_.size() ||
         !researchedTechs_[(size_t)object.player].count(
             kShieldWallTech)))
        return nullptr;
    const Object *closest = nullptr;
    float closestDistance =
        std::numeric_limits<float>::max();
    // Shield generators are few; scan the cached list instead of every
    // object (this used to be O(objects^2) per frame).
    if (shieldGeneratorCacheSize_ != objects_.size()) {
        shieldGeneratorIndices_.clear();
        for (size_t i = 0; i < objects_.size(); i++)
            if (isShieldGenerator(objects_[i]))
                shieldGeneratorIndices_.push_back((uint32_t)i);
        shieldGeneratorCacheSize_ = objects_.size();
    }
    for (uint32_t sourceIndex : shieldGeneratorIndices_) {
        const Object &source = objects_[sourceIndex];
        if (!source.active || source.hidden ||
            source.underConstruction ||
            source.player != object.player ||
            !isShieldGenerator(source))
            continue;
        const float dx = source.x - object.x;
        const float dy = source.y - object.y;
        const float range =
            9.0f + collisionRadius(object);
        const float distance = dx * dx + dy * dy;
        if (distance <= range * range &&
            distance < closestDistance) {
            closest = &source;
            closestDistance = distance;
        }
    }
    return closest;
}

bool Game::isShielded(const Object &object) const {
    const Object *source =
        shieldGeneratorFor(object);
    return source && isPowered(*source);
}

void Game::updateShields(float dt) {
    for (Object &object : objects_) {
        if (!object.active || object.hidden ||
            object.underConstruction || !object.unit) {
            object.shieldPoints = 0;
            object.maxShieldPoints = 0;
            continue;
        }
        const Object *source =
            shieldGeneratorFor(object);
        if (source)
            object.maxShieldPoints =
                object.maxHitPoints;
        object.shieldPoints = std::min(
            object.shieldPoints,
            object.maxShieldPoints);
        if (!source || !isPowered(*source)) {
            const bool superconducting =
                object.player >= 0 &&
                (size_t)object.player <
                    researchedTechs_.size() &&
                researchedTechs_[(size_t)object.player]
                    .count(kSuperconductingShieldsTech);
            object.shieldPoints = std::max(
                0.0f,
                object.shieldPoints -
                    dt * (superconducting ? 20.0f
                                         : 40.0f));
            if (!source &&
                object.shieldPoints <= 0.0f)
                object.maxShieldPoints = 0.0f;
            continue;
        }
        float regeneration = 2.0f;
        if (object.shieldPoints >= 4000.0f)
            regeneration = 20.0f;
        else if (object.shieldPoints >= 3008.0f)
            regeneration = 16.0f;
        else if (object.shieldPoints >= 2000.0f)
            regeneration = 12.0f;
        else if (object.shieldPoints >= 1000.0f)
            regeneration = 8.0f;
        else if (object.shieldPoints >= 100.0f)
            regeneration = 4.0f;
        object.shieldPoints = std::min(
            object.maxShieldPoints,
            object.shieldPoints + regeneration * dt);
    }
}

const dat::Unit *Game::builderUnit(
    const Object &worker) const {
    if (!worker.unit) return nullptr;
    std::string name = worker.unit->name;
    if (name.size() >= 2 &&
        name.back() == '1' &&
        (name[name.size() - 2] == 'A' ||
         name[name.size() - 2] == 'B')) {
        name.back() = '2';
        if (const dat::Unit *builder =
                findUnit(
                    civilizationForPlayer(worker.player),
                    name))
            return builder;
    }
    return findUnit(
        civilizationForPlayer(worker.player), 118);
}

int Game::builderWorkingGraphic(
    const Object &worker) const {
    const dat::Unit *builder = builderUnit(worker);
    if (!builder || builder->id < 0 ||
        (size_t)builder->id >=
            assets_.dat().unitHeaders.size())
        return -1;
    const dat::UnitHeader &header =
        assets_.dat().unitHeaders[(size_t)builder->id];
    for (const dat::Task &task : header.tasks)
        if ((task.pickForConstruction ||
             task.actionType == 101) &&
            task.proceedingGraphic >= 0)
            return task.proceedingGraphic;
    return -1;
}

bool Game::isGatherable(
    const Object &object) const {
    return object.active && !object.hidden &&
           object.unit && object.resourceType >= 0 &&
           object.resourceType <= 3 &&
           object.resourceAmount > 0.0f;
}

const dat::Unit *Game::gathererUnit(
    const Object &worker) const {
    if (!worker.unit) return nullptr;
    int variant = -1;
    const Object *target =
        findObject(worker.gatherTargetId);
    if (target && target->unit) {
        switch (target->unit->cls) {
        case 1: variant = 11; break;
        case 27: variant = 5; break;
        case 29: variant = 6; break;
        case 30: variant = 9; break;
        case 31: variant = 8; break;
        default: break;
        }
    }
    if (variant < 0) {
        switch (worker.carriedResourceType) {
        case 0: variant = 5; break;
        case 1: variant = 8; break;
        case 2: variant = 9; break;
        case 3: variant = 6; break;
        default: break;
        }
    }
    if (variant < 0) return nullptr;
    std::string name = worker.unit->name;
    if (name.size() < 2 ||
        name.back() != '1' ||
        (name[name.size() - 2] != 'A' &&
         name[name.size() - 2] != 'B'))
        return nullptr;
    name.back() = (char)('0' + variant);
    if (variant >= 10) {
        name.pop_back();
        name += std::to_string(variant);
    }
    return findUnit(
        civilizationForPlayer(worker.player), name);
}

const dat::Task *Game::gatherTask(
    const Object &worker,
    const dat::Unit &gatherer) const {
    if (gatherer.id < 0 ||
        (size_t)gatherer.id >=
            assets_.dat().unitHeaders.size())
        return nullptr;
    const Object *target =
        findObject(worker.gatherTargetId);
    const int targetClass =
        target && target->unit
            ? target->unit->cls
            : -1;
    for (const dat::Task &task :
         assets_.dat()
             .unitHeaders[(size_t)gatherer.id]
             .tasks) {
        if (task.actionType != 5 &&
            task.actionType != 110)
            continue;
        if (targetClass >= 0 &&
            task.classId >= 0 &&
            task.classId != targetClass)
            continue;
        if (targetClass < 0 &&
            worker.carriedResourceType >= 0 &&
            task.resourceIn >= 0 &&
            task.resourceIn !=
                worker.carriedResourceType)
            continue;
        return &task;
    }
    return nullptr;
}

Game::Object *Game::nearestDropSite(
    const Object &worker,
    const dat::Unit &gatherer) {
    Object *closest = nullptr;
    float closestDistance =
        std::numeric_limits<float>::max();
    for (Object &building : objects_) {
        if (!building.active || building.hidden ||
            building.underConstruction ||
            building.player != worker.player ||
            !building.unit ||
            building.unit->type !=
                dat::UT_Building)
            continue;
        if (!buildingAcceptsResource(
                building, gatherer))
            continue;
        const float dx = building.x - worker.x;
        const float dy = building.y - worker.y;
        const float distance = dx * dx + dy * dy;
        if (distance < closestDistance) {
            closest = &building;
            closestDistance = distance;
        }
    }
    return closest;
}

bool Game::buildingAcceptsResource(
    const Object &building,
    const dat::Unit &gatherer) const {
    if (!building.unit ||
        building.unit->type != dat::UT_Building)
        return false;
    if (building.unit->name.rfind(
            "BLDG-MAIN", 0) == 0)
        return true;
    for (int16_t dropSite : gatherer.dropSites)
        if (dropSite >= 0 &&
            (building.unit->id == dropSite ||
             building.unit->baseId == dropSite ||
             building.unit->copyId == dropSite))
            return true;
    return false;
}

bool Game::assignAutomaticWorkerTask(
    Object &worker,
    const Object &completedBuilding) {
    if (!isWorker(worker) ||
        worker.player != completedBuilding.player)
        return false;
    const float searchRange =
        std::max(4.0f, worker.unit->lineOfSight);
    const float searchRangeSquared =
        searchRange * searchRange;
    Object *resourceTarget = nullptr;
    float resourceDistance =
        std::numeric_limits<float>::max();
    for (Object &resource : objects_) {
        if (!isGatherable(resource)) continue;
        const float dx = resource.x - worker.x;
        const float dy = resource.y - worker.y;
        const float distance = dx * dx + dy * dy;
        if (distance > searchRangeSquared ||
            distance >= resourceDistance)
            continue;
        Object probe = worker;
        probe.gatherTargetId = resource.spawnId;
        const dat::Unit *gatherer =
            gathererUnit(probe);
        if (!gatherer ||
            !buildingAcceptsResource(
                completedBuilding, *gatherer))
            continue;
        resourceTarget = &resource;
        resourceDistance = distance;
    }
    if (resourceTarget)
        return issueGatherCommand(
            worker, *resourceTarget);

    Object *foundation = nullptr;
    float foundationDistance =
        std::numeric_limits<float>::max();
    for (Object &building : objects_) {
        if (!building.active ||
            !building.underConstruction ||
            building.player != worker.player ||
            building.spawnId ==
                completedBuilding.spawnId)
            continue;
        const float dx = building.x - worker.x;
        const float dy = building.y - worker.y;
        const float distance = dx * dx + dy * dy;
        if (distance > searchRangeSquared ||
            distance >= foundationDistance)
            continue;
        foundation = &building;
        foundationDistance = distance;
    }
    return foundation &&
           assignBuilder(worker, *foundation);
}

bool Game::issueGatherCommand(
    Object &worker, Object &resource) {
    if (!isWorker(worker) ||
        !isGatherable(resource))
        return false;
    clearConstructionAssignment(worker);
    worker.gatherTargetId = 0;
    worker.dropOffTargetId = 0;
    worker.attackTargetId = 0;
    worker.attackAutomatic = false;
    worker.garrisonTargetId = 0;
    worker.repairTargetId = 0;
    worker.gatherTargetId = resource.spawnId;
    worker.dropOffTargetId = 0;
    worker.manualDropOff = false;
    worker.wander = false;
    worker.moveGoalActive = false;
    worker.path.clear();
    worker.state = State::Idle;
    worker.animTime = 0;
    return true;
}

bool Game::issueDropOffCommand(
    Object &worker, Object &building) {
    if (!isWorker(worker) ||
        worker.carriedAmount <= 0.001f ||
        worker.carriedResourceType < 0 ||
        !building.active || building.hidden ||
        building.underConstruction ||
        building.player != worker.player ||
        !building.unit ||
        building.unit->type != dat::UT_Building)
        return false;
    const dat::Unit *gatherer = gathererUnit(worker);
    if (!gatherer ||
        !buildingAcceptsResource(building, *gatherer))
        return false;
    clearConstructionAssignment(worker);
    worker.attackTargetId = 0;
    worker.attackAutomatic = false;
    worker.garrisonTargetId = 0;
    worker.repairTargetId = 0;
    worker.dropOffTargetId = building.spawnId;
    worker.manualDropOff = true;
    worker.wander = false;
    worker.moveGoalActive = false;
    worker.path.clear();
    worker.state = State::Idle;
    worker.animTime = 0;
    return true;
}

bool Game::isFriendlyPlayer(
    int sourcePlayer, int targetPlayer) const {
    if (sourcePlayer <= 0 || targetPlayer <= 0)
        return false;
    if (sourcePlayer == targetPlayer) return true;
    const size_t sourceIndex =
        (size_t)sourcePlayer - 1;
    return sourceIndex < players_.size() &&
           targetPlayer < 16 &&
           players_[sourceIndex]
                   .diplomacy[(size_t)targetPlayer] ==
               0;
}

bool Game::isRepairableBy(
    const Object &worker, const Object &target,
    bool requireDamage) const {
    if (!isWorker(worker) || !target.active ||
        target.hidden || !target.unit ||
        target.underConstruction ||
        !isFriendlyPlayer(
            worker.player, target.player) ||
        (requireDamage &&
         target.hitPoints >=
             target.maxHitPoints - 0.001f))
        return false;
    if (target.unit->type == dat::UT_Building)
        return true;
    if (worker.unit->id < 0 ||
        (size_t)worker.unit->id >=
            assets_.dat().unitHeaders.size())
        return false;
    for (const dat::Task &task :
         assets_.dat()
             .unitHeaders[(size_t)worker.unit->id]
             .tasks)
        if (task.actionType == 3 &&
            (task.classId < 0 ||
             task.classId == target.unit->cls))
            return true;
    return false;
}

bool Game::issueRepairCommand(
    Object &worker, Object &target) {
    if (!isRepairableBy(worker, target))
        return false;
    clearConstructionAssignment(worker);
    worker.gatherTargetId = 0;
    worker.dropOffTargetId = 0;
    worker.attackTargetId = 0;
    worker.attackAutomatic = false;
    worker.garrisonTargetId = 0;
    worker.repairTargetId = target.spawnId;
    worker.wander = false;
    worker.moveGoalActive = false;
    worker.path.clear();
    worker.state = State::Idle;
    worker.animTime = 0;
    return true;
}

void Game::updateRepairing(float dt) {
    for (Object &worker : objects_) {
        if (!worker.active || worker.hidden ||
            !worker.repairTargetId)
            continue;
        Object *target =
            findObject(worker.repairTargetId);
        if (!target ||
            !isRepairableBy(
                worker, *target, false) ||
            target->hitPoints >=
                target->maxHitPoints - 0.001f) {
            worker.repairTargetId = 0;
            worker.moveGoalActive = false;
            worker.path.clear();
            worker.state = State::Idle;
            worker.animTime = 0;
            continue;
        }
        if (!withinInteractionRange(
                worker, *target, 0.75f)) {
            if (!worker.moveGoalActive &&
                worker.path.empty()) {
                approach(worker, *target, 0.35f);
            }
            continue;
        }
        if (worker.state != State::Repair)
            worker.animTime = 0;
        worker.state = State::Repair;
        worker.facing =
            std::atan2(
                target->y - worker.y,
                target->x - worker.x);
        worker.moveGoalActive = false;
        worker.path.clear();

        float repaired =
            std::min(
                target->maxHitPoints -
                    target->hitPoints,
                dt * 12.5f *
                    std::max(
                        0.1f,
                        worker.unit->workRate));
        for (const dat::ResourceCost &cost :
             target->unit->costs) {
            if (!cost.flag || cost.type < 0 ||
                cost.amount <= 0)
                continue;
            const float perHitPoint =
                cost.amount * 0.5f /
                std::max(
                    1.0f,
                    target->maxHitPoints);
            const auto resourceIt =
                resources_[(size_t)worker.player]
                    .find(cost.type);
            const float available =
                resourceIt ==
                        resources_[(size_t)worker.player]
                            .end()
                    ? 0.0f
                    : resourceIt->second;
            if (perHitPoint > 0)
                repaired = std::min(
                    repaired,
                    available / perHitPoint);
        }
        if (repaired <= 0.0001f) {
            worker.repairTargetId = 0;
            worker.state = State::Idle;
            worker.animTime = 0;
            statusMessage_ =
                "NOT ENOUGH RESOURCES TO REPAIR";
            statusTime_ = 3.0f;
            continue;
        }
        for (const dat::ResourceCost &cost :
             target->unit->costs) {
            if (!cost.flag || cost.type < 0 ||
                cost.amount <= 0)
                continue;
            resources_[(size_t)worker.player]
                      [cost.type] -=
                repaired * cost.amount * 0.5f /
                std::max(
                    1.0f,
                    target->maxHitPoints);
        }
        target->hitPoints = std::min(
            target->maxHitPoints,
            target->hitPoints + repaired);
    }
}

void Game::updateGathering(float dt) {
    bool depletedResource = false;
    for (Object &worker : objects_) {
        if (!worker.active || worker.hidden ||
            !worker.unit ||
            (!worker.gatherTargetId &&
             !worker.manualDropOff))
            continue;
        const dat::Unit *gatherer =
            gathererUnit(worker);
        if (!gatherer) {
            worker.gatherTargetId = 0;
            worker.dropOffTargetId = 0;
            continue;
        }
        Object *resource =
            findObject(worker.gatherTargetId);
        const float capacity =
            std::max(
                1.0f,
                (float)gatherer->resourceCapacity);
        const bool needsDropOff =
            worker.manualDropOff ||
            worker.carriedAmount >=
                capacity - 0.001f ||
            (resource &&
             worker.carriedAmount > 0 &&
             worker.carriedResourceType !=
                 resource->resourceType) ||
            (!resource || !isGatherable(*resource));
        if (needsDropOff &&
            worker.carriedAmount > 0) {
            Object *dropSite =
                findObject(worker.dropOffTargetId);
            if (!dropSite || !dropSite->active) {
                dropSite =
                    nearestDropSite(worker, *gatherer);
                worker.dropOffTargetId =
                    dropSite ? dropSite->spawnId : 0;
            }
            if (!dropSite) continue;
            if (!withinInteractionRange(
                    worker, *dropSite, 0.35f)) {
                approach(worker, *dropSite, 0.25f);
                continue;
            }
            resources_[(size_t)worker.player]
                      [worker.carriedResourceType] +=
                worker.carriedAmount;
            if (const dat::Task *task =
                    gatherTask(worker, *gatherer))
                playWorldUnitSound(
                    worker, task->resourceDepositSound);
            worker.carriedAmount = 0;
            worker.carriedResourceType = -1;
            worker.dropOffTargetId = 0;
            worker.manualDropOff = false;
            worker.state = State::Idle;
            worker.animTime = 0;
            if (!resource ||
                !isGatherable(*resource))
                worker.gatherTargetId = 0;
            continue;
        }
        if (!resource ||
            !isGatherable(*resource)) {
            worker.gatherTargetId = 0;
            worker.state = State::Idle;
            continue;
        }
        const dat::Task *task =
            gatherTask(worker, *gatherer);
        const float workRange =
            task ? std::max(
                       0.25f, task->workRange)
                 : 0.35f;
        if (!withinInteractionRange(
                worker, *resource, workRange)) {
            approach(worker, *resource, workRange);
            continue;
        }
        if (worker.state != State::Gather) {
            worker.animTime = 0;
            if (task)
                playWorldUnitSound(
                    worker,
                    task->resourceGatheringSound);
        }
        // Standing carbon (class 31) is felled as soon as a worker starts on
        // it, like the original: the object keeps its resources but drops to
        // 0 hit points and shows its dying graphic (the felled pile). Felled
        // trees no longer obstruct and can be built over.
        if (!resource->felled && resource->unit->cls == 31 &&
            resource->unit->dyingGraphic >= 0) {
            resource->felled = true;
            resource->hitPoints = 0.0f;
            resource->animTime = 0.0f;
            depletedResource = true; // rebuild obstruction/adjacency below
        }
        worker.state = State::Gather;
        worker.path.clear();
        worker.moveGoalActive = false;
        worker.facing = std::atan2(
            resource->y - worker.y,
            resource->x - worker.x);
        const float gathered = std::min(
            {resource->resourceAmount,
             capacity - worker.carriedAmount,
             gatherer->workRate * dt});
        if (gathered <= 0) continue;
        worker.carriedResourceType =
            resource->resourceType;
        worker.carriedAmount += gathered;
        resource->resourceAmount -= gathered;
        if (resource->maxHitPoints > 0 && !resource->felled) {
            float initialAmount = 0.0f;
            for (const dat::ResourceStorage &storage :
                 resource->unit->resourceStorages)
                if (storage.type == resource->resourceType)
                    initialAmount =
                        std::max(
                            initialAmount,
                            storage.amount);
            if (initialAmount > 0)
                resource->hitPoints = std::max(
                    1.0f,
                    resource->maxHitPoints *
                        resource->resourceAmount /
                        initialAmount);
        }
        if (resource->resourceAmount <= 0.001f) {
            killObject(*resource, false);
            depletedResource = true;
        }
    }
    if (depletedResource)
        rebuildAdjacency();
}

uint8_t Game::garrisonCategory(
    const Object &unit) const {
    if (!unit.unit || unit.unit->flyMode != 0 ||
        unit.unit->type == dat::UT_Building)
        return 0;
    switch (unit.unit->cls) {
    case 58: return 1; // workers
    case 52: return 2; // infantry
    case 50:
    case 51: return 8; // Jedi/Sith
    default: return 0;
    }
}

bool Game::canGarrison(const Object &unit,
                       const Object &building) const {
    const uint8_t category =
        garrisonCategory(unit);
    return unit.active && !unit.hidden && unit.unit &&
           building.active && building.unit &&
           !building.underConstruction &&
           unit.player == building.player &&
           unit.spawnId != building.spawnId &&
           unit.unit->speed > 0 &&
           unit.unit->type != dat::UT_Building &&
           unit.unit->flyMode == 0 &&
           building.unit->type == dat::UT_Building &&
           building.unit->garrisonCapacity > 0 &&
           category != 0 &&
           (building.unit->garrisonType & category) != 0;
}

size_t Game::garrisonedCount(
    const Object &building, bool includeIncoming) const {
    size_t count = 0;
    for (const Object &object : objects_)
        if (object.active &&
            (object.garrisonedInId ==
                 (int32_t)building.spawnId ||
             (includeIncoming &&
              object.garrisonTargetId ==
                  building.spawnId)))
            count++;
    return count;
}

bool Game::issueGarrisonCommand(Object &building) {
    size_t reserved =
        garrisonedCount(building, true);
    const size_t capacity =
        building.unit->garrisonCapacity;
    size_t assigned = 0;
    for (Object *unit : selectedObjectsInOrder(false)) {
        if (!unit || !canGarrison(*unit, building) ||
            reserved >= capacity)
            continue;
        if (isWorker(*unit))
            clearConstructionAssignment(*unit);
        unit->attackTargetId = 0;
        unit->attackAutomatic = false;
        unit->garrisonTargetId = building.spawnId;
        float targetX = 0.0f;
        float targetY = 0.0f;
        interactionPoint(*unit, building, 0.35f,
                         targetX, targetY);
        if (!issueMove(*unit, targetX, targetY, &building, 0.35f)) {
            unit->garrisonTargetId = 0;
            continue;
        }
        unit->wander = false;
        reserved++;
        assigned++;
    }
    if (assigned > 0) {
        playUnitAcknowledgement(
            *selectedObjectsInOrder(false).front(), false);
        statusMessage_ = "GARRISONING " +
                         std::to_string(assigned) +
                         (assigned == 1 ? " UNIT" : " UNITS");
        statusTime_ = 3.0f;
        return true;
    }
    statusMessage_ =
        reserved >= capacity
            ? "GARRISON IS FULL"
            : "NO ELIGIBLE UNITS SELECTED";
    statusTime_ = 3.0f;
    return false;
}

void Game::updateGarrisoning() {
    bool changed = false;
    for (Object &unit : objects_) {
        if (!unit.active || unit.hidden ||
            unit.garrisonTargetId == 0)
            continue;
        Object *building =
            findObject(unit.garrisonTargetId);
        if (!building ||
            !canGarrison(unit, *building)) {
            unit.garrisonTargetId = 0;
            continue;
        }
        if (garrisonedCount(*building, false) >=
            building->unit->garrisonCapacity) {
            unit.garrisonTargetId = 0;
            unit.moveGoalActive = false;
            unit.state = State::Idle;
            continue;
        }
        if (!withinInteractionRange(
                unit, *building, 0.75f))
            continue;
        unit.hidden = true;
        unit.selected = false;
        unit.garrisonedInId =
            (int32_t)building->spawnId;
        unit.garrisonTargetId = 0;
        unit.moveGoalActive = false;
        unit.moveGroupId = 0;
        unit.path.clear();
        unit.pathIndex = 0;
        unit.attackTargetId = 0;
        unit.state = State::Idle;
        unit.x = unit.homeX = building->x;
        unit.y = unit.homeY = building->y;
        changed = true;
        playWorldUnitSound(
            *building, assets_.dat().garrisonSound);
    }
    if (changed) {
        syncSelectionOrder();
        rebuildMobileOccupancy();
    }
}

bool Game::ejectGarrisonedUnit(
    Object &building, Object &unit,
    size_t placementOffset) {
    if (!building.active || !unit.active ||
        unit.garrisonedInId !=
            (int32_t)building.spawnId)
        return false;
    const float distance =
        collisionRadius(building) +
        collisionRadius(unit) + 0.55f;
    bool placed = false;
    for (int ring = 0; ring < 3 && !placed; ++ring)
        for (int slot = 0; slot < 16; ++slot) {
            const float angle =
                (slot + placementOffset * 5) *
                (2.0f * kPi / 16.0f);
            const float radius =
                distance + ring * 0.75f;
            unit.x =
                building.x + std::cos(angle) * radius;
            unit.y =
                building.y + std::sin(angle) * radius;
            unit.hidden = false;
            if (positionPassable(
                    unit, unit.x, unit.y, true))
                placed = true;
            else
                unit.hidden = true;
        }
    if (!placed) return false;
    unit.garrisonedInId = -1;
    unit.homeX = unit.targetX = unit.moveAnchorX =
        unit.x;
    unit.homeY = unit.targetY = unit.moveAnchorY =
        unit.y;
    unit.state = State::Idle;
    unit.animTime = 0;
    unit.wander = false;
    rebuildMobileOccupancy();
    playWorldUnitSound(
        building, assets_.dat().ungarrisonSound);
    return true;
}

size_t Game::ejectGarrisoned(Object &building) {
    size_t ejected = 0;
    for (Object &unit : objects_)
        if (ejectGarrisonedUnit(
                building, unit, ejected))
            ejected++;
    return ejected;
}

bool Game::destroyLastSelected() {
    syncSelectionOrder();
    while (!selectionOrder_.empty()) {
        const uint32_t spawnId = selectionOrder_.back();
        selectionOrder_.pop_back();
        Object *object = findObject(spawnId);
        if (!object || !object->active ||
            object->hidden ||
            object->player != localPlayer_)
            continue;
        const std::string name =
            unitDisplayName(*object->unit);
        killObject(*object);
        statusMessage_ = "DESTROYED " + name;
        statusTime_ = 2.0f;
        syncSelectionOrder();
        rebuildMobileOccupancy();
        return true;
    }
    return false;
}

void Game::setGateLocked(Object &gate, bool locked) {
    if (!gate.gate) return;
    gate.locked = locked;
    if (locked && gate.gateClosedUnit &&
        gate.gateOpenAmount <= 0.01f)
        gate.unit = gate.gateClosedUnit;
    rebuildAdjacency();
    statusMessage_ =
        locked ? "GATE LOCKED" : "GATE UNLOCKED";
    statusTime_ = 3.0f;
    int soundId = gate.unit->transformSound;
    if (soundId < 0)
        soundId = gate.unit->constructionSound;
    if (soundId < 0)
        soundId = gate.unit->selectionSound;
    playWorldUnitSound(gate, soundId);
}

bool Game::openSelectedActionMenu() {
    std::vector<Object *> selected =
        selectedObjectsInOrder(false);
    if (selected.empty()) return false;
    Object *subject = selected.front();
    if (selected.size() > 1 &&
        !std::all_of(
            selected.begin(), selected.end(),
            [&](const Object *object) {
                return object &&
                       object->player == localPlayer_ &&
                       isWorker(*object);
            }))
        return false;
    if (!subject || subject->player != localPlayer_)
        return false;
    if (subject->underConstruction) {
        statusMessage_ = "BUILDING UNDER CONSTRUCTION";
        statusTime_ = 3.0f;
        return true;
    }
    if (isWorker(*subject)) {
        actionMenuTab_ = ActionMenuTab::Economy;
        if (buildingOptionIds(subject->spawnId).empty()) {
            statusMessage_ = "NO BUILDINGS AVAILABLE";
            statusTime_ = 3.0f;
            return true;
        }
    } else if (subject->gate) {
        actionMenuTab_ = ActionMenuTab::Commands;
    } else if (subject->unit->type == dat::UT_Building) {
        actionMenuTab_ =
            productionOptions(*subject).empty()
                ? (researchOptions(*subject).empty()
                       ? ActionMenuTab::Commands
                       : ActionMenuTab::Research)
                : ActionMenuTab::Units;
    } else {
        return false;
    }
    if (productionOptions(*subject).empty() &&
        researchOptions(*subject).empty() &&
        !subject->gate &&
        garrisonedCount(*subject, false) == 0 &&
        (!isWorker(*subject) ||
         buildingOptionIds(subject->spawnId).empty())) {
        return false;
    }
    actionMenuOpen_ = true;
    actionMenuObjectId_ = subject->spawnId;
    actionMenuSelection_ = 0;
    actionMenuScroll_ = 0;
    return true;
}

bool Game::cancelProductionItem(Object &building,
                                size_t index) {
    if (index >= building.productionQueue.size())
        return false;
    const ProductionItem item =
        building.productionQueue[index];
    if (item.unit) {
        for (const dat::ResourceCost &cost :
             item.unit->costs)
            if (cost.flag && cost.type >= 0 &&
                cost.amount > 0)
                resources_[(size_t)building.player]
                          [cost.type] += cost.amount;
    } else if (item.technologyId >= 0 &&
               (size_t)item.technologyId <
                   assets_.dat().techs.size()) {
        for (const dat::Tech::Cost &cost :
             assets_.dat()
                 .techs[(size_t)item.technologyId]
                 .costs)
            if (cost.flag && cost.type >= 0 &&
                cost.amount > 0)
                resources_[(size_t)building.player]
                          [cost.type] += cost.amount;
    }
    building.productionQueue.erase(
        building.productionQueue.begin() +
        (ptrdiff_t)index);
    if (index == 0)
        building.productionRemaining =
            building.productionQueue.empty()
                ? 0.0f
                : building.productionQueue.front().duration;
    statusMessage_ = "QUEUE ITEM CANCELLED";
    statusTime_ = 2.0f;
    return true;
}

bool Game::handleActionMenuClick(float screenX, float screenY,
                                 int screenW, int screenH) {
    if (!actionMenuOpen_) return false;
    Object *subject = findObject(actionMenuObjectId_);
    if (!subject || !subject->active ||
        subject->player != localPlayer_) {
        actionMenuOpen_ = false;
        actionMenuObjectId_ = 0;
        return true;
    }
    if (screenX < kActionMenuX ||
        screenX >= std::min(
            (float)screenW,
            kActionMenuX + kActionMenuWidth) ||
        screenY < kActionMenuTop ||
        screenY >= kActionMenuTop + kActionMenuHeight)
        return true;
    if (screenY < kActionMenuY) {
        if (actionMenuTab_ ==
            ActionMenuTab::Stances) {
            return true;
        } else if (isWorker(*subject)) {
            const int tab = std::max(
                0, std::min(
                       2, (int)((screenX - kActionMenuX) /
                                (kActionMenuWidth / 3.0f))));
            actionMenuTab_ =
                tab == 0
                    ? ActionMenuTab::Economy
                    : tab == 1
                          ? ActionMenuTab::Military
                          : ActionMenuTab::Defense;
        } else {
            std::vector<ActionMenuTab> tabs;
            if (!productionOptions(*subject).empty())
                tabs.push_back(ActionMenuTab::Units);
            if (!researchOptions(*subject).empty())
                tabs.push_back(ActionMenuTab::Research);
            if (subject->gate ||
                subject->unit->garrisonCapacity > 0)
                tabs.push_back(ActionMenuTab::Commands);
            if (tabs.empty()) return true;
            const int tab = std::max(
                0, std::min(
                       (int)tabs.size() - 1,
                       (int)((screenX - kActionMenuX) /
                             (kActionMenuWidth /
                              tabs.size()))));
            actionMenuTab_ = tabs[(size_t)tab];
        }
        actionMenuSelection_ = 0;
        actionMenuScroll_ = 0;
        return true;
    }
    size_t optionCount = 0;
    if (actionMenuTab_ ==
        ActionMenuTab::Stances) {
        optionCount = 4;
    } else if (isWorker(*subject)) {
        optionCount =
            buildingOptions(*subject, actionMenuTab_).size();
    } else if (actionMenuTab_ ==
               ActionMenuTab::Commands) {
        optionCount = 1;
    } else if (actionMenuTab_ ==
               ActionMenuTab::Units) {
        optionCount = productionOptions(*subject).size();
    } else {
        optionCount = researchOptions(*subject).size();
    }
    const float queueY =
        kActionMenuY +
        kActionMenuRows * kActionMenuCell +
        48.0f;
    if (actionMenuTab_ !=
            ActionMenuTab::Stances &&
        !subject->productionQueue.empty() &&
        screenY >= queueY &&
        screenY < queueY + 50.0f) {
        const float slotWidth =
            (kActionMenuWidth - 12.0f) / 5.0f;
        const int index =
            (int)((screenX - kActionMenuX - 6.0f) /
                  slotWidth);
        if (index >= 0)
            cancelProductionItem(
                *subject, (size_t)index);
        return true;
    }
    const float gridX = kActionMenuX + 12.0f;
    const float gridWidth =
        kActionMenuColumns * kActionMenuCell;
    const float gridHeight =
        kActionMenuRows * kActionMenuCell;
    if (screenX < gridX ||
        screenX >= gridX + gridWidth ||
        screenY < kActionMenuY ||
        screenY >= kActionMenuY + gridHeight)
        return true;
    const size_t column =
        (size_t)((screenX - gridX) /
                 kActionMenuCell);
    const size_t row =
        (size_t)((screenY - kActionMenuY) /
                 kActionMenuCell);
    const size_t optionIndex =
        actionMenuScroll_ +
        row * kActionMenuColumns + column;
    if (optionIndex >= optionCount) return true;
    actionMenuSelection_ = optionIndex;

    if (actionMenuTab_ ==
        ActionMenuTab::Stances) {
        setSelectedAttackMode(
            (AttackMode)optionIndex);
        actionMenuOpen_ = false;
        actionMenuObjectId_ = 0;
        return true;
    }

    if (isWorker(*subject)) {
        const std::vector<const dat::Unit *> options =
            buildingOptions(*subject, actionMenuTab_);
        if (optionIndex >= options.size()) return true;
        return beginBuildingPlacement(
            *subject, *options[optionIndex]);
    }

    if (actionMenuTab_ == ActionMenuTab::Commands) {
        if (optionIndex != 0) return true;
        if (subject->gate) {
            setGateLocked(*subject, !subject->locked);
        } else {
            const size_t ejected =
                ejectGarrisoned(*subject);
            statusMessage_ =
                ejected > 0
                    ? "EJECTED " +
                          std::to_string(ejected) +
                          (ejected == 1 ? " UNIT"
                                        : " UNITS")
                    : "NO UNITS GARRISONED";
            statusTime_ = 3.0f;
        }
        actionMenuOpen_ = false;
        actionMenuObjectId_ = 0;
        return true;
    }

    if (subject->productionQueue.size() >= 5) {
        statusMessage_ = "PRODUCTION QUEUE FULL";
        statusTime_ = 3.0f;
        return true;
    }

    ProductionItem item;
    if (actionMenuTab_ == ActionMenuTab::Research) {
        const std::vector<int> options =
            researchOptions(*subject);
        if (optionIndex >= options.size()) return true;
        const int technologyId = options[optionIndex];
        const dat::Tech &technology =
            assets_.dat().techs[(size_t)technologyId];
        if (!technologyRequirementsMet(
                localPlayer_, technology)) {
            // Original string 3062: shown when the tech-level button is
            // pressed before its building prerequisites exist.
            std::string message = assets_.localizedString(3062);
            if (message.empty())
                message = technologyDisplayName(technologyId) +
                          " IS LOCKED";
            statusMessage_ = message;
            statusTime_ = 3.0f;
            return true;
        }
        for (const dat::Tech::Cost &cost : technology.costs) {
            if (!cost.flag || cost.type < 0 ||
                cost.amount <= 0)
                continue;
            if (resource(localPlayer_, cost.type) + 0.001f <
                cost.amount) {
                statusMessage_ = "NOT ENOUGH RESOURCES";
                statusTime_ = 3.0f;
                return true;
            }
        }
        for (const dat::Tech::Cost &cost : technology.costs)
            if (cost.flag && cost.type >= 0 &&
                cost.amount > 0)
                resources_[(size_t)localPlayer_][cost.type] -=
                    cost.amount;
        item.technologyId = technologyId;
        item.duration =
            std::max(0.1f, (float)technology.researchTime);
        statusMessage_ = "RESEARCH QUEUED";
    } else {
        const std::vector<const dat::Unit *> options =
            productionOptions(*subject);
        if (optionIndex >= options.size()) return true;
        const dat::Unit *unit = options[optionIndex];
        for (const dat::ResourceCost &cost : unit->costs) {
            if (!cost.flag || cost.type < 0 ||
                cost.amount <= 0)
                continue;
            if (resource(localPlayer_, cost.type) + 0.001f <
                cost.amount) {
                statusMessage_ = "NOT ENOUGH RESOURCES";
                statusTime_ = 3.0f;
                return true;
            }
        }
        for (const dat::ResourceCost &cost : unit->costs)
            if (cost.flag && cost.type >= 0 &&
                cost.amount > 0)
                resources_[(size_t)localPlayer_][cost.type] -=
                    cost.amount;
        item.unit = unit;
        item.duration =
            std::max(0.1f, (float)unit->trainTime);
        statusMessage_ = "UNIT QUEUED";
    }
    subject->productionQueue.push_back(item);
    if (subject->productionQueue.size() == 1)
        subject->productionRemaining = item.duration;
    statusTime_ = 2.0f;
    return true;
}

bool Game::beginBuildingPlacement(
    Object &worker, const dat::Unit &building) {
    placementUnit_ = &building;
    placementBuilderId_ = worker.spawnId;
    actionMenuOpen_ = false;
    actionMenuObjectId_ = 0;
    statusMessage_ =
        "PLACE " + unitDisplayName(building) +
        " WITH X; O CANCELS";
    statusTime_ = 4.0f;
    return true;
}

void Game::clearConstructionAssignment(Object &worker) {
    if (worker.constructionTargetId != 0) {
        Object *building =
            findObject(worker.constructionTargetId);
        if (building &&
            building->constructionBuilderId ==
                worker.spawnId)
            building->constructionBuilderId = 0;
    }
    worker.constructionTargetId = 0;
    worker.repairTargetId = 0;
    if (worker.state == State::Build) {
        worker.state = State::Idle;
        worker.animTime = 0.0f;
    }
}

bool Game::assignBuilder(Object &worker,
                         Object &building) {
    if (!isWorker(worker) ||
        !building.active ||
        !building.underConstruction ||
        building.player != worker.player)
        return false;
    clearConstructionAssignment(worker);
    float targetX = 0.0f;
    float targetY = 0.0f;
    interactionPoint(worker, building, 0.35f,
                     targetX, targetY);
    if (!issueMove(worker, targetX, targetY, &building, 0.35f))
        return false;
    if (building.constructionBuilderId == 0)
        building.constructionBuilderId =
            worker.spawnId;
    worker.constructionTargetId = building.spawnId;
    return true;
}

bool Game::placeBuilding(float screenX, float screenY,
                         int screenW, int screenH) {
    if (!placementUnit_) return false;
    Object *worker = findObject(placementBuilderId_);
    if (!worker || !isWorker(*worker)) {
        placementUnit_ = nullptr;
        placementBuilderId_ = 0;
        return true;
    }

    float worldX = 0, worldY = 0;
    screenToWorld(screenX, screenY, screenW, screenH,
                  worldX, worldY);
    snapAdjacentBuildingPosition(
        *placementUnit_, worldX, worldY);
    Object candidate;
    candidate.unit = placementUnit_;
    candidate.player = localPlayer_;
    if (!positionPassable(
            candidate, worldX, worldY, true)) {
        statusMessage_ = "BUILDING CANNOT BE PLACED HERE";
        statusTime_ = 3.0f;
        return true;
    }
    for (const dat::ResourceCost &cost :
         placementUnit_->costs) {
        if (!cost.flag || cost.type < 0 ||
            cost.amount <= 0)
            continue;
        if (resource(localPlayer_, cost.type) + 0.001f <
            cost.amount) {
            statusMessage_ = "NOT ENOUGH RESOURCES";
            statusTime_ = 3.0f;
            return true;
        }
    }
    for (const dat::ResourceCost &cost :
         placementUnit_->costs)
        if (cost.flag && cost.type >= 0 &&
            cost.amount > 0)
            resources_[(size_t)localPlayer_][cost.type] -=
                cost.amount;

    std::vector<uint32_t> builderIds;
    for (Object *selected :
         selectedObjectsInOrder(true))
        if (selected &&
            isWorker(*selected) &&
            selected->player == localPlayer_)
            builderIds.push_back(
                selected->spawnId);
    if (builderIds.empty())
        builderIds.push_back(worker->spawnId);
    const dat::Unit *unit = placementUnit_;
    Object *building = addObject(
        unit, localPlayer_, worldX, worldY, 0,
        nextSpawnId_++);
    if (!building) return true;
    building->wander = false;
    building->underConstruction = true;
    building->constructionTotal =
        std::max(1.0f, (float)unit->trainTime);
    building->constructionRemaining =
        forceBuildCheat_ ? 0.0f
                         : building->constructionTotal;
    building->hitPoints = 1.0f;

    rebuildAdjacency();
    Object *acknowledgement = nullptr;
    for (uint32_t builderId : builderIds) {
        worker = findObject(builderId);
        if (worker &&
            assignBuilder(*worker, *building) &&
            !acknowledgement)
            acknowledgement = worker;
    }
    if (acknowledgement)
        playUnitAcknowledgement(
            *acknowledgement, false);
    placementUnit_ = nullptr;
    placementBuilderId_ = 0;
    statusMessage_ = "CONSTRUCTION STARTED";
    statusTime_ = 2.0f;
    return true;
}

void Game::updateConstruction(float dt) {
    struct CompletedBuilding {
        uint32_t spawnId;
        const dat::Unit *unit;
        int player;
        float x, y, facing;
        std::vector<uint32_t> builders;
    };
    std::vector<CompletedBuilding> completed;
    const size_t count = objects_.size();
    for (size_t index = 0; index < count; index++) {
        Object &building = objects_[index];
        if (!building.active ||
            !building.underConstruction)
            continue;
        const bool forceComplete =
            forceBuildCheat_ &&
            building.player == localPlayer_;
        size_t workingBuilders = 0;
        for (Object &builder : objects_) {
            if (!builder.active ||
                builder.constructionTargetId !=
                    building.spawnId)
                continue;
            const bool working =
                withinInteractionRange(
                    builder, building, 0.75f);
            if (working) {
                if (builder.state != State::Build)
                    builder.animTime = 0.0f;
                if (builder.state != State::Build &&
                    building.unit->constructionSound >= 0)
                    playWorldUnitSound(
                        building,
                        building.unit->constructionSound);
                builder.state = State::Build;
                builder.facing =
                    std::atan2(building.y - builder.y,
                               building.x - builder.x);
                builder.moveGoalActive = false;
                builder.path.clear();
                workingBuilders++;
            } else if (builder.state == State::Build) {
                builder.state = State::Idle;
                builder.animTime = 0.0f;
            }
        }
        if (forceComplete)
            building.constructionRemaining = 0.0f;
        else if (workingBuilders > 0) {
            const float buildRate =
                3.0f * workingBuilders /
                (workingBuilders + 2.0f);
            building.constructionRemaining =
                std::max(
                    0.0f,
                    building.constructionRemaining -
                        dt * buildRate);
        }
        const float progress =
            building.constructionTotal > 0
                ? 1.0f -
                      building.constructionRemaining /
                          building.constructionTotal
                : 1.0f;
        building.hitPoints = std::max(
            1.0f, building.maxHitPoints * progress);
        if (building.constructionRemaining > 0)
            continue;
        building.underConstruction = false;
        building.hitPoints = building.maxHitPoints;
        int completionSound =
            building.unit->trainSound;
        if (completionSound < 0)
            completionSound =
                building.unit->constructionSound;
        if (completionSound < 0)
            completionSound =
                building.unit->selectionSound;
        playWorldUnitSound(
            building, completionSound);
        std::vector<uint32_t> builders;
        for (Object &builder : objects_)
            if (builder.constructionTargetId ==
                building.spawnId) {
                builders.push_back(builder.spawnId);
                builder.constructionTargetId = 0;
                builder.state = State::Idle;
                builder.animTime = 0.0f;
            }
        building.constructionBuilderId = 0;
        completed.push_back(
            {building.spawnId, building.unit,
             building.player, building.x,
             building.y, building.facing,
             std::move(builders)});
    }
    for (const CompletedBuilding &building : completed) {
        const int civilization =
            civilizationForPlayer(building.player);
        for (const dat::BuildingAnnex &annex :
             building.unit->annexes) {
            const dat::Unit *part =
                findUnit(civilization, annex.unitId);
            if (!part) continue;
            Object *created = addObject(
                part, building.player,
                building.x + annex.misplacementY,
                building.y - annex.misplacementX,
                building.facing, 0, 0, false, -1,
                false);
            if (created) created->wander = false;
        }
    }
    if (!completed.empty()) {
        refreshAllAutomaticTechnologies();
        rebuildAdjacency();
        for (const CompletedBuilding &building :
             completed) {
            const Object *completedObject =
                findObject(building.spawnId);
            if (!completedObject) continue;
            for (uint32_t builderId :
                 building.builders) {
                Object *builder =
                    findObject(builderId);
                if (builder)
                    assignAutomaticWorkerTask(
                        *builder, *completedObject);
            }
        }
    }
}

void Game::commandAtScreen(float screenX, float screenY, int screenW, int screenH) {
    std::vector<Object *> selected = selectedObjectsInOrder(true);
    if (selected.empty()) return;

    Object *resource =
        gatherableAtScreen(
            screenX, screenY, screenW, screenH);
    if (resource) {
        Object *acknowledgement = nullptr;
        for (Object *object : selected)
            if (issueGatherCommand(
                    *object, *resource) &&
                !acknowledgement)
                acknowledgement = object;
        if (acknowledgement) {
            playUnitAcknowledgement(
                *acknowledgement, false);
            commandMarkerX_ = resource->x;
            commandMarkerY_ = resource->y;
            commandMarkerTime_ = 0.65f;
            statusMessage_ = "GATHERING " +
                unitDisplayName(*resource->unit);
            statusTime_ = 2.0f;
            return;
        }
    }
    Object *friendly =
        objectAtScreen(
            screenX, screenY, screenW,
            screenH);
    if (friendly) {
        Object *depositor = nullptr;
        size_t assigned = 0;
        for (Object *object : selected)
            if (issueDropOffCommand(
                    *object, *friendly)) {
                if (!depositor) depositor = object;
                assigned++;
            }
        if (depositor) {
            playUnitAcknowledgement(*depositor, false);
            commandMarkerX_ = friendly->x;
            commandMarkerY_ = friendly->y;
            commandMarkerTime_ = 0.65f;
            statusMessage_ =
                "DEPOSITING RESOURCES (" +
                std::to_string(assigned) + ")";
            statusTime_ = 2.0f;
            return;
        }
        Object *acknowledgement = nullptr;
        for (Object *object : selected)
            if (issueRepairCommand(
                    *object, *friendly) &&
                !acknowledgement)
                acknowledgement = object;
        if (acknowledgement) {
            playUnitAcknowledgement(
                *acknowledgement, false);
            commandMarkerX_ = friendly->x;
            commandMarkerY_ = friendly->y;
            commandMarkerTime_ = 0.65f;
            statusMessage_ =
                "REPAIRING " +
                unitDisplayName(
                    *friendly->unit);
            statusTime_ = 2.0f;
            return;
        }
    }
    if (friendly && friendly->underConstruction) {
        Object *acknowledgement = nullptr;
        for (Object *object : selected)
            if (isWorker(*object) &&
                object->player == friendly->player &&
                assignBuilder(*object, *friendly) &&
                !acknowledgement)
                acknowledgement = object;
        if (acknowledgement) {
            playUnitAcknowledgement(
                *acknowledgement, false);
            commandMarkerX_ = friendly->x;
            commandMarkerY_ = friendly->y;
            commandMarkerTime_ = 0.65f;
            statusMessage_ = "CONSTRUCTION RESUMED";
            statusTime_ = 2.0f;
            return;
        }
    }

    Object *enemy = enemyAtScreen(screenX, screenY, screenW, screenH);
    if (enemy) {
        struct ReservedAttackSlot {
            float x, y, radius;
        };
        Object *acknowledgement = nullptr;
        std::vector<Object *> attackers;
        for (Object *source : selected)
            if (!canAttack(*source) || !isEnemy(*source, *enemy)) continue;
            else
                attackers.push_back(source);
        float centroidX = 0, centroidY = 0;
        for (const Object *source : attackers) {
            centroidX += source->x;
            centroidY += source->y;
        }
        const float baseAngle =
            attackers.empty()
                ? 0
                : std::atan2(centroidY / attackers.size() - enemy->y,
                             centroidX / attackers.size() - enemy->x);
        std::stable_sort(
            attackers.begin(), attackers.end(),
            [&](const Object *a, const Object *b) {
                return attackRange(*a, *enemy) <
                       attackRange(*b, *enemy);
            });
        std::vector<ReservedAttackSlot> reserved;
        reserved.reserve(attackers.size());
        for (Object *source : attackers) {
            if (isWorker(*source))
                clearConstructionAssignment(*source);
            const float sourceRadius = collisionRadius(*source);
            const float contact =
                sourceRadius + collisionRadius(*enemy) + 0.08f;
            const float maximumDistance =
                std::max(contact, attackRange(*source, *enemy) - 0.05f);
            const float minimumDistance =
                source->unit->minRange > 0
                    ? std::min(maximumDistance,
                               std::max(contact,
                                        source->unit->minRange + 0.2f))
                    : contact;
            const float preferredDistance =
                std::max(minimumDistance,
                         std::min(maximumDistance,
                                  attackRange(*source, *enemy) * 0.8f));
            const float sourceAngle =
                std::atan2(source->y - enemy->y,
                           source->x - enemy->x);
            const float radialStep =
                std::max(0.35f, sourceRadius * 2.0f + 0.12f);
            float slotAngle = sourceAngle;
            float slotDistance = preferredDistance;
            bool foundSlot = false;
            auto slotOpen = [&](float angle, float distance) {
                const float x = enemy->x + std::cos(angle) * distance;
                const float y = enemy->y + std::sin(angle) * distance;
                if (!positionPassable(*source, x, y, false))
                    return false;
                for (const ReservedAttackSlot &slot : reserved) {
                    const float dx = x - slot.x;
                    const float dy = y - slot.y;
                    const float separation =
                        sourceRadius + slot.radius + 0.08f;
                    if (dx * dx + dy * dy <
                        separation * separation)
                        return false;
                }
                return true;
            };
            for (int band = 0; band <= 12 && !foundSlot; band++) {
                const float offsets[] = {
                    band == 0 ? 0.0f : -band * radialStep,
                    band == 0 ? 0.0f : band * radialStep};
                for (float radialOffset : offsets) {
                    const float distance =
                        preferredDistance + radialOffset;
                    if (distance < minimumDistance - 0.001f ||
                        distance > maximumDistance + 0.001f)
                        continue;
                    const float separation =
                        sourceRadius * 2.0f + 0.12f;
                    const int samples =
                        std::max(16, std::min(
                                         96,
                                         (int)std::ceil(
                                             2.0f * kPi * distance /
                                             separation)));
                    for (int sample = 0; sample < samples; sample++) {
                        const int alternating =
                            sample == 0
                                ? 0
                                : ((sample + 1) / 2) *
                                      (sample & 1 ? 1 : -1);
                        const float angle =
                            sourceAngle +
                            alternating *
                                (2.0f * kPi / samples);
                        if (!slotOpen(angle, distance))
                            continue;
                        slotAngle = angle;
                        slotDistance = distance;
                        foundSlot = true;
                        break;
                    }
                    if (foundSlot) break;
                }
            }
            for (int ring = 1; ring <= 16 && !foundSlot; ring++) {
                const float distance =
                    maximumDistance + ring * radialStep;
                const int samples =
                    std::max(16, std::min(
                                     96,
                                     (int)std::ceil(
                                         2.0f * kPi * distance /
                                         (sourceRadius * 2.0f + 0.12f))));
                for (int sample = 0; sample < samples; sample++) {
                    const float angle =
                        baseAngle +
                        (sample + (ring & 1) * 0.5f) *
                            (2.0f * kPi / samples);
                    if (!slotOpen(angle, distance))
                        continue;
                    slotAngle = angle;
                    slotDistance = distance;
                    foundSlot = true;
                    break;
                }
            }
            reserved.push_back(
                {enemy->x + std::cos(slotAngle) * slotDistance,
                 enemy->y + std::sin(slotAngle) * slotDistance,
                 sourceRadius});
            issueAttack(*source, *enemy, slotAngle, false,
                        slotDistance);
            if (!acknowledgement) acknowledgement = source;
        }
        if (!acknowledgement) return;
        attackOrdersIssued_++;
        playUnitAcknowledgement(*acknowledgement, true);
        commandMarkerX_ = enemy->x;
        commandMarkerY_ = enemy->y;
        commandMarkerTime_ = 0.65f;
        return;
    }

    float targetX, targetY;
    screenToWorld(screenX, screenY, screenW, screenH, targetX, targetY);
    for (Object *object : selected) {
        if (isWorker(*object))
            clearConstructionAssignment(*object);
        object->gatherTargetId = 0;
        object->dropOffTargetId = 0;
        object->attackTargetId = 0;
        object->attackAutomatic = false;
    }
    playUnitAcknowledgement(*selected.front(), false);
    issueGroupMove(std::move(selected), targetX, targetY, selectedFormation_);
    commandMarkerX_ = targetX;
    commandMarkerY_ = targetY;
    commandMarkerTime_ = 0.65f;
}

void Game::playUnitAcknowledgement(const Object &object, bool attack) {
    if (!playUnitSound_) return;
    int soundId = attack ? object.unit->attackSound : object.unit->moveSound;
    if (soundId < 0) soundId = object.unit->selectionSound;
    if (soundId >= 0) playUnitSound_(soundId, civilizationForPlayer(object.player));
}

bool Game::worldSoundAudible(float x, float y) const {
    float screenX, screenY;
    toScreen(x, y, screenX, screenY);
    const float margin = 96.0f / zoom_;
    const float halfWidth = 480.0f / zoom_ + margin;
    const float halfHeight = 272.0f / zoom_ + margin;
    return std::abs(screenX - camX_) <= halfWidth &&
           std::abs(screenY - camY_) <= halfHeight;
}

void Game::playWorldUnitSound(const Object &object, int soundId) {
    if (!playUnitSound_ || soundId < 0 ||
        !worldSoundAudible(object.x, object.y))
        return;
    playUnitSound_(soundId, civilizationForPlayer(object.player));
}

void Game::updateAmbience(float dt, int screenW, int screenH) {
    if (!playAmbientSound_ || terrain_.empty() ||
        mapSize_ <= 0)
        return;
    ambienceTime_ -= dt;
    if (ambienceTime_ > 0) return;

    float worldX = 0, worldY = 0;
    screenToWorld(screenW * 0.5f, screenH * 0.5f,
                  screenW, screenH, worldX, worldY);
    const int x = std::max(
        0, std::min(mapSize_ - 1, (int)std::floor(worldX)));
    const int y = std::max(
        0, std::min(mapSize_ - 1, (int)std::floor(worldY)));
    const int terrainId = terrainAt(x, y);
    const auto &terrains = assets_.dat().terrainBlock.terrains;
    if (terrainId < 0 ||
        (size_t)terrainId >= terrains.size()) {
        ambienceTime_ = 12.0f;
        return;
    }

    const dat::Terrain &terrain = terrains[(size_t)terrainId];
    std::string name = terrain.name + " " + terrain.name2;
    std::transform(
        name.begin(), name.end(), name.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    const auto contains = [&](const char *text) {
        return name.find(text) != std::string::npos;
    };
    const char *family = nullptr;
    int variations = 0;
    if (contains("volcan") || contains("lava")) {
        family = "EF-TERR-VOLCANIC";
        variations = 7;
    } else if (contains("sludge")) {
        family = "EF-TERR-SLUDGE";
        variations = 3;
    } else if (contains("swamp")) {
        family = "EF-TERR-SWAMP";
        variations = 4;
    } else if (contains("snow")) {
        family = "EF-TERR-SNOW";
        variations = 7;
    } else if (contains("ice")) {
        family = "EF-TERR-ICE";
        variations = 3;
    } else if (contains("jungle")) {
        family = "EF-TERR-JUNGLE";
        variations = 5;
    } else if (contains("forest") ||
               contains("tree")) {
        family = "EF-TERR-FOREST";
        variations = 3;
    } else if (contains("desert") ||
               contains("sand")) {
        family = "EF-TERR-DESERT";
        variations = 5;
    } else if (contains("shore") ||
               contains("water") ||
               contains("ocean")) {
        family = "EF-TERR-SHORE";
        variations = 3;
    } else if (contains("metal") ||
               contains("industrial")) {
        family = "EF-TERR-METAL";
        variations = 10;
    }
    if (!family) {
        ambienceTime_ = 10.0f;
        return;
    }

    const int variation =
        1 + (int)((ambienceSequence_++ +
                   (uint32_t)terrainId * 3u) %
                  (uint32_t)variations);
    std::ostringstream file;
    file << family << "-" << std::setw(2)
         << std::setfill('0') << variation;
    const float duration = playAmbientSound_(file.str());
    ambienceTime_ =
        std::max(8.0f, duration + 4.0f +
                           (ambienceSequence_ % 5));
}

void Game::defeatCheatPlayer(int player) {
    if (player <= 0 || player > 8) return;
    for (Object &object : objects_) {
        if (object.active && object.player == player)
            object.active = false;
    }
    syncSelectionOrder();
    rebuildAdjacency();
    rebuildMobileOccupancy();
}

bool Game::spawnCheatUnit(int unitId, bool requireWater,
                          int screenW, int screenH) {
    const dat::Unit *unit =
        findUnit(civilizationForPlayer(localPlayer_), unitId);
    if (!unit) return false;

    float cursorX = 0.0f, cursorY = 0.0f;
    screenToWorld(cursorX_, cursorY_, screenW, screenH,
                  cursorX, cursorY);
    Object candidate;
    candidate.unit = unit;
    candidate.player = localPlayer_;
    bool found = false;
    float spawnX = cursorX, spawnY = cursorY;
    for (int ring = 0; ring < 24 && !found; ring++) {
        const int samples = ring == 0 ? 1 : 12 + ring * 4;
        for (int sample = 0; sample < samples; sample++) {
            const float angle =
                sample * (2.0f * kPi / samples);
            const float distance = ring * 0.5f;
            candidate.x = cursorX + std::cos(angle) * distance;
            candidate.y = cursorY + std::sin(angle) * distance;
            if (!positionPassable(candidate, candidate.x,
                                  candidate.y, true))
                continue;
            spawnX = candidate.x;
            spawnY = candidate.y;
            found = true;
            break;
        }
    }
    if (!found) {
        statusMessage_ = requireWater
            ? "PLACE CURSOR OVER OPEN WATER"
            : "NO VALID SPAWN POSITION";
        statusTime_ = 3.0f;
        return false;
    }

    clearSelection();
    Object *created = addObject(
        unit, localPlayer_, spawnX, spawnY, -kPi * 0.5f,
        nextSpawnId_++);
    if (!created) return false;
    created->wander = false;
    created->stateTime = 1.0f;
    selectObject(*created, true);
    rebuildAdjacency();
    rebuildMobileOccupancy();
    return true;
}

void Game::activateCheat(size_t index, int screenW, int screenH) {
    if (index >= std::size(kCheats)) return;
    const CheatEntry &cheat = kCheats[index];
    bool activated = true;
    bool customStatus = false;
    switch (cheat.action) {
    case CheatAction::Food:
        resources_[(size_t)localPlayer_][0] += 1000.0f;
        break;
    case CheatAction::Carbon:
        resources_[(size_t)localPlayer_][1] += 1000.0f;
        break;
    case CheatAction::Nova:
        resources_[(size_t)localPlayer_][3] += 1000.0f;
        break;
    case CheatAction::Ore:
        resources_[(size_t)localPlayer_][2] += 1000.0f;
        break;
    case CheatAction::ForceBuild:
        forceBuildCheat_ = !forceBuildCheat_;
        for (Object &object : objects_) {
            if (object.active && !object.productionQueue.empty())
                object.productionRemaining = 0.0f;
        }
        statusMessage_ = forceBuildCheat_
            ? "FORCEBUILD ENABLED" : "FORCEBUILD DISABLED";
        customStatus = true;
        break;
    case CheatAction::ForceTech:
        fullTechTreeCheat_ = true;
        disabledTechs_[(size_t)localPlayer_].clear();
        disabledUnits_[(size_t)localPlayer_].clear();
        refreshAutomaticTechnologies(localPlayer_);
        statusMessage_ = "FULL CIVILIZATION TECH TREE ENABLED";
        customStatus = true;
        break;
    case CheatAction::ForceSight:
        forceSightCheat_ = true;
        statusMessage_ = "VISIBILITY IS ALREADY UNRESTRICTED";
        customStatus = true;
        break;
    case CheatAction::ForceExplore:
        forceExploreCheat_ = true;
        statusMessage_ = "THE MAP IS ALREADY FULLY EXPLORED";
        customStatus = true;
        break;
    case CheatAction::Spawn:
        activated = spawnCheatUnit(
            cheat.value, cheat.requireWater, screenW, screenH);
        break;
    case CheatAction::Tarkin:
        for (int player = 1; player <= 8; player++) {
            bool enemy = player != localPlayer_;
            if (localPlayer_ > 0 &&
                (size_t)localPlayer_ <= players_.size()) {
                const auto &diplomacy =
                    players_[(size_t)localPlayer_ - 1].diplomacy;
                enemy = (size_t)player < diplomacy.size() &&
                        diplomacy[(size_t)player] == 3;
            }
            if (enemy)
                defeatCheatPlayer(player);
        }
        break;
    case CheatAction::Skywalker:
        victoryState_ = 1;
        statusMessage_ = "SCENARIO WON";
        customStatus = true;
        break;
    case CheatAction::Darkside:
        defeatCheatPlayer(cheat.value);
        break;
    case CheatAction::Technology: {
        std::vector<std::pair<Object *, float>> oldMaximums;
        for (Object &object : objects_)
            if (object.active &&
                object.player == localPlayer_)
                oldMaximums.push_back(
                    {&object, object.maxHitPoints});
        activated = researchTechnology(localPlayer_, cheat.value);
        if (activated)
            for (const auto &entry : oldMaximums) {
                Object &object = *entry.first;
                const float maximum = std::max(
                    1.0f, modifiedUnitAttribute(
                              object, 0,
                              object.unit->hitPoints));
                object.hitPoints = std::max(
                    1.0f, object.hitPoints +
                              maximum - entry.second);
                object.maxHitPoints = maximum;
            }
        break;
    }
    }
    if (activated && !customStatus)
        statusMessage_ = std::string(cheat.code) + " ACTIVATED";
    if (activated) statusTime_ = 3.0f;
}

void Game::cycleSelectedAttackMode() {
    bool changed = false;
    for (Object &object : objects_) {
        if (!object.selected || !isSelectable(object) || !canAttack(object)) continue;
        switch (object.attackMode) {
        case AttackMode::Aggressive:
            object.attackMode = AttackMode::Defensive;
            break;
        case AttackMode::Defensive:
            object.attackMode = AttackMode::StandGround;
            break;
        case AttackMode::StandGround:
            object.attackMode = AttackMode::Passive;
            break;
        case AttackMode::Passive:
            object.attackMode = AttackMode::Aggressive;
            break;
        }
        if (object.attackAutomatic &&
            (object.attackMode == AttackMode::Passive ||
             object.attackMode == AttackMode::StandGround))
            finishAttack(object, object.attackMode != AttackMode::StandGround);
        changed = true;
    }
    if (changed) attackModeChanges_++;
}

void Game::setSelectedAttackMode(
    AttackMode mode) {
    bool changed = false;
    for (Object *object :
         selectedObjectsInOrder(false)) {
        if (!object || !canAttack(*object))
            continue;
        if (object->attackMode != mode) {
            object->attackMode = mode;
            changed = true;
        }
        if (object->attackAutomatic &&
            (mode == AttackMode::Passive ||
             mode == AttackMode::StandGround))
            finishAttack(
                *object,
                mode != AttackMode::StandGround);
    }
    if (changed) attackModeChanges_++;
    static constexpr const char *labels[] = {
        "AGGRESSIVE", "DEFENSIVE",
        "STAND GROUND", "PASSIVE",
    };
    statusMessage_ =
        std::string("STANCE: ") +
        labels[(size_t)mode];
    statusTime_ = 2.0f;
}

void Game::issueAttack(Object &source, Object &target, float approachAngle,
                       bool automatic, float approachDistance) {
    source.gatherTargetId = 0;
    source.dropOffTargetId = 0;
    if (approachDistance <= 0) {
        const float contact =
            collisionRadius(source) + collisionRadius(target) +
            0.08f;
        const float range = attackRange(source, target);
        approachDistance =
            std::max(contact, range * 0.8f);
        if (source.unit->minRange > 0)
            approachDistance =
                std::max(approachDistance,
                         source.unit->minRange + 0.2f);
    }
    source.attackTargetId = target.spawnId;
    source.attackRepathTime = 0;
    source.attackApproachAngle = approachAngle;
    source.attackApproachDistance = approachDistance;
    if (approachDistance > 0) {
        source.targetX =
            target.x + std::cos(approachAngle) * approachDistance;
        source.targetY =
            target.y + std::sin(approachAngle) * approachDistance;
    }
    source.attackSlotRetries = 0;
    source.attackStallTime = 0;
    source.attackBestDistance = std::numeric_limits<float>::max();
    source.moveGoalActive = false;
    source.moveGroupId = 0;
    source.moveSpeedLimit = 0;
    source.attackAutomatic = automatic;
    source.path.clear();
    source.pathIndex = 0;
    source.blockedTime = 0;
    source.state = State::Idle;
    source.wander = false;
}

float Game::automaticAcquisitionRadius(const Object &source) const {
    const float weaponRange = std::max(0.0f, source.unit->maxRange);
    const float sight = std::max(0.0f, source.unit->lineOfSight);
    if (source.unit->type == dat::UT_Building ||
        source.attackMode == AttackMode::StandGround)
        return std::max(weaponRange + 0.5f, attackRange(source, source));
    if (source.attackMode == AttackMode::Aggressive)
        return std::max(6.0f, std::max(sight, source.unit->searchRadius));
    if (source.attackMode == AttackMode::Defensive)
        return std::max(4.0f, std::max(weaponRange + 2.0f, sight * 0.75f));
    return 0;
}

float Game::automaticPursuitLeash(const Object &source) const {
    if (source.unit->type == dat::UT_Building ||
        source.attackMode == AttackMode::StandGround)
        return 0;
    const float acquisition = automaticAcquisitionRadius(source);
    return source.attackMode == AttackMode::Aggressive
               ? std::numeric_limits<float>::max()
               : std::max(6.0f, acquisition);
}

void Game::finishAttack(Object &source, bool returnToPost) {
    source.attackTargetId = 0;
    source.attackAutomatic = false;
    source.path.clear();
    source.pathIndex = 0;
    source.blockedTime = 0;
    source.state = State::Idle;
    source.animTime = 0;
    if (!returnToPost || source.unit->speed <= 0 ||
        source.unit->type == dat::UT_Building)
        return;
    const float dx = source.homeX - source.x;
    const float dy = source.homeY - source.y;
    if (dx * dx + dy * dy > 0.04f) {
        source.moveGoalActive = true;
        source.moveRetryTime = 0;
        source.moveAnchorX = source.homeX;
        source.moveAnchorY = source.homeY;
        source.moveBestDistance = std::sqrt(dx * dx + dy * dy);
        issueMove(source, source.homeX, source.homeY);
    }
}

void Game::retryAttackApproach(Object &source) {
    source.attackSlotRetries++;
    const float handedness = (source.spawnId & 1u) ? 1.0f : -1.0f;
    source.attackApproachAngle +=
        handedness * 2.39996323f;
    if (const Object *target =
            findObject(source.attackTargetId)) {
        const float contact =
            collisionRadius(source) +
            collisionRadius(*target) + 0.08f;
        const float range = attackRange(source, *target);
        source.attackApproachDistance =
            std::max(contact, range * 0.8f);
        if (source.unit->minRange > 0)
            source.attackApproachDistance =
                std::max(source.attackApproachDistance,
                         source.unit->minRange + 0.2f);
        source.targetX =
            target->x +
            std::cos(source.attackApproachAngle) *
                source.attackApproachDistance;
        source.targetY =
            target->y +
            std::sin(source.attackApproachAngle) *
                source.attackApproachDistance;
    }
    source.path.clear();
    source.pathIndex = 0;
    source.state = State::Idle;
    source.blockedTime = 0;
    source.attackRepathTime = 0;
    source.attackStallTime = 0;
    source.attackBestDistance = std::numeric_limits<float>::max();
    attackApproachRetries_++;
}

void Game::acquireAutomaticTarget(Object &source) {
    if (!canAttack(source) || source.attackTargetId ||
        source.attackMode == AttackMode::Passive || source.state != State::Idle ||
        source.moveGoalActive ||
        mobileObjectGridWidth_ <= 0 ||
        combatObjectCells_.size() !=
            (size_t)mobileObjectGridWidth_ * mobileObjectGridWidth_)
        return;

    constexpr float cellSize = 4.0f;
    const float radius = automaticAcquisitionRadius(source);
    if (radius <= 0) return;
    const int centerX = (int)std::floor(source.x / cellSize);
    const int centerY = (int)std::floor(source.y / cellSize);
    const int cellRadius = std::max(1, (int)std::ceil(radius / cellSize));
    const int minX = std::max(0, centerX - cellRadius);
    const int maxX = std::min(mobileObjectGridWidth_ - 1, centerX + cellRadius);
    const int minY = std::max(0, centerY - cellRadius);
    const int maxY = std::min(mobileObjectGridWidth_ - 1, centerY + cellRadius);
    Object *best = nullptr;
    float bestDistanceSquared = radius * radius;
    for (int cellY = minY; cellY <= maxY; cellY++)
        for (int cellX = minX; cellX <= maxX; cellX++)
            for (uint32_t index :
                 combatObjectCells_[
                     (size_t)cellY * mobileObjectGridWidth_ + cellX]) {
                Object &candidate = objects_[(size_t)index];
                // Gaia wildlife/resources may be manually hunted, but defensive
                // units should not start clearing neutral fauna on sight.
                if (candidate.player <= 0 || !isEnemy(source, candidate))
                    continue;
                const float dx = candidate.x - source.x;
                const float dy = candidate.y - source.y;
                const float distanceSquared = dx * dx + dy * dy;
                if (distanceSquared > bestDistanceSquared) continue;
                best = &candidate;
                bestDistanceSquared = distanceSquared;
            }
    if (!best) return;
    const float jitter =
        ((float)((source.spawnId * 2654435761u) >> 29) - 3.5f) * 0.10f;
    const float approachAngle =
        std::atan2(source.y - best->y, source.x - best->x) + jitter;
    issueAttack(source, *best, approachAngle, true);
    automaticTargetsAcquired_++;
    if (source.unit->type == dat::UT_Building) armedBuildingsEngaged_++;
}

void Game::killObject(Object &object, bool countKill) {
    const bool wasStatic = object.unit->speed <= 0 || object.unit->type == dat::UT_Building;
    object.active = false;
    object.selected = false;
    object.state = State::Idle;
    object.path.clear();
    object.attackTargetId = 0;

    Remains remains;
    remains.dyingGraphic = object.felled ? -1 : object.unit->dyingGraphic;
    remains.deadUnit =
        findUnit(civilizationForPlayer(object.player), object.unit->deadUnitId);
    remains.player = object.player;
    remains.x = object.x;
    remains.y = object.y;
    remains.facing = object.facing;
    remains.drawShadows = object.drawShadows;
    if (const dat::Graphic *graphic = assets_.dat().graphic(remains.dyingGraphic))
        remains.dyingDuration =
            std::max(0.1f, graphic->frameCount * graphic->frameDuration);
    if (remains.deadUnit && remains.deadUnit->standingGraphic[0] >= 0) {
        const dat::Graphic *graphic =
            assets_.dat().graphic(remains.deadUnit->standingGraphic[0]);
        remains.remainsDuration =
            graphic && (graphic->sequenceType & kSequenceAnimated) &&
                    graphic->frameCount > 1 && graphic->frameDuration > 0
                ? graphic->frameCount * graphic->frameDuration
                : 60.0f;
    }
    if (remains.dyingGraphic >= 0 || remains.deadUnit) remains_.push_back(remains);

    int soundId = object.unit->dyingSound;
    if (soundId < 0) soundId = graphicSound(object.unit->dyingGraphic);
    playWorldUnitSound(object, soundId);
    if (countKill) unitsKilled_++;
    if (wasStatic) rebuildAdjacency();
}

void Game::damageObject(Object &object, int damage, uint32_t attackerId) {
    if (!object.active || damage <= 0) return;
    const float previousDamage =
        100.0f * (1.0f - object.hitPoints / std::max(1.0f, object.maxHitPoints));
    float hitPointDamage = (float)damage;
    if (object.shieldPoints > 0) {
        const float absorbed =
            std::min(object.shieldPoints,
                     hitPointDamage);
        object.shieldPoints -= absorbed;
        hitPointDamage -= absorbed;
        if (object.unit->type != dat::UT_Building)
            hitPointDamage += 1.0f;
    }
    object.hitPoints = std::max(
        0.0f, object.hitPoints - hitPointDamage);
    object.flashTime = std::max(object.flashTime, 0.15f);
    attacksLanded_++;
    if (object.hitPoints <= 0) {
        killObject(object);
        return;
    }

    Object *attacker = findObject(attackerId);
    if (attacker && attacker->active && canAttack(object) &&
        isEnemy(object, *attacker) && object.attackMode != AttackMode::Passive &&
        (!object.attackTargetId || object.attackAutomatic)) {
        const float dx = attacker->x - object.x;
        const float dy = attacker->y - object.y;
        const float distanceSquared = dx * dx + dy * dy;
        const float leash = automaticPursuitLeash(object);
        const float homeDx = attacker->x - object.homeX;
        const float homeDy = attacker->y - object.homeY;
        const bool canReach =
            object.attackMode == AttackMode::StandGround ||
                    object.unit->type == dat::UT_Building
                ? distanceSquared <=
                      automaticAcquisitionRadius(object) *
                          automaticAcquisitionRadius(object)
                : homeDx * homeDx + homeDy * homeDy <= leash * leash;
        if (canReach && object.attackTargetId != attackerId) {
            issueAttack(object, *attacker,
                        std::atan2(object.y - attacker->y,
                                   object.x - attacker->x),
                        true);
            retaliationOrders_++;
            if (object.unit->type == dat::UT_Building)
                armedBuildingsEngaged_++;
        }
    }

    int soundId = object.unit->damageSound;
    const float currentDamage =
        100.0f * (1.0f - object.hitPoints / std::max(1.0f, object.maxHitPoints));
    for (const dat::DamageGraphic &damageGraphic : object.unit->damageGraphics)
        if (previousDamage < damageGraphic.damagePercent &&
            currentDamage >= damageGraphic.damagePercent)
            soundId = graphicSound(damageGraphic.graphicId);
    playWorldUnitSound(object, soundId);
}

void Game::launchProjectile(const Object &source, const Object &target, int damage) {
    const dat::Unit *projectileUnit =
        findUnit(civilizationForPlayer(source.player), source.unit->projectileUnitId);
    if (!projectileUnit || projectileUnit->standingGraphic[0] < 0) {
        const int soundId = graphicSound(source.unit->attackGraphic);
        playWorldUnitSound(source, soundId);
        if (Object *liveTarget = findObject(target.spawnId))
            damageObject(*liveTarget, damage, source.spawnId);
        return;
    }

    const float forward = std::max(0.1f, source.unit->graphicDisplacement[1]);
    const float side = source.unit->graphicDisplacement[0];
    Projectile projectile;
    projectile.unit = projectileUnit;
    projectile.player = source.player;
    projectile.x = source.x + std::cos(source.facing) * forward -
                   std::sin(source.facing) * side;
    projectile.y = source.y + std::sin(source.facing) * forward +
                   std::cos(source.facing) * side;
    projectile.z = std::max(0.0f, source.unit->graphicDisplacement[2]);
    projectile.targetZ =
        std::max(0.2f, std::min(1.5f, target.unit->outlineSize[2] * 0.5f));
    projectile.facing = source.facing;
    projectile.targetId = target.spawnId;
    projectile.sourceId = source.spawnId;
    projectile.damage = damage;
    projectiles_.push_back(projectile);
    projectilesLaunched_++;

    int soundId = graphicSound(projectileUnit->standingGraphic[0]);
    if (soundId < 0) soundId = graphicSound(source.unit->attackGraphic);
    playWorldUnitSound(source, soundId);
}

void Game::updateProjectiles(float dt) {
    size_t index = 0;
    while (index < projectiles_.size()) {
        Projectile &projectile = projectiles_[index];
        Object *target = findObject(projectile.targetId);
        if (!target || !target->active) {
            projectiles_[index] = projectiles_.back();
            projectiles_.pop_back();
            continue;
        }

        const float dx = target->x - projectile.x;
        const float dy = target->y - projectile.y;
        const float dz = projectile.targetZ - projectile.z;
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float step = std::max(0.1f, projectile.unit->speed) * dt;
        projectile.animTime += dt;
        if (distance <= step || distance <= 0.0001f) {
            const int damage = projectile.damage;
            projectiles_[index] = projectiles_.back();
            projectiles_.pop_back();
            damageObject(*target, damage, projectile.sourceId);
            continue;
        }
        projectile.x += dx * step / distance;
        projectile.y += dy * step / distance;
        projectile.z += dz * step / distance;
        projectile.facing = std::atan2(dy, dx);
        index++;
    }
}

void Game::updateRemains(float dt) {
    for (Remains &remains : remains_) remains.age += dt;
    remains_.erase(
        std::remove_if(remains_.begin(), remains_.end(),
                       [](const Remains &remains) {
                           return remains.age >=
                                  remains.dyingDuration + remains.remainsDuration;
                       }),
        remains_.end());
}

void Game::updateAttack(Object &source, float dt) {
    source.attackCooldown = std::max(0.0f, source.attackCooldown - dt);
    source.attackRepathTime -= dt;
    source.autoAcquireTime -= dt;
    if (!source.attackTargetId) {
        if (source.autoAcquireTime <= 0) {
            source.autoAcquireTime =
                0.45f + (source.spawnId % 5) * 0.04f;
            acquireAutomaticTarget(source);
        }
        if (!source.attackTargetId) return;
    }

    Object *target = findObject(source.attackTargetId);
    if (!target || !isEnemy(source, *target) || !canAttack(source)) {
        const bool returnToPost =
            source.attackAutomatic &&
            source.attackMode == AttackMode::Defensive;
        finishAttack(source, returnToPost);
        return;
    }

    float dx = target->x - source.x, dy = target->y - source.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance > 0.0001f) source.facing = std::atan2(dy, dx);
    const float range = attackRange(source, *target);
    // Original range test: gap between the attacker's circle and the
    // target's footprint rectangle, compared with the unit's max range.
    const float edgeGap = std::max(
        0.0f,
        std::sqrt(std::pow(std::max(0.0f, std::abs(target->x - source.x) -
                                              std::max(0.1f, target->unit->collisionSize[0])), 2.0f) +
                  std::pow(std::max(0.0f, std::abs(target->y - source.y) -
                                              std::max(0.1f, target->unit->collisionSize[1])), 2.0f)) -
            collisionRadius(source));
    if (source.attackAutomatic) {
        const float leash = automaticPursuitLeash(source);
        const float targetHomeDx = target->x - source.homeX;
        const float targetHomeDy = target->y - source.homeY;
        if ((leash <= 0 && distance > range + 0.05f) ||
            (leash < std::numeric_limits<float>::max() &&
             targetHomeDx * targetHomeDx + targetHomeDy * targetHomeDy >
                 leash * leash)) {
            finishAttack(
                source,
                source.attackMode == AttackMode::Defensive);
            return;
        }
    }
    if (edgeGap <= source.unit->maxRange + 0.1f &&
        edgeGap + 0.05f >= source.unit->minRange) {
        source.path.clear();
        source.pathIndex = 0;
        source.blockedTime = 0;
        source.attackStallTime = 0;
        source.attackBestDistance = distance;
        source.state = State::Attack;
        if (source.attackCooldown <= 0) {
            const int damage = attackDamage(source, *target);
            if (source.unit->projectileUnitId >= 0)
                launchProjectile(source, *target, damage);
            else {
                const int soundId = graphicSound(source.unit->attackGraphic);
                playWorldUnitSound(source, soundId);
                damageObject(*target, damage, source.spawnId);
            }
            source.attackCooldown = std::max(
                0.1f, modifiedUnitAttribute(
                          source, 10,
                          source.unit->reloadTime));
            source.animTime = 0;
            if (!target->active) {
                const bool returnToPost =
                    source.attackAutomatic &&
                    source.attackMode == AttackMode::Defensive;
                finishAttack(source, returnToPost);
            }
        }
        return;
    }

    const float contact = collisionRadius(source) + collisionRadius(*target) + 0.08f;
    float desiredDistance =
        source.attackApproachDistance > 0
            ? source.attackApproachDistance
            : std::max(contact, range * 0.8f);
    if (source.attackApproachDistance <= 0 &&
        source.unit->minRange > 0)
        desiredDistance =
            std::max(desiredDistance, source.unit->minRange + 0.2f);
    const float awayX = std::cos(source.attackApproachAngle);
    const float awayY = std::sin(source.attackApproachAngle);
    const float destinationX = target->x + awayX * desiredDistance;
    const float destinationY = target->y + awayY * desiredDistance;
    const float destinationDx = destinationX - source.x;
    const float destinationDy = destinationY - source.y;
    const float destinationDistance =
        std::sqrt(destinationDx * destinationDx + destinationDy * destinationDy);
    const float destinationShiftX = destinationX - source.targetX;
    const float destinationShiftY = destinationY - source.targetY;
    if (destinationShiftX * destinationShiftX +
            destinationShiftY * destinationShiftY >
        0.25f) {
        source.attackStallTime = 0;
        source.attackBestDistance = destinationDistance;
    } else if (destinationDistance + 0.05f < source.attackBestDistance) {
        source.attackStallTime = 0;
        source.attackBestDistance = destinationDistance;
    } else {
        source.attackStallTime += dt;
    }
    if (source.attackStallTime >= 1.25f) {
        retryAttackApproach(source);
        return;
    }
    if (source.attackRepathTime > 0) return;
    if (source.state == State::Walk && source.pathIndex < source.path.size()) {
        const float destinationDx = destinationX - source.targetX;
        const float destinationDy = destinationY - source.targetY;
        if (destinationDx * destinationDx + destinationDy * destinationDy <= 0.25f)
            return;
    }
    attackPathsComputed_++;
    source.attackRepathTime =
        issueMove(source, destinationX, destinationY, target,
                  std::max(0.05f, source.unit->maxRange - 0.05f))
            ? 0.5f
            : 1.5f;
}

bool Game::terrainPassable(const Object &object, float x, float y) const {
    if (x < 0 || y < 0 || x >= mapSize_ || y >= mapSize_) return false;
    if (isAirUnit(object)) return true;
    const int restriction = object.unit->terrainRestriction;
    const auto &restrictions = assets_.dat().terrainRestrictions;
    if (restriction < 0 || (size_t)restriction >= restrictions.size()) return false;
    const auto &multipliers =
        restrictions[(size_t)restriction].passableBuildableDmgMultiplier;
    const int terrain = terrainAt((int)std::floor(x), (int)std::floor(y));
    return terrain >= 0 && (size_t)terrain < multipliers.size() &&
           multipliers[(size_t)terrain] > 0.0f;
}

// First mobile unit the moving object would overlap at (x, y), or null.
// Units only block when the move brings them closer together, so units that
// already overlap can always separate.
const Game::Object *Game::unitBlockerAt(const Object &object, float x,
                                        float y) const {
    if (mobileObjectGridWidth_ <= 0 ||
        mobileObjectCells_.size() !=
            (size_t)mobileObjectGridWidth_ * mobileObjectGridWidth_)
        return nullptr;
    constexpr float cellSize = 4.0f;
    const float radius = collisionRadius(object);
    const bool air = isAirUnit(object);
    const int cellRadius = std::max(
        1, (int)std::ceil((radius + maxMobileCollisionRadius_ + 0.04f) / cellSize));
    const int centerX = (int)std::floor(x / cellSize);
    const int centerY = (int)std::floor(y / cellSize);
    for (int cy = std::max(0, centerY - cellRadius);
         cy <= std::min(mobileObjectGridWidth_ - 1, centerY + cellRadius); cy++)
        for (int cx = std::max(0, centerX - cellRadius);
             cx <= std::min(mobileObjectGridWidth_ - 1, centerX + cellRadius); cx++)
            for (uint32_t index :
                 mobileObjectCells_[(size_t)cy * mobileObjectGridWidth_ + cx]) {
                const Object &other = objects_[(size_t)index];
                if (&other == &object || !other.active || other.hidden ||
                    isAirUnit(other) != air)
                    continue;
                const float dx = x - other.x, dy = y - other.y;
                const float separation = radius + collisionRadius(other) + 0.05f;
                const float oldDx = object.x - other.x, oldDy = object.y - other.y;
                if (dx * dx + dy * dy < separation * separation &&
                    dx * dx + dy * dy <= oldDx * oldDx + oldDy * oldDy)
                    return &other;
            }
    return nullptr;
}

// Short-range detour: a fine (quarter-tile) search in a window around the
// unit that treats nearby units as obstacles, used when the long-range path
// is blocked by other units. Rejoins the path at the first waypoint outside
// the window (or the goal when it is inside).
bool Game::detourAround(Object &object, float goalX, float goalY) {
    constexpr float kStep = 0.25f;
    constexpr int kHalf = 16; // 4 tiles each way
    constexpr int kSize = kHalf * 2 + 1;
    const float originX = object.x - kHalf * kStep;
    const float originY = object.y - kHalf * kStep;
    auto cellPos = [&](int cx, int cy, float &x, float &y) {
        x = originX + cx * kStep;
        y = originY + cy * kStep;
    };
    // Target cell: goal clamped into the window.
    float tx = std::max(originX, std::min(originX + (kSize - 1) * kStep, goalX));
    float ty = std::max(originY, std::min(originY + (kSize - 1) * kStep, goalY));
    const int goalCX = (int)std::lround((tx - originX) / kStep);
    const int goalCY = (int)std::lround((ty - originY) / kStep);
    std::vector<uint8_t> open(kSize * kSize, 0);
    const float radius = collisionRadius(object);
    for (int cy = 0; cy < kSize; cy++)
        for (int cx = 0; cx < kSize; cx++) {
            float x, y;
            cellPos(cx, cy, x, y);
            bool ok = staticPassableAt(object, x, y);
            if (ok)
                for (uint32_t index : mobileObjectIndices_) {
                    const Object &other = objects_[(size_t)index];
                    if (&other == &object || !other.active || other.hidden ||
                        isAirUnit(other) != isAirUnit(object))
                        continue;
                    const float dx = x - other.x, dy = y - other.y;
                    const float sep = radius + collisionRadius(other) + 0.05f;
                    if (dx * dx + dy * dy < sep * sep) {
                        ok = false;
                        break;
                    }
                }
            open[cy * kSize + cx] = ok;
        }
    const int start = kHalf * kSize + kHalf;
    open[start] = 1;
    std::vector<float> cost(kSize * kSize, 1e9f);
    std::vector<int> parent(kSize * kSize, -1);
    struct Node {
        float f;
        int i;
        bool operator<(const Node &o) const { return f > o.f; }
    };
    std::priority_queue<Node> queue;
    auto h = [&](int i) {
        const float dx = (float)(i % kSize - goalCX), dy = (float)(i / kSize - goalCY);
        return std::sqrt(dx * dx + dy * dy);
    };
    cost[start] = 0;
    queue.push({h(start), start});
    int best = start;
    float bestH = h(start);
    static const int dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1},
                                   {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
    while (!queue.empty()) {
        const Node n = queue.top();
        queue.pop();
        if (n.f > cost[n.i] + h(n.i) + 1e-3f) continue;
        const float hh = h(n.i);
        if (hh < bestH) {
            bestH = hh;
            best = n.i;
        }
        if (hh < 0.5f) break;
        const int cx = n.i % kSize, cy = n.i / kSize;
        for (const auto &d : dirs) {
            const int nx = cx + d[0], ny = cy + d[1];
            if (nx < 0 || ny < 0 || nx >= kSize || ny >= kSize) continue;
            const int ni = ny * kSize + nx;
            if (!open[ni]) continue;
            if (d[0] && d[1] && (!open[cy * kSize + nx] || !open[ny * kSize + cx]))
                continue;
            const float nc = cost[n.i] + ((d[0] && d[1]) ? 1.41421356f : 1.0f);
            if (nc >= cost[ni]) continue;
            cost[ni] = nc;
            parent[ni] = n.i;
            queue.push({nc + h(ni), ni});
        }
    }
    // Only take the detour if it makes real progress toward the goal.
    if (best == start || bestH > h(start) - 2.0f) return false;
    std::vector<std::array<float, 2>> detour;
    for (int i = best; i != start && i >= 0; i = parent[i]) {
        float x, y;
        cellPos(i % kSize, i / kSize, x, y);
        detour.push_back({x, y});
    }
    std::reverse(detour.begin(), detour.end());
    // Thin the detour to corner points.
    std::vector<std::array<float, 2>> thinned;
    float fromX = object.x, fromY = object.y;
    for (size_t i = 0; i < detour.size();) {
        size_t far = i;
        for (size_t j = detour.size(); j-- > i + 1;)
            if (segmentClear(object, fromX, fromY, detour[j][0], detour[j][1])) {
                far = j;
                break;
            }
        thinned.push_back(detour[far]);
        fromX = detour[far][0];
        fromY = detour[far][1];
        i = far + 1;
    }
    std::vector<std::array<float, 2>> rest(
        object.path.begin() + std::min(object.path.size(), object.pathIndex),
        object.path.end());
    // Skip remaining waypoints the detour end can see past, and refuse a
    // detour that would leave the unit without a clear line back to the
    // route (it would try to walk through whatever the route went around).
    const std::array<float, 2> detourEnd =
        thinned.empty() ? std::array<float, 2>{object.x, object.y} : thinned.back();
    while (rest.size() > 1 &&
           segmentClear(object, detourEnd[0], detourEnd[1], rest[1][0], rest[1][1]))
        rest.erase(rest.begin());
    if (!rest.empty() &&
        !segmentClear(object, detourEnd[0], detourEnd[1], rest.front()[0],
                      rest.front()[1]))
        return false;
    object.path = thinned;
    object.path.insert(object.path.end(), rest.begin(), rest.end());
    object.pathIndex = 0;
    return true;
}

// A closed gate only stops units it would not open for: enemies, or
// everyone while it is locked. Open gates have no obstruction at all.
bool Game::gateBlocks(const Object &gate, const Object &mover) const {
    if (!gate.gate) return true;
    if (gate.unit && gate.unit->obstructionType == 0) return false;
    if (gate.locked) return true;
    return mover.player != gate.player && isEnemy(gate, mover);
}

bool Game::positionPassable(const Object &object, float x, float y, bool dynamic) const {
    const float radius = collisionRadius(object);
    if (x < radius || y < radius || x >= mapSize_ - radius || y >= mapSize_ - radius ||
        !terrainPassable(object, x, y))
        return false;

    const bool air = isAirUnit(object);
    if (dynamic && mobileObjectGridWidth_ > 0 &&
        mobileObjectCells_.size() ==
            (size_t)mobileObjectGridWidth_ * mobileObjectGridWidth_) {
        constexpr float cellSize = 4.0f;
        const int cellRadius = std::max(
            1, (int)std::ceil((radius + maxMobileCollisionRadius_ + 0.04f) / cellSize));
        const int centerX = (int)std::floor(x / cellSize);
        const int centerY = (int)std::floor(y / cellSize);
        const int minMobileX = std::max(0, centerX - cellRadius);
        const int maxMobileX =
            std::min(mobileObjectGridWidth_ - 1, centerX + cellRadius);
        const int minMobileY = std::max(0, centerY - cellRadius);
        const int maxMobileY =
            std::min(mobileObjectGridWidth_ - 1, centerY + cellRadius);
        for (int cellY = minMobileY; cellY <= maxMobileY; cellY++)
            for (int cellX = minMobileX; cellX <= maxMobileX; cellX++)
                for (uint32_t index :
                     mobileObjectCells_[
                         (size_t)cellY * mobileObjectGridWidth_ + cellX]) {
                    const Object &other = objects_[(size_t)index];
                    if (&other == &object || !other.active || other.hidden ||
                        isAirUnit(other) != air)
                        continue;
                    if (object.moveGoalActive &&
                        object.moveGroupId != 0 &&
                        object.moveGroupId == other.moveGroupId)
                        continue;
                    if (object.attackTargetId != 0 &&
                        object.attackTargetId ==
                            other.attackTargetId &&
                        object.attackApproachDistance > 0 &&
                        other.attackApproachDistance > 0) {
                        const float slotDx =
                            object.targetX - other.targetX;
                        const float slotDy =
                            object.targetY - other.targetY;
                        const float slotSeparation =
                            radius + collisionRadius(other) +
                            0.04f;
                        if (slotDx * slotDx + slotDy * slotDy >=
                            slotSeparation * slotSeparation)
                            continue;
                    }
                    const float dx = x - other.x, dy = y - other.y;
                    const float separation =
                        radius + collisionRadius(other) + 0.04f;
                    const float oldDx = object.x - other.x;
                    const float oldDy = object.y - other.y;
                    if (dx * dx + dy * dy < separation * separation &&
                        dx * dx + dy * dy <= oldDx * oldDx + oldDy * oldDy)
                        return false;
                }
    }
    if (air) return true;
    const int minX = std::max(0, (int)std::floor(x - radius));
    const int maxX = std::min(mapSize_ - 1, (int)std::floor(x + radius));
    const int minY = std::max(0, (int)std::floor(y - radius));
    const int maxY = std::min(mapSize_ - 1, (int)std::floor(y + radius));
    for (int cellY = minY; cellY <= maxY; cellY++)
        for (int cellX = minX; cellX <= maxX; cellX++)
            for (uint32_t index :
                 staticObstructionCells_[(size_t)cellY * mapSize_ + cellX]) {
                const Object &other = objects_[(size_t)index];
                if (!other.active || other.hidden) continue;
                if (other.gate && !gateBlocks(other, object)) continue;
                const dat::Unit *obstructionUnit = other.unit;
                const float halfX = std::max(
                    0.05f,
                    obstructionUnit->collisionSize[0]);
                const float halfY = std::max(
                    0.05f,
                    obstructionUnit->collisionSize[1]);
                const float extentX = halfX + radius - 0.01f,
                            extentY = halfY + radius - 0.01f;
                const float newDx = std::abs(x - other.x), newDy = std::abs(y - other.y);
                if (newDx >= extentX || newDy >= extentY) continue;
                const float oldDx = std::abs(object.x - other.x);
                const float oldDy = std::abs(object.y - other.y);
                const bool wasInside = oldDx < extentX && oldDy < extentY;
                const float newPenetration = std::min(extentX - newDx, extentY - newDy);
                const float oldPenetration = std::min(extentX - oldDx, extentY - oldDy);
                if (!wasInside || newPenetration >= oldPenetration) return false;
            }
    return true;
}

// True if the unit's circle can travel the straight segment without touching
// a static obstruction or impassable terrain (sampled every 0.2 tiles).
bool Game::segmentClear(const Object &object, float ax, float ay, float bx,
                        float by) const {
    const float dx = bx - ax, dy = by - ay;
    const float length = std::sqrt(dx * dx + dy * dy);
    const int samples = std::max(1, (int)std::ceil(length / 0.2f));
    const float radius = collisionRadius(object);
    const bool air = isAirUnit(object);
    // Exact swept test against footprints (strict interior, like
    // positionPassable), so paths cannot clip a corner between samples.
    // Footprints that already contain the start are ignored: the unit is
    // allowed to back out of an overlap.
    auto hitsBox = [&](float px, float py, float qx, float qy,
                       const Object &other) {
        const float ex = std::max(0.05f, other.unit->collisionSize[0]) + radius - 0.01f;
        const float ey = std::max(0.05f, other.unit->collisionSize[1]) + radius - 0.01f;
        if (std::abs(ax - other.x) < ex && std::abs(ay - other.y) < ey) return false;
        float t0 = 0.0f, t1 = 1.0f;
        const float d[2] = {qx - px, qy - py};
        const float p[2] = {px - other.x, py - other.y};
        const float e[2] = {ex, ey};
        for (int axis = 0; axis < 2; axis++) {
            if (std::abs(d[axis]) < 1e-6f) {
                if (std::abs(p[axis]) >= e[axis]) return false;
                continue;
            }
            float ta = (-e[axis] - p[axis]) / d[axis];
            float tb = (e[axis] - p[axis]) / d[axis];
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            if (t0 >= t1) return false;
        }
        return true;
    };
    float prevX = ax, prevY = ay;
    for (int sample = 1; sample <= samples; sample++) {
        const float t = (float)sample / samples;
        const float x = ax + dx * t, y = ay + dy * t;
        if (x < radius || y < radius || x >= mapSize_ - radius ||
            y >= mapSize_ - radius || !terrainPassable(object, x, y))
            return false;
        if (!air) {
            const int minX = std::max(0, (int)std::floor(std::min(prevX, x) - radius));
            const int maxX = std::min(mapSize_ - 1, (int)std::floor(std::max(prevX, x) + radius));
            const int minY = std::max(0, (int)std::floor(std::min(prevY, y) - radius));
            const int maxY = std::min(mapSize_ - 1, (int)std::floor(std::max(prevY, y) + radius));
            for (int cellY = minY; cellY <= maxY; cellY++)
                for (int cellX = minX; cellX <= maxX; cellX++)
                    for (uint32_t index :
                         staticObstructionCells_[(size_t)cellY * mapSize_ + cellX]) {
                        const Object &other = objects_[(size_t)index];
                        if (!other.active || other.hidden || &other == &object) continue;
                        if (other.gate && !gateBlocks(other, object)) continue;
                        if (hitsBox(prevX, prevY, x, y, other)) return false;
                    }
        }
        prevX = x;
        prevY = y;
    }
    return true;
}

// Static-only passability for an arbitrary point: terrain, map edge and
// building/tree/wall footprints (gates per gateBlocks), ignoring units.
bool Game::staticPassableAt(const Object &object, float x, float y) const {
    const float radius = collisionRadius(object);
    if (x < radius || y < radius || x >= mapSize_ - radius ||
        y >= mapSize_ - radius || !terrainPassable(object, x, y))
        return false;
    if (isAirUnit(object)) return true;
    const int minX = std::max(0, (int)std::floor(x - radius));
    const int maxX = std::min(mapSize_ - 1, (int)std::floor(x + radius));
    const int minY = std::max(0, (int)std::floor(y - radius));
    const int maxY = std::min(mapSize_ - 1, (int)std::floor(y + radius));
    for (int cellY = minY; cellY <= maxY; cellY++)
        for (int cellX = minX; cellX <= maxX; cellX++)
            for (uint32_t index :
                 staticObstructionCells_[(size_t)cellY * mapSize_ + cellX]) {
                const Object &other = objects_[(size_t)index];
                if (!other.active || other.hidden || &other == &object) continue;
                if (other.gate && !gateBlocks(other, object)) continue;
                const float halfX = std::max(0.05f, other.unit->collisionSize[0]);
                const float halfY = std::max(0.05f, other.unit->collisionSize[1]);
                if (std::abs(x - other.x) < halfX + radius - 0.01f &&
                    std::abs(y - other.y) < halfY + radius - 0.01f)
                    return false;
            }
    return true;
}

// Long-range path (see pathfinding.h for how it mirrors the original).
// goalObject/clearance turn the destination into a goal *region* around an
// object, which is how the original approaches gather/build/attack targets.
bool Game::findPath(const Object &object, float targetX, float targetY,
                    std::vector<std::array<float, 2>> &path,
                    const Object *goalObject, float clearance) const {
    path.clear();
    if (mapSize_ <= 0) return false;
    const bool air = isAirUnit(object);
    const int restriction = object.unit->terrainRestriction;
    const int radiusHundredths =
        (int)std::ceil(collisionRadius(object) * 100.0f);
    auto cache = std::find_if(
        pathGridCache_.begin(), pathGridCache_.end(),
        [&](const PathGridCache &entry) {
            return entry.terrainRestriction == restriction &&
                   entry.air == air && entry.player == object.player &&
                   entry.radiusHundredths == radiusHundredths;
        });
    if (cache == pathGridCache_.end()) {
        PathGridCache entry;
        entry.terrainRestriction = restriction;
        entry.air = air;
        entry.player = object.player;
        entry.radiusHundredths = radiusHundredths;
        // A tile is blocked when its centre lies inside a static footprint
        // that blocks this player (buildings, trees, walls, closed gates).
        std::vector<uint8_t> blocked((size_t)mapSize_ * mapSize_, 0);
        if (!air)
            for (uint32_t index : staticObstructionIndices_) {
                const Object &other = objects_[(size_t)index];
                if (!other.active || other.hidden) continue;
                if (other.gate && !gateBlocks(other, object)) continue;
                const float halfX = std::max(0.05f, other.unit->collisionSize[0]);
                const float halfY = std::max(0.05f, other.unit->collisionSize[1]);
                const int minX = std::max(0, (int)std::floor(other.x - halfX));
                const int maxX = std::min(mapSize_ - 1, (int)std::floor(other.x + halfX));
                const int minY = std::max(0, (int)std::floor(other.y - halfY));
                const int maxY = std::min(mapSize_ - 1, (int)std::floor(other.y + halfY));
                for (int y = minY; y <= maxY; y++)
                    for (int x = minX; x <= maxX; x++)
                        if (std::abs(x + 0.5f - other.x) < halfX &&
                            std::abs(y + 0.5f - other.y) < halfY)
                            blocked[(size_t)y * mapSize_ + x] = 1;
            }
        // ...and a unit must also fit at the tile centre with its radius.
        entry.finder.build(
            mapSize_,
            [&](int x, int y) {
                return !blocked[(size_t)y * mapSize_ + x] &&
                       staticPassableAt(object, x + 0.5f, y + 0.5f);
            },
            [&](int x, int y, int dir) {
                return segmentClear(object, x + 0.5f, y + 0.5f,
                                    x + 0.5f + (dir == 0 ? 1.0f : 0.0f),
                                    y + 0.5f + (dir == 1 ? 1.0f : 0.0f));
            });
        pathGridCache_.push_back(std::move(entry));
        cache = pathGridCache_.end() - 1;
    }

    PathGoal goal;
    goal.x = targetX;
    goal.y = targetY;
    if (goalObject && goalObject->unit) {
        goal.x = goalObject->x;
        goal.y = goalObject->y;
        goal.halfX = std::max(0.1f, goalObject->unit->collisionSize[0]);
        goal.halfY = std::max(0.1f, goalObject->unit->collisionSize[1]);
        goal.range = collisionRadius(object) + std::max(0.0f, clearance);
    }
    std::vector<std::array<float, 2>> tiles;
    PathResult result =
        cache->finder.find(object.x, object.y, goal, tiles, 30000,
                           [&](int x, int y) {
                               return segmentClear(object, object.x, object.y,
                                                   x + 0.5f, y + 0.5f);
                           });
    pathSearches_++;
    if (result == PathResult::Failed && !goalObject) return false;
    if (goalObject && result != PathResult::Complete) {
        // The goal region can be empty for footprints that are not tile
        // aligned. Close in on the footprint edge directly when the last
        // stretch is clear, which is what the unit-level movement in the
        // original does once the long-range path ends.
        const std::array<float, 2> from =
            tiles.empty() ? std::array<float, 2>{object.x, object.y} : tiles.back();
        Object probe = object;
        probe.x = from[0];
        probe.y = from[1];
        float edgeX = 0, edgeY = 0;
        interactionPoint(probe, *goalObject, std::max(0.0f, clearance) * 0.6f,
                         edgeX, edgeY);
        if (staticPassableAt(object, edgeX, edgeY) &&
            segmentClear(object, from[0], from[1], edgeX, edgeY)) {
            tiles.push_back({edgeX, edgeY});
            result = PathResult::Complete;
            targetX = edgeX;
            targetY = edgeY;
        } else if (result == PathResult::Failed) {
            return false;
        }
    }

    // Final point: the exact destination when it is reachable from the last
    // tile, otherwise the goal tile centre.
    std::array<float, 2> finalPoint =
        tiles.empty() ? std::array<float, 2>{object.x, object.y} : tiles.back();
    if (result == PathResult::Complete) {
        const float exactX = targetX, exactY = targetY;
        if (staticPassableAt(object, exactX, exactY) &&
            segmentClear(object, finalPoint[0], finalPoint[1], exactX, exactY)) {
            finalPoint = {exactX, exactY};
        } else if (goalObject) {
            // Close the last gap: step from the goal tile straight toward the
            // target's footprint edge (melee units must actually touch it).
            Object probe = object;
            probe.x = finalPoint[0];
            probe.y = finalPoint[1];
            float edgeX = 0, edgeY = 0;
            interactionPoint(probe, *goalObject, std::max(0.0f, clearance) * 0.6f,
                             edgeX, edgeY);
            if (staticPassableAt(object, edgeX, edgeY) &&
                segmentClear(object, finalPoint[0], finalPoint[1], edgeX, edgeY))
                finalPoint = {edgeX, edgeY};
        }
    }
    if (tiles.empty()) tiles.push_back(finalPoint);
    else tiles.back() = finalPoint;
    // The search may leave the start tile via its centre (see
    // TilePathfinder::find); walk there first when the direct step is not clear.
    if (!segmentClear(object, object.x, object.y, tiles.front()[0], tiles.front()[1])) {
        const float centreX = std::floor(object.x) + 0.5f;
        const float centreY = std::floor(object.y) + 0.5f;
        if (segmentClear(object, object.x, object.y, centreX, centreY))
            tiles.insert(tiles.begin(), {centreX, centreY});
    }

    // Line-of-sight smoothing (string pulling), the equivalent of the
    // original's block-edge crossing points.
    float fromX = object.x, fromY = object.y;
    size_t i = 0;
    while (i < tiles.size()) {
        size_t furthest = i;
        for (size_t j = tiles.size(); j-- > i + 1;) {
            if (segmentClear(object, fromX, fromY, tiles[j][0], tiles[j][1])) {
                furthest = j;
                break;
            }
        }
        path.push_back(tiles[furthest]);
        fromX = tiles[furthest][0];
        fromY = tiles[furthest][1];
        i = furthest + 1;
    }
    return !path.empty();
}

// Walk into interaction range of an object. Re-plans at most every half
// second while the unit is not already walking to this object.
bool Game::approach(Object &object, const Object &target, float clearance) {
    if (object.state == State::Walk && object.pathGoalId == target.spawnId &&
        object.pathIndex < object.path.size())
        return true;
    if (object.approachRetry > 0.0f) return false;
    object.approachRetry = 0.5f;
    float x = 0, y = 0;
    // Aim a little inside the interaction range so float rounding at the
    // end of the walk cannot leave the unit just outside it.
    interactionPoint(object, target, clearance * 0.6f, x, y);
    return issueMove(object, x, y, &target, clearance);
}

bool Game::issueMove(Object &object, float targetX, float targetY,
                     const Object *goalObject, float clearance) {
    object.targetX = targetX;
    object.targetY = targetY;
    object.pathIndex = 0;
    object.blockedTime = 0;
    object.detourTime = 0;
    object.pathGoalId = goalObject ? goalObject->spawnId : 0;
    object.pathGoalClearance = clearance;
    const float dx = targetX - object.x, dy = targetY - object.y;
    if (dx * dx + dy * dy < 0.01f ||
        (goalObject && withinInteractionRange(object, *goalObject, clearance))) {
        object.path.clear();
        object.state = State::Idle;
        if (!goalObject) object.moveGoalActive = false;
        return true;
    }
    // A unit standing inside a footprint (spawned or pushed there) first
    // steps straight out along the shallowest axis, then plans from there.
    if (!isAirUnit(object) && !staticPassableAt(object, object.x, object.y)) {
        float bestX = 0, bestY = 0, bestDistance = std::numeric_limits<float>::max();
        const float radius = collisionRadius(object);
        for (uint32_t index : staticObstructionIndices_) {
            const Object &other = objects_[(size_t)index];
            if (!other.active || other.hidden || &other == &object) continue;
            if (other.gate && !gateBlocks(other, object)) continue;
            const float ex = std::max(0.05f, other.unit->collisionSize[0]) + radius;
            const float ey = std::max(0.05f, other.unit->collisionSize[1]) + radius;
            if (std::abs(object.x - other.x) >= ex - 0.01f ||
                std::abs(object.y - other.y) >= ey - 0.01f)
                continue;
            const float candidates[4][2] = {{other.x + ex + 0.02f, object.y},
                                            {other.x - ex - 0.02f, object.y},
                                            {object.x, other.y + ey + 0.02f},
                                            {object.x, other.y - ey - 0.02f}};
            for (const auto &c : candidates) {
                const float d = std::abs(c[0] - object.x) + std::abs(c[1] - object.y);
                if (d < bestDistance && staticPassableAt(object, c[0], c[1])) {
                    bestDistance = d;
                    bestX = c[0];
                    bestY = c[1];
                }
            }
        }
        if (bestDistance < std::numeric_limits<float>::max()) {
            Object probe = object;
            probe.x = bestX;
            probe.y = bestY;
            std::vector<std::array<float, 2>> rest;
            if (findPath(probe, targetX, targetY, rest, goalObject, clearance)) {
                object.path.assign(1, {bestX, bestY});
                object.path.insert(object.path.end(), rest.begin(), rest.end());
                object.state = State::Walk;
                object.wander = false;
                object.animTime = 0;
                return true;
            }
        }
    }
    // Straight line when nothing static is in the way (the original also
    // walks directly when the target is in line of sight), otherwise ask the
    // tile pathfinder for a route to the goal (region).
    if (!goalObject && staticPassableAt(object, targetX, targetY) &&
        segmentClear(object, object.x, object.y, targetX, targetY)) {
        object.path.assign(1, {targetX, targetY});
    } else if (!findPath(object, targetX, targetY, object.path, goalObject,
                         clearance)) {
        object.path.clear();
        object.state = State::Idle;
        return false;
    }
    object.state = State::Walk;
    object.wander = false;
    object.animTime = 0;
    return true;
}

void Game::issueGroupMove(std::vector<Object *> targets, float targetX, float targetY,
                          FormationType formation) {
    struct ReservedDestination {
        float x, y, radius;
    };
    auto movesIndependently = [](const Object &object) {
        const int unitClass = object.unit->cls;
        return unitClass == 14 || unitClass == 45 ||
               unitClass == 58 ||
               object.unit->name.find("WORKER") !=
                   std::string::npos ||
               object.unit->name2.find("WORKER") !=
                   std::string::npos;
    };
    std::vector<Object *> formationTargets;
    std::vector<Object *> independentTargets;
    formationTargets.reserve(targets.size());
    independentTargets.reserve(targets.size());
    float candidateCentroidX = 0;
    float candidateCentroidY = 0;
    size_t candidateCount = 0;
    for (Object *object : targets) {
        if (object->hidden || object->unit->speed <= 0)
            continue;
        if (movesIndependently(*object)) {
            independentTargets.push_back(object);
            continue;
        }
        formationTargets.push_back(object);
        candidateCentroidX += object->x;
        candidateCentroidY += object->y;
        candidateCount++;
    }
    if (candidateCount > 1) {
        candidateCentroidX /= candidateCount;
        candidateCentroidY /= candidateCount;
        std::vector<Object *> cohesive;
        cohesive.reserve(formationTargets.size());
        for (Object *object : formationTargets) {
            const float dx = object->x - candidateCentroidX;
            const float dy = object->y - candidateCentroidY;
            if (dx * dx + dy * dy > 100.0f)
                independentTargets.push_back(object);
            else
                cohesive.push_back(object);
        }
        formationTargets = std::move(cohesive);
    }
    for (Object *object : independentTargets) {
        object->attackTargetId = 0;
        object->attackAutomatic = false;
        object->moveGroupId = 0;
        object->moveSpeedLimit = 0;
        object->homeX = object->moveAnchorX = targetX;
        object->homeY = object->moveAnchorY = targetY;
        object->moveGoalActive = true;
        object->moveRetryTime = 0;
        object->moveStallTime = 0;
        object->moveSpreadRetries = 0;
        const float dx = targetX - object->x;
        const float dy = targetY - object->y;
        object->moveBestDistance = std::sqrt(dx * dx + dy * dy);
        if (!issueMove(*object, targetX, targetY))
            object->moveGoalActive = false;
    }
    if (formationTargets.empty()) return;

    std::vector<ReservedDestination> reserved;
    reserved.reserve(formationTargets.size());
    const uint32_t moveGroupId = nextMoveGroupId_++;
    if (nextMoveGroupId_ == 0) nextMoveGroupId_ = 1;
    float slotSpacing = 0.75f;
    float groupSpeed = std::numeric_limits<float>::max();
    float centroidX = 0, centroidY = 0;
    size_t mobileCount = 0;
    for (const Object *object : formationTargets)
        if (!object->hidden && object->unit->speed > 0) {
            slotSpacing =
                std::max(slotSpacing, collisionRadius(*object) * 2.0f + 0.25f);
            groupSpeed = std::min(
                groupSpeed,
                modifiedUnitAttribute(
                    *object, 5, object->unit->speed));
            centroidX += object->x;
            centroidY += object->y;
            mobileCount++;
        }
    if (mobileCount == 0) return;
    centroidX /= mobileCount;
    centroidY /= mobileCount;
    const float travelX = targetX - centroidX;
    const float travelY = targetY - centroidY;
    const float travelDistance =
        std::sqrt(travelX * travelX + travelY * travelY);
    const float forwardX =
        travelDistance > 0.001f ? travelX / travelDistance : 0;
    const float forwardY =
        travelDistance > 0.001f ? travelY / travelDistance : 1;
    const float rightX = -forwardY;
    const float rightY = forwardX;
    size_t slot = 0;
    std::vector<Object *> remaining = formationTargets;
    std::sort(remaining.begin(), remaining.end(),
              [&](const Object *a, const Object *b) {
                  const float aLateral =
                      (a->x - centroidX) * rightX +
                      (a->y - centroidY) * rightY;
                  const float bLateral =
                      (b->x - centroidX) * rightX +
                      (b->y - centroidY) * rightY;
                  if (std::abs(aLateral - bLateral) > 0.1f)
                      return aLateral < bLateral;
                  const float aForward =
                      (a->x - centroidX) * forwardX +
                      (a->y - centroidY) * forwardY;
                  const float bForward =
                      (b->x - centroidX) * forwardX +
                      (b->y - centroidY) * forwardY;
                  return aForward < bForward;
              });
    // Original formation layout (battlegrounds_x1.exe 0x480060 / 0x478e30 /
    // 0x479760 / 0x47c280). Units are split into four ranks by class
    // (0x478cc0); each rank's spacing is its widest unit's diameter
    // (0x47d270). Line/Staggered/Flank stack the ranks front to back, Box
    // nests them as rings with rank 3 innermost. The clicked point is the
    // front centre of the formation, as in the original.
    std::vector<Object *> members;
    std::vector<Object *> others;
    for (Object *object : remaining)
        (object->hidden || object->unit->speed <= 0 ? others : members).push_back(object);
    auto rankOf = [](const Object *object) {
        switch (object->unit->cls) {
        case 0x0d: case 0x26: case 0x2b: case 0x32: case 0x38: case 0x3f:
            return 1;
        case 0x0f: case 0x27: case 0x2c: case 0x2f: case 0x30: case 0x34:
        case 0x35: case 0x40:
            return 2;
        case 1: case 2: case 0x0b: case 0x0c: case 0x0e: case 0x10: case 0x11:
        case 0x15: case 0x20: case 0x21: case 0x22: case 0x23: case 0x24:
        case 0x25: case 0x28: case 0x2d: case 0x2e: case 0x31: case 0x33:
        case 0x36: case 0x37: case 0x39: case 0x3a: case 0x3b: case 0x3d:
        case 0x3e:
            return 3;
        default:
            // Not a formation class in the original (it moves on its own);
            // keep it with the main body here.
            return 2;
        }
    };
    auto spacingOf = [&](const Object *object) {
        float radius = object->unit->collisionSize[0];
        const int unitClass = object->unit->cls;
        if (unitClass == 0x10 || unitClass == 0x0b || unitClass == 0x3e)
            radius *= 1.75f;
        else if (unitClass == 0x23)
            radius *= 1.25f;
        // The original packs units shoulder to shoulder (2 * radius); keep
        // the small gap our collision needs between neighbours.
        return std::max(radius, collisionRadius(*object)) * 2.0f + 0.1f;
    };
    std::stable_sort(members.begin(), members.end(),
                     [&](const Object *a, const Object *b) {
                         return rankOf(a) < rankOf(b);
                     });
    int rankCount[4] = {0, 0, 0, 0};
    float rankSpacing[4] = {0, 0, 0, 0};
    float groupSpacing = 0.0f;
    for (const Object *object : members) {
        const int rank = rankOf(object);
        rankCount[rank]++;
        rankSpacing[rank] = std::max(rankSpacing[rank], spacingOf(object));
        groupSpacing = std::max(groupSpacing, rankSpacing[rank]);
    }
    std::vector<std::pair<float, float>> formationOffsets; // lateral, forward
    std::vector<int> slotRank;
    if (formation == FormationType::Box) {
        // Rings from the inside out: rank 3, 2, 1, 0 (0x479760).
        float minX = -0.5f * groupSpacing, maxX = 0.5f * groupSpacing;
        float minZ = minX, maxZ = maxX;
        bool first = true;
        std::vector<std::pair<float, float>> ring; // x = forward, z = lateral
        for (int rank = 3; rank >= 0; rank--) {
            const int count = rankCount[rank];
            if (count <= 0) continue;
            ring.clear();
            if (first && count == 1) {
                minX = maxX = minZ = maxZ = 0.0f;
                ring.push_back({0.0f, 0.0f});
            } else {
                if (first) {
                    minX = minZ = -0.5f * groupSpacing;
                    maxX = maxZ = 0.5f * groupSpacing;
                } else {
                    minX -= groupSpacing; minZ -= groupSpacing;
                    maxX += groupSpacing; maxZ += groupSpacing;
                }
                // 0x47c280: grow the square until its perimeter holds the rank.
                float perimeter = (maxX - minX) * 4.0f;
                const float needed = count * groupSpacing;
                if (perimeter < needed) {
                    const float grow = (needed * 0.25f - (maxX - minX)) * 0.5f;
                    minX -= grow; minZ -= grow; maxX += grow; maxZ += grow;
                    perimeter = needed;
                }
                const float step = perimeter / count;
                const float cx = (minX + maxX) * 0.5f, cz = (minZ + maxZ) * 0.5f;
                switch (count) {
                case 1: ring = {{maxX, cz}}; break;
                case 2: ring = {{maxX, cz}, {minX, cz}}; break;
                case 3: ring = {{maxX, minZ}, {maxX, maxZ}, {minX, cz}}; break;
                case 4: ring = {{maxX, minZ}, {maxX, maxZ}, {minX, minZ}, {minX, maxZ}}; break;
                case 5: ring = {{maxX, minZ}, {maxX, cz}, {maxX, maxZ}, {minX, minZ}, {minX, maxZ}}; break;
                case 6: ring = {{maxX, minZ}, {maxX, cz}, {maxX, maxZ}, {minX, minZ}, {minX, cz}, {minX, maxZ}}; break;
                case 7: ring = {{maxX, minZ}, {maxX, cz}, {maxX, maxZ}, {cx, minZ}, {cx, maxZ}, {minX, minZ}, {minX, maxZ}}; break;
                case 8: ring = {{maxX, minZ}, {maxX, cz}, {maxX, maxZ}, {cx, minZ}, {cx, maxZ}, {minX, minZ}, {minX, cz}, {minX, maxZ}}; break;
                default: {
                    // Walk the perimeter: front edge (+z), right edge (-x),
                    // back edge (-z), left edge (+x).
                    const float side = maxX - minX;
                    for (int i = 0; i < count; i++) {
                        float d = std::fmod(i * step, perimeter);
                        float x, z;
                        if (d < side) { x = maxX; z = minZ + d; }
                        else if (d < 2 * side) { x = maxX - (d - side); z = maxZ; }
                        else if (d < 3 * side) { x = minX; z = maxZ - (d - 2 * side); }
                        else { x = minX + (d - 3 * side); z = minZ; }
                        ring.push_back({x, z});
                    }
                }
                }
            }
            for (const auto &point : ring) {
                formationOffsets.push_back({point.second, point.first});
                slotRank.push_back(rank);
            }
            first = false;
        }
        // Shift so the front edge sits on the clicked point.
        float front = 0.0f;
        for (const auto &offset : formationOffsets) front = std::max(front, offset.second);
        for (auto &offset : formationOffsets) offset.second -= front;
    } else {
        // 0x478c10: rows no more than half the row length.
        int rows[4] = {0, 0, 0, 0}, perRow[4] = {0, 0, 0, 0};
        int widest = -1, present = 0;
        float widestWidth = 0.0f;
        for (int rank = 0; rank < 4; rank++) {
            const int n = rankCount[rank];
            if (n <= 0) continue;
            present++;
            int r = std::max(1, (int)std::floor(std::sqrt((float)n)));
            int depth = std::max(1, n / r);
            int width = (n + depth - 1) / depth;
            while (depth > width / 2) {
                depth--;
                if (depth < 1) { depth = 1; width = n; break; }
                width = (n + depth - 1) / depth;
            }
            rows[rank] = depth;
            perRow[rank] = width;
            if (widest == -1 || widestWidth < rankSpacing[rank] * width) {
                widestWidth = rankSpacing[rank] * width;
                widest = rank;
            }
        }
        // Other ranks are reshaped to the widest rank's frontage.
        if (widest != -1 && present > 1)
            for (int rank = 0; rank < 4; rank++) {
                const int n = rankCount[rank];
                if (rank == widest || n <= 0) continue;
                if (rankSpacing[rank] * n <= widestWidth) {
                    rows[rank] = 1;
                    perRow[rank] = n;
                    continue;
                }
                int depth = std::max(1, (int)std::ceil(rankSpacing[rank] * n / widestWidth));
                int width = (n + depth - 1) / depth;
                while (width < width * depth - n && depth > 1) {
                    depth--;
                    width = (n + depth - 1) / depth;
                }
                rows[rank] = depth;
                perRow[rank] = width;
            }
        int totalRows = 0;
        for (int rank = 0; rank < 4; rank++) totalRows += rows[rank];
        const bool staggered = formation == FormationType::Staggered;
        const bool flank = formation == FormationType::Flank;
        float depth = 0.0f, previousStep = 0.0f;
        int globalRow = 0;
        for (int rank = 0; rank < 4; rank++) {
            const int n = rankCount[rank];
            if (n <= 0) continue;
            const float spacing = rankSpacing[rank];
            const float step = staggered ? spacing * 2.0f : spacing;
            if (depth > 1e-7f && previousStep > 1e-7f && previousStep < spacing)
                depth += spacing - previousStep;
            for (int row = 0; row < rows[rank]; row++) {
                int inRow = perRow[rank];
                if (rows[rank] > 1 && row == rows[rank] - 1)
                    inRow = n - (rows[rank] - 1) * perRow[rank];
                float lateral = step * (float)(inRow / 2);
                if (staggered)
                    lateral += (globalRow % 2 == 0) ? -step * 0.25f : step * 0.25f;
                else if (inRow % 2 == 0)
                    lateral -= step * 0.5f;
                std::vector<float> row_laterals;
                for (int k = 0; k < inRow; k++) {
                    float value = lateral;
                    if (flank) {
                        const float gap = 2.5f - (float)globalRow / std::max(1, totalRows);
                        value += value >= 0.0f ? gap : -gap;
                    }
                    row_laterals.push_back(value);
                    lateral -= step;
                }
                std::sort(row_laterals.begin(), row_laterals.end());
                for (float value : row_laterals) {
                    formationOffsets.push_back({value, -depth});
                    slotRank.push_back(rank);
                }
                globalRow++;
                depth += step;
                previousStep = step;
            }
        }
    }
    // Hand out slots so units do not cross each other: within each rank the
    // units furthest forward take the front rows, and each row is filled
    // left to right by the units' current lateral order.
    {
        std::vector<std::pair<float, float>> orderedOffsets;
        std::vector<Object *> orderedMembers;
        for (int rank = 0; rank < 4; rank++) {
            std::vector<size_t> slots;
            for (size_t i = 0; i < formationOffsets.size(); i++)
                if (slotRank[i] == rank) slots.push_back(i);
            std::vector<Object *> units;
            for (Object *object : members)
                if (rankOf(object) == rank) units.push_back(object);
            if (units.empty()) continue;
            std::stable_sort(slots.begin(), slots.end(), [&](size_t a, size_t b) {
                if (std::abs(formationOffsets[a].second - formationOffsets[b].second) > 0.01f)
                    return formationOffsets[a].second > formationOffsets[b].second;
                return formationOffsets[a].first < formationOffsets[b].first;
            });
            auto forwardOf = [&](const Object *o) {
                return (o->x - centroidX) * forwardX + (o->y - centroidY) * forwardY;
            };
            auto lateralOf = [&](const Object *o) {
                return (o->x - centroidX) * rightX + (o->y - centroidY) * rightY;
            };
            std::stable_sort(units.begin(), units.end(), [&](const Object *a, const Object *b) {
                return forwardOf(a) > forwardOf(b);
            });
            size_t next = 0;
            while (next < slots.size() && next < units.size()) {
                size_t end = next + 1;
                while (end < slots.size() &&
                       std::abs(formationOffsets[slots[end]].second -
                                formationOffsets[slots[next]].second) <= 0.01f)
                    end++;
                end = std::min(end, units.size());
                std::sort(units.begin() + next, units.begin() + end,
                          [&](const Object *a, const Object *b) {
                              return lateralOf(a) < lateralOf(b);
                          });
                for (size_t i = next; i < end; i++) {
                    orderedOffsets.push_back(formationOffsets[slots[i]]);
                    orderedMembers.push_back(units[i]);
                }
                next = end;
            }
        }
        formationOffsets = std::move(orderedOffsets);
        members = std::move(orderedMembers);
    }
    remaining = members;
    remaining.insert(remaining.end(), others.begin(), others.end());
    Object *pathAnchor = nullptr;
    float anchorDistance = std::numeric_limits<float>::max();
    for (Object *object : remaining) {
        if (object->hidden || object->unit->speed <= 0) continue;
        const float dx = object->x - centroidX;
        const float dy = object->y - centroidY;
        if (dx * dx + dy * dy < anchorDistance) {
            anchorDistance = dx * dx + dy * dy;
            pathAnchor = object;
        }
    }
    std::vector<std::array<float, 2>> macroPath;
    const bool hasMacroPath =
        pathAnchor &&
        findPath(*pathAnchor, targetX, targetY, macroPath);
    for (Object *object : remaining) {
        if (object->hidden || object->unit->speed <= 0) continue;
        object->attackTargetId = 0;
        object->attackAutomatic = false;
        object->moveGroupId = moveGroupId;
        object->moveSpeedLimit = groupSpeed;
        const float lateral = formationOffsets[slot].first;
        const float longitudinal = formationOffsets[slot].second;
        float slotX =
            targetX + rightX * lateral + forwardX * longitudinal;
        float slotY =
            targetY + rightY * lateral + forwardY * longitudinal;
        const float radius = collisionRadius(*object);
        auto destinationOpen = [&](float x, float y) {
            if (!positionPassable(*object, x, y, false)) return false;
            for (const ReservedDestination &other : reserved) {
                const float dx = x - other.x, dy = y - other.y;
                const float separation = radius + other.radius + 0.08f;
                if (dx * dx + dy * dy < separation * separation) return false;
            }
            for (uint32_t index : mobileObjectIndices_) {
                const Object &other = objects_[(size_t)index];
                if (!other.active || other.hidden || &other == object ||
                    std::find(formationTargets.begin(), formationTargets.end(),
                              &other) != formationTargets.end() ||
                    isAirUnit(*object) != isAirUnit(other))
                    continue;
                const float dx = x - other.x, dy = y - other.y;
                const float separation =
                    radius + collisionRadius(other) + 0.08f;
                if (dx * dx + dy * dy < separation * separation) return false;
            }
            return true;
        };
        if (!destinationOpen(slotX, slotY)) {
            bool found = false;
            constexpr float searchStep = 0.35f;
            for (int ring = 1; ring <= 24 && !found; ring++) {
                const int samples = std::max(8, ring * 8);
                const float startAngle =
                    (object->spawnId % 16) * (2.0f * kPi / 16.0f);
                for (int sample = 0; sample < samples; sample++) {
                    const float angle =
                        startAngle + sample * (2.0f * kPi / samples);
                    const float candidateX =
                        slotX + std::cos(angle) * ring * searchStep;
                    const float candidateY =
                        slotY + std::sin(angle) * ring * searchStep;
                    if (!destinationOpen(candidateX, candidateY)) continue;
                    slotX = candidateX;
                    slotY = candidateY;
                    found = true;
                    break;
                }
            }
            if (!found) {
                object->moveGoalActive = false;
                object->moveGroupId = 0;
                object->state = State::Idle;
                slot++;
                continue;
            }
        }
        reserved.push_back({slotX, slotY, radius});
        object->homeX = object->moveAnchorX = slotX;
        object->homeY = object->moveAnchorY = slotY;
        object->moveGoalActive = true;
        object->moveRetryTime = 0;
        object->moveStallTime = 0;
        object->moveSpreadRetries = 0;
        const float goalDx = slotX - object->x;
        const float goalDy = slotY - object->y;
        object->moveBestDistance =
            std::sqrt(goalDx * goalDx + goalDy * goalDy);
        bool moveIssued = false;
        if (hasMacroPath) {
            object->targetX = slotX;
            object->targetY = slotY;
            object->path.clear();
            object->path.reserve(macroPath.size());
            for (size_t point = 0; point < macroPath.size();
                 point++) {
                const bool finalPoint =
                    point + 1 == macroPath.size();
                float offsetLateral = lateral;
                float offsetLongitudinal = longitudinal;
                if (!finalPoint && travelDistance > 10.0f) {
                    offsetLateral =
                        ((slot & 1u) ? 0.5f : -0.5f) *
                        slotSpacing;
                    offsetLongitudinal =
                        -((int)slot / 2) * slotSpacing;
                }
                float pathForwardX = forwardX;
                float pathForwardY = forwardY;
                if (!finalPoint) {
                    const size_t previous =
                        point > 0 ? point - 1 : point;
                    const size_t next =
                        std::min(point + 1,
                                 macroPath.size() - 1);
                    const float tangentX =
                        macroPath[next][0] -
                        (point > 0 ? macroPath[previous][0]
                                   : centroidX);
                    const float tangentY =
                        macroPath[next][1] -
                        (point > 0 ? macroPath[previous][1]
                                   : centroidY);
                    const float tangentLength =
                        std::sqrt(tangentX * tangentX +
                                  tangentY * tangentY);
                    if (tangentLength > 0.001f) {
                        pathForwardX =
                            tangentX / tangentLength;
                        pathForwardY =
                            tangentY / tangentLength;
                    }
                }
                const float pathRightX = -pathForwardY;
                const float pathRightY = pathForwardX;
                float pointX =
                    finalPoint
                        ? slotX
                        : macroPath[point][0] +
                              pathRightX * offsetLateral +
                              pathForwardX * offsetLongitudinal;
                float pointY =
                    finalPoint
                        ? slotY
                        : macroPath[point][1] +
                              pathRightY * offsetLateral +
                              pathForwardY * offsetLongitudinal;
                if (!finalPoint &&
                    !positionPassable(*object, pointX, pointY,
                                      false)) {
                    pointX = macroPath[point][0];
                    pointY = macroPath[point][1];
                }
                if (object->path.empty() ||
                    std::abs(object->path.back()[0] - pointX) >
                        0.05f ||
                    std::abs(object->path.back()[1] - pointY) >
                        0.05f)
                    object->path.push_back({pointX, pointY});
            }
            // Arrive in shape: a staging point one tile short of the slot
            // along the travel direction, so the ranks close up together
            // instead of cutting through each other at the destination.
            if (!object->path.empty() && travelDistance > 2.0f) {
                const float stageX = slotX - forwardX * 1.0f;
                const float stageY = slotY - forwardY * 1.0f;
                const std::array<float, 2> before =
                    object->path.size() >= 2 ? object->path[object->path.size() - 2]
                                             : std::array<float, 2>{object->x, object->y};
                const float bx = stageX - before[0], by = stageY - before[1];
                if (bx * forwardX + by * forwardY > 0.0f &&
                    staticPassableAt(*object, stageX, stageY))
                    object->path.insert(object->path.end() - 1, {stageX, stageY});
            }
            // Every leg of the offset route must be walkable; otherwise
            // this member plans its own path to its slot.
            float legX = object->x, legY = object->y;
            for (const auto &point : object->path) {
                if (!segmentClear(*object, legX, legY, point[0], point[1])) {
                    object->path.clear();
                    break;
                }
                legX = point[0];
                legY = point[1];
            }
            if (!object->path.empty()) {
                object->pathIndex = 0;
                object->blockedTime = 0;
                object->state = State::Walk;
                object->wander = false;
                object->animTime = 0;
                moveIssued = true;
            }
        }
        if (!moveIssued)
            moveIssued = issueMove(*object, slotX, slotY);
        if (!moveIssued) {
            reserved.pop_back();
            object->moveGoalActive = false;
        }
        slot++;
    }
}

void Game::setTriggerEnabled(int id, bool enabled) {
    if (id < 0 || (size_t)id >= triggerRuntime_.size()) {
        log("trigger effect references invalid trigger " + std::to_string(id));
        return;
    }
    TriggerRuntime &runtime = triggerRuntime_[(size_t)id];
    if (enabled && !runtime.enabled) {
        runtime.enabled = true;
        runtime.fired = false;
        runtime.elapsed = 0;
    } else if (!enabled) {
        runtime.enabled = false;
    }
}

void Game::startInstruction(Instruction instruction) {
    currentInstruction_ = std::move(instruction.text);
    instructionTime_ = instruction.duration;
    if (!instruction.sound.empty() && playSound_) {
        const float soundDuration = playSound_(instruction.sound);
        if (soundDuration > 0) instructionTime_ = std::max(1.0f, soundDuration + 0.35f);
    }
}

void Game::queueInstruction(const std::string &text, float duration, const std::string &sound) {
    if (text.empty()) return;
    Instruction instruction{text, sound, std::max(1.0f, duration)};
    if (currentInstruction_.empty()) {
        startInstruction(std::move(instruction));
    } else {
        instructions_.push_back(std::move(instruction));
    }
    log("instruction: " + text);
}

bool Game::conditionMet(const ScenarioCondition &condition, float triggerElapsed) {
    const int amount = triggerField(condition.fields, 0);
    const int attribute = triggerField(condition.fields, 1);
    const int unitObject = triggerField(condition.fields, 2);
    const int unitId = triggerField(condition.fields, 4);
    const int player = triggerField(condition.fields, 5);
    const int x1 = triggerField(condition.fields, 9), y1 = triggerField(condition.fields, 10);
    const int x2 = triggerField(condition.fields, 11), y2 = triggerField(condition.fields, 12);
    const int group = triggerField(condition.fields, 13);
    const int type = triggerField(condition.fields, 14);

    auto countMatching = [&](bool requireArea) {
        int count = 0;
        for (const Object &object : objects_) {
            if (!objectMatches(object, unitId, player, group, type)) continue;
            if (requireArea && !inSourceArea(object, x1, y1, x2, y2)) continue;
            count++;
        }
        return count;
    };

    switch (condition.type) {
    case 1: {
        const Object *object = findObject((uint32_t)unitObject);
        return object && object->active && inSourceArea(*object, x1, y1, x2, y2);
    }
    case 3:
        return countMatching(false) >= amount;
    case 4:
        return countMatching(false) <= amount;
    case 5:
        return countMatching(true) >= amount;
    case 6: {
        const Object *object = findObject((uint32_t)unitObject);
        return object && !object->active;
    }
    case 8:
        return resource(player, attribute) >= amount;
    case 10:
        return triggerElapsed >= std::max(0, triggerField(condition.fields, 7));
    case 11: {
        if (unitObject >= 0) {
            const Object *object = findObject((uint32_t)unitObject);
            return object && object->active && object->selected;
        }
        int selected = 0;
        for (const Object &object : objects_)
            if (object.selected && objectMatches(object, unitId, player, group, type))
                selected++;
        return selected >= std::max(1, amount);
    }
    case 15: {
        const Object *object = findObject((uint32_t)unitObject);
        if (!object || !object->active || object->hidden || !object->draw) return false;
        float sx, sy;
        toScreen(object->x, object->y, sx, sy);
        sy -= elevationAt(object->x, object->y) * assets_.dat().terrainBlock.elevHeight;
        const float halfW = 480.0f / zoom_, halfH = 272.0f / zoom_;
        return std::fabs(sx - camX_) <= halfW && std::fabs(sy - camY_) <= halfH;
    }
    case 19:
        return difficulty_ == amount;
    case 21: {
        int selected = 0;
        for (const Object &object : objects_)
            if (object.selected && objectMatches(object, unitId, player, group, type) &&
                inSourceArea(object, x1, y1, x2, y2))
                selected++;
        return selected >= std::max(1, amount);
    }
    default:
        if (warnedConditions_.insert(condition.type).second)
            log(std::string("unsupported trigger condition ") + conditionLabel(condition.type) +
                " (" + std::to_string(condition.type) + ")");
        return false;
    }
}

void Game::executeEffect(const ScenarioEffect &effect) {
    const int amount = triggerField(effect.fields, 1);
    const int resourceId = triggerField(effect.fields, 2);
    const int sourcePlayer = triggerField(effect.fields, 7);
    const int targetPlayer = triggerField(effect.fields, 8);
    const int technology = triggerField(effect.fields, 9);

    switch (effect.type) {
    case 1: {
        const int stance = triggerField(effect.fields, 3);
        if (sourcePlayer > 0 && (size_t)sourcePlayer <= players_.size() &&
            targetPlayer >= 0 && targetPlayer < 16)
            players_[(size_t)sourcePlayer - 1].diplomacy[(size_t)targetPlayer] = (uint32_t)stance;
        break;
    }
    case 2:
        researchTechnology(sourcePlayer, technology);
        if (warnedEffects_.insert(effect.type).second)
            log("technology research applies DAT attack and armor modifiers; "
                "other modifiers remain");
        break;
    case 3:
        queueInstruction(effect.message, triggerField(effect.fields, 12), effect.sound);
        break;
    case 4:
        if (!effect.sound.empty() && playSound_) playSound_(effect.sound);
        break;
    case 5:
        if (sourcePlayer >= 0 && (size_t)sourcePlayer < resources_.size() &&
            targetPlayer >= 0 && (size_t)targetPlayer < resources_.size() && resourceId >= 0) {
            resources_[(size_t)sourcePlayer][resourceId] -= amount;
            resources_[(size_t)targetPlayer][resourceId] += amount;
        }
        break;
    case 6:
    case 7: {
        for (Object *object : effectTargets(effect)) {
            configureGate(*object);
            object->locked = effect.type == 7;
            if (object->locked && object->gateClosedUnit) {
                object->unit = object->gateClosedUnit;
                object->gateOpenAmount = 0;
            }
        }
        rebuildAdjacency();
        break;
    }
    case 8:
        setTriggerEnabled(triggerField(effect.fields, 13), true);
        break;
    case 9:
        setTriggerEnabled(triggerField(effect.fields, 13), false);
        break;
    case 10:
        if (warnedEffects_.insert(effect.type).second)
            log("AI script goals are not connected to an AI runtime yet");
        break;
    case 11:
    case 25: {
        const int unitId = triggerField(effect.fields, 6);
        const int x = triggerField(effect.fields, 14), y = triggerField(effect.fields, 15);
        const dat::Unit *unit = findUnit(civilizationForPlayer(sourcePlayer), unitId);
        if (!unit || x < 0 || y < 0) {
            log("could not create trigger object " + std::to_string(unitId));
            break;
        }
        const float worldX = (float)y, worldY = (float)mapSize_ - x;
        Object *created = addObject(unit, sourcePlayer, worldX, worldY, -kPi * 0.5f, nextSpawnId_++);
        if (created) {
            created->wander = false;
            created->drawShadows = unit->name != "BLDG-LLAMBDASH";
        }
        if (unit->type == dat::UT_Building) {
            for (const dat::BuildingAnnex &annex : unit->annexes) {
                const dat::Unit *part = findUnit(civilizationForPlayer(sourcePlayer), annex.unitId);
                if (!part) continue;
                Object *createdAnnex = addObject(part, sourcePlayer, worldX + annex.misplacementY,
                                                 worldY - annex.misplacementX, -kPi * 0.5f, 0,
                                                 0, false, -1, false);
                if (createdAnnex) createdAnnex->wander = false;
            }
            refreshAutomaticTechnologies(sourcePlayer);
        }
        rebuildAdjacency();
        break;
    }
    case 12: {
        float targetX = 0, targetY = 0;
        bool hasTarget = false;
        const int locationObject = triggerField(effect.fields, 5);
        if (locationObject >= 0) {
            const Object *target = findObject((uint32_t)locationObject);
            if (target && target->active) {
                targetX = target->x;
                targetY = target->y;
                hasTarget = true;
            }
        }
        const int x = triggerField(effect.fields, 14), y = triggerField(effect.fields, 15);
        if (!hasTarget && x >= 0 && y >= 0) {
            targetX = (float)y;
            targetY = (float)mapSize_ - x;
            hasTarget = true;
        }
        if (hasTarget) issueGroupMove(effectTargets(effect), targetX, targetY);
        break;
    }
    case 13:
        if (sourcePlayer == localPlayer_) {
            victoryState_ = 1;
            queueInstruction("Victory", 3600);
        } else if (sourcePlayer > 0) {
            victoryState_ = 0;
            queueInstruction("Defeat", 3600);
        }
        break;
    case 14:
    case 15:
        for (Object *object : effectTargets(effect)) object->active = false;
        rebuildAdjacency();
        break;
    case 16:
    case 30: {
        const int x = triggerField(effect.fields, 14), y = triggerField(effect.fields, 15);
        if (x >= 0 && y >= 0) lookAt((float)y, (float)mapSize_ - x);
        break;
    }
    case 18:
        for (Object *object : effectTargets(effect)) object->player = targetPlayer;
        refreshAllAutomaticTechnologies();
        rebuildAdjacency();
        break;
    case 20:
        queueInstruction(effect.message, triggerField(effect.fields, 12), effect.sound);
        break;
    case 21:
        instructions_.clear();
        currentInstruction_.clear();
        instructionTime_ = 0;
        break;
    case 32:
        if (sourcePlayer >= 0 &&
            (size_t)sourcePlayer <
                disabledTechs_.size()) {
            disabledTechs_[(size_t)sourcePlayer].erase(technology);
            refreshAutomaticTechnologies(sourcePlayer);
        }
        break;
    case 33:
        if (sourcePlayer >= 0 &&
            (size_t)sourcePlayer <
                disabledTechs_.size() &&
            !(fullTechTreeCheat_ &&
              sourcePlayer == localPlayer_))
            disabledTechs_[(size_t)sourcePlayer].insert(technology);
        break;
    case 34:
        if (sourcePlayer >= 0 &&
            (size_t)sourcePlayer <
                disabledUnits_.size())
            disabledUnits_[(size_t)sourcePlayer].erase(
                technology);
        break;
    case 35:
        if (sourcePlayer >= 0 &&
            (size_t)sourcePlayer <
                disabledUnits_.size() &&
            !(fullTechTreeCheat_ &&
              sourcePlayer == localPlayer_))
            disabledUnits_[(size_t)sourcePlayer].insert(
                technology);
        break;
    case 36:
        for (Object *object : effectTargets(effect)) object->flashTime = 7.0f;
        break;
    default:
        if (warnedEffects_.insert(effect.type).second)
            log(std::string("unsupported trigger effect ") + effectLabel(effect.type) +
                " (" + std::to_string(effect.type) + ")");
        break;
    }
}

void Game::updateTriggers(float dt) {
    if (triggers_.empty()) return;
    for (size_t i = 0; i < triggerRuntime_.size(); i++)
        if (triggerRuntime_[i].enabled) triggerRuntime_[i].elapsed += dt;
    std::vector<bool> firedThisUpdate(triggers_.size(), false);

    std::vector<uint32_t> order = triggerOrder_;
    if (order.size() != triggers_.size()) {
        order.resize(triggers_.size());
        for (size_t i = 0; i < order.size(); i++) order[i] = (uint32_t)i;
    }

    for (size_t pass = 0; pass <= triggers_.size(); pass++) {
        bool firedAny = false;
        for (uint32_t triggerId : order) {
            if ((size_t)triggerId >= triggers_.size()) continue;
            ScenarioTrigger &trigger = triggers_[(size_t)triggerId];
            TriggerRuntime &runtime = triggerRuntime_[(size_t)triggerId];
            if (!runtime.enabled || firedThisUpdate[(size_t)triggerId]) continue;

            bool met = true;
            if (trigger.conditionOrder.size() == trigger.conditions.size()) {
                for (int32_t conditionId : trigger.conditionOrder) {
                    if (conditionId < 0 || (size_t)conditionId >= trigger.conditions.size() ||
                        !conditionMet(trigger.conditions[(size_t)conditionId], runtime.elapsed)) {
                        met = false;
                        break;
                    }
                }
            } else {
                for (const ScenarioCondition &condition : trigger.conditions)
                    if (!conditionMet(condition, runtime.elapsed)) {
                        met = false;
                        break;
                    }
            }
            if (!met) continue;

            runtime.fired = true;
            runtime.enabled = trigger.looping;
            runtime.elapsed = 0;
            firedThisUpdate[(size_t)triggerId] = true;
            log("trigger " + std::to_string(triggerId) + " fired: " + trigger.name);

            if (trigger.effectOrder.size() == trigger.effects.size()) {
                for (int32_t effectId : trigger.effectOrder)
                    if (effectId >= 0 && (size_t)effectId < trigger.effects.size())
                        executeEffect(trigger.effects[(size_t)effectId]);
            } else {
                for (const ScenarioEffect &effect : trigger.effects) executeEffect(effect);
            }
            firedAny = true;
        }
        if (!firedAny) return;
    }
    log("trigger cascade exceeded bounded execution passes");
}

int Game::playerColorBase(int player) const {
    const auto &pc = assets_.dat().playerColours;
    if (player <= 0)
        return pc.size() > 8
                   ? pc[8].playerColorBase
                   : 0;
    const size_t playerIndex = (size_t)(player - 1);
    size_t colorIndex = playerIndex < players_.size() ? players_[playerIndex].color : playerIndex;
    if (localPlayer_ > 0) {
        if (player == localPlayer_) {
            colorIndex = 0; // blue
        } else {
            const ScenarioPlayer &local = players_[(size_t)localPlayer_ - 1];
            const uint32_t stance = (size_t)player < local.diplomacy.size()
                                        ? local.diplomacy[(size_t)player]
                                        : UINT32_MAX;
            if (stance == 0) colorIndex = 3; // yellow ally
            if (stance == 3) colorIndex = 1; // red enemy
        }
    }
    return colorIndex < pc.size() ? pc[colorIndex].playerColorBase : 16;
}

void Game::lookAt(float tx, float ty) { toScreen(tx, ty, camX_, camY_); }

bool Game::lookAtObject(uint32_t spawnId) {
    const Object *object = findObject(spawnId);
    if (!object || !object->active) return false;
    lookAt(object->x, object->y);
    return true;
}

bool Game::selectObjectForTesting(uint32_t spawnId) {
    Object *target = findObject(spawnId);
    if (!target || !isInspectable(*target))
        return false;
    for (Object &object : objects_)
        object.selected = false;
    target->selected = true;
    selectionOrder_.clear();
    selectionOrder_.push_back(spawnId);
    return true;
}

std::string Game::describeObjectForTesting(uint32_t spawnId) const {
    const Object *o = findObject(spawnId);
    if (!o) return "missing";
    char buffer[256];
    snprintf(buffer, sizeof buffer,
             "state=%d goal=%d target=%.2f,%.2f path=%zu/%zu blocked=%.2f "
             "stall=%.2f repath=%d group=%u",
             (int)o->state, (int)o->moveGoalActive, o->targetX, o->targetY,
             o->pathIndex, o->path.size(), o->blockedTime, o->moveStallTime,
             (int)o->repathCount, o->moveGroupId);
    std::string text = buffer;
    for (uint32_t index : staticObstructionIndices_) {
        const Object &other = objects_[(size_t)index];
        if (!other.active || std::abs(other.x - o->x) > 2 || std::abs(other.y - o->y) > 2) continue;
        snprintf(buffer, sizeof buffer, " {%s %.2f,%.2f %.2fx%.2f}", other.unit->name.c_str(), other.x, other.y,
                 other.unit->collisionSize[0], other.unit->collisionSize[1]);
        text += buffer;
    }
    snprintf(buffer, sizeof buffer, " r=%.2f segWp=%d", collisionRadius(*o),
             o->pathIndex < o->path.size()
                 ? (int)segmentClear(*o, o->x, o->y, o->path[o->pathIndex][0], o->path[o->pathIndex][1])
                 : -1);
    text += buffer;
    for (float step = 0.05f; step <= 0.3f; step += 0.05f) {
        snprintf(buffer, sizeof buffer, " [+%.2f t%d s%d p%d]", step,
                 (int)terrainPassable(*o, o->x + step, o->y),
                 (int)staticPassableAt(*o, o->x + step, o->y),
                 (int)positionPassable(*o, o->x + step, o->y, false));
        text += buffer;
    }
    for (const auto &p : o->path) {
        snprintf(buffer, sizeof buffer, " (%.2f,%.2f)", p[0], p[1]);
        text += buffer;
    }
    return text;
}

void Game::groupMoveForTesting(const std::vector<uint32_t> &spawnIds,
                               float x, float y, int formation) {
    std::vector<Object *> targets;
    for (uint32_t id : spawnIds)
        if (Object *object = findObject(id)) targets.push_back(object);
    issueGroupMove(std::move(targets), x, y,
                   formation < 0 ? selectedFormation_ : (FormationType)formation);
}

bool Game::selectObjectsForTesting(
    const std::vector<uint32_t> &spawnIds) {
    for (Object &object : objects_)
        object.selected = false;
    selectionOrder_.clear();
    for (uint32_t spawnId : spawnIds) {
        Object *target = findObject(spawnId);
        if (!target || !isSelectable(*target)) {
            for (Object &object : objects_)
                object.selected = false;
            selectionOrder_.clear();
            return false;
        }
        target->selected = true;
        selectionOrder_.push_back(spawnId);
    }
    return !selectionOrder_.empty();
}

void Game::update(float dt, const InputState &in) {
    statusTime_ = std::max(0.0f, statusTime_ - dt);
    if (statusTime_ <= 0) statusMessage_.clear();
    if (!currentInstruction_.empty()) {
        instructionTime_ -= dt;
        if (instructionTime_ <= 0) {
            currentInstruction_.clear();
            if (!instructions_.empty()) {
                Instruction instruction = std::move(instructions_.front());
                instructions_.pop_front();
                startInstruction(std::move(instruction));
            }
        }
    }

    if (in.toggleCheatMenu) {
        cheatMenuOpen_ = !cheatMenuOpen_;
        actionMenuOpen_ = false;
        actionMenuObjectId_ = 0;
    }
    const bool consumeWorldInput = cheatMenuOpen_;
    if (cheatMenuOpen_) {
        if (in.menuUp)
            cheatMenuSelection_ =
                cheatMenuSelection_ == 0
                    ? std::size(kCheats) - 1
                    : cheatMenuSelection_ - 1;
        if (in.menuDown)
            cheatMenuSelection_ =
                (cheatMenuSelection_ + 1) % std::size(kCheats);
        if (in.menuActivate)
            activateCheat(cheatMenuSelection_,
                          in.screenW, in.screenH);
        if (in.menuBack)
            cheatMenuOpen_ = false;
    }
    bool actionMenuControlConsumed = false;
    if (actionMenuOpen_ &&
        (in.actionTabLeft || in.actionTabRight)) {
        actionMenuControlConsumed = true;
        Object *subject = findObject(actionMenuObjectId_);
        if (actionMenuTab_ ==
            ActionMenuTab::Stances) {
        } else if (subject && isWorker(*subject)) {
            const int direction =
                in.actionTabRight ? 1 : -1;
            int category =
                actionMenuTab_ == ActionMenuTab::Economy
                    ? 0
                    : actionMenuTab_ ==
                              ActionMenuTab::Military
                          ? 1
                          : 2;
            category = (category + direction + 3) % 3;
            actionMenuTab_ =
                category == 0
                    ? ActionMenuTab::Economy
                    : category == 1
                          ? ActionMenuTab::Military
                          : ActionMenuTab::Defense;
        } else if (subject) {
            std::vector<ActionMenuTab> tabs;
            if (!productionOptions(*subject).empty())
                tabs.push_back(ActionMenuTab::Units);
            if (!researchOptions(*subject).empty())
                tabs.push_back(ActionMenuTab::Research);
            if (subject->gate ||
                subject->unit->garrisonCapacity > 0)
                tabs.push_back(ActionMenuTab::Commands);
            if (!tabs.empty()) {
                auto current = std::find(
                    tabs.begin(), tabs.end(),
                    actionMenuTab_);
                int index =
                    current == tabs.end()
                        ? 0
                        : (int)(current - tabs.begin());
                index =
                    (index +
                     (in.actionTabRight ? 1 : -1) +
                     (int)tabs.size()) %
                    (int)tabs.size();
                actionMenuTab_ = tabs[(size_t)index];
            }
        }
        actionMenuSelection_ = 0;
        actionMenuScroll_ = 0;
    }
    if (actionMenuOpen_) {
        Object *subject =
            findObject(actionMenuObjectId_);
        size_t optionCount = 0;
        if (actionMenuTab_ ==
            ActionMenuTab::Stances)
            optionCount = 4;
        else if (subject && isWorker(*subject))
            optionCount =
                buildingOptions(
                    *subject, actionMenuTab_).size();
        else if (subject &&
                 actionMenuTab_ ==
                     ActionMenuTab::Commands)
            optionCount = 1;
        else if (subject &&
                 actionMenuTab_ ==
                     ActionMenuTab::Units)
            optionCount =
                productionOptions(*subject).size();
        else if (subject)
            optionCount =
                researchOptions(*subject).size();

        const float gridX = kActionMenuX + 12.0f;
        if (in.cursorVisible &&
            in.pointerX >= gridX &&
            in.pointerX <
                gridX +
                    kActionMenuColumns *
                        kActionMenuCell &&
            in.pointerY >= kActionMenuY &&
            in.pointerY <
                kActionMenuY +
                    kActionMenuRows *
                        kActionMenuCell) {
            const size_t column =
                (size_t)((in.pointerX - gridX) /
                         kActionMenuCell);
            const size_t row =
                (size_t)((in.pointerY -
                          kActionMenuY) /
                         kActionMenuCell);
            const size_t hovered =
                actionMenuScroll_ +
                row * kActionMenuColumns +
                column;
            if (hovered < optionCount)
                actionMenuSelection_ = hovered;
        }
        if (optionCount > 0 &&
            (in.menuUp || in.menuDown ||
             in.menuLeft || in.menuRight)) {
            actionMenuControlConsumed = true;
            int next = (int)std::min(
                actionMenuSelection_,
                optionCount - 1);
            if (in.menuUp)
                next -= (int)kActionMenuColumns;
            if (in.menuDown)
                next += (int)kActionMenuColumns;
            if (in.menuLeft) next--;
            if (in.menuRight) next++;
            next = std::max(
                0, std::min(
                       (int)optionCount - 1, next));
            actionMenuSelection_ = (size_t)next;
            actionMenuScroll_ =
                (actionMenuSelection_ /
                 kActionMenuVisibleItems) *
                kActionMenuVisibleItems;
        }
        if (in.menuActivate && optionCount > 0) {
            actionMenuControlConsumed = true;
            const size_t visible =
                actionMenuSelection_ -
                actionMenuScroll_;
            handleActionMenuClick(
                gridX +
                    (visible %
                     kActionMenuColumns) *
                        kActionMenuCell +
                    kActionMenuCell * 0.5f,
                kActionMenuY +
                    (visible /
                     kActionMenuColumns) *
                        kActionMenuCell +
                    kActionMenuCell * 0.5f,
                in.screenW, in.screenH);
        } else if (in.menuBack) {
            actionMenuControlConsumed = true;
            actionMenuOpen_ = false;
            actionMenuObjectId_ = 0;
        }
    }

    const bool consumeCameraInput =
        consumeWorldInput || actionMenuOpen_;
    float scrollX =
        consumeCameraInput ? 0.0f : in.scrollX;
    float scrollY =
        consumeCameraInput ? 0.0f : in.scrollY;
    if (in.cursorVisible && !consumeCameraInput) {
        constexpr float edge = 24.0f;
        if (in.pointerX <= edge)
            scrollX = std::min(scrollX, -(edge - in.pointerX) / edge);
        else if (in.pointerX >= in.screenW - edge)
            scrollX = std::max(scrollX, (in.pointerX - (in.screenW - edge)) / edge);
        if (in.pointerY <= edge)
            scrollY = std::min(scrollY, -(edge - in.pointerY) / edge);
        else if (in.pointerY >= in.screenH - edge)
            scrollY = std::max(scrollY, (in.pointerY - (in.screenH - edge)) / edge);
    }
    const float scrollSpeed = 900.0f / zoom_;
    camX_ += scrollX * scrollSpeed * dt -
             (consumeCameraInput ? 0.0f
                                 : in.dragX / zoom_);
    camY_ += scrollY * scrollSpeed * dt -
             (consumeCameraInput ? 0.0f
                                 : in.dragY / zoom_);
    // Clamp the camera to the map diamond's bounding box.
    float minX = -mapSize_ * kTileHalfW, maxX = mapSize_ * kTileHalfW;
    float maxY = 2.0f * mapSize_ * kTileHalfH;
    camX_ = std::max(minX, std::min(maxX, camX_));
    camY_ = std::max(0.0f, std::min(maxY, camY_));
    updateAmbience(dt, in.screenW, in.screenH);

    if (!consumeWorldInput && !actionMenuOpen_) {
        if (in.zoomStep > 0)
            zoom_ = std::min(1.0f, zoom_ * 1.25f);
        if (in.zoomStep < 0)
            zoom_ = std::max(0.4f, zoom_ / 1.25f);
        if (in.toggleDebug) debug_ = !debug_;
    }

    updateTriggers(dt);
    struct ProductionSpawn {
        const dat::Unit *unit;
        int player;
        float x, y;
    };
    std::vector<ProductionSpawn> productionSpawns;
    const size_t productionObjectCount = objects_.size();
    for (size_t index = 0; index < productionObjectCount;
         index++) {
        Object &building = objects_[index];
        if (!building.active ||
            building.productionQueue.empty())
            continue;
        if (forceBuildCheat_ && building.player == localPlayer_)
            building.productionRemaining = 0.0f;
        else {
            const float rate =
                isPowered(building) ? 1.0f : 0.25f;
            building.productionRemaining -= dt * rate;
        }
        if (building.productionRemaining > 0) continue;
        const ProductionItem item =
            building.productionQueue.front();
        if (item.technologyId >= 0) {
            researchTechnology(
                building.player, item.technologyId);
            building.productionQueue.pop_front();
            building.productionRemaining =
                building.productionQueue.empty()
                    ? 0.0f
                    : building.productionQueue.front().duration;
            statusMessage_ =
                assets_.localizedString(
                    assets_.dat()
                        .techs[(size_t)item.technologyId]
                        .languageDllName) +
                " COMPLETE";
            statusTime_ = 3.0f;
            continue;
        }
        const dat::Unit *unit = item.unit;
        if (!unit) {
            building.productionQueue.pop_front();
            building.productionRemaining =
                building.productionQueue.empty()
                    ? 0.0f
                    : building.productionQueue.front().duration;
            continue;
        }
        Object candidate;
        candidate.unit = unit;
        candidate.player = building.player;
        bool foundExit = false;
        const float startDistance =
            collisionRadius(building) +
            collisionRadius(candidate) + 0.25f;
        for (int ring = 0; ring < 12 && !foundExit; ring++) {
            const int samples = 12 + ring * 4;
            const float distance =
                startDistance + ring * 0.35f;
            for (int sample = 0; sample < samples; sample++) {
                const float angle =
                    sample * (2.0f * kPi / samples);
                const float x =
                    building.x + std::cos(angle) * distance;
                const float y =
                    building.y + std::sin(angle) * distance;
                candidate.x = x;
                candidate.y = y;
                if (!positionPassable(candidate, x, y, true))
                    continue;
                productionSpawns.push_back(
                    {unit, building.player, x, y});
                if (unit->trainSound >= 0)
                    playWorldUnitSound(
                        building, unit->trainSound);
                foundExit = true;
                break;
            }
        }
        if (!foundExit) {
            building.productionRemaining = 0;
            continue;
        }
        building.productionQueue.pop_front();
        building.productionRemaining =
            building.productionQueue.empty()
                ? 0.0f
                : forceBuildCheat_ &&
                         building.player == localPlayer_
                     ? 0.0f
                : building.productionQueue.front().duration;
    }
    for (const ProductionSpawn &spawned : productionSpawns) {
        Object *created = addObject(
            spawned.unit, spawned.player, spawned.x, spawned.y,
            -kPi * 0.5f, nextSpawnId_++);
        if (created) {
            created->wander = false;
            created->state = State::Idle;
            created->animTime = 0.0f;
            created->stateTime = 0.0f;
            created->initialFrame = 0;
        }
    }
    updateConstruction(dt);
    updateShields(dt);
    rebuildMobileOccupancy();
    updateLivestockOwnership();
    updateGathering(dt);
    updateRepairing(dt);
    // Gate logic, following the original (battlegrounds_x1.exe 0x558390):
    // the gate watches a 0.4-tile-wide corridor between its two end posts.
    // A closed, unlocked gate swaps to its OPEN unit the moment a friendly
    // unit is in the corridor; an open gate swaps back to CLOSED 0.5s after
    // the corridor is empty of all units. There is no intermediate frame.
    for (Object &gate : objects_) {
        if (!gate.active || !gate.gate || !gate.gateClosedUnit ||
            !gate.gateOpenUnit || gate.underConstruction)
            continue;
        const dat::Unit *layout = gate.gateClosedUnit;
        float postX[2] = {gate.x, gate.x}, postY[2] = {gate.y, gate.y};
        int posts = 0;
        for (const dat::BuildingAnnex &annex : layout->annexes) {
            if (annex.unitId < 0 || posts >= 2) continue;
            postX[posts] = gate.x + annex.misplacementY;
            postY[posts] = gate.y - annex.misplacementX;
            posts++;
        }
        if (posts < 2) {
            // No posts: use the long axis of the gate footprint.
            const bool alongX = layout->collisionSize[0] >= layout->collisionSize[1];
            const float half = std::max(layout->collisionSize[0], layout->collisionSize[1]);
            postX[0] = gate.x - (alongX ? half : 0); postX[1] = gate.x + (alongX ? half : 0);
            postY[0] = gate.y - (alongX ? 0 : half); postY[1] = gate.y + (alongX ? 0 : half);
        }
        const float segX = postX[1] - postX[0], segY = postY[1] - postY[0];
        const float segLen2 = std::max(1e-4f, segX * segX + segY * segY);
        int total = 0, friendly = 0;
        for (uint32_t index : mobileObjectIndices_) {
            const Object &unit = objects_[(size_t)index];
            if (!unit.active || unit.hidden || isAirUnit(unit) ||
                &unit == &gate)
                continue;
            const int cls = unit.unit->cls;
            if (cls == 6 || cls == 8 || cls == 9 || cls == 10 || cls == 18)
                continue;
            const float t = std::max(0.0f, std::min(1.0f,
                ((unit.x - postX[0]) * segX + (unit.y - postY[0]) * segY) / segLen2));
            const float dx = unit.x - (postX[0] + segX * t);
            const float dy = unit.y - (postY[0] + segY * t);
            const float reach = 0.4f + collisionRadius(unit);
            if (dx * dx + dy * dy > reach * reach) continue;
            total++;
            if (unit.player == gate.player || !isEnemy(gate, unit)) friendly++;
        }
        const bool open = gate.unit == gate.gateOpenUnit;
        if (!open) {
            if (!gate.locked && friendly > 0) {
                gate.unit = gate.gateOpenUnit;
                gate.gateOpenAmount = 1.0f;
                gate.gateCloseTimer = 0.0f;
                rebuildAdjacency();
            }
        } else if (total == 0) {
            if (gate.gateCloseTimer <= 0.0f) {
                gate.gateCloseTimer = 0.5f;
            } else {
                gate.gateCloseTimer -= dt;
                if (gate.gateCloseTimer < 0.0f) {
                    gate.gateCloseTimer = 0.0f;
                    gate.unit = gate.gateClosedUnit;
                    gate.gateOpenAmount = 0.0f;
                    rebuildAdjacency();
                }
            }
        }
    }
    commandMarkerTime_ = std::max(0.0f, commandMarkerTime_ - dt);
    selectionClickAge_ += dt;
    for (Object &object : objects_)
        if (object.selected && !isInspectable(object)) object.selected = false;
    syncSelectionOrder();

    cursorVisible_ = in.cursorVisible;
    cursorX_ = in.pointerX;
    cursorY_ = in.pointerY;
    boxSelectActive_ = in.boxSelectActive;
    boxStartX_ = in.boxStartX;
    boxStartY_ = in.boxStartY;
    boxEndX_ = in.boxEndX;
    boxEndY_ = in.boxEndY;
    if (!consumeWorldInput && in.boxSelectCommit) {
        actionMenuOpen_ = false;
        selectBox(in.boxStartX, in.boxStartY, in.boxEndX, in.boxEndY,
                  in.screenW, in.screenH);
    }
    bool placementHandled = false;
    if (!consumeWorldInput && placementUnit_ &&
        in.commandPressed) {
        placementUnit_ = nullptr;
        placementBuilderId_ = 0;
        statusMessage_ = "CONSTRUCTION CANCELLED";
        statusTime_ = 2.0f;
        placementHandled = true;
    } else if (!consumeWorldInput && placementUnit_ &&
               (in.selectPressed || in.pointerTap)) {
        placeBuilding(in.pointerX, in.pointerY,
                      in.screenW, in.screenH);
        placementHandled = true;
    }
    bool garrisonHandled = false;
    if (!consumeWorldInput && !placementHandled &&
        garrisonCursorActive_ && in.commandPressed) {
        garrisonCursorActive_ = false;
        statusMessage_ = "GARRISON CANCELLED";
        statusTime_ = 2.0f;
        garrisonHandled = true;
    } else if (!consumeWorldInput && !placementHandled &&
               garrisonCursorActive_ &&
               (in.selectPressed || in.pointerTap) &&
               in.pointerY <
                   in.screenH - kSelectionPanelHeight) {
        Object *building =
            objectAtScreen(in.pointerX, in.pointerY,
                           in.screenW, in.screenH);
        if (!building ||
            building->player != localPlayer_ ||
            building->unit->type != dat::UT_Building ||
            building->unit->garrisonCapacity == 0 ||
            building->underConstruction) {
            statusMessage_ =
                "CANNOT GARRISON IN THAT BUILDING";
            statusTime_ = 3.0f;
        } else if (issueGarrisonCommand(*building)) {
            garrisonCursorActive_ = false;
            commandMarkerX_ = building->x;
            commandMarkerY_ = building->y;
            commandMarkerTime_ = 0.65f;
        }
        garrisonHandled = true;
    }
    bool repairHandled = false;
    if (!consumeWorldInput && !placementHandled &&
        !garrisonHandled &&
        repairCursorActive_ && in.commandPressed) {
        repairCursorActive_ = false;
        statusMessage_ = "REPAIR CANCELLED";
        statusTime_ = 2.0f;
        repairHandled = true;
    } else if (!consumeWorldInput &&
               !placementHandled &&
               !garrisonHandled &&
               repairCursorActive_ &&
               (in.selectPressed ||
                in.pointerTap) &&
               in.pointerY <
                   in.screenH -
                       kSelectionPanelHeight) {
        Object *target =
            objectAtScreen(
                in.pointerX, in.pointerY,
                in.screenW, in.screenH);
        Object *acknowledgement = nullptr;
        if (target)
            for (Object *worker :
                 selectedObjectsInOrder(true))
                if (issueRepairCommand(
                        *worker, *target) &&
                    !acknowledgement)
                    acknowledgement = worker;
        if (acknowledgement) {
            playUnitAcknowledgement(
                *acknowledgement, false);
            repairCursorActive_ = false;
            commandMarkerX_ = target->x;
            commandMarkerY_ = target->y;
            commandMarkerTime_ = 0.65f;
            statusMessage_ =
                "REPAIRING " +
                unitDisplayName(*target->unit);
            statusTime_ = 2.0f;
        } else {
            statusMessage_ =
                "THAT TARGET CANNOT BE REPAIRED";
            statusTime_ = 3.0f;
        }
        repairHandled = true;
    }
    if (!consumeWorldInput &&
        !actionMenuControlConsumed &&
        !placementHandled &&
        !garrisonHandled && !repairHandled &&
        in.selectPressed) {
        if (actionMenuOpen_)
            handleActionMenuClick(
                in.pointerX, in.pointerY, in.screenW, in.screenH);
        else if (!handleSelectionPanelClick(
                     in.pointerX, in.pointerY, in.screenW,
                     in.screenH))
            selectAtScreen(in.pointerX, in.pointerY, in.screenW,
                           in.screenH);
    }
    if (!consumeWorldInput &&
        !actionMenuControlConsumed &&
        !placementHandled &&
        !garrisonHandled && !repairHandled &&
        in.cycleAttackMode) {
        if (actionMenuOpen_) {
            actionMenuOpen_ = false;
            actionMenuObjectId_ = 0;
        } else if (!openSelectedActionMenu()) {
            cycleSelectedAttackMode();
        }
    }
    if (!consumeWorldInput &&
        !actionMenuControlConsumed &&
        !placementHandled &&
        !garrisonHandled && !repairHandled &&
        in.commandPressed) {
        if (actionMenuOpen_) {
            actionMenuOpen_ = false;
            actionMenuObjectId_ = 0;
        } else {
            commandAtScreen(in.pointerX, in.pointerY,
                            in.screenW, in.screenH);
        }
    }
    if (!consumeWorldInput &&
        !actionMenuControlConsumed &&
        !placementHandled &&
        !garrisonHandled && !repairHandled &&
        in.pointerTap) {
        if (actionMenuOpen_) {
            handleActionMenuClick(
                in.pointerX, in.pointerY, in.screenW, in.screenH);
        } else if (handleSelectionPanelClick(
                in.pointerX, in.pointerY, in.screenW, in.screenH)) {
        } else if (hasSelectedAttacker() &&
            enemyAtScreen(in.pointerX, in.pointerY, in.screenW, in.screenH))
            commandAtScreen(in.pointerX, in.pointerY, in.screenW, in.screenH);
        else if (
            gatherableAtScreen(
                in.pointerX, in.pointerY,
                in.screenW, in.screenH) &&
            std::any_of(
                objects_.begin(), objects_.end(),
                [&](const Object &object) {
                    return object.selected &&
                           isWorker(object);
                }))
            commandAtScreen(
                in.pointerX, in.pointerY,
                in.screenW, in.screenH);
        else if (Object *target =
                     objectAtScreen(
                         in.pointerX, in.pointerY,
                         in.screenW, in.screenH, true)) {
            bool selectedWorker = false;
            const Object *repairWorker = nullptr;
            for (const Object &object : objects_)
                if (object.selected &&
                    isWorker(object)) {
                    selectedWorker = true;
                    repairWorker = &object;
                    break;
                }
            if ((target->underConstruction ||
                 isGatherable(*target) ||
                 (repairWorker &&
                  isRepairableBy(
                      *repairWorker, *target))) &&
                selectedWorker)
                commandAtScreen(
                    in.pointerX, in.pointerY,
                    in.screenW, in.screenH);
            else
                selectAtScreen(
                    in.pointerX, in.pointerY,
                    in.screenW, in.screenH);
        }
        else if (hasSelectedUnit())
            commandAtScreen(in.pointerX, in.pointerY, in.screenW, in.screenH);
        else
            clearSelection();
    }
    cursorMode_ = CursorMode::Normal;
    if (!consumeWorldInput && cursorVisible_ &&
        garrisonCursorActive_)
        cursorMode_ = CursorMode::Garrison;
    else if (!consumeWorldInput &&
             cursorVisible_ &&
             repairCursorActive_)
        cursorMode_ = CursorMode::Repair;
    else if (!consumeWorldInput && cursorVisible_ &&
        !boxSelectActive_ && hasSelectedUnit()) {
        Object *hovered =
            objectAtScreen(
                cursorX_, cursorY_,
                in.screenW, in.screenH);
        Object *gatherable =
            gatherableAtScreen(
                cursorX_, cursorY_,
                in.screenW, in.screenH);
        bool selectedWorker = false;
        const Object *cursorWorker = nullptr;
        for (const Object &object : objects_)
            if (object.selected &&
                isWorker(object)) {
                selectedWorker = true;
                cursorWorker = &object;
                break;
            }
        cursorMode_ =
            selectedWorker && gatherable
                ? CursorMode::Gather
                : selectedWorker && hovered &&
                          (hovered->underConstruction ||
                           (cursorWorker &&
                            isRepairableBy(
                                *cursorWorker,
                                *hovered)))
                      ? CursorMode::Repair
                : enemyAtScreen(
                      cursorX_, cursorY_,
                      in.screenW, in.screenH)
                      ? CursorMode::Attack
                      : CursorMode::Move;
    }

    std::uniform_real_distribution<float> r01(0, 1);
    auto spreadBlockedMoveGoal = [&](Object &object) {
        const float originalX = object.moveAnchorX;
        const float originalY = object.moveAnchorY;
        constexpr float searchStep = 0.35f;
        const uint8_t retry = object.moveSpreadRetries++;
        for (int ring = 1; ring <= 8; ring++) {
            const int samples = ring * 8;
            const float startAngle =
                (object.spawnId % 16) * (2.0f * kPi / 16.0f) +
                retry * 2.39996323f;
            for (int sample = 0; sample < samples; sample++) {
                const float angle =
                    startAngle + sample * (2.0f * kPi / samples);
                const float candidateX =
                    originalX + std::cos(angle) * ring * searchStep;
                const float candidateY =
                    originalY + std::sin(angle) * ring * searchStep;
                if (!positionPassable(object, candidateX, candidateY, true) ||
                    !segmentClear(object, originalX, originalY, candidateX,
                                  candidateY))
                    continue; // must be on the goal's side of any wall
                bool formationSlotOpen = true;
                if (object.moveGroupId != 0)
                    for (const Object &other : objects_) {
                        if (&other == &object || !other.active ||
                            other.moveGroupId != object.moveGroupId ||
                            isAirUnit(other) != isAirUnit(object))
                            continue;
                        const float dx = candidateX - other.targetX;
                        const float dy = candidateY - other.targetY;
                        const float separation =
                            collisionRadius(object) +
                            collisionRadius(other) + 0.08f;
                        if (dx * dx + dy * dy <
                            separation * separation) {
                            formationSlotOpen = false;
                            break;
                        }
                    }
                if (!formationSlotOpen) continue;
                object.targetX = object.homeX = candidateX;
                object.targetY = object.homeY = candidateY;
                object.moveStallTime = 0;
                const float dx = candidateX - object.x;
                const float dy = candidateY - object.y;
                object.moveBestDistance = std::sqrt(dx * dx + dy * dy);
                issueMove(object, candidateX, candidateY);
                object.moveGoalActive = true;
                return true;
            }
        }
        object.moveStallTime = 0;
        return false;
    };
    struct GroupCohesion {
        float nearestRemaining =
            std::numeric_limits<float>::max();
        size_t movingMembers = 0;
    };
    std::unordered_map<uint32_t, GroupCohesion> groupCohesion;
    for (const Object &object : objects_) {
        if (!object.active || !object.moveGoalActive ||
            object.moveGroupId == 0)
            continue;
        const float dx = object.targetX - object.x;
        const float dy = object.targetY - object.y;
        GroupCohesion &cohesion = groupCohesion[object.moveGroupId];
        const float remaining =
            std::sqrt(dx * dx + dy * dy);
        cohesion.nearestRemaining =
            std::min(cohesion.nearestRemaining, remaining);
        cohesion.movingMembers++;
    }
    for (Object &o : objects_) {
        if (!o.active) continue;
        o.animTime += dt;
        o.approachRetry = std::max(0.0f, o.approachRetry - dt);
        o.flashTime = std::max(0.0f, o.flashTime - dt);
        updateAttack(o, dt);
        if (!o.active) continue;
        if (o.hidden || o.unit->type < dat::UT_DeadFish || o.unit->speed <= 0 ||
            o.unit->type == dat::UT_Building)
            continue;
        o.stateTime -= dt;
        if (o.state == State::Attack ||
            o.state == State::Build ||
            o.state == State::Gather ||
            o.state == State::Repair)
            continue;
        if (o.state == State::Idle) {
            if (o.moveGoalActive) {
                o.moveRetryTime -= dt;
                if (o.moveRetryTime <= 0) {
                    o.moveRetryTime =
                        0.35f + (o.spawnId % 5) * 0.04f;
                    issueMove(o, o.targetX, o.targetY);
                }
                continue;
            }
            if (!o.wander) continue;
            if (o.stateTime <= 0) {
                // Wander to a nearby point around home.
                float a = r01(rng_) * 2 * kPi, d = 1.0f + r01(rng_) * 4.0f;
                const float targetX =
                    std::max(1.0f, std::min(mapSize_ - 2.0f, o.homeX + std::cos(a) * d));
                const float targetY =
                    std::max(1.0f, std::min(mapSize_ - 2.0f, o.homeY + std::sin(a) * d));
                o.moveGroupId = 0;
                issueMove(o, targetX, targetY);
                o.wander = true;
            }
        } else {
            if (o.moveGoalActive && !o.attackTargetId) {
                const float goalDx = o.targetX - o.x;
                const float goalDy = o.targetY - o.y;
                float arrival =
                    std::max(0.20f,
                             collisionRadius(o) * 0.75f);
                if (o.moveGroupId != 0 &&
                    o.pathIndex + 1 >= o.path.size())
                    arrival = 0.10f;
                if (goalDx * goalDx + goalDy * goalDy <=
                    arrival * arrival) {
                    if (o.moveGroupId != 0 &&
                        positionPassable(o, o.targetX, o.targetY, false) &&
                        !unitBlockerAt(o, o.targetX, o.targetY)) {
                        o.x = o.targetX;
                        o.y = o.targetY;
                    }
                    o.moveGoalActive = false;
                    o.moveSpeedLimit = 0;
                    o.path.clear();
                    o.pathIndex = 0;
                    o.state = State::Idle;
                    o.stateTime =
                        1.5f + r01(rng_) * 5.0f;
                    o.animTime = 0;
                    continue;
                }
            }
            if (o.pathIndex >= o.path.size()) {
                o.state = State::Idle;
                o.stateTime = 1.5f + r01(rng_) * 5.0f;
                o.animTime = 0;
                const float goalDx = o.targetX - o.x;
                const float goalDy = o.targetY - o.y;
                const float arrival =
                    std::max(0.20f, collisionRadius(o) * 0.75f);
                if (o.moveGoalActive &&
                    goalDx * goalDx + goalDy * goalDy <= arrival * arrival) {
                    o.moveGoalActive = false;
                    o.moveSpeedLimit = 0;
                }
                continue;
            }
            const auto &waypoint = o.path[o.pathIndex];
            float dx = waypoint[0] - o.x, dy = waypoint[1] - o.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            const float unitSpeed =
                modifiedUnitAttribute(o, 5, o.unit->speed);
            float moveSpeed =
                o.moveSpeedLimit > 0
                    ? std::min(unitSpeed, o.moveSpeedLimit)
                    : unitSpeed;
            if (o.moveGroupId != 0 && o.moveGoalActive) {
                const auto group = groupCohesion.find(o.moveGroupId);
                if (group != groupCohesion.end() &&
                    group->second.movingMembers > 1) {
                    const float goalDx = o.targetX - o.x;
                    const float goalDy = o.targetY - o.y;
                    const float remaining =
                        std::sqrt(goalDx * goalDx + goalDy * goalDy);
                    const float lag =
                        remaining -
                        group->second.nearestRemaining;
                    if (lag > 1.0f) {
                        const float catchUp =
                            1.0f +
                            std::min(0.30f,
                                     (lag - 1.0f) * 0.12f);
                        moveSpeed *= catchUp;
                    }
                }
            }
            const float step = moveSpeed * dt;
            const float scale = dist <= step || dist <= 0.0001f ? 1.0f : step / dist;
            float nextX = o.x + dx * scale, nextY = o.y + dy * scale;
            bool moved = false;
            const bool staticOk = positionPassable(o, nextX, nextY, false);
            const Object *blocker =
                staticOk ? unitBlockerAt(o, nextX, nextY) : nullptr;
            if (staticOk && !blocker) {
                moved = true;
            } else if (!staticOk) {
                // Slide along the obstruction edge (axis-separated move).
                if (std::abs(dx) > 0.0001f &&
                    positionPassable(o, nextX, o.y, false) &&
                    !unitBlockerAt(o, nextX, o.y)) {
                    nextY = o.y;
                    moved = true;
                } else if (std::abs(dy) > 0.0001f &&
                           positionPassable(o, o.x, nextY, false) &&
                           !unitBlockerAt(o, o.x, nextY)) {
                    nextX = o.x;
                    moved = true;
                }
            } else {
                // Blocked by a unit. Idle friendly units are asked to step
                // aside, as in the original; then try to flow around it.
                Object *other = const_cast<Object *>(blocker);
                o.blockerId = other->spawnId;
                const bool friendlyWalker =
                    other->player == o.player && other->state == State::Walk;
                const bool mutual =
                    friendlyWalker && other->blockerId == o.spawnId &&
                    other->blockedTime > 0.0f;
                if (friendlyWalker && !mutual && other->blockedTime < 0.3f) {
                    // Queue behind a friendly unit that is still moving
                    // (a chokepoint): wait instead of shoving sideways.
                    o.blockedTime = std::min(o.blockedTime, 0.4f);
                    o.moveStallTime = std::max(0.0f, o.moveStallTime - dt);
                } else if (mutual && o.spawnId > other->spawnId && dist > 0.0001f) {
                    // Two units pushing into each other: the later one gives
                    // way by stepping back/aside at half speed.
                    const float awayX = o.x - other->x, awayY = o.y - other->y;
                    const float awayLength = std::max(0.001f, std::sqrt(awayX * awayX + awayY * awayY));
                    const float baseAngle = std::atan2(awayY / awayLength, awayX / awayLength);
                    static constexpr float yields[5] = {0.0f, 0.7853982f, -0.7853982f,
                                                        1.5707963f, -1.5707963f};
                    for (float turn : yields) {
                        const float candidateX = o.x + std::cos(baseAngle + turn) * step * 0.5f;
                        const float candidateY = o.y + std::sin(baseAngle + turn) * step * 0.5f;
                        if (positionPassable(o, candidateX, candidateY, false) &&
                            !unitBlockerAt(o, candidateX, candidateY)) {
                            nextX = candidateX;
                            nextY = candidateY;
                            moved = true;
                            break;
                        }
                    }
                    o.blockedTime = std::min(o.blockedTime, 0.4f);
                }
                if (moved || (friendlyWalker && !mutual && other->blockedTime < 0.3f)) {
                    // handled above
                } else {
                if (other->state == State::Idle && !other->moveGoalActive &&
                    other->player == o.player && other->unit->speed > 0 &&
                    !other->attackTargetId && !other->gatherTargetId &&
                    !other->constructionTargetId && !other->repairTargetId &&
                    other->approachRetry <= 0.0f && dist > 0.0001f) {
                    const float side = ((o.spawnId ^ other->spawnId) & 1u) ? 1.0f : -1.0f;
                    const float px = -dy / dist * side, py = dx / dist * side;
                    const float push =
                        collisionRadius(o) + collisionRadius(*other) + 0.3f;
                    const float aside[2][2] = {
                        {other->x + px * push, other->y + py * push},
                        {other->x - px * push, other->y - py * push}};
                    for (const auto &point : aside)
                        if (staticPassableAt(*other, point[0], point[1])) {
                            const bool wander = other->wander;
                            issueMove(*other, point[0], point[1]);
                            other->wander = wander;
                            other->approachRetry = 1.0f;
                            break;
                        }
                }
                if (dist > 0.0001f) {
                    const float directionX = dx / dist, directionY = dy / dist;
                    const float handedness = (o.spawnId & 1u) ? 1.0f : -1.0f;
                    static constexpr float turns[6] = {
                        0.39269908f, -0.39269908f, 0.78539816f,
                        -0.78539816f, 1.17809725f, -1.17809725f};
                    for (float turn : turns) {
                        const float angle = turn * handedness;
                        const float sx = directionX * std::cos(angle) - directionY * std::sin(angle);
                        const float sy = directionX * std::sin(angle) + directionY * std::cos(angle);
                        const float candidateX = o.x + sx * step;
                        const float candidateY = o.y + sy * step;
                        if (positionPassable(o, candidateX, candidateY, false) &&
                            !unitBlockerAt(o, candidateX, candidateY)) {
                            nextX = candidateX;
                            nextY = candidateY;
                            moved = true;
                            break;
                        }
                    }
                }
                }
            }
            if (moved) {
                if (!blocker) o.blockerId = 0;
                o.x = nextX;
                o.y = nextY;
                o.facing = std::atan2(dy, dx);
                o.blockedTime = std::max(0.0f, o.blockedTime - dt);
                const float newGoalDx = o.targetX - o.x;
                const float newGoalDy = o.targetY - o.y;
                const float newGoalDistance =
                    std::sqrt(newGoalDx * newGoalDx + newGoalDy * newGoalDy);
                if (newGoalDistance + 0.05f < o.moveBestDistance) {
                    o.moveBestDistance = newGoalDistance;
                    o.moveStallTime = 0;
                    o.repathCount = 0;
                } else {
                    o.moveStallTime += dt;
                    // Sliding along an obstacle without getting closer (the
                    // unit was pushed off its route): plan again from here.
                    if (o.moveStallTime >= 1.5f && o.detourTime <= 0.0f &&
                        o.pathIndex < o.path.size() &&
                        !segmentClear(o, o.x, o.y, o.path[o.pathIndex][0],
                                      o.path[o.pathIndex][1])) {
                        o.moveStallTime = 0.0f;
                        const Object *goalObject = findObject(o.pathGoalId);
                        const bool wander = o.wander;
                        const float bestDistance = o.moveBestDistance;
                        issueMove(o, o.targetX, o.targetY, goalObject,
                                  o.pathGoalClearance);
                        o.wander = wander;
                        o.moveBestDistance = bestDistance;
                        o.detourTime = 1.0f;
                        continue;
                    }
                }
                // Reached (or passed) the waypoint.
                const float wx = waypoint[0] - o.x, wy = waypoint[1] - o.y;
                if (dist <= step || dist <= 0.0001f ||
                    wx * wx + wy * wy <= 0.0025f) {
                    o.pathIndex++;
                    if (o.pathIndex >= o.path.size()) {
                        o.state = State::Idle;
                        o.stateTime = 1.5f + r01(rng_) * 5.0f;
                        o.animTime = 0;
                        const float goalDx = o.targetX - o.x;
                        const float goalDy = o.targetY - o.y;
                        const float arrival =
                            std::max(0.20f, collisionRadius(o) * 0.75f);
                        if (o.moveGoalActive &&
                            goalDx * goalDx + goalDy * goalDy <=
                                arrival * arrival) {
                            o.moveGoalActive = false;
                            o.moveSpeedLimit = 0;
                        }
                    }
                }
            } else {
                o.blockedTime += dt;
                o.detourTime -= dt;
                if (o.moveGoalActive && !o.attackTargetId)
                    o.moveStallTime += dt;
                const float targetDx = o.targetX - o.x, targetDy = o.targetY - o.y;
                const float targetDistance =
                    std::sqrt(targetDx * targetDx + targetDy * targetDy);
                const float arrival =
                    std::max(0.20f, collisionRadius(o) * 0.75f);
                // A crowd at the destination: close enough counts as arrived
                // (formation members settle next to each other).
                const bool crowdedArrival =
                    blocker && targetDistance <=
                                   arrival + collisionRadius(o) * 2.0f + 0.4f;
                if ((targetDistance <= arrival || crowdedArrival) &&
                    o.blockedTime >= 0.3f) {
                    o.state = State::Idle;
                    o.stateTime = 1.5f + r01(rng_) * 5.0f;
                    o.animTime = 0;
                    o.path.clear();
                    o.pathIndex = 0;
                    o.moveGoalActive = false;
                    o.moveSpeedLimit = 0;
                    continue;
                }
                // Jammed by other units just short of the goal: take a free
                // spot nearby (or stop there) instead of pushing forever.
                const bool blockerSettled =
                    blocker && blocker->state != State::Walk && !blocker->moveGoalActive;
                if (blocker && o.moveGoalActive && !o.attackTargetId &&
                    o.moveStallTime >= (blockerSettled ? 1.5f : 3.0f) &&
                    targetDistance < 3.0f) {
                    o.moveStallTime = 0.0f;
                    if (targetDistance < 2.5f && o.moveSpreadRetries < 3 &&
                        spreadBlockedMoveGoal(o))
                        continue;
                    o.state = State::Idle;
                    o.stateTime = 1.5f + r01(rng_) * 5.0f;
                    o.animTime = 0;
                    o.path.clear();
                    o.pathIndex = 0;
                    o.moveGoalActive = false;
                    o.moveSpeedLimit = 0;
                    continue;
                }
                if (blocker && o.blockedTime >= 0.5f && o.detourTime <= 0.0f) {
                    // Rejoin the route beyond the blockage via a local detour.
                    size_t rejoin = o.pathIndex;
                    while (rejoin + 1 < o.path.size()) {
                        const float rx = o.path[rejoin][0] - o.x;
                        const float ry = o.path[rejoin][1] - o.y;
                        if (rx * rx + ry * ry > 9.0f) break;
                        rejoin++;
                    }
                    o.detourTime = 0.75f;
                    if (detourAround(o, o.path[rejoin][0], o.path[rejoin][1]))
                        continue;
                }
                if (!staticOk && o.blockedTime >= 0.25f && o.detourTime <= 0.0f) {
                    // The static world changed under the path: re-plan.
                    o.detourTime = 0.75f;
                    const Object *goalObject = findObject(o.pathGoalId);
                    const bool wander = o.wander;
                    issueMove(o, o.targetX, o.targetY, goalObject,
                              o.pathGoalClearance);
                    o.wander = wander;
                    o.repathCount++;
                    continue;
                }
                if (o.blockedTime >= 2.0f) {
                    o.blockedTime = 0.0f;
                    if (o.attackTargetId) {
                        retryAttackApproach(o);
                        continue;
                    }
                    if (++o.repathCount > 4) {
                        // Give up, like the original does after repeated
                        // failures: stop and idle where we are.
                        o.state = State::Idle;
                        o.path.clear();
                        o.pathIndex = 0;
                        o.moveGoalActive = false;
                        o.moveSpeedLimit = 0;
                        o.repathCount = 0;
                        continue;
                    }
                    if (o.moveGoalActive && targetDistance < 2.5f &&
                        spreadBlockedMoveGoal(o))
                        continue;
                    const Object *goalObject = findObject(o.pathGoalId);
                    const bool wander = o.wander;
                    issueMove(o, o.targetX, o.targetY, goalObject,
                              o.pathGoalClearance);
                    o.wander = wander;
                }
            }
        }
    }
    updateGarrisoning();
    updateProjectiles(dt);
    updateRemains(dt);
}

// Picks the SLP frame for a graphic given a world-space facing and time.
// Genie stores angles starting at "south" (screen-down) going clockwise; the
// east half is mirrored from the west half when mirroringMode is set.
static bool pickFrame(const dat::Graphic &g, size_t slpFrames, float facing, float t, int initialFrame,
                      size_t &frame, bool &flip) {
    int angles = std::max<int>(1, g.angleCount);
    int perAngle = std::max<int>(1, g.frameCount);
    float step = 2 * kPi / angles;
    int a;
    if (angles == 5) {
        // Wall and fence rotations encode one of five connection shapes,
        // rather than a world-space facing.
        a = (int)std::lround(facing / step);
    } else {
        // World direction -> screen direction (iso projection), y grows downwards.
        float sdx = (std::cos(facing) - std::sin(facing)) * 2.0f;
        float sdy = (std::cos(facing) + std::sin(facing));
        float theta = std::atan2(sdy, sdx);             // 0 = screen east
        float rel = theta - kPi / 2;                    // 0 = screen south
        a = (int)std::lround(rel / step);
    }
    a = ((a % angles) + angles) % angles;
    flip = false;
    int stored = g.mirroringMode ? angles / 2 + 1 : angles;
    if ((int)slpFrames < stored * perAngle) stored = std::max<int>(1, (int)slpFrames / perAngle);
    if (a >= stored) {
        if (g.mirroringMode) {
            a = angles - a;
            flip = true;
            if (a >= stored) a = stored - 1;
        } else {
            a = a % stored;
        }
    }
    int f = perAngle > 0 ? initialFrame % perAngle : 0;
    if ((g.sequenceType & kSequenceAnimated) && g.frameDuration > 0 && perAngle > 1) {
        float cycle = perAngle * g.frameDuration + std::max(0.0f, g.replayDelay);
        float tt = std::fmod(t, cycle);
        f = (f + std::min(perAngle - 1, (int)(tt / g.frameDuration))) % perAngle;
    }
    frame = (size_t)a * perAngle + f;
    if (frame >= slpFrames) frame = slpFrames ? slpFrames - 1 : 0;
    return slpFrames > 0;
}

static std::vector<SpriteDraw> g_draws; // reused between frames

int Game::graphicSortLayer(int graphicId, int depth) const {
    if (depth >= 8) return 0;
    const dat::Graphic *graphic =
        assets_.dat().graphic(graphicId);
    if (!graphic) return 0;
    int layer = std::min<int>(graphic->layer, 20);
    for (const dat::GraphicDelta &delta : graphic->deltas)
        layer = std::max(
            layer,
            graphicSortLayer(delta.graphicId, depth + 1));
    return layer;
}

void Game::drawGraphic(Renderer &r, int graphicId, float sx, float sy, float facing, float animTime, int player,
                       int initialFrame, int depth, bool drawShadows, float viewW, float viewH,
                       int sortLayerOverride, int sortBias,
                       float sortYOverride, uint32_t ownerId,
                       bool outlineCandidate, int powerState,
                       int frameOverride) {
    const dat::Graphic *g = assets_.dat().graphic(graphicId);
    if (!g) return;
    if (!drawShadows && depth > 0 && g->layer == 5) return;
    int resolvedFrame = initialFrame;
    float resolvedTime = animTime;
    if (powerState > 0 &&
        g->frameCount == 2 &&
        g->slp >= kPowerIndicatorSlpFirst &&
        g->slp + 6 <= kPowerIndicatorSlpLast) {
        // Unpowered buildings carry EFFECT-LIGHT-OFF (grey/red, 0.4s per
        // frame, so it blinks on its own). Powered buildings show the
        // matching EFFECT-LIGHT-ON (green) graphic, six ids later in the dat.
        const dat::Graphic *on = assets_.dat().graphic(graphicId + 6);
        if (on && on->slp == g->slp + 6) {
            g = on;
            graphicId += 6;
        }
    }
    if (!g->deltas.empty() && depth < 3) {
        bool drewSelf = false;
        for (const auto &d : g->deltas) {
            if (d.graphicId == -1) {
                // -1 means "draw my own SLP here".
                drewSelf = true;
                dat::Graphic self = *g;
                self.deltas.clear();
                const SpriteSheet *sh = assets_.sheet(self.slp, playerColorBase(player));
                if (!sh) continue;
                size_t fr; bool flip;
                if (frameOverride >= 0) {
                    fr = std::min<size_t>(
                        (size_t)frameOverride,
                        sh->frames.size() - 1);
                    flip = false;
                } else if (!pickFrame(
                               self, sh->frames.size(),
                               facing, resolvedTime,
                               resolvedFrame, fr, flip)) {
                    continue;
                }
                const SpriteFrame &f = sh->frames[fr];
                float x = sx + d.offsetX - (flip ? f.w - f.hotX : f.hotX), y = sy + d.offsetY - f.hotY;
                if (viewW > 0 && (x + f.w <= 0 || x >= viewW || y + f.h <= 0 || y >= viewH)) continue;
                Quad q{x, y, (float)f.w, (float)f.h, flip ? f.u + f.w : f.u, f.v, flip ? f.u : f.u + f.w, f.v + f.h};
                const int sortLayer =
                    sortLayerOverride >= 0
                        ? sortLayerOverride
                        : std::min<int>(g->layer, 20);
                g_draws.push_back(
                    {(int64_t)sortLayer << 40 |
                         (int64_t)((sortYOverride > -100000000.0f
                                        ? sortYOverride
                                        : sy) *
                                       16 +
                                   65536)
                             << 8 |
                         ((sortBias + depth) & 0xFF),
                     f.tex, q, f.outlineTex, ownerId,
                     outlineCandidate, g->layer != 5});
            } else {
                drawGraphic(r, d.graphicId, sx + d.offsetX, sy + d.offsetY, facing, animTime, player,
                            initialFrame, depth + 1, drawShadows, viewW, viewH,
                            sortLayerOverride, sortBias,
                            sortYOverride, ownerId,
                            outlineCandidate, powerState);
            }
        }
        if (drewSelf || g->slp < 0)
            return;
    }
    const SpriteSheet *sh = assets_.sheet(g->slp, playerColorBase(player));
    if (!sh) return;
    size_t fr;
    bool flip;
    if (frameOverride >= 0) {
        fr = std::min<size_t>(
            (size_t)frameOverride,
            sh->frames.size() - 1);
        flip = false;
    } else if (!pickFrame(
                   *g, sh->frames.size(), facing,
                   resolvedTime, resolvedFrame,
                   fr, flip)) {
        return;
    }
    const SpriteFrame &f = sh->frames[fr];
    if (f.w == 0 || f.h == 0) return;
    float x = sx - (flip ? f.w - f.hotX : f.hotX), y = sy - f.hotY;
    if (viewW > 0 && (x + f.w <= 0 || x >= viewW || y + f.h <= 0 || y >= viewH)) return;
    Quad q{x, y, (float)f.w, (float)f.h, flip ? f.u + f.w : f.u, f.v, flip ? f.u : f.u + f.w, f.v + f.h};
    // Sort: graphic layer first (shadows/rubble under units), then screen y.
    const int sortLayer =
        sortLayerOverride >= 0
            ? sortLayerOverride
            : std::min<int>(g->layer, 20);
    const float sortY =
        sortYOverride > -100000000.0f
            ? sortYOverride
            : sy;
    int64_t key = (int64_t)sortLayer << 40 |
                  (int64_t)(sortY * 16 + 65536) << 8 |
                  ((sortBias + depth) & 0xFF);
    g_draws.push_back(
        {key, f.tex, q, f.outlineTex, ownerId,
         outlineCandidate, g->layer != 5});
}

void Game::drawGraphicNow(Renderer &r, int graphicId, float sx, float sy, float facing, float t, int player) {
    g_draws.clear();
    drawGraphic(r, graphicId, sx, sy, facing, t, player, 0, 0, true, 0, 0);
    std::stable_sort(g_draws.begin(), g_draws.end(),
                     [](const SpriteDraw &a, const SpriteDraw &b) { return a.key < b.key; });
    for (const SpriteDraw &d : g_draws) r.draw(d.tex, d.q);
    g_draws.clear();
}

void Game::render(Renderer &r, int screenW, int screenH) {
    stats_ = FrameStats{};
    assets_.beginTerrainFrame(64u * 1024u * 1024u);
    const float viewW = screenW / zoom_, viewH = screenH / zoom_;
    const float ox = camX_ - viewW / 2, oy = camY_ - viewH / 2; // world-pixel of screen top-left
    const bool overview = zoom_ < 0.6f;
    const bool reducedTerrainLighting = overview;
    Texture *selectionRing = assets_.selectionRing();
    const SpriteSheet *cursors = assets_.interfaceSheet(kCursorSlp);
    const SpriteSheet *commandIcons =
        assets_.interfaceSheet(kCommandIconSlp);
    const Object *panelObject = nullptr;
    const Object *selectedAttacker = nullptr;
    size_t panelSelectionCount = 0;
    float panelHitPoints = 0, panelMaxHitPoints = 0;
    float panelShieldPoints = 0;
    float panelMaxShieldPoints = 0;
    bool panelMixedUnits = false;
    bool mixedAttackModes = false;
    const std::vector<Object *> panelSelection =
        selectedObjectsInOrder(false);
    for (const Object *object : panelSelection) {
        if (!panelObject)
            panelObject = object;
        else if (panelObject->unit->id != object->unit->id)
            panelMixedUnits = true;
        panelSelectionCount++;
        panelHitPoints += object->hitPoints;
        panelMaxHitPoints += object->maxHitPoints;
        panelShieldPoints += object->shieldPoints;
        panelMaxShieldPoints +=
            object->maxShieldPoints;
        if (!canAttack(*object)) continue;
        if (!selectedAttacker)
            selectedAttacker = object;
        else if (selectedAttacker->attackMode != object->attackMode)
            mixedAttackModes = true;
    }
    const auto selectedStanceLabel = [&]() {
        if (!selectedAttacker || mixedAttackModes)
            return "MIXED";
        switch (selectedAttacker->attackMode) {
        case AttackMode::Aggressive:
            return "AGGRESSIVE";
        case AttackMode::Defensive:
            return "DEFENSIVE";
        case AttackMode::StandGround:
            return "STAND GROUND";
        case AttackMode::Passive:
            return "PASSIVE";
        }
        return "MIXED";
    };
    const SpriteFrame *panelPortrait = nullptr;
    bool panelPortraitFlipped = false;
    if (panelObject) {
        const int civilization = civilizationForPlayer(panelObject->player);
        const int iconSet =
            civilization >= 0 &&
                    (size_t)civilization < assets_.dat().civs.size()
                ? assets_.dat().civs[(size_t)civilization].iconSet
                : 1;
        const int iconSlpBase =
            panelObject->unit->type == dat::UT_Building
                ? kBuildingIconSlpBase
                : kUnitIconSlpBase;
        const SpriteSheet *icons =
            assets_.interfaceSheet(iconSlpBase + std::max(1, iconSet) - 1);
        if (icons && panelObject->unit->iconId >= 0 &&
            (size_t)panelObject->unit->iconId < icons->frames.size()) {
            panelPortrait = &icons->frames[(size_t)panelObject->unit->iconId];
        } else {
            const dat::Graphic *graphic =
                assets_.dat().graphic(panelObject->unit->standingGraphic[0]);
            const SpriteSheet *sheet =
                graphic ? assets_.sheet(graphic->slp,
                                        playerColorBase(panelObject->player))
                        : nullptr;
            size_t frame = 0;
            if (graphic && sheet &&
                pickFrame(*graphic, sheet->frames.size(), panelObject->facing,
                          panelObject->animTime, 0, frame,
                          panelPortraitFlipped))
                panelPortrait = &sheet->frames[frame];
        }
    }
    // At overview zoom the blend overlays are sub-pixel detail but account for
    // hundreds of extra masked draws and slope-mask cache entries on Vita.
    const bool drawTerrainBlends = !overview && assets_.hasBlendMasks();

    // --- terrain -------------------------------------------------------
    const auto &terrains = assets_.dat().terrainBlock.terrains;
    // Visible tile range: invert the projection at the four screen corners.
    auto toTile = [&](float px, float py, float &tx, float &ty) {
        tx = (px / kTileHalfW + py / kTileHalfH) * 0.5f;
        ty = (py / kTileHalfH - px / kTileHalfW) * 0.5f;
    };
    float cx[4], cy[4];
    toTile(ox, oy, cx[0], cy[0]);
    toTile(ox + viewW, oy, cx[1], cy[1]);
    toTile(ox, oy + viewH, cx[2], cy[2]);
    toTile(ox + viewW, oy + viewH, cx[3], cy[3]);
    int x0 = std::max(0, (int)std::floor(*std::min_element(cx, cx + 4)) - 1);
    int x1 = std::min(mapSize_ - 1, (int)std::ceil(*std::max_element(cx, cx + 4)) + 1);
    int y0 = std::max(0, (int)std::floor(*std::min_element(cy, cy + 4)) - 1);
    int y1 = std::min(mapSize_ - 1, (int)std::ceil(*std::max_element(cy, cy + 4)) + 1);

    auto frameIndexFor = [](const SpriteSheet *sheet, int tx, int ty, int slope) -> size_t {
        if (!sheet || sheet->frames.empty()) return 0;
        uint32_t hash = (uint32_t)tx * 374761393u ^ (uint32_t)ty * 668265263u;
        hash = (hash ^ (hash >> 13)) * 1274126177u;
        hash ^= hash >> 16;
        const size_t variants = slope ? 1 : sheet->frames.size();
        return hash % variants;
    };
    static constexpr int neighborX[8] = {-1, 0, 1, 1, 1, 0, -1, -1};
    static constexpr int neighborY[8] = {-1, -1, -1, 0, 1, 1, 1, 0};
    static constexpr int slopeNeighborX[8] = {-1, 1, 1, -1, 0, 1, 0, -1};
    static constexpr int slopeNeighborY[8] = {-1, -1, 1, 1, -1, 0, 1, 0};

    // VitaGL texture creation during an active frame can expose uninitialized
    // tiles for one swap. Populate every visible cache entry before beginFrame.
    for (int ty = y0; ty <= y1; ty++) {
        for (int tx = x0; tx <= x1; tx++) {
            float sx, sy;
            toScreen((float)tx, (float)ty, sx, sy);
            const size_t tileIndex = (size_t)ty * mapSize_ + tx;
            const int slope = tileSlope_[tileIndex];
            sy -= tileElevation_[tileIndex] * assets_.dat().terrainBlock.elevHeight;
            sx -= ox;
            sy -= oy;
            if (sx + kTileHalfW < 0 || sx - kTileHalfW > viewW || sy > viewH + 48 ||
                sy + 3 * kTileHalfH < 0)
                continue;

            std::array<int8_t, 8> neighborSlopes;
            neighborSlopes.fill(reducedTerrainLighting ? 0 : -1);
            if (!reducedTerrainLighting)
                for (size_t i = 0; i < neighborSlopes.size(); i++) {
                    int nx = tx + slopeNeighborX[i], ny = ty + slopeNeighborY[i];
                    if (nx >= 0 && ny >= 0 && nx < mapSize_ && ny < mapSize_)
                        neighborSlopes[i] = tileSlope_[(size_t)ny * mapSize_ + nx];
                }
            const int terrainId = terrainAt(tx, ty);
            const dat::Terrain &terrain = terrains[terrainId];
            const dat::Terrain &draw = drawTerrain(terrains, terrainId);
            const SpriteSheet *flat = assets_.terrainSheet(draw.slp);
            const size_t terrainFrame = frameIndexFor(flat, tx, ty, slope);
            assets_.terrainSlopeFrame(draw.slp, slope, terrainFrame, neighborSlopes);

            if (!drawTerrainBlends) continue;
            std::array<int, 8> neighbors;
            neighbors.fill(-1);
            std::array<bool, 8> active;
            for (int i = 0; i < 8; i++) {
                int nx = tx + neighborX[i], ny = ty + neighborY[i];
                if (nx < 0 || ny < 0 || nx >= mapSize_ || ny >= mapSize_) {
                    active[i] = false;
                    continue;
                }
                int id = terrainAt(nx, ny);
                neighbors[i] = id;
                active[i] = id != terrainId && (size_t)id < terrains.size() &&
                            terrains[id].blendPriority > terrain.blendPriority;
            }
            std::map<int, uint8_t> grouped;
            for (int i = 0; i < 8; i++) {
                if (!active[i]) continue;
                if ((i & 1) == 0 && (active[(i + 7) & 7] || active[(i + 1) & 7])) continue;
                grouped[neighbors[i]] |= (uint8_t)(1u << i);
            }
            for (const auto &entry : grouped) {
                const dat::Terrain &overlay = terrains[entry.first];
                const dat::Terrain &drawOverlay = drawTerrain(terrains, entry.first);
                const SpriteSheet *flatOverlay = assets_.terrainSheet(drawOverlay.slp);
                assets_.terrainSlopeFrame(drawOverlay.slp, slope,
                                          frameIndexFor(flatOverlay, tx, ty, slope),
                                          neighborSlopes);
                std::array<int, 5> maskIds;
                int maskCount = blendMasksFor(entry.second, tx, ty, maskIds);
                int mode = blendModeFor(terrain.blendType, overlay.blendType);
                for (int i = 0; i < maskCount; i++) assets_.blendMask(mode, maskIds[i], slope);
            }
        }
    }

    g_draws.clear();
    for (const Object &o : objects_) {
        if (!o.active || o.hidden || !o.draw) continue;
        if (overview &&
            (o.unit->type == dat::UT_Trees || o.unit->type == dat::UT_AoeTrees)) {
            const uint32_t x = (uint32_t)std::lround(o.x * 2.0f);
            const uint32_t y = (uint32_t)std::lround(o.y * 2.0f);
            const uint32_t hash = x * 73856093u ^ y * 19349663u;
            const uint32_t mask = zoom_ < 0.45f ? 3u : 1u;
            if (hash & mask) continue;
        }
        float sx, sy;
        toScreen(o.x, o.y, sx, sy);
        sy -= elevationAt(o.x, o.y) * assets_.dat().terrainBlock.elevHeight;
        sx -= ox;
        sy -= oy;
        if (sx < -400 || sx > viewW + 400 || sy < -100 || sy > viewH + 500) continue;
        const dat::Unit *visualUnit =
            o.state == State::Build ||
                    o.state == State::Repair
                ? builderUnit(o)
                : (o.gatherTargetId ||
                   o.carriedAmount > 0)
                      ? gathererUnit(o)
                      : o.unit;
        if (!visualUnit) visualUnit = o.unit;
        int gid = visualUnit->standingGraphic[0];
        if (o.state == State::Build ||
            o.state == State::Repair) {
            const int workingGraphic =
                builderWorkingGraphic(o);
            if (workingGraphic >= 0)
                gid = workingGraphic;
        }
        if (o.state == State::Gather) {
            const dat::Task *task =
                gatherTask(o, *visualUnit);
            if (task) {
                if (task->workingGraphic >= 0)
                    gid = task->workingGraphic;
                else if (task->proceedingGraphic >= 0)
                    gid = task->proceedingGraphic;
            }
        }
        if (o.underConstruction &&
            o.unit->constructionGraphic >= 0)
            gid = o.unit->constructionGraphic;
        if (o.felled && o.unit->dyingGraphic >= 0)
            gid = o.unit->dyingGraphic;
        if (o.state == State::Walk) {
            const dat::Task *task =
                o.carriedAmount > 0
                    ? gatherTask(o, *visualUnit)
                    : nullptr;
            if (task && task->carryingGraphic >= 0)
                gid = task->carryingGraphic;
            else if (visualUnit->walkingGraphic >= 0)
                gid = visualUnit->walkingGraphic;
        }
        if (o.state == State::Attack &&
            o.unit->attackGraphic >= 0)
            gid = o.unit->attackGraphic;
        gid = civilizationGraphic(gid, o.player);
        float graphicTime = o.animTime;
        float graphicFacing = o.facing;
        int graphicFrameOverride = -1;
        if (o.underConstruction) {
            const dat::Graphic *construction =
                assets_.dat().graphic(gid);
            const float progress =
                o.constructionTotal > 0
                    ? 1.0f -
                          o.constructionRemaining /
                              o.constructionTotal
                    : 1.0f;
            if (construction &&
                construction->frameCount == 1 &&
                construction->angleCount == 3) {
                const int stage = std::max(
                    0, std::min(
                           2, (int)(std::max(
                                          0.0f,
                                          std::min(
                                              0.999f,
                                              progress)) *
                                      3.0f)));
                graphicFrameOverride = stage;
                graphicTime = 0.0f;
            } else if (construction &&
                construction->frameDuration > 0 &&
                construction->frameCount > 1) {
                graphicTime = std::max(
                    0.0f,
                    std::min(0.999f, progress)) *
                    construction->frameCount *
                    construction->frameDuration;
            }
        }
        const int powerState =
            !o.underConstruction &&
                    o.unit->type == dat::UT_Building &&
                    requiresPower(o)
                ? (isPowered(o) ? 1 : 0)
                : -1;
        drawGraphic(
            r, gid, sx, sy, graphicFacing, graphicTime,
            o.player, o.initialFrame, 0,
            o.drawShadows && !overview, viewW, viewH,
            -1, 0, -1000000000.0f, o.spawnId,
            o.player > 0 &&
                o.unit->type != dat::UT_Building,
            powerState, graphicFrameOverride);
        if (isPowerSource(o) &&
            (o.selected || o.underConstruction)) {
            drawGraphic(
                r, kPowerRadiusGraphic,
                sx, sy, o.facing,
                o.animTime, o.player, 0, 0, false,
                viewW, viewH);
        } else if (isShieldGenerator(o) &&
                   isPowered(o)) {
            const int civilization =
                civilizationForPlayer(o.player);
            const bool secondTier =
                o.unit->name.rfind(
                    "BLDG-SHLDGEN2", 0) == 0;
            int fieldGraphic = -1;
            if (civilization >= 1 &&
                civilization <= 6)
                fieldGraphic =
                    (secondTier ? 2799 : 2751) +
                    civilization;
            else if (civilization == 7 ||
                     civilization == 8)
                fieldGraphic =
                    (secondTier ? 8003 : 7987) +
                    (civilization - 7);
            if (fieldGraphic >= 0)
                drawGraphic(
                    r, fieldGraphic,
                    sx, sy, o.facing,
                    o.animTime, o.player,
                    0, 0, false,
                    viewW, viewH);
        }

        const float damagePercent =
            o.underConstruction
                ? 0.0f
                : 100.0f *
                      (1.0f -
                       o.hitPoints /
                           std::max(1.0f, o.maxHitPoints));
        const int damageSortLayer =
            damagePercent > 0 && !o.unit->damageGraphics.empty()
                ? graphicSortLayer(gid)
                : 0;
        const dat::DamageGraphic *replacement = nullptr;
        for (const dat::DamageGraphic &damageGraphic : o.unit->damageGraphics) {
            if (damageGraphic.graphicId < 0 ||
                damagePercent + 0.001f < damageGraphic.damagePercent)
                continue;
            if (damageGraphic.applyMode == 0) {
                if (!replacement ||
                    damageGraphic.damagePercent > replacement->damagePercent)
                    replacement = &damageGraphic;
            } else {
                drawGraphic(r, damageGraphic.graphicId, sx, sy, o.facing, o.animTime,
                            o.player, 0, 0, false, viewW, viewH,
                            damageSortLayer, 128, sy,
                            o.spawnId, false);
            }
        }
        if (replacement)
            drawGraphic(r, replacement->graphicId, sx, sy, o.facing, o.animTime,
                        o.player, 0, 0, false, viewW, viewH,
                        damageSortLayer, 128, sy,
                        o.spawnId, false);
    }
    for (const Remains &remains : remains_) {
        int graphicId = remains.dyingGraphic;
        float animTime = remains.age;
        if (remains.age >= remains.dyingDuration) {
            graphicId = remains.deadUnit ? remains.deadUnit->standingGraphic[0] : -1;
            animTime -= remains.dyingDuration;
        }
        if (graphicId < 0) continue;
        float sx, sy;
        toScreen(remains.x, remains.y, sx, sy);
        sy -= elevationAt(remains.x, remains.y) *
              assets_.dat().terrainBlock.elevHeight;
        sx -= ox;
        sy -= oy;
        if (sx < -400 || sx > viewW + 400 || sy < -100 || sy > viewH + 500) continue;
        drawGraphic(r, graphicId, sx, sy, remains.facing, animTime,
                    remains.player, 0, 0, remains.drawShadows && !overview,
                    viewW, viewH);
    }
    std::vector<std::array<float, 2>> blasterGlowPoints;
    for (const Projectile &projectile : projectiles_) {
        if (!projectile.unit) continue;
        const dat::Graphic *graphic =
            assets_.dat().graphic(projectile.unit->standingGraphic[0]);
        const SpriteSheet *sheet =
            graphic ? assets_.sheet(graphic->slp, playerColorBase(projectile.player)) : nullptr;
        const bool tiny = sheet && !sheet->frames.empty() &&
                          sheet->frames[0].w <= 4 && sheet->frames[0].h <= 4;
        const int trailCopies = tiny ? 5 : 1;
        for (int trail = trailCopies - 1; trail >= 0; trail--) {
            const float trailDistance = trail * 0.10f;
            const float x = projectile.x - std::cos(projectile.facing) * trailDistance;
            const float y = projectile.y - std::sin(projectile.facing) * trailDistance;
            float sx, sy;
            toScreen(x, y, sx, sy);
            sy -= elevationAt(x, y) * assets_.dat().terrainBlock.elevHeight +
                  projectile.z * kTileHalfH;
            sx -= ox;
            sy -= oy;
            if (sx < -100 || sx > viewW + 100 || sy < -100 || sy > viewH + 100)
                continue;
            if (tiny && graphic && graphic->slp == 2738)
                blasterGlowPoints.push_back({sx, sy});
            drawGraphic(r, projectile.unit->standingGraphic[0], sx, sy,
                        projectile.facing, projectile.animTime, projectile.player,
                        0, 0, false, viewW, viewH);
        }
    }
    r.beginFrame(screenW, screenH, zoom_, 0, 0, 0);

    for (int ty = y0; ty <= y1; ty++) {
        for (int tx = x0; tx <= x1; tx++) {
            float sx, sy;
            toScreen((float)tx, (float)ty, sx, sy);
            const size_t tileIndex = (size_t)ty * mapSize_ + tx;
            const int slope = tileSlope_[tileIndex];
            const int elevation = tileElevation_[tileIndex];
            sy -= elevation * assets_.dat().terrainBlock.elevHeight;
            sx -= ox;
            sy -= oy;
            if (sx + kTileHalfW < 0 || sx - kTileHalfW > viewW || sy > viewH + 48 || sy + 3 * kTileHalfH < 0)
                continue;
            const int terrainId = terrainAt(tx, ty);
            const dat::Terrain &t = terrains[terrainId];
            const dat::Terrain &draw = drawTerrain(terrains, terrainId);
            std::array<int8_t, 8> neighborSlopes;
            neighborSlopes.fill(reducedTerrainLighting ? 0 : -1);
            if (!reducedTerrainLighting)
                for (size_t i = 0; i < neighborSlopes.size(); i++) {
                    int nx = tx + slopeNeighborX[i], ny = ty + slopeNeighborY[i];
                    if (nx >= 0 && ny >= 0 && nx < mapSize_ && ny < mapSize_)
                        neighborSlopes[i] = tileSlope_[(size_t)ny * mapSize_ + nx];
                }
            const SpriteSheet *flatBase = assets_.terrainSheet(draw.slp);
            const size_t terrainFrame = frameIndexFor(flatBase, tx, ty, slope);
            const SpriteFrame *baseFrame =
                assets_.terrainSlopeFrame(draw.slp, slope, terrainFrame, neighborSlopes);
            if (!baseFrame) continue;
            const SpriteFrame &f = *baseFrame;
            const int deltaY = slope < (int)assets_.dat().terrainBlock.tileSizes.size()
                                   ? assets_.dat().terrainBlock.tileSizes[(size_t)slope].deltaY
                                   : 0;
            const float tileY = sy - deltaY - (slope ? 12.0f : 0.0f);
            Quad q{sx - kTileHalfW, tileY, (float)f.w, (float)f.h, f.u, f.v, f.u + f.w, f.v + f.h};
            r.draw(f.tex, q);
            stats_.tiles++;

            if (!drawTerrainBlends) continue;
            std::array<int, 8> neighbors;
            neighbors.fill(-1);
            auto influences = [&](int direction) {
                int nx = tx + neighborX[direction], ny = ty + neighborY[direction];
                if (nx < 0 || ny < 0 || nx >= mapSize_ || ny >= mapSize_) return false;
                int id = terrainAt(nx, ny);
                neighbors[direction] = id;
                return id != terrainId && (size_t)id < terrains.size() &&
                       terrains[id].blendPriority > t.blendPriority;
            };
            std::array<bool, 8> active;
            for (int i = 0; i < 8; i++) active[i] = influences(i);

            std::map<int, uint8_t> grouped;
            for (int i = 0; i < 8; i++) {
                if (!active[i]) continue;
                if ((i & 1) == 0 && (active[(i + 7) & 7] || active[(i + 1) & 7])) continue;
                grouped[neighbors[i]] |= (uint8_t)(1u << i);
            }
            std::vector<TerrainInfluence> ordered;
            ordered.reserve(grouped.size());
            for (const auto &entry : grouped) ordered.push_back({entry.first, entry.second});
            std::sort(ordered.begin(), ordered.end(), [&](const TerrainInfluence &a, const TerrainInfluence &b) {
                int ap = terrains[a.terrain].blendPriority, bp = terrains[b.terrain].blendPriority;
                return ap != bp ? ap < bp : a.terrain < b.terrain;
            });

            for (const TerrainInfluence &influence : ordered) {
                const dat::Terrain &overlayTerrain = terrains[influence.terrain];
                const dat::Terrain &drawOverlay = drawTerrain(terrains, influence.terrain);
                const SpriteSheet *flatOverlay = assets_.terrainSheet(drawOverlay.slp);
                const size_t overlayFrame = frameIndexFor(flatOverlay, tx, ty, slope);
                const SpriteFrame *overlay =
                    assets_.terrainSlopeFrame(drawOverlay.slp, slope, overlayFrame, neighborSlopes);
                if (!overlay) continue;
                Quad overlayQ{sx - kTileHalfW, tileY, (float)overlay->w, (float)overlay->h,
                              overlay->u, overlay->v, overlay->u + overlay->w, overlay->v + overlay->h};
                std::array<int, 5> maskIds;
                int maskCount = blendMasksFor(influence.directions, tx, ty, maskIds);
                int mode = blendModeFor(t.blendType, overlayTerrain.blendType);
                for (int i = 0; i < maskCount; i++) {
                    const SpriteFrame *mask = assets_.blendMask(mode, maskIds[i], slope);
                    if (!mask) continue;
                    Quad maskQ{0, 0, (float)mask->w, (float)mask->h,
                               mask->u, mask->v, mask->u + mask->w, mask->v + mask->h};
                    r.drawMasked(overlay->tex, overlayQ, mask->tex, maskQ);
                }
            }
        }
    }

    // Selection footprints sit on the terrain so foreground sprites occlude them.
    for (const Object &object : objects_) {
        if (!object.active || object.hidden || !object.draw || !object.selected)
            continue;
        float screenX, screenY;
        objectScreenPosition(object, screenW, screenH, screenX, screenY);
        const float sx = screenX / zoom_, sy = screenY / zoom_;
        if (object.unit->type != dat::UT_Building) {
            if (!selectionRing) continue;
            const float radiusX =
                std::max(14.0f, object.unit->outlineSize[0] * 96.0f);
            const float radiusY =
                std::max(6.0f, object.unit->outlineSize[1] * 48.0f);
            const float halfW = radiusX * 64.0f / 59.0f;
            const float halfH = radiusY * 32.0f / 26.0f;
            r.draw(selectionRing,
                   {sx - halfW, sy - halfH, halfW * 2.0f, halfH * 2.0f,
                    0, 0, 128, 64});
            continue;
        }

        auto drawEdge = [&](const std::array<float, 2> &from,
                            const std::array<float, 2> &to,
                            float thickness, uint8_t color) {
            const float dx = to[0] - from[0], dy = to[1] - from[1];
            const int steps =
                std::max(1, (int)std::ceil(std::max(std::abs(dx), std::abs(dy))));
            for (int step = 0; step <= steps; step++) {
                const float amount = (float)step / steps;
                r.fillRect(from[0] + dx * amount - thickness * 0.5f,
                           from[1] + dy * amount - thickness * 0.5f,
                           thickness, thickness, color, color, color, 255);
            }
        };
        auto drawFootprint = [&](float centerX, float centerY,
                                 const dat::Unit &unit) {
            const float halfX =
                std::max(0.5f, unit.collisionSize[0]);
            const float halfY =
                std::max(0.5f, unit.collisionSize[1]);
            const std::array<std::array<float, 2>, 4> points = {{
                {centerX + (-halfX + halfY) * kTileHalfW,
                 centerY + (-halfX - halfY) * kTileHalfH},
                {centerX + (halfX + halfY) * kTileHalfW,
                 centerY + (halfX - halfY) * kTileHalfH},
                {centerX + (halfX - halfY) * kTileHalfW,
                 centerY + (halfX + halfY) * kTileHalfH},
                {centerX + (-halfX - halfY) * kTileHalfW,
                 centerY + (-halfX + halfY) * kTileHalfH},
            }};
            for (size_t point = 0; point < points.size(); point++)
                drawEdge(
                    points[point],
                    points[(point + 1) % points.size()],
                    3.0f / zoom_, 0);
            for (size_t point = 0; point < points.size(); point++)
                drawEdge(
                    points[point],
                    points[(point + 1) % points.size()],
                    1.5f / zoom_, 255);
        };
        const dat::Unit *footprintUnit =
            object.gate && object.gateClosedUnit
                ? object.gateClosedUnit
                : object.unit;
        drawFootprint(sx, sy, *footprintUnit);
        if (object.gate) {
            const int civilization =
                civilizationForPlayer(object.player);
            for (const dat::BuildingAnnex &annex :
                 footprintUnit->annexes) {
                const dat::Unit *part =
                    findUnit(civilization, annex.unitId);
                if (!part) continue;
                const float dx = annex.misplacementY;
                const float dy = -annex.misplacementX;
                drawFootprint(
                    sx + (dx - dy) * kTileHalfW,
                    sy + (dx + dy) * kTileHalfH,
                    *part);
            }
        }
    }

    if (placementUnit_) {
        float worldX = 0, worldY = 0;
        screenToWorld(cursorX_, cursorY_, screenW, screenH,
                      worldX, worldY);
        snapAdjacentBuildingPosition(
            *placementUnit_, worldX, worldY);
        Object candidate;
        candidate.unit = placementUnit_;
        candidate.player = localPlayer_;
        bool valid =
            positionPassable(
                candidate, worldX, worldY, true);
        for (const dat::ResourceCost &cost :
             placementUnit_->costs)
            if (cost.flag && cost.type >= 0 &&
                cost.amount > 0 &&
                resource(localPlayer_, cost.type) +
                        0.001f <
                    cost.amount) {
                valid = false;
                break;
            }
        float sx = 0, sy = 0;
        toScreen(worldX, worldY, sx, sy);
        sy -= elevationAt(worldX, worldY) *
              assets_.dat().terrainBlock.elevHeight;
        sx -= ox;
        sy -= oy;
        const int graphic =
            placementUnit_->constructionGraphic >= 0
                ? placementUnit_->constructionGraphic
                : placementUnit_->standingGraphic[0];
        drawGraphic(
            r, graphic, sx, sy, 0, 0, localPlayer_, 0,
            0, false, viewW, viewH);
        if (isPowerSource(candidate) ||
            isShieldGenerator(candidate))
            drawGraphic(
                r,
                isPowerSource(candidate)
                    ? kPowerRadiusGraphic
                    : kShieldRadiusGraphic,
                sx, sy, 0, 0, localPlayer_, 0, 0,
                false, viewW, viewH);
        const float halfX =
            std::max(0.5f,
                     placementUnit_->collisionSize[0]);
        const float halfY =
            std::max(0.5f,
                     placementUnit_->collisionSize[1]);
        const std::array<std::array<float, 2>, 4>
            footprint = {{
                {sx + (-halfX + halfY) * kTileHalfW,
                 sy + (-halfX - halfY) * kTileHalfH},
                {sx + (halfX + halfY) * kTileHalfW,
                 sy + (halfX - halfY) * kTileHalfH},
                {sx + (halfX - halfY) * kTileHalfW,
                 sy + (halfX + halfY) * kTileHalfH},
                {sx + (-halfX - halfY) * kTileHalfW,
                 sy + (-halfX + halfY) * kTileHalfH},
            }};
        const float flash =
            0.5f +
            0.5f * std::sin(selectionClickAge_ * 9.0f);
        const uint8_t red = valid ? 40 : 255;
        const uint8_t green = valid ? 255 : 35;
        const uint8_t alpha =
            (uint8_t)std::lround(100.0f +
                                 flash * 155.0f);
        auto drawPlacementEdge =
            [&](const std::array<float, 2> &from,
                const std::array<float, 2> &to,
                float thickness) {
                const float dx = to[0] - from[0];
                const float dy = to[1] - from[1];
                const int steps = std::max(
                    1, (int)std::ceil(
                           std::max(std::abs(dx),
                                    std::abs(dy))));
                for (int step = 0; step <= steps;
                     step++) {
                    const float amount =
                        (float)step / steps;
                    r.fillRect(
                        from[0] + dx * amount -
                            thickness * 0.5f,
                        from[1] + dy * amount -
                            thickness * 0.5f,
                        thickness, thickness,
                        red, green, 30, alpha);
                }
            };
        for (size_t point = 0;
             point < footprint.size(); point++)
            drawPlacementEdge(
                footprint[point],
                footprint[(point + 1) %
                          footprint.size()],
                (2.0f + flash * 3.0f) / zoom_);
        if (!valid) {
            drawPlacementEdge(
                footprint[0], footprint[2],
                (2.0f + flash * 2.0f) / zoom_);
            drawPlacementEdge(
                footprint[1], footprint[3],
                (2.0f + flash * 2.0f) / zoom_);
        }
        r.fillRect(
            sx - 28.0f / zoom_, sy - 4.0f / zoom_,
            56.0f / zoom_, 4.0f / zoom_,
            red, green, 30, alpha);
    }

    // --- objects -------------------------------------------------------
    std::stable_sort(g_draws.begin(), g_draws.end(),
                     [](const SpriteDraw &a, const SpriteDraw &b) { return a.key < b.key; });
    for (const SpriteDraw &d : g_draws) r.draw(d.tex, d.q);
    stats_.sprites = (int)g_draws.size();

    for (size_t unitIndex = 0;
         unitIndex < g_draws.size(); unitIndex++) {
        const SpriteDraw &unit = g_draws[unitIndex];
        if (!unit.outlineCandidate || !unit.outlineTex ||
            unit.ownerId == 0)
            continue;
        const Object *object = findObject(unit.ownerId);
        if (!object || !object->active || object->hidden)
            continue;
        const Rgba &color =
            assets_.palette()[(uint8_t)(
                playerColorBase(object->player) + 4)];
        for (size_t foregroundIndex = unitIndex + 1;
             foregroundIndex < g_draws.size();
             foregroundIndex++) {
            const SpriteDraw &foreground =
                g_draws[foregroundIndex];
            if (!foreground.occludes ||
                foreground.ownerId == unit.ownerId ||
                foreground.tex->alphaOnly)
                continue;
            const float left =
                std::max(unit.q.x, foreground.q.x);
            const float top =
                std::max(unit.q.y, foreground.q.y);
            const float right =
                std::min(unit.q.x + unit.q.w,
                         foreground.q.x + foreground.q.w);
            const float bottom =
                std::min(unit.q.y + unit.q.h,
                         foreground.q.y + foreground.q.h);
            if (left >= right || top >= bottom)
                continue;
            const float spread = 1.35f / zoom_;
            for (int offsetY = -1;
                 offsetY <= 1; offsetY++)
                for (int offsetX = -1;
                     offsetX <= 1; offsetX++) {
                    Quad outline = unit.q;
                    outline.x +=
                        offsetX * spread;
                    outline.y +=
                        offsetY * spread;
                    const float shiftedLeft =
                        std::max(
                            outline.x,
                            foreground.q.x);
                    const float shiftedTop =
                        std::max(
                            outline.y,
                            foreground.q.y);
                    const float shiftedRight =
                        std::min(
                            outline.x + outline.w,
                            foreground.q.x +
                                foreground.q.w);
                    const float shiftedBottom =
                        std::min(
                            outline.y + outline.h,
                            foreground.q.y +
                                foreground.q.h);
                    if (shiftedLeft >= shiftedRight ||
                        shiftedTop >= shiftedBottom)
                        continue;
                    r.drawMaskedTinted(
                        unit.outlineTex,
                        clippedQuad(
                            outline, shiftedLeft,
                            shiftedTop, shiftedRight,
                            shiftedBottom),
                        foreground.tex,
                        clippedQuad(
                            foreground.q,
                            shiftedLeft,
                            shiftedTop, shiftedRight,
                            shiftedBottom),
                        color.r, color.g, color.b,
                        offsetX == 0 &&
                                offsetY == 0
                            ? 255
                            : 220);
                }
        }
    }

    for (const auto &point : blasterGlowPoints) {
        const float glowWidth = 6.0f / zoom_;
        const float glowHeight = 4.0f / zoom_;
        r.fillRect(point[0] - glowWidth * 0.5f,
                   point[1] - glowHeight * 0.5f,
                   glowWidth, glowHeight, 255, 35, 8, 170);
        const float coreWidth = 3.0f / zoom_;
        const float coreHeight = 2.0f / zoom_;
        r.fillRect(point[0] - coreWidth * 0.5f,
                   point[1] - coreHeight * 0.5f,
                   coreWidth, coreHeight, 255, 230, 135, 255);
    }

    for (const Object &object : objects_) {
        if (!object.active || object.hidden || !object.draw || object.flashTime <= 0) continue;
        float sx, sy;
        toScreen(object.x, object.y, sx, sy);
        sy -= elevationAt(object.x, object.y) * assets_.dat().terrainBlock.elevHeight;
        sx -= ox;
        sy -= oy;
        const float pulse = 18.0f + std::sin(object.flashTime * 8.0f) * 4.0f;
        r.fillRect(sx - pulse, sy - pulse, pulse * 2, 2 / zoom_, 255, 255, 0, 220);
        r.fillRect(sx - pulse, sy + pulse, pulse * 2, 2 / zoom_, 255, 255, 0, 220);
        r.fillRect(sx - pulse, sy - pulse, 2 / zoom_, pulse * 2, 255, 255, 0, 220);
        r.fillRect(sx + pulse, sy - pulse, 2 / zoom_, pulse * 2, 255, 255, 0, 220);
    }

    for (const Object &object : objects_) {
        if (!object.active || object.hidden || !object.draw || !object.selected) continue;
        float screenX, screenY;
        objectScreenPosition(object, screenW, screenH, screenX, screenY);
        const float sx = screenX / zoom_, sy = screenY / zoom_;
        const float radiusX = std::max(14.0f, object.unit->outlineSize[0] * 96.0f);
        const float barWidth = std::min(42.0f, std::max(26.0f, radiusX)) / zoom_;
        const float barHeight = 3.0f / zoom_;
        const float barY = sy -
            (std::max(1.0f, object.unit->outlineSize[2]) * 24.0f + 8.0f);
        const float health =
            std::max(0.0f, std::min(1.0f, object.hitPoints / object.maxHitPoints));
        r.fillRect(sx - barWidth * 0.5f - 1.0f / zoom_, barY - 1.0f / zoom_,
                   barWidth + 2.0f / zoom_, barHeight + 2.0f / zoom_,
                   0, 0, 0, 230);
        r.fillRect(sx - barWidth * 0.5f, barY, barWidth, barHeight,
                   185, 32, 28, 255);
        r.fillRect(sx - barWidth * 0.5f, barY, barWidth * health, barHeight,
                   20, 220, 55, 255);
        if (object.maxShieldPoints > 0) {
            const float shield = std::max(
                0.0f,
                std::min(
                    1.0f,
                    object.shieldPoints /
                        object.maxShieldPoints));
            const float shieldY =
                barY + barHeight + 2.0f / zoom_;
            r.fillRect(
                sx - barWidth * 0.5f -
                    1.0f / zoom_,
                shieldY - 1.0f / zoom_,
                barWidth + 2.0f / zoom_,
                barHeight + 2.0f / zoom_,
                0, 0, 0, 230);
            r.fillRect(
                sx - barWidth * 0.5f,
                shieldY, barWidth, barHeight,
                117, 72, 12, 255);
            r.fillRect(
                sx - barWidth * 0.5f,
                shieldY, barWidth * shield,
                barHeight, 244, 190, 43, 255);
        }
    }

    if (commandMarkerTime_ > 0) {
        float sx, sy;
        toScreen(commandMarkerX_, commandMarkerY_, sx, sy);
        sy -= elevationAt(commandMarkerX_, commandMarkerY_) *
              assets_.dat().terrainBlock.elevHeight;
        sx -= ox;
        sy -= oy;
        if (cursors && kCursorCommand < cursors->frames.size()) {
            const SpriteFrame &frame = cursors->frames[kCursorCommand];
            const float width = frame.w / zoom_, height = frame.h / zoom_;
            r.draw(frame.tex, {sx - width * 0.5f, sy - height * 0.5f, width, height,
                               frame.u, frame.v, frame.u + frame.w, frame.v + frame.h});
        } else {
            const float radius = (10.0f + commandMarkerTime_ * 12.0f) / zoom_;
            const float line = 2.0f / zoom_;
            r.fillRect(sx - radius, sy - line * 0.5f, radius * 2, line,
                       230, 45, 25, 230);
            r.fillRect(sx - line * 0.5f, sy - radius, line, radius * 2,
                       230, 45, 25, 230);
        }
    }

    if (debug_) {
        // Coarse debug minimap; one quad per full tile is prohibitively
        // expensive on Vita for large maps.
        constexpr int step = 8;
        const float s = 2.0f / zoom_;
        const float mx = viewW - mapSize_ * s - 8 / zoom_, my = 8 / zoom_;
        r.fillRect(mx - 2 / zoom_, my - 2 / zoom_, mapSize_ * s + 4 / zoom_, mapSize_ * s + 4 / zoom_, 0, 0, 0, 180);
        for (int ty = 0; ty < mapSize_; ty += step)
            for (int tx = 0; tx < mapSize_; tx += step) {
                const dat::Terrain &t = terrains[terrainAt(tx, ty)];
                r.fillRect(mx + tx * s, my + ty * s, std::min(step, mapSize_ - tx) * s,
                           std::min(step, mapSize_ - ty) * s,
                           t.colors[0] ? assets_.palette()[t.colors[0]].r : 60,
                           t.colors[0] ? assets_.palette()[t.colors[0]].g : 120,
                           t.colors[0] ? assets_.palette()[t.colors[0]].b : 60, 255);
            }
    }

    if (panelObject) {
        const float invZoom = 1.0f / zoom_;
        const float panelHeight = 112.0f;
        const float panelX = 0;
        const float panelY = (screenH - panelHeight) * invZoom;
        const float panelW = screenW * invZoom;
        const float portraitX = 14.0f * invZoom;
        const float portraitY = panelY + 12.0f * invZoom;
        const float portraitW = 86.0f * invZoom;
        const float portraitH = 86.0f * invZoom;
        r.fillRect(panelX, panelY, panelW, panelHeight * invZoom,
                   5, 8, 16, 238);
        r.fillRect(panelX, panelY, panelW, 3.0f * invZoom,
                   196, 188, 145, 255);
        if (panelSelectionCount > 1) {
            static constexpr FormationType formations[] = {
                FormationType::Line, FormationType::Box,
                FormationType::Staggered, FormationType::Flank,
            };
            static constexpr const char *formationLabels[] = {
                "LINE", "BOX", "STAG", "FLANK",
            };
            for (size_t index = 0; index < std::size(formations); index++) {
                const float x =
                    (kFormationButtonX +
                     (index % 2) * kFormationButtonStepX) *
                    invZoom;
                const float y =
                    panelY +
                    (kFormationButtonY +
                     (index / 2) * kFormationButtonStepY) *
                        invZoom;
                const bool active = selectedFormation_ == formations[index];
                r.fillRect(x, y, kFormationButtonSize * invZoom,
                           kFormationButtonSize * invZoom,
                           active ? 38 : 16, active ? 76 : 27,
                           active ? 104 : 40, 255);
                r.fillRect(x, y, kFormationButtonSize * invZoom,
                           2.0f * invZoom,
                           active ? 130 : 74, active ? 220 : 91,
                           active ? 255 : 105, 255);
                drawBitmapText(
                    r, {formationLabels[index]},
                    x + 3.0f * invZoom, y + 12.0f * invZoom,
                    0.72f * invZoom,
                    active ? 225 : 160, active ? 245 : 180,
                    active ? 255 : 190);
            }

            const float commandsX =
                std::max(500.0f, screenW - 300.0f);
            const int columns = std::max(
                1, (int)((commandsX - kGroupPortraitX - 8.0f) /
                         kGroupPortraitStepX));
            size_t iconIndex = 0;
            for (const Object *object : panelSelection) {
                const int column =
                    (int)(iconIndex % (size_t)columns);
                const int row =
                    (int)(iconIndex / (size_t)columns);
                if (row >= 2) break;
                const float x =
                    (kGroupPortraitX +
                     column * kGroupPortraitStepX) *
                    invZoom;
                const float y =
                    panelY +
                    (kGroupPortraitY +
                     row * kGroupPortraitStepY) *
                        invZoom;
                r.fillRect(x, y, kGroupPortraitSize * invZoom,
                           kGroupPortraitSize * invZoom,
                           17, 23, 34, 255);
                r.fillRect(x, y, kGroupPortraitSize * invZoom,
                           1.0f * invZoom, 107, 126, 140, 255);

                const int civilization =
                    civilizationForPlayer(object->player);
                const int iconSet =
                    civilization >= 0 &&
                            (size_t)civilization <
                                assets_.dat().civs.size()
                        ? assets_.dat()
                              .civs[(size_t)civilization]
                              .iconSet
                        : 1;
                const int iconSlpBase =
                    object->unit->type == dat::UT_Building
                        ? kBuildingIconSlpBase
                        : kUnitIconSlpBase;
                const SpriteSheet *icons = assets_.interfaceSheet(
                    iconSlpBase + std::max(1, iconSet) - 1);
                const SpriteFrame *portrait =
                    icons && object->unit->iconId >= 0 &&
                            (size_t)object->unit->iconId <
                                icons->frames.size()
                        ? &icons->frames[(size_t)object->unit->iconId]
                        : nullptr;
                if (portrait && portrait->w > 0 &&
                    portrait->h > 0) {
                    const float maxSize = 31.0f * invZoom;
                    const float scale =
                        std::min(maxSize / portrait->w,
                                 maxSize / portrait->h);
                    const float width = portrait->w * scale;
                    const float height = portrait->h * scale;
                    r.draw(
                        portrait->tex,
                        {x + (kGroupPortraitSize * invZoom - width) *
                                 0.5f,
                         y + (33.0f * invZoom - height) * 0.5f,
                         width, height,
                         portrait->u, portrait->v,
                         portrait->u + portrait->w,
                         portrait->v + portrait->h});
                }
                const float health = std::max(
                    0.0f,
                    std::min(1.0f,
                             object->hitPoints /
                                 std::max(1.0f, object->maxHitPoints)));
                const float barY = y + 32.0f * invZoom;
                r.fillRect(x + 1.0f * invZoom, barY,
                           34.0f * invZoom, 4.0f * invZoom,
                           150, 24, 24, 255);
                r.fillRect(x + 1.0f * invZoom, barY,
                           34.0f * health * invZoom,
                           2.0f * invZoom, 22, 210, 55, 255);
                if (object->maxShieldPoints > 0) {
                    const float shield = std::max(
                        0.0f,
                        std::min(
                            1.0f,
                            object->shieldPoints /
                                object->maxShieldPoints));
                    r.fillRect(
                        x + 1.0f * invZoom,
                        barY + 2.0f * invZoom,
                        34.0f * invZoom,
                        2.0f * invZoom,
                        117, 72, 12, 255);
                    r.fillRect(
                        x + 1.0f * invZoom,
                        barY + 2.0f * invZoom,
                        34.0f * shield * invZoom,
                        2.0f * invZoom,
                        244, 190, 43, 255);
                } else {
                    r.fillRect(
                        x + 1.0f * invZoom,
                        barY + 2.0f * invZoom,
                        34.0f * health * invZoom,
                        2.0f * invZoom,
                        22, 210, 55, 255);
                }
                iconIndex++;
            }
        } else {
        r.fillRect(portraitX, portraitY, portraitW, portraitH,
                   18, 25, 38, 255);
        r.fillRect(portraitX, portraitY, portraitW, 2.0f * invZoom,
                   104, 119, 132, 255);
        r.fillRect(portraitX, portraitY + portraitH - 2.0f * invZoom,
                   portraitW, 2.0f * invZoom, 104, 119, 132, 255);
        r.fillRect(portraitX, portraitY, 2.0f * invZoom, portraitH,
                   104, 119, 132, 255);
        r.fillRect(portraitX + portraitW - 2.0f * invZoom, portraitY,
                   2.0f * invZoom, portraitH, 104, 119, 132, 255);
        if (panelPortrait && panelPortrait->w > 0 && panelPortrait->h > 0) {
            const float maxPortraitW = 76.0f * invZoom;
            const float maxPortraitH = 76.0f * invZoom;
            const float portraitScale =
                std::min(maxPortraitW / panelPortrait->w,
                         maxPortraitH / panelPortrait->h);
            const float width = panelPortrait->w * portraitScale;
            const float height = panelPortrait->h * portraitScale;
            const float x = portraitX + (portraitW - width) * 0.5f;
            const float y = portraitY + (portraitH - height) * 0.5f;
            r.draw(panelPortrait->tex,
                   {x, y, width, height,
                    panelPortraitFlipped ? panelPortrait->u + panelPortrait->w
                                         : panelPortrait->u,
                    panelPortrait->v,
                    panelPortraitFlipped ? panelPortrait->u
                                         : panelPortrait->u + panelPortrait->w,
                    panelPortrait->v + panelPortrait->h});
        } else {
            drawBitmapText(r, {"?"}, portraitX + 34.0f * invZoom,
                           portraitY + 29.0f * invZoom, 3.0f * invZoom,
                           150, 160, 170);
        }

        std::string title;
        if (panelMixedUnits)
            title = std::to_string(panelSelectionCount) + " UNITS SELECTED";
        else {
            title =
                panelObject->state == State::Repair &&
                        isWorker(*panelObject)
                    ? "Repairing"
                    : panelObject->state == State::Build &&
                        isWorker(*panelObject)
                    ? "Builder"
                    : isWorker(*panelObject) &&
                              (panelObject->gatherTargetId ||
                               panelObject->carriedAmount > 0)
                          ? panelObject->carriedResourceType == 0
                                ? "Food Gatherer"
                                : panelObject->carriedResourceType == 1
                                      ? "Carbon Collector"
                                      : panelObject->carriedResourceType == 2
                                            ? "Ore Miner"
                                            : panelObject->carriedResourceType == 3
                                                  ? "Nova Collector"
                                                  : "Gatherer"
                    : unitDisplayName(*panelObject->unit);
            if (panelObject->player > 0)
                title += " (" +
                         std::string(
                             1,
                             factionAbbreviation(
                                 civilizationForPlayer(
                                     panelObject->player))) +
                         ")";
            if (panelObject->gate &&
                panelObject->locked)
                title += " (Locked)";
            if (panelSelectionCount > 1)
                title += " X" + std::to_string(panelSelectionCount);
        }
        if (title.size() > 40) title.resize(40);
        const float infoX = 116.0f * invZoom;
        drawBitmapText(r, {title}, infoX, panelY + 11.0f * invZoom,
                       1.8f * invZoom, 238, 231, 190);

        const int hitPoints = std::max(0, (int)std::lround(panelHitPoints));
        const int maxHitPoints =
            std::max(1, (int)std::lround(panelMaxHitPoints));
        drawBitmapText(r,
                       {"HP " + std::to_string(hitPoints) + " / " +
                        std::to_string(maxHitPoints)},
                       infoX, panelY + 33.0f * invZoom, 1.25f * invZoom);
        if (panelMaxShieldPoints > 0) {
            const int shieldPoints =
                std::max(
                    0,
                    (int)std::lround(
                        panelShieldPoints));
            const int maxShieldPoints =
                std::max(
                    1,
                    (int)std::lround(
                        panelMaxShieldPoints));
            drawBitmapText(
                r,
                {"SP " +
                 std::to_string(shieldPoints) +
                 " / " +
                 std::to_string(maxShieldPoints)},
                infoX + 142.0f * invZoom,
                panelY + 33.0f * invZoom,
                1.1f * invZoom, 244, 190, 43);
        }
        const float health = std::max(
            0.0f, std::min(1.0f, panelHitPoints / panelMaxHitPoints));
        const float healthBarY = panelY + 47.0f * invZoom;
        const float healthBarW = 270.0f * invZoom;
        r.fillRect(infoX, healthBarY, healthBarW, 10.0f * invZoom,
                   0, 0, 0, 255);
        const float healthFillW = healthBarW - 2.0f * invZoom;
        r.fillRect(infoX + 1.0f * invZoom, healthBarY + 1.0f * invZoom,
                   healthFillW, 8.0f * invZoom, 185, 32, 28, 255);
        r.fillRect(infoX + 1.0f * invZoom, healthBarY + 1.0f * invZoom,
                   healthFillW * health, 8.0f * invZoom, 20, 205, 45, 255);
        if (panelMaxShieldPoints > 0) {
            const float shield = std::max(
                0.0f,
                std::min(
                    1.0f,
                    panelShieldPoints /
                        panelMaxShieldPoints));
            const float shieldBarY =
                healthBarY + 11.0f * invZoom;
            r.fillRect(
                infoX, shieldBarY,
                healthBarW, 6.0f * invZoom,
                0, 0, 0, 255);
            r.fillRect(
                infoX + 1.0f * invZoom,
                shieldBarY + 1.0f * invZoom,
                healthFillW, 4.0f * invZoom,
                117, 72, 12, 255);
            r.fillRect(
                infoX + 1.0f * invZoom,
                shieldBarY + 1.0f * invZoom,
                healthFillW * shield,
                4.0f * invZoom,
                244, 190, 43, 255);
        }

        std::string combatLine;
        if (panelMixedUnits) {
            bool hasAttack = false;
            bool hasArmor = false;
            bool hasRange = false;
            for (const Object *object :
                 panelSelection) {
                hasAttack =
                    hasAttack ||
                    canAttack(*object);
                hasArmor =
                    hasArmor ||
                    object->unit
                            ->displayedMeleeArmour >
                        0;
                hasRange =
                    hasRange ||
                    (canAttack(*object) &&
                     std::max(
                         object->unit
                             ->displayedRange,
                         object->unit->maxRange) >
                         0);
            }
            if (hasAttack)
                combatLine = "ATTACK --";
            if (hasArmor)
                combatLine +=
                    (combatLine.empty()
                         ? ""
                         : "   ") +
                    std::string("ARMOR --");
            if (hasRange)
                combatLine +=
                    (combatLine.empty()
                         ? ""
                         : "   ") +
                    std::string("RANGE --");
        } else {
            const dat::Unit &unit = *panelObject->unit;
            const float range =
                unit.displayedRange > 0 ? unit.displayedRange : unit.maxRange;
            if (canAttack(*panelObject))
                combatLine =
                    "ATTACK " +
                    std::to_string(
                        std::max(
                            0,
                            (int)unit
                                .displayedAttack));
            if (unit.displayedMeleeArmour > 0)
                combatLine +=
                    (combatLine.empty()
                         ? ""
                         : "   ") +
                    std::string("ARMOR ") +
                    std::to_string(
                        (int)unit
                            .displayedMeleeArmour);
            if (canAttack(*panelObject) &&
                range > 0)
                combatLine +=
                    (combatLine.empty()
                         ? ""
                         : "   ") +
                    std::string("RANGE ") +
                    displayDecimal(range);
        }
        if (!panelMixedUnits &&
            isWorker(*panelObject) &&
            panelObject->carriedAmount > 0.001f) {
            static constexpr const char
                *resourceNames[] = {
                    "FOOD", "CARBON", "ORE", "NOVA",
                };
            const dat::Unit *gatherer =
                gathererUnit(*panelObject);
            const int resourceType =
                panelObject->carriedResourceType;
            combatLine =
                std::string("CARRY ") +
                (resourceType >= 0 &&
                         resourceType < 4
                     ? resourceNames[
                           (size_t)resourceType]
                     : "RESOURCE") +
                " " +
                std::to_string((int)std::lround(
                    panelObject->carriedAmount)) +
                " / " +
                std::to_string(
                    gatherer
                        ? std::max<int16_t>(
                              1,
                              gatherer
                                  ->resourceCapacity)
                        : 0);
        }
        if (!combatLine.empty())
            drawBitmapText(
                r, {combatLine}, infoX,
                panelY + 67.0f * invZoom,
                1.4f * invZoom,
                190, 210, 220);

        std::string status =
            ownershipLabel(panelObject->player);
        if (panelObject->player == localPlayer_ &&
            panelObject->unit->garrisonCapacity > 0)
            status +=
                "  GARRISON " +
                std::to_string(garrisonedCount(
                    *panelObject, false)) +
                "/" +
                std::to_string(
                    panelObject->unit
                        ->garrisonCapacity);
        else if (panelObject->player == localPlayer_ &&
                 selectedAttacker)
            status +=
                std::string("  STANCE: ") +
                selectedStanceLabel();
        drawBitmapText(r, {status}, infoX, panelY + 89.0f * invZoom,
                       1.25f * invZoom, 120, 220, 255);
        if (!panelMixedUnits &&
            panelObject->unit->type ==
                dat::UT_Building) {
            auto drawBuildingStatus =
                [&](float x, size_t iconId,
                    bool active) {
                    const float y =
                        panelY + 69.0f * invZoom;
                    r.fillRect(
                        x, y, 22.0f * invZoom,
                        22.0f * invZoom,
                        active ? 18 : 54,
                        active ? 65 : 24,
                        active ? 46 : 28, 255);
                    if (commandIcons &&
                        (size_t)iconId <
                            commandIcons->frames.size()) {
                        const SpriteFrame &icon =
                            commandIcons
                                ->frames[(size_t)iconId];
                        const float scale =
                            std::min(18.0f / icon.w,
                                     18.0f / icon.h) *
                            invZoom;
                        r.draw(
                            icon.tex,
                            {x + 2.0f * invZoom,
                             y + 2.0f * invZoom,
                             icon.w * scale,
                             icon.h * scale,
                             icon.u, icon.v,
                             icon.u + icon.w,
                             icon.v + icon.h});
                    }
                    if (!active && commandIcons &&
                        !commandIcons->frames.empty()) {
                        const SpriteFrame &disabled =
                            commandIcons->frames[0];
                        const float scale =
                            std::min(
                                10.0f / disabled.w,
                                10.0f / disabled.h) *
                            invZoom;
                        r.draw(
                            disabled.tex,
                            {x + 11.0f * invZoom,
                             y + 11.0f * invZoom,
                             disabled.w * scale,
                             disabled.h * scale,
                             disabled.u, disabled.v,
                             disabled.u + disabled.w,
                             disabled.v + disabled.h});
                    }
                };
            const bool powered =
                isPowered(*panelObject);
            if (requiresPower(*panelObject) ||
                isPowerCore(*panelObject))
                drawBuildingStatus(
                    404.0f * invZoom,
                    kCommandPowerStatusIcon,
                    powered);
            const bool shielded =
                isShielded(*panelObject);
            drawBuildingStatus(
                432.0f * invZoom,
                kCommandShieldStatusIcon,
                shielded);
            if (cursorVisible_ &&
                cursorY_ >=
                    screenH -
                        kSelectionPanelHeight +
                        69.0f &&
                cursorY_ <=
                    screenH -
                        kSelectionPanelHeight +
                        91.0f) {
                std::string tooltip;
                if (cursorX_ >= 404.0f &&
                    cursorX_ <= 426.0f &&
                    (requiresPower(*panelObject) ||
                     isPowerCore(*panelObject)))
                    tooltip =
                        powered ? "POWERED"
                                : "UNPOWERED";
                else if (cursorX_ >= 432.0f &&
                         cursorX_ <= 454.0f)
                    tooltip =
                        shielded ? "SHIELDED"
                                 : "NOT SHIELDED";
                if (!tooltip.empty()) {
                    const float tooltipX =
                        390.0f * invZoom;
                    const float tooltipY =
                        panelY - 23.0f * invZoom;
                    r.fillRect(
                        tooltipX, tooltipY,
                        112.0f * invZoom,
                        21.0f * invZoom,
                        5, 8, 16, 240);
                    drawBitmapText(
                        r, {tooltip},
                        tooltipX + 6.0f * invZoom,
                        tooltipY + 5.0f * invZoom,
                        1.0f * invZoom,
                        238, 231, 190);
                }
            }
        }
        if (panelSelectionCount == 1 &&
            panelObject->player == localPlayer_ &&
            panelObject->unit->type ==
                dat::UT_Building) {
            size_t iconIndex = 0;
            for (const Object &unit : objects_) {
                if (!unit.active ||
                    unit.garrisonedInId !=
                        (int32_t)panelObject->spawnId)
                    continue;
                const int column =
                    (int)(iconIndex % 5);
                const int row =
                    (int)(iconIndex / 5);
                if (row >= 2) break;
                const float x =
                    (460.0f + column * 38.0f) *
                    invZoom;
                const float y =
                    panelY +
                    (12.0f + row * 42.0f) *
                        invZoom;
                r.fillRect(
                    x, y, 34.0f * invZoom,
                    34.0f * invZoom,
                    18, 27, 39, 255);
                r.fillRect(
                    x, y, 34.0f * invZoom,
                    2.0f * invZoom,
                    102, 184, 203, 255);
                const int civilization =
                    civilizationForPlayer(
                        unit.player);
                const int iconSet =
                    civilization >= 0 &&
                            (size_t)civilization <
                                assets_.dat()
                                    .civs.size()
                        ? std::max(
                              1,
                              (int)assets_.dat()
                                  .civs[(size_t)civilization]
                                  .iconSet)
                        : 1;
                const SpriteSheet *icons =
                    assets_.interfaceSheet(
                        kUnitIconSlpBase +
                        iconSet - 1);
                const SpriteFrame *icon =
                    icons && unit.unit->iconId >= 0 &&
                            (size_t)unit.unit->iconId <
                                icons->frames.size()
                        ? &icons->frames[
                              (size_t)unit.unit->iconId]
                        : nullptr;
                if (icon) {
                    const float scale =
                        std::min(
                            29.0f / icon->w,
                            29.0f / icon->h) *
                        invZoom;
                    r.draw(
                        icon->tex,
                        {x + 2.5f * invZoom,
                         y + 2.5f * invZoom,
                         icon->w * scale,
                         icon->h * scale,
                         icon->u, icon->v,
                         icon->u + icon->w,
                         icon->v + icon->h});
                }
                iconIndex++;
            }
        }
        }

        const float commandsX = std::max(500.0f, screenW - 300.0f) * invZoom;
        r.fillRect(commandsX, panelY + 12.0f * invZoom,
                   (screenW * invZoom - commandsX - 14.0f * invZoom),
                   86.0f * invZoom, 14, 20, 31, 245);
        if (panelObject->player == localPlayer_) {
            const bool showGarrisonCommand =
                std::any_of(
                    panelSelection.begin(),
                    panelSelection.end(),
                    [&](const Object *object) {
                        return object && object->active &&
                               !object->hidden &&
                               garrisonCategory(*object) != 0;
                    });
            const bool showRepairCommand =
                std::any_of(
                    panelSelection.begin(),
                    panelSelection.end(),
                    [&](const Object *object) {
                        return object &&
                               isWorker(*object);
                    });
            float commandsTextX =
                commandsX + 12.0f * invZoom;
            if (showGarrisonCommand) {
                const float buttonX =
                    commandsX + 12.0f * invZoom;
                const float buttonY =
                    panelY + 18.0f * invZoom;
                const float buttonSize =
                    46.0f * invZoom;
                r.fillRect(
                    buttonX, buttonY, buttonSize,
                    buttonSize,
                    garrisonCursorActive_ ? 58 : 22,
                    garrisonCursorActive_ ? 105 : 38,
                    garrisonCursorActive_ ? 72 : 55,
                    255);
                if (commandIcons &&
                    kCommandGarrisonIcon <
                        commandIcons->frames.size()) {
                    const SpriteFrame &icon =
                        commandIcons->frames[
                            kCommandGarrisonIcon];
                    const float scale =
                        std::min(40.0f / icon.w,
                                 40.0f / icon.h) *
                        invZoom;
                    r.draw(
                        icon.tex,
                        {buttonX + 3.0f * invZoom,
                         buttonY + 3.0f * invZoom,
                         icon.w * scale,
                         icon.h * scale,
                         icon.u, icon.v,
                         icon.u + icon.w,
                         icon.v + icon.h});
                }
                drawBitmapText(
                    r, {"GARRISON"},
                    buttonX,
                    panelY + 71.0f * invZoom,
                    0.72f * invZoom,
                    165, 225, 185);
                commandsTextX =
                    commandsX + 68.0f * invZoom;
            }
            if (showRepairCommand) {
                const float buttonX =
                    commandsX + 64.0f * invZoom;
                const float buttonY =
                    panelY + 18.0f * invZoom;
                const float buttonSize =
                    46.0f * invZoom;
                r.fillRect(
                    buttonX, buttonY, buttonSize,
                    buttonSize,
                    repairCursorActive_ ? 58 : 22,
                    repairCursorActive_ ? 105 : 38,
                    repairCursorActive_ ? 72 : 55,
                    255);
                if (commandIcons &&
                    kCommandRepairIcon <
                        commandIcons->frames.size()) {
                    const SpriteFrame &icon =
                        commandIcons->frames[
                            kCommandRepairIcon];
                    const float scale =
                        std::min(
                            40.0f / icon.w,
                            40.0f / icon.h) *
                        invZoom;
                    r.draw(
                        icon.tex,
                        {buttonX + 3.0f * invZoom,
                         buttonY + 3.0f * invZoom,
                         icon.w * scale,
                         icon.h * scale,
                         icon.u, icon.v,
                         icon.u + icon.w,
                         icon.v + icon.h});
                }
                drawBitmapText(
                    r, {"REPAIR"},
                    buttonX + 4.0f * invZoom,
                    panelY + 71.0f * invZoom,
                    0.72f * invZoom,
                    165, 225, 185);
                commandsTextX =
                    std::max(
                        commandsTextX,
                        commandsX +
                            120.0f * invZoom);
            }
            {
                const float buttonX =
                    commandsX + 116.0f * invZoom;
                const float buttonY =
                    panelY + 18.0f * invZoom;
                r.fillRect(
                    buttonX, buttonY,
                    46.0f * invZoom,
                    46.0f * invZoom,
                    72, 24, 27, 255);
                if (commandIcons &&
                    kCommandDestroyIcon <
                        commandIcons->frames.size()) {
                    const SpriteFrame &icon =
                        commandIcons->frames[
                            kCommandDestroyIcon];
                    const float scale =
                        std::min(
                            40.0f / icon.w,
                            40.0f / icon.h) *
                        invZoom;
                    r.draw(
                        icon.tex,
                        {buttonX + 3.0f * invZoom,
                         buttonY + 3.0f * invZoom,
                         icon.w * scale,
                         icon.h * scale,
                         icon.u, icon.v,
                         icon.u + icon.w,
                         icon.v + icon.h});
                }
                drawBitmapText(
                    r, {"DESTROY"},
                    buttonX,
                    panelY + 71.0f * invZoom,
                    0.72f * invZoom,
                    245, 160, 150);
                commandsTextX =
                    std::max(
                        commandsTextX,
                        commandsX +
                            172.0f * invZoom);
            }
            if (selectedAttacker) {
                const float buttonX =
                    commandsX + 168.0f * invZoom;
                const float buttonY =
                    panelY + 18.0f * invZoom;
                r.fillRect(
                    buttonX, buttonY,
                    46.0f * invZoom,
                    46.0f * invZoom,
                    22, 54, 72, 255);
                // Show the active stance with its original (pressed) icon.
                if (commandIcons && !mixedAttackModes) {
                    const size_t frame = kStanceActiveIcons[
                        (size_t)selectedAttacker->attackMode & 3];
                    if (frame < commandIcons->frames.size()) {
                        const SpriteFrame &icon = commandIcons->frames[frame];
                        const float scale =
                            std::min(40.0f / icon.w, 40.0f / icon.h) * invZoom;
                        r.draw(icon.tex,
                               {buttonX + 3.0f * invZoom, buttonY + 3.0f * invZoom,
                                icon.w * scale, icon.h * scale, icon.u, icon.v,
                                icon.u + icon.w, icon.v + icon.h});
                    }
                } else {
                    drawBitmapText(r, {"MIXED"}, buttonX + 4.0f * invZoom,
                                   buttonY + 17.0f * invZoom, 1.0f * invZoom,
                                   142, 225, 245);
                }
                drawBitmapText(
                    r, {"STANCE"},
                    buttonX,
                    panelY + 71.0f * invZoom,
                    0.72f * invZoom,
                    165, 225, 235);
                commandsTextX =
                    std::max(
                        commandsTextX,
                        commandsX +
                            224.0f * invZoom);
            }
            const bool showCommandHelp =
                commandsTextX <
                (screenW - 100.0f) * invZoom;
            if (panelSelectionCount > 1) {
                const std::string stance =
                    selectedAttacker
                        ? std::string("STANCE: ") +
                              selectedStanceLabel()
                        : "STANCE: N/A";
                if (commandsTextX <
                    (screenW - 70.0f) * invZoom)
                    drawBitmapText(
                        r, {stance},
                        commandsTextX,
                        panelY + 20.0f * invZoom,
                        0.9f * invZoom,
                        mixedAttackModes ? 255 : 120,
                        mixedAttackModes ? 190 : 220,
                        mixedAttackModes ? 100 : 255);
            } else if (
                showCommandHelp &&
                panelObject->unit->type ==
                    dat::UT_Building) {
                const bool hasProduction =
                    !productionOptions(*panelObject).empty() ||
                    !researchOptions(*panelObject).empty();
                const bool hasCommands =
                    panelObject->gate ||
                    garrisonedCount(
                        *panelObject, false) > 0;
                if (hasProduction || hasCommands) {
                    drawBitmapText(
                        r,
                        {panelObject->gate
                             ? "TRIANGLE: GATE COMMANDS"
                             : hasProduction && hasCommands
                                   ? "TRIANGLE: PRODUCTION / EJECT"
                                   : hasProduction
                                         ? "TRIANGLE: PRODUCTION"
                                         : "TRIANGLE: EJECT"},
                        commandsTextX,
                        panelY + 24.0f * invZoom,
                        1.25f * invZoom);
                }
                drawBitmapText(r, {"X: SELECT"},
                               commandsTextX,
                               panelY +
                                   (hasProduction || hasCommands
                                        ? 50.0f
                                        : 34.0f) *
                                       invZoom,
                               1.2f * invZoom);
            } else if (showCommandHelp) {
                drawBitmapText(r, {"O: MOVE / ATTACK"},
                               commandsTextX,
                               panelY + 24.0f * invZoom,
                               1.35f * invZoom);
                drawBitmapText(r, {"TRIANGLE: CHANGE STANCE"},
                               commandsTextX,
                               panelY + 48.0f * invZoom,
                               1.2f * invZoom);
                drawBitmapText(r, {"X: SELECT   SQUARE: BOX"},
                               commandsTextX,
                               panelY + 71.0f * invZoom,
                               1.15f * invZoom,
                               175, 190, 205);
            }
        } else {
            std::string relationship =
                ownershipLabel(panelObject->player);
            const size_t separator =
                relationship.find_first_of(" :");
            if (separator != std::string::npos)
                relationship.resize(separator);
            const bool enemy =
                relationship == "ENEMY";
            const bool ally =
                relationship == "ALLY";
            drawBitmapText(r,
                           {relationship +
                            " UNIT / BUILDING"},
                           commandsX + 12.0f * invZoom,
                           panelY + 30.0f * invZoom, 1.35f * invZoom,
                           enemy ? 255 : ally ? 120 : 235,
                           enemy ? 120 : ally ? 230 : 205,
                           enemy ? 105 : ally ? 150 : 120);
            drawBitmapText(r, {"X: INSPECT"},
                           commandsX + 12.0f * invZoom,
                           panelY + 59.0f * invZoom, 1.2f * invZoom,
                           175, 190, 205);
        }
    }

    if (localPlayer_ > 0) {
        static constexpr int resourceIds[] = {0, 1, 3, 2};
        static constexpr size_t resourceFrames[] = {2, 0, 1, 3};
        const SpriteSheet *resourceIcons =
            assets_.interfaceSheet(50732);
        const float invZoom = 1.0f / zoom_;
        const float fieldW = 94.0f * invZoom;
        const float fieldH = 32.0f * invZoom;
        const float startX =
            (screenW - 8.0f -
             (std::size(resourceIds) + 2) * 94.0f) *
            invZoom;
        const float y = 6.0f * invZoom;
        int techLevel = 1;
        for (int technologyId = 1;
             technologyId <= 3;
             technologyId++)
            if (researchedTechs_[
                    (size_t)localPlayer_]
                    .count(technologyId))
                techLevel = technologyId + 1;
        float techProgress = -1.0f;
        for (const Object &building : objects_) {
            if (!building.active ||
                building.player != localPlayer_ ||
                building.productionQueue.empty())
                continue;
            const ProductionItem &item =
                building.productionQueue.front();
            if (item.technologyId < 1 ||
                item.technologyId > 3 ||
                item.duration <= 0.0f)
                continue;
            techProgress = std::max(
                0.0f,
                std::min(
                    1.0f,
                    1.0f -
                        building.productionRemaining /
                            item.duration));
            break;
        }
        size_t population = 0;
        for (const Object &object : objects_)
            if (object.active &&
                object.player == localPlayer_ &&
                object.unit->type >= dat::UT_Combatant &&
                object.unit->type != dat::UT_Building)
                population++;
        const int populationLimit =
            localPlayer_ > 0 &&
                    (size_t)localPlayer_ <= players_.size()
                ? std::max(
                      0, (int)std::lround(
                             players_[(size_t)localPlayer_ -
                                      1]
                                 .populationLimit))
                : 0;
        r.fillRect(startX, y,
                   fieldW - 3.0f * invZoom, fieldH,
                   5, 8, 16, 225);
        r.fillRect(startX, y,
                   fieldW - 3.0f * invZoom,
                   2.0f * invZoom,
                   110, 122, 130, 255);
        drawBitmapText(
            r,
            {"TECH " + std::to_string(techLevel)},
            startX + 8.0f * invZoom,
            y + 8.0f * invZoom,
            1.05f * invZoom, 238, 231, 190);
        if (techProgress >= 0.0f) {
            r.fillRect(
                startX + 3.0f * invZoom,
                y + 26.0f * invZoom,
                (fieldW - 9.0f * invZoom),
                3.0f * invZoom,
                34, 48, 62, 255);
            r.fillRect(
                startX + 3.0f * invZoom,
                y + 26.0f * invZoom,
                (fieldW - 9.0f * invZoom) *
                    techProgress,
                3.0f * invZoom,
                93, 198, 255, 255);
        }
        const float populationX =
            startX + fieldW;
        r.fillRect(populationX, y,
                   fieldW - 3.0f * invZoom, fieldH,
                   5, 8, 16, 225);
        r.fillRect(populationX, y,
                   fieldW - 3.0f * invZoom,
                   2.0f * invZoom,
                   110, 122, 130, 255);
        drawBitmapText(
            r,
            {"POP " + std::to_string(population) +
             "/" + std::to_string(populationLimit)},
            populationX + 8.0f * invZoom,
            y + 8.0f * invZoom,
            1.05f * invZoom, 238, 231, 190);
        for (size_t index = 0; index < std::size(resourceIds); index++) {
            const float x =
                startX + (index + 2) * fieldW;
            r.fillRect(x, y, fieldW - 3.0f * invZoom, fieldH,
                       5, 8, 16, 225);
            r.fillRect(x, y, fieldW - 3.0f * invZoom,
                       2.0f * invZoom, 110, 122, 130, 255);
            if (resourceIcons &&
                resourceFrames[index] <
                    resourceIcons->frames.size()) {
                const SpriteFrame &icon =
                    resourceIcons->frames[resourceFrames[index]];
                const float iconScale =
                    std::min(26.0f / std::max(1, icon.w),
                             24.0f / std::max(1, icon.h)) *
                    invZoom;
                r.draw(icon.tex,
                       {x + 4.0f * invZoom,
                        y + 4.0f * invZoom,
                        icon.w * iconScale,
                        icon.h * iconScale,
                        icon.u, icon.v,
                        icon.u + icon.w,
                        icon.v + icon.h});
            }
            const int amount = (int)std::floor(
                std::max(
                    0.0f,
                    resource(localPlayer_, resourceIds[index])));
            drawBitmapText(
                r, {std::to_string(amount)},
                x + 35.0f * invZoom, y + 8.0f * invZoom,
                1.35f * invZoom, 238, 231, 190);
        }
    }

    if (panelObject &&
        (!panelObject->productionQueue.empty() ||
         panelObject->underConstruction)) {
        const float invZoom = 1.0f / zoom_;
        const float x = 286.0f * invZoom;
        const float y =
            (screenH - kSelectionPanelHeight - 30.0f) *
            invZoom;
        float progress = 0.0f;
        std::string label;
        if (panelObject->underConstruction) {
            progress =
                panelObject->constructionTotal > 0
                    ? 1.0f -
                          panelObject
                                  ->constructionRemaining /
                              panelObject->constructionTotal
                    : 1.0f;
            label = "BUILDING";
        } else {
            const ProductionItem &item =
                panelObject->productionQueue.front();
            progress =
                item.duration > 0
                    ? 1.0f -
                          panelObject->productionRemaining /
                              item.duration
                    : 1.0f;
            if (item.unit)
                label = unitDisplayName(*item.unit);
            else if (item.technologyId >= 0)
                label = assets_.localizedString(
                    assets_.dat()
                        .techs[(size_t)item.technologyId]
                        .languageDllName);
        }
        progress = std::max(
            0.0f, std::min(1.0f, progress));
        if (label.size() > 19) label.resize(19);
        r.fillRect(x, y, 388.0f * invZoom,
                   24.0f * invZoom, 5, 8, 16, 230);
        drawBitmapText(
            r, {label}, x + 8.0f * invZoom,
            y + 7.0f * invZoom, 0.85f * invZoom,
            220, 226, 230);
        r.fillRect(
            x + 165.0f * invZoom,
            y + 6.0f * invZoom, 170.0f * invZoom,
            12.0f * invZoom, 28, 38, 48, 255);
        r.fillRect(
            x + 167.0f * invZoom,
            y + 8.0f * invZoom,
            166.0f * progress * invZoom,
            8.0f * invZoom, 66, 180, 220, 255);
        drawBitmapText(
            r,
            {std::to_string((int)std::lround(
                 progress * 100.0f)) +
             "%"},
            x + 343.0f * invZoom,
            y + 7.0f * invZoom, 0.85f * invZoom,
            180, 220, 235);
    }

    if (actionMenuOpen_) {
        Object *subject = findObject(actionMenuObjectId_);
        if (subject && subject->active) {
            const float invZoom = 1.0f / zoom_;
            const bool stanceMenu =
                actionMenuTab_ ==
                ActionMenuTab::Stances;
            const bool worker =
                !stanceMenu && isWorker(*subject);
            const std::vector<const dat::Unit *> units =
                worker
                    ? buildingOptions(*subject,
                                      actionMenuTab_)
                    : actionMenuTab_ ==
                              ActionMenuTab::Units
                          ? productionOptions(*subject)
                          : std::vector<
                                const dat::Unit *>{};
            const std::vector<int> technologies =
                !worker &&
                        actionMenuTab_ ==
                            ActionMenuTab::Research
                    ? researchOptions(*subject)
                    : std::vector<int>{};
            const bool commandOption =
                !worker &&
                actionMenuTab_ ==
                    ActionMenuTab::Commands;
            const size_t optionCount =
                stanceMenu
                    ? 4
                    : commandOption
                    ? 1
                    : units.empty()
                          ? technologies.size()
                          : units.size();
            if (optionCount > 0) {
                actionMenuSelection_ =
                    std::min(actionMenuSelection_,
                             optionCount - 1);
                if (actionMenuSelection_ <
                        actionMenuScroll_ ||
                    actionMenuSelection_ >=
                        actionMenuScroll_ +
                            kActionMenuVisibleItems)
                    actionMenuScroll_ =
                        (actionMenuSelection_ /
                         kActionMenuVisibleItems) *
                        kActionMenuVisibleItems;
            } else {
                actionMenuSelection_ = 0;
                actionMenuScroll_ = 0;
            }

            r.fillRect(
                kActionMenuX * invZoom,
                kActionMenuTop * invZoom,
                kActionMenuWidth * invZoom,
                kActionMenuHeight * invZoom,
                2, 8, 14, 248);
            r.fillRect(
                kActionMenuX * invZoom,
                kActionMenuTop * invZoom,
                kActionMenuWidth * invZoom,
                3.0f * invZoom,
                85, 210, 226, 255);
            r.fillRect(
                kActionMenuX * invZoom,
                (kActionMenuTop +
                 kActionMenuHeight - 3.0f) *
                    invZoom,
                kActionMenuWidth * invZoom,
                3.0f * invZoom,
                20, 87, 115, 255);

            std::vector<ActionMenuTab> tabs;
            std::vector<std::string> tabLabels;
            if (stanceMenu) {
                tabs = {ActionMenuTab::Stances};
                tabLabels = {"ATTACK STANCE"};
            } else if (worker) {
                tabs = {
                    ActionMenuTab::Economy,
                    ActionMenuTab::Military,
                    ActionMenuTab::Defense,
                };
                tabLabels = {
                    "L  ECONOMY", "MILITARY",
                    "DEFENSE  R",
                };
            } else {
                if (!productionOptions(*subject).empty()) {
                    tabs.push_back(
                        ActionMenuTab::Units);
                    tabLabels.push_back("L  UNITS");
                }
                if (!researchOptions(*subject).empty()) {
                    tabs.push_back(
                        ActionMenuTab::Research);
                    tabLabels.push_back("RESEARCH");
                }
                if (subject->gate ||
                    subject->unit->garrisonCapacity > 0) {
                    tabs.push_back(
                        ActionMenuTab::Commands);
                    tabLabels.push_back("COMMANDS  R");
                }
            }
            const float tabWidth =
                kActionMenuWidth /
                std::max<size_t>(1, tabs.size());
            for (size_t tab = 0;
                 tab < tabs.size(); tab++) {
                const bool active =
                    actionMenuTab_ == tabs[tab];
                const float x =
                    kActionMenuX +
                    tab * tabWidth;
                r.fillRect(
                    x * invZoom,
                    (kActionMenuTop + 7.0f) *
                        invZoom,
                    (tabWidth - 3.0f) * invZoom,
                    34.0f * invZoom,
                    active ? 15 : 5,
                    active ? 80 : 25,
                    active ? 105 : 38, 255);
                r.fillRect(
                    x * invZoom,
                    (kActionMenuTop + 7.0f) *
                        invZoom,
                    (tabWidth - 3.0f) * invZoom,
                    2.0f * invZoom,
                    active ? 118 : 38,
                    active ? 227 : 92,
                    active ? 235 : 112, 255);
                drawBitmapText(
                    r, {tabLabels[tab]},
                    (x + 12.0f) * invZoom,
                    (kActionMenuTop + 18.0f) *
                        invZoom,
                    1.5f * invZoom,
                    active ? 255 : 180,
                    active ? 255 : 205,
                    active ? 245 : 220);
            }

            const int civilization =
                civilizationForPlayer(subject->player);
            const int iconSet =
                civilization >= 0 &&
                        (size_t)civilization <
                            assets_.dat().civs.size()
                    ? std::max(
                          1,
                          (int)assets_.dat()
                              .civs[(size_t)civilization]
                              .iconSet)
                    : 1;
            const SpriteSheet *technologyIcons =
                assets_.interfaceSheet(
                    kTechnologyIconSlpBase +
                    iconSet - 1);
            const float gridX =
                kActionMenuX + 12.0f;
            for (size_t visible = 0;
                 visible <
                 kActionMenuVisibleItems;
                 visible++) {
                const size_t option =
                    actionMenuScroll_ + visible;
                const float x =
                    gridX +
                    (visible %
                     kActionMenuColumns) *
                        kActionMenuCell;
                const float y =
                    kActionMenuY +
                    (visible /
                     kActionMenuColumns) *
                        kActionMenuCell;
                const bool selected =
                    option < optionCount &&
                    option == actionMenuSelection_;
                r.fillRect(
                    x * invZoom, y * invZoom,
                    kActionMenuIconSize * invZoom,
                    kActionMenuIconSize * invZoom,
                    selected ? 34 : 8,
                    selected ? 126 : 35,
                    selected ? 151 : 50, 255);
                r.fillRect(
                    (x + 2.0f) * invZoom,
                    (y + 2.0f) * invZoom,
                    (kActionMenuIconSize - 4.0f) *
                        invZoom,
                    (kActionMenuIconSize - 4.0f) *
                        invZoom,
                    2, 8, 14, 255);
                if (option >= optionCount) continue;

                const dat::Unit *unit =
                    !units.empty()
                        ? units[option]
                        : nullptr;
                const dat::Tech *technology =
                    !technologies.empty()
                        ? &assets_.dat().techs[
                              (size_t)
                                  technologies[option]]
                        : nullptr;
                const SpriteSheet *icons =
                    unit
                        ? assets_.interfaceSheet(
                              (unit->type ==
                                       dat::UT_Building
                                   ? kBuildingIconSlpBase
                                   : kUnitIconSlpBase) +
                              iconSet - 1)
                        : technology
                              ? technologyIcons
                              : commandIcons;
                const int iconId =
                    unit
                        ? unit->iconId
                        : technology
                              ? technology->iconId
                              : stanceMenu
                                    ? (int)((size_t)subject->attackMode == option
                                                ? kStanceActiveIcons[option & 3]
                                                : kStanceIcons[option & 3])
                              : subject->gate
                                    ? (subject->locked
                                           ? (int)
                                                 kCommandUnlockGateIcon
                                           : (int)
                                                 kCommandLockGateIcon)
                                    : (int)
                                          kCommandEjectIcon;
                if (icons && iconId >= 0 &&
                    (size_t)iconId <
                        icons->frames.size()) {
                    const SpriteFrame &icon =
                        icons->frames[
                            (size_t)iconId];
                    const float scale =
                        std::min(
                            (kActionMenuIconSize -
                             8.0f) /
                                icon.w,
                            (kActionMenuIconSize -
                             8.0f) /
                                icon.h) *
                        invZoom;
                    r.draw(
                        icon.tex,
                        {(x + 4.0f) * invZoom,
                         (y + 4.0f) * invZoom,
                         icon.w * scale,
                         icon.h * scale,
                         icon.u, icon.v,
                         icon.u + icon.w,
                         icon.v + icon.h});
                }
                const bool locked =
                    technology &&
                    !technologyRequirementsMet(
                        subject->player,
                        *technology);
                if (locked) {
                    r.fillRect(
                        (x + 2.0f) * invZoom,
                        (y + 27.0f) * invZoom,
                        (kActionMenuIconSize - 4.0f) *
                            invZoom,
                        15.0f * invZoom,
                        35, 5, 8, 225);
                    drawBitmapText(
                        r, {"LOCKED"},
                        (x + 5.0f) * invZoom,
                        (y + 31.0f) * invZoom,
                        0.9f * invZoom,
                        255, 170, 150);
                }
            }

            const float detailsX =
                kActionMenuX + 286.0f;
            const float detailsY =
                kActionMenuY;
            const float detailsWidth =
                kActionMenuWidth - 298.0f;
            r.fillRect(
                detailsX * invZoom,
                detailsY * invZoom,
                detailsWidth * invZoom,
                194.0f * invZoom,
                4, 17, 25, 255);
            r.fillRect(
                detailsX * invZoom,
                detailsY * invZoom,
                3.0f * invZoom,
                194.0f * invZoom,
                48, 156, 178, 255);

            const dat::Unit *selectedUnit =
                !units.empty() &&
                        actionMenuSelection_ <
                            units.size()
                    ? units[actionMenuSelection_]
                    : nullptr;
            const int selectedTechnology =
                !technologies.empty() &&
                        actionMenuSelection_ <
                            technologies.size()
                    ? technologies[
                          actionMenuSelection_]
                    : -1;
            std::string title;
            if (stanceMenu) {
                title = assets_.localizedString(
                    kStanceNameStrings[std::min<size_t>(
                        actionMenuSelection_, 3)]);
            } else if (selectedUnit)
                title =
                    unitDisplayName(*selectedUnit);
            else if (selectedTechnology >= 0)
                title = technologyDisplayName(
                    selectedTechnology);
            else if (commandOption)
                title = subject->gate
                            ? (subject->locked
                                   ? "Unlock Gate"
                                   : "Lock Gate")
                            : "Eject All";
            if (title.size() > 38)
                title.resize(38);
            drawBitmapText(
                r, {title},
                (detailsX + 14.0f) * invZoom,
                (detailsY + 11.0f) * invZoom,
                2.0f * invZoom,
                255, 244, 190);

            std::string costText;
            static constexpr const char
                *resourceNames[] = {
                    "FOOD", "CARBON", "ORE", "NOVA",
                };
            auto appendCost =
                [&](int type, int amount,
                    bool enabled) {
                    if (!enabled || type < 0 ||
                        amount <= 0)
                        return;
                    if (!costText.empty())
                        costText += "   ";
                    costText +=
                        type < 4
                            ? resourceNames[(size_t)type]
                            : "RESOURCE " +
                                  std::to_string(type);
                    costText += " " +
                                std::to_string(amount);
                };
            int duration = 0;
            if (selectedUnit) {
                duration =
                    std::max<int16_t>(
                        0, selectedUnit->trainTime);
                for (const dat::ResourceCost &cost :
                     selectedUnit->costs)
                    appendCost(
                        cost.type, cost.amount,
                        cost.flag != 0);
            } else if (selectedTechnology >= 0) {
                const dat::Tech &technology =
                    assets_.dat().techs[
                        (size_t)selectedTechnology];
                duration =
                    std::max<int16_t>(
                        0, technology.researchTime);
                for (const dat::Tech::Cost &cost :
                     technology.costs)
                    appendCost(
                        cost.type, cost.amount,
                        cost.flag != 0);
            }
            if (!costText.empty()) {
                costText += "   TIME " +
                            std::to_string(duration) +
                            "S";
                drawBitmapText(
                    r, {costText},
                    (detailsX + 14.0f) * invZoom,
                    (detailsY + 35.0f) * invZoom,
                    1.35f * invZoom,
                    166, 225, 232);
            }

            std::vector<std::string> detailLines;
            if (stanceMenu) {
                // Original rollover help: "<b>Name<b> \nDescription".
                std::string help = assets_.localizedString(
                    kStanceHelpStrings[std::min<size_t>(
                        actionMenuSelection_, 3)]);
                const size_t newline = help.find('\n');
                if (newline != std::string::npos)
                    help = help.substr(newline + 1);
                for (const std::string &line : wrapText(help, 47))
                    detailLines.push_back(line);
                if ((size_t)subject->attackMode ==
                    actionMenuSelection_) {
                    detailLines.push_back("");
                    detailLines.push_back("Current stance.");
                }
            } else if (selectedUnit) {
                const std::string description =
                    assets_.localizedString(
                        selectedUnit
                            ->languageDllHelp);
                const std::vector<std::string> wrapped =
                    wrapText(
                        description.empty()
                            ? "Creates or constructs " +
                                  unitDisplayName(
                                      *selectedUnit) +
                                  "."
                            : description,
                        47);
                detailLines.insert(
                    detailLines.end(),
                    wrapped.begin(), wrapped.end());
                detailLines.push_back(
                    "Hit points: " +
                    std::to_string(
                        selectedUnit->hitPoints));
            } else if (selectedTechnology >= 0) {
                for (const std::string &effect :
                     technologyEffectLines(
                         selectedTechnology)) {
                    const auto wrapped =
                        wrapText(effect, 47);
                    detailLines.insert(
                        detailLines.end(),
                        wrapped.begin(),
                        wrapped.end());
                }
                detailLines.push_back("");
                for (const std::string &requirement :
                     technologyRequirementLines(
                         subject->player,
                         selectedTechnology)) {
                    const auto wrapped =
                        wrapText(requirement, 47);
                    detailLines.insert(
                        detailLines.end(),
                        wrapped.begin(),
                        wrapped.end());
                }
            } else if (commandOption) {
                detailLines.push_back(
                    subject->gate
                        ? "Changes whether units may pass through this gate."
                        : "Ejects every garrisoned unit from this building.");
            }
            if (detailLines.size() > 11)
                detailLines.resize(11);
            drawBitmapText(
                r, detailLines,
                (detailsX + 14.0f) * invZoom,
                (detailsY + 59.0f) * invZoom,
                1.35f * invZoom,
                234, 239, 238);

            const std::string pageText =
                optionCount == 0
                    ? "NO OPTIONS"
                    : std::to_string(
                          actionMenuScroll_ + 1) +
                          "-" +
                          std::to_string(
                              std::min(
                                  optionCount,
                                  actionMenuScroll_ +
                                      kActionMenuVisibleItems)) +
                          " / " +
                          std::to_string(optionCount) +
                          "   D-PAD: SELECT   X: CHOOSE";
            drawBitmapText(
                r, {pageText},
                (gridX + 2.0f) * invZoom,
                (kActionMenuY +
                 kActionMenuRows *
                     kActionMenuCell +
                 9.0f) *
                    invZoom,
                1.05f * invZoom,
                172, 217, 226);

            if (!stanceMenu &&
                !subject->productionQueue.empty()) {
                const float queueY =
                    kActionMenuY +
                    kActionMenuRows *
                        kActionMenuCell +
                    48.0f;
                const float slotWidth =
                    (kActionMenuWidth - 24.0f) /
                    5.0f;
                for (size_t index = 0;
                     index < 5; index++) {
                    const float x =
                        kActionMenuX + 12.0f +
                        index * slotWidth;
                    const bool occupied =
                        index <
                        subject->productionQueue.size();
                    r.fillRect(
                        x * invZoom,
                        queueY * invZoom,
                        (slotWidth - 4.0f) *
                            invZoom,
                        43.0f * invZoom,
                        occupied ? 7 : 3,
                        occupied ? 47 : 17,
                        occupied ? 65 : 25,
                        255);
                    if (!occupied) continue;
                    const ProductionItem &queued =
                        subject
                            ->productionQueue[index];
                    std::string name =
                        queued.unit
                            ? unitDisplayName(
                                  *queued.unit)
                            : technologyDisplayName(
                                  queued.technologyId);
                    if (name.size() > 17)
                        name.resize(17);
                    drawBitmapText(
                        r,
                        {std::to_string(index + 1) +
                         " " + name},
                        (x + 5.0f) * invZoom,
                        (queueY + 7.0f) *
                            invZoom,
                        1.05f * invZoom,
                        220, 235, 238);
                    drawBitmapText(
                        r, {"X CANCEL"},
                        (x + 5.0f) * invZoom,
                        (queueY + 24.0f) *
                            invZoom,
                        0.9f * invZoom,
                        244, 151, 129);
                    if (index == 0) {
                        const float progress =
                            queued.duration > 0
                                ? std::max(
                                      0.0f,
                                      std::min(
                                          1.0f,
                                          1.0f -
                                              subject
                                                      ->productionRemaining /
                                                  queued.duration))
                                : 1.0f;
                        r.fillRect(
                            (x + 3.0f) *
                                invZoom,
                            (queueY + 38.0f) *
                                invZoom,
                            (slotWidth - 10.0f) *
                                progress * invZoom,
                            3.0f * invZoom,
                            78, 214, 223, 255);
                    }
                }
            }
        }
    }

    if (false && actionMenuOpen_) {
        Object *subject = findObject(actionMenuObjectId_);
        if (subject && subject->active) {
            const float invZoom = 1.0f / zoom_;
            const bool worker = isWorker(*subject);
            const std::vector<const dat::Unit *> units =
                worker
                    ? buildingOptions(*subject,
                                      actionMenuTab_)
                    : actionMenuTab_ ==
                              ActionMenuTab::Units
                          ? productionOptions(*subject)
                          : std::vector<
                                const dat::Unit *>{};
            const std::vector<int> technologies =
                !worker &&
                        actionMenuTab_ ==
                            ActionMenuTab::Research
                    ? researchOptions(*subject)
                    : std::vector<int>{};
            const bool commandOption =
                !worker &&
                actionMenuTab_ ==
                    ActionMenuTab::Commands;
            const size_t optionCount =
                commandOption
                    ? 1
                    : worker || actionMenuTab_ ==
                                    ActionMenuTab::Units
                    ? units.size()
                    : technologies.size();
            const size_t rows = std::min(
                optionCount, kActionMenuMaxRows);
            const bool hasQueue =
                !subject->productionQueue.empty();
            const float height =
                42.0f + rows * kActionMenuRowHeight +
                (hasQueue ? 52.0f : 0.0f);
            r.fillRect(kActionMenuX * invZoom,
                       28.0f * invZoom,
                       kActionMenuWidth * invZoom,
                       height * invZoom, 5, 8, 16, 246);
            r.fillRect(kActionMenuX * invZoom,
                       28.0f * invZoom,
                       kActionMenuWidth * invZoom,
                       3.0f * invZoom,
                       196, 188, 145, 255);
            if (worker) {
                const float tabWidth =
                    kActionMenuWidth / 3.0f;
                const ActionMenuTab tabs[] = {
                    ActionMenuTab::Economy,
                    ActionMenuTab::Military,
                    ActionMenuTab::Defense,
                };
                const char *labels[] = {
                    "L ECONOMY",
                    "MILITARY",
                    "DEFENSE R",
                };
                for (size_t tab = 0;
                     tab < std::size(tabs); tab++) {
                    const bool active =
                        actionMenuTab_ == tabs[tab];
                    r.fillRect(
                        (kActionMenuX +
                         tab * tabWidth) *
                            invZoom,
                        34.0f * invZoom,
                        tabWidth * invZoom,
                        28.0f * invZoom,
                        active ? 75 : 17,
                        active ? 87 : 27,
                        active ? 103 : 41,
                        255);
                    drawBitmapText(
                        r, {labels[tab]},
                        (kActionMenuX +
                         tab * tabWidth + 12.0f) *
                            invZoom,
                        42.0f * invZoom,
                        0.82f * invZoom,
                        225, 230, 235);
                }
            } else {
                std::vector<ActionMenuTab> tabs;
                std::vector<std::string> labels;
                if (!productionOptions(*subject).empty()) {
                    tabs.push_back(ActionMenuTab::Units);
                    labels.push_back("UNITS");
                }
                if (!researchOptions(*subject).empty()) {
                    tabs.push_back(ActionMenuTab::Research);
                    labels.push_back("RESEARCH");
                }
                if (subject->gate ||
                    subject->unit->garrisonCapacity > 0) {
                    tabs.push_back(ActionMenuTab::Commands);
                    labels.push_back("COMMANDS");
                }
                const float tabWidth =
                    kActionMenuWidth /
                    std::max<size_t>(1, tabs.size());
                for (size_t tab = 0;
                     tab < tabs.size(); ++tab) {
                    const bool active =
                        actionMenuTab_ == tabs[tab];
                    r.fillRect(
                        (kActionMenuX +
                         tab * tabWidth) *
                            invZoom,
                        34.0f * invZoom,
                        tabWidth * invZoom,
                        28.0f * invZoom,
                        active ? 75 : 17,
                        active ? 87 : 27,
                        active ? 103 : 41,
                        255);
                    std::string label = labels[tab];
                    if (tab == 0) label = "L  " + label;
                    if (tab + 1 == tabs.size())
                        label += "  R";
                    drawBitmapText(
                        r, {label},
                        (kActionMenuX +
                         tab * tabWidth + 14.0f) *
                            invZoom,
                        42.0f * invZoom,
                        0.88f * invZoom,
                        225, 230, 235);
                }
            }
            for (size_t index = 0; index < rows; index++) {
                const dat::Unit *unit =
                    units.empty() ? nullptr : units[index];
                const dat::Tech *technology =
                    technologies.empty()
                        ? nullptr
                        : &assets_.dat().techs[
                              (size_t)technologies[index]];
                const float y =
                    (kActionMenuY +
                     index * kActionMenuRowHeight) *
                    invZoom;
                r.fillRect(
                    (kActionMenuX + 6.0f) * invZoom, y,
                    (kActionMenuWidth - 12.0f) * invZoom,
                    (kActionMenuRowHeight - 3.0f) * invZoom,
                    15, 23, 35, 255);
                const int civilization =
                    civilizationForPlayer(subject->player);
                const int iconSet =
                    civilization >= 0 &&
                            (size_t)civilization <
                                assets_.dat().civs.size()
                        ? assets_.dat()
                              .civs[(size_t)civilization]
                              .iconSet
                        : 1;
                const SpriteSheet *icons =
                    unit
                        ? assets_.interfaceSheet(
                              (unit->type ==
                                       dat::UT_Building
                                   ? kBuildingIconSlpBase
                                   : kUnitIconSlpBase) +
                              std::max(1, iconSet) - 1)
                        : commandOption
                              ? commandIcons
                              : nullptr;
                const int iconId =
                    unit ? unit->iconId
                         : technology
                               ? technology->iconId
                               : subject->gate
                                     ? (subject->locked
                                            ? (int)kCommandUnlockGateIcon
                                            : (int)kCommandLockGateIcon)
                                     : (int)kCommandEjectIcon;
                const SpriteFrame *icon =
                    icons && iconId >= 0 &&
                            (size_t)iconId <
                                icons->frames.size()
                        ? &icons->frames[
                              (size_t)iconId]
                        : nullptr;
                if (icon) {
                    const float scale =
                        std::min(32.0f / icon->w,
                                 32.0f / icon->h) *
                        invZoom;
                    r.draw(
                        icon->tex,
                        {(kActionMenuX + 10.0f) *
                             invZoom,
                         y + 3.0f * invZoom,
                         icon->w * scale,
                         icon->h * scale,
                         icon->u, icon->v,
                         icon->u + icon->w,
                         icon->v + icon->h});
                }
                std::string name =
                    unit
                        ? unitDisplayName(*unit)
                        : technology
                              ? assets_.localizedString(
                                    technology
                                        ->languageDllName)
                              : subject->gate
                                    ? (subject->locked
                                           ? "Unlock Gate"
                                           : "Lock Gate")
                                    : "Eject All (" +
                                          std::to_string(
                                              garrisonedCount(
                                                  *subject,
                                                  false)) +
                                          ")";
                if (name.size() > 20) name.resize(20);
                drawBitmapText(
                    r, {name},
                    (kActionMenuX + 50.0f) * invZoom,
                    y + 8.0f * invZoom,
                    1.15f * invZoom, 225, 230, 235);
                std::string costs;
                static constexpr const char *labels[] = {
                    "F", "C", "O", "N"};
                auto appendCost =
                    [&](int type, int amount,
                        bool enabled) {
                        if (!enabled || type < 0 ||
                            amount <= 0)
                            return;
                        if (!costs.empty()) costs += "  ";
                        costs +=
                            type < 4
                                ? labels[(size_t)type]
                                : "R";
                        costs += std::to_string(amount);
                    };
                if (unit)
                    for (const dat::ResourceCost &cost :
                         unit->costs)
                        appendCost(
                            cost.type, cost.amount,
                            cost.flag != 0);
                else if (technology)
                    for (const dat::Tech::Cost &cost :
                         technology->costs)
                        appendCost(
                            cost.type, cost.amount,
                            cost.flag != 0);
                drawBitmapText(
                    r, {costs},
                    (kActionMenuX + 260.0f) * invZoom,
                    y + 8.0f * invZoom,
                    1.05f * invZoom, 177, 208, 218);
                drawBitmapText(
                    r,
                    {commandOption
                         ? ""
                         : std::to_string(
                               unit
                                   ? std::max<int16_t>(
                                         0,
                                         unit->trainTime)
                                   : std::max<int16_t>(
                                         0,
                                         technology
                                             ->researchTime)) +
                               "S"},
                    (kActionMenuX + 390.0f) * invZoom,
                    y + 8.0f * invZoom,
                    1.0f * invZoom, 160, 175, 188);
            }
            if (hasQueue) {
                const float footerY =
                    kActionMenuY +
                    rows * kActionMenuRowHeight;
                const float slotWidth =
                    (kActionMenuWidth - 12.0f) / 5.0f;
                for (size_t index = 0; index < 5; index++) {
                    const float x =
                        kActionMenuX + 6.0f +
                        index * slotWidth;
                    const bool occupied =
                        index <
                        subject->productionQueue.size();
                    r.fillRect(
                        x * invZoom,
                        (footerY + 3.0f) * invZoom,
                        (slotWidth - 3.0f) * invZoom,
                        44.0f * invZoom,
                        occupied ? 20 : 10,
                        occupied ? 34 : 16,
                        occupied ? 48 : 24, 255);
                    if (!occupied) continue;
                    const ProductionItem &queued =
                        subject->productionQueue[index];
                    std::string name;
                    if (queued.unit)
                        name =
                            unitDisplayName(*queued.unit);
                    else if (queued.technologyId >= 0)
                        name = assets_.localizedString(
                            assets_.dat()
                                .techs[(size_t)
                                           queued.technologyId]
                                .languageDllName);
                    if (name.size() > 10)
                        name.resize(10);
                    drawBitmapText(
                        r,
                        {std::to_string(index + 1) +
                         " " + name},
                        (x + 4.0f) * invZoom,
                        (footerY + 10.0f) * invZoom,
                        0.72f * invZoom,
                        205, 220, 230);
                    drawBitmapText(
                        r, {"CANCEL"},
                        (x + 4.0f) * invZoom,
                        (footerY + 27.0f) * invZoom,
                        0.62f * invZoom,
                        235, 125, 105);
                    if (index == 0) {
                        const float progress =
                            queued.duration > 0
                                ? std::max(
                                      0.0f,
                                      std::min(
                                          1.0f,
                                          1.0f -
                                              subject
                                                      ->productionRemaining /
                                                  queued.duration))
                                : 1.0f;
                        r.fillRect(
                            (x + 3.0f) * invZoom,
                            (footerY + 41.0f) * invZoom,
                            (slotWidth - 9.0f) *
                                progress * invZoom,
                            3.0f * invZoom,
                            66, 180, 220, 255);
                    }
                }
            }
        }
    }

    if (cheatMenuOpen_) {
        const float invZoom = 1.0f / zoom_;
        const size_t cheatCount = std::size(kCheats);
        const size_t rows =
            std::min(cheatCount, kCheatMenuVisibleRows);
        size_t first =
            cheatMenuSelection_ >= rows
                ? cheatMenuSelection_ - rows + 1
                : 0;
        if (first + rows > cheatCount)
            first = cheatCount - rows;
        const float height =
            56.0f + rows * kCheatMenuRowHeight;
        r.fillRect(
            kCheatMenuX * invZoom, kCheatMenuY * invZoom,
            kCheatMenuWidth * invZoom, height * invZoom,
            4, 7, 14, 248);
        r.fillRect(
            kCheatMenuX * invZoom, kCheatMenuY * invZoom,
            kCheatMenuWidth * invZoom, 3.0f * invZoom,
            207, 190, 120, 255);
        drawBitmapText(
            r, {"GALACTIC BATTLEGROUNDS CHEATS"},
            (kCheatMenuX + 12.0f) * invZoom,
            (kCheatMenuY + 11.0f) * invZoom,
            1.25f * invZoom, 238, 231, 190);
        for (size_t row = 0; row < rows; row++) {
            const size_t index = first + row;
            const CheatEntry &cheat = kCheats[index];
            const float y =
                (kCheatMenuY + 38.0f +
                 row * kCheatMenuRowHeight) *
                invZoom;
            const bool selected =
                index == cheatMenuSelection_;
            r.fillRect(
                (kCheatMenuX + 6.0f) * invZoom, y,
                (kCheatMenuWidth - 12.0f) * invZoom,
                (kCheatMenuRowHeight - 3.0f) * invZoom,
                selected ? 82 : 15,
                selected ? 94 : 23,
                selected ? 108 : 35, 255);
            drawBitmapText(
                r, {std::string(selected ? "> " : "  ") +
                    cheat.code},
                (kCheatMenuX + 12.0f) * invZoom,
                y + 8.0f * invZoom,
                0.95f * invZoom,
                selected ? 255 : 220,
                selected ? 238 : 226,
                selected ? 174 : 230);
            std::string effect = cheat.effect;
            if (cheat.action == CheatAction::ForceBuild)
                effect += forceBuildCheat_ ? " [ON]" : " [OFF]";
            else if (cheat.action == CheatAction::ForceTech)
                effect += fullTechTreeCheat_ ? " [ON]" : " [OFF]";
            else if (cheat.action == CheatAction::ForceSight)
                effect += forceSightCheat_ ? " [ON]" : " [OFF]";
            else if (cheat.action == CheatAction::ForceExplore)
                effect += forceExploreCheat_ ? " [ON]" : " [OFF]";
            drawBitmapText(
                r, {effect},
                (kCheatMenuX + 355.0f) * invZoom,
                y + 8.0f * invZoom,
                0.85f * invZoom, 164, 205, 219);
        }
        drawBitmapText(
            r, {"UP/DOWN: SELECT   X: ACTIVATE   O: CLOSE"},
            (kCheatMenuX + 12.0f) * invZoom,
            (kCheatMenuY + 43.0f +
             rows * kCheatMenuRowHeight) *
                invZoom,
            0.95f * invZoom, 194, 202, 210);
    }

    if (!currentInstruction_.empty() && !cheatMenuOpen_) {
        const float boxWidthPixels = std::min(720.0f, screenW - 32.0f);
        const int textColumns = std::max(20, (int)((boxWidthPixels - 24.0f) / 12.0f));
        const std::vector<std::string> lines = wrapText(currentInstruction_, textColumns);
        const float invZoom = 1.0f / zoom_;
        const float boxHeightPixels = 24.0f + lines.size() * 18.0f;
        const float boxX = 16.0f * invZoom;
        const float boxY = 44.0f * invZoom;
        r.fillRect(boxX, boxY, boxWidthPixels * invZoom, boxHeightPixels * invZoom,
                   5, 8, 16, 220);
        r.fillRect(boxX, boxY, boxWidthPixels * invZoom, 2.0f * invZoom,
                   210, 210, 190, 255);
        drawBitmapText(r, lines, 28.0f * invZoom, boxY + 12.0f * invZoom,
                       2.0f * invZoom);
    }

    if (boxSelectActive_) {
        const float invZoom = 1.0f / zoom_;
        const float x = std::min(boxStartX_, boxEndX_) * invZoom;
        const float y = std::min(boxStartY_, boxEndY_) * invZoom;
        const float w = std::abs(boxEndX_ - boxStartX_) * invZoom;
        const float h = std::abs(boxEndY_ - boxStartY_) * invZoom;
        const float line = 2.0f * invZoom;
        r.fillRect(x, y, w, h, 40, 180, 255, 35);
        r.fillRect(x, y, w, line, 80, 210, 255, 230);
        r.fillRect(x, y + h - line, w, line, 80, 210, 255, 230);
        r.fillRect(x, y, line, h, 80, 210, 255, 230);
        r.fillRect(x + w - line, y, line, h, 80, 210, 255, 230);
    }

    if (!statusMessage_.empty()) {
        const float invZoom = 1.0f / zoom_;
        const float width =
            statusMessage_.size() * 10.0f + 24.0f;
        const float x =
            (screenW - width) * 0.5f * invZoom;
        const float y = 104.0f * invZoom;
        r.fillRect(x, y, width * invZoom,
                   28.0f * invZoom, 38, 12, 12, 235);
        drawBitmapText(
            r, {statusMessage_},
            x + 12.0f * invZoom, y + 8.0f * invZoom,
            1.05f * invZoom, 255, 220, 180);
    }

    if (cursorVisible_) {
        const float invZoom = 1.0f / zoom_;
        const float x = cursorX_ * invZoom, y = cursorY_ * invZoom;
        size_t frameIndex = kCursorNormal;
        if (cursorMode_ == CursorMode::Move) frameIndex = kCursorMove;
        if (cursorMode_ == CursorMode::Attack) frameIndex = kCursorAttack;
        if (cursorMode_ == CursorMode::Garrison)
            frameIndex = kCursorGarrison;
        if (cursorMode_ == CursorMode::Gather)
            frameIndex = kCursorGather;
        if (cursorMode_ == CursorMode::Repair)
            frameIndex = kCursorRepair;
        if (cursors && frameIndex < cursors->frames.size()) {
            const SpriteFrame &frame = cursors->frames[frameIndex];
            const float width = frame.w * invZoom, height = frame.h * invZoom;
            const bool centered =
                cursorMode_ == CursorMode::Attack ||
                cursorMode_ == CursorMode::Garrison ||
                cursorMode_ == CursorMode::Gather ||
                cursorMode_ == CursorMode::Repair;
            r.draw(frame.tex, {x - (centered ? width * 0.5f : 0.0f),
                               y - (centered ? height * 0.5f : 0.0f),
                               width, height, frame.u, frame.v,
                               frame.u + frame.w, frame.v + frame.h});
        } else {
            const float line = 2.0f * invZoom, arm = 10.0f * invZoom;
            r.fillRect(x - arm - line, y - line, arm * 2 + line * 2, line * 3,
                       0, 0, 0, 210);
            r.fillRect(x - line, y - arm - line, line * 3, arm * 2 + line * 2,
                       0, 0, 0, 210);
            r.fillRect(x - arm, y, arm * 2, line, 245, 245, 245, 255);
            r.fillRect(x, y - arm, line, arm * 2, 245, 245, 245, 255);
        }
    }
    r.endFrame();
}

} // namespace swgb
