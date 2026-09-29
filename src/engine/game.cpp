// SPDX-License-Identifier: GPL-3.0-or-later
#include "game.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
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
};

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
    lookAt(mapSize * 0.30f + 2, mapSize * 0.35f + 2);
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
    for (const ScenarioTrigger &trigger : triggers_)
        for (const ScenarioEffect &effect : trigger.effects)
            if (effect.type == 6 || effect.type == 7)
                for (uint32_t spawnId : effect.selectedUnitIds)
                    if (Object *object = findObject(spawnId)) object->gate = true;
    rebuildAdjacency();
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
    object.hitPoints = object.maxHitPoints = std::max(1, (int)unit->hitPoints);
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

Game::Object *Game::spawn(int civ, const std::string &name, int player, float x, float y, float facing) {
    const dat::Unit *u = findUnit(civ, name);
    if (!u) return nullptr;
    Object *o = addObject(u, player, x, y, facing, nextSpawnId_++);
    if (!o) return nullptr;
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
        if (object.draw && object.spawnId &&
            object.unit->type >= dat::UT_Combatant)
            combatObjectIndices_.push_back((uint32_t)index);
        const bool mobile = object.unit->speed > 0 && object.unit->type != dat::UT_Building;
        if (mobile) {
            mobileObjectIndices_.push_back((uint32_t)index);
            continue;
        }
        if (object.unit->obstructionType == 0 || (object.gate && !object.locked)) continue;
        staticObstructionIndices_.push_back((uint32_t)index);
        const float halfX = std::max(0.05f, object.unit->collisionSize[0]);
        const float halfY = std::max(0.05f, object.unit->collisionSize[1]);
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
    spawn(civ, "BLDG-TRAINFOOT1", player, cx + 5.5f, cy - 2.5f, 0);
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

void drawBitmapText(Renderer &renderer, const std::vector<std::string> &lines,
                    float x, float y, float pixel) {
    for (size_t line = 0; line < lines.size(); line++) {
        for (size_t column = 0; column < lines[line].size(); column++) {
            const uint64_t bits = glyphBits(lines[line][column]);
            for (int row = 0; row < 7; row++) {
                const uint8_t rowBits = (uint8_t)((bits >> (row * 5)) & 31);
                for (int bit = 0; bit < 5;) {
                    if (!(rowBits & (1 << (4 - bit)))) {
                        bit++;
                        continue;
                    }
                    const int start = bit;
                    while (bit < 5 && (rowBits & (1 << (4 - bit)))) bit++;
                    renderer.fillRect(x + (column * 6 + start) * pixel,
                                      y + (line * 9 + row) * pixel,
                                      (bit - start) * pixel, pixel, 255, 255, 255, 255);
                }
            }
        }
    }
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

bool Game::objectActive(uint32_t spawnId) const {
    const Object *object = findObject(spawnId);
    return object && object->active;
}

float Game::resource(int player, int resourceId) const {
    if (player < 0 || (size_t)player >= resources_.size()) return 0;
    const auto found = resources_[(size_t)player].find(resourceId);
    return found == resources_[(size_t)player].end() ? 0 : found->second;
}

size_t Game::activeObjectCount() const {
    return (size_t)std::count_if(objects_.begin(), objects_.end(),
                                 [](const Object &object) { return object.active; });
}

size_t Game::selectedObjectCount() const {
    return (size_t)std::count_if(objects_.begin(), objects_.end(),
                                 [](const Object &object) {
                                     return object.active && object.selected;
                                 });
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
            if (dx * dx + dy * dy < separation * separation) stats.overlappingPairs++;
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

bool Game::objectSelected(uint32_t spawnId) const {
    const Object *object = findObject(spawnId);
    return object && object->active && object->selected;
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

int Game::attackDamage(const Object &source, const Object &target) const {
    int damage = 0;
    for (const dat::AttackOrArmor &attack : source.unit->attacks) {
        int armour = target.unit->baseArmor;
        for (const dat::AttackOrArmor &candidate : target.unit->armours)
            if (candidate.cls == attack.cls) {
                armour = candidate.amount;
                break;
            }
        damage += std::max(0, (int)attack.amount - armour);
    }
    return source.unit->attacks.empty() ? 0 : std::max(1, damage);
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

Game::Object *Game::objectAtScreen(float screenX, float screenY, int screenW, int screenH) {
    Object *best = nullptr;
    float bestScore = std::numeric_limits<float>::max();
    for (Object &object : objects_) {
        if (!isInspectable(object)) continue;
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

Game::Object *Game::enemyAtScreen(float screenX, float screenY, int screenW, int screenH) {
    const Object *source = nullptr;
    for (const Object &object : objects_)
        if (object.selected && isSelectable(object) && canAttack(object)) {
            source = &object;
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
        for (Object &candidate : objects_) {
            if (!isSelectable(candidate) || candidate.unit->id != object->unit->id) continue;
            float candidateX, candidateY;
            objectScreenPosition(candidate, screenW, screenH, candidateX, candidateY);
            if (candidateX >= 0 && candidateY >= 0 &&
                candidateX < screenW && candidateY < screenH)
                candidate.selected = true;
        }
    } else if (object) {
        object->selected = true;
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
    Object *acknowledgement = nullptr;
    for (Object &object : objects_) {
        if (!isSelectable(object)) continue;
        float objectX, objectY;
        objectScreenPosition(object, screenW, screenH, objectX, objectY);
        if (objectX >= minX && objectX <= maxX && objectY >= minY && objectY <= maxY) {
            object.selected = true;
            if (!acknowledgement) acknowledgement = &object;
        }
    }
    if (acknowledgement && playUnitSound_ && acknowledgement->unit->selectionSound >= 0)
        playUnitSound_(acknowledgement->unit->selectionSound,
                       civilizationForPlayer(acknowledgement->player));
}

void Game::commandAtScreen(float screenX, float screenY, int screenW, int screenH) {
    std::vector<Object *> selected;
    for (Object &object : objects_)
        if (isSelectable(object) && object.selected) selected.push_back(&object);
    if (selected.empty()) return;

    Object *enemy = enemyAtScreen(screenX, screenY, screenW, screenH);
    if (enemy) {
        Object *acknowledgement = nullptr;
        std::vector<Object *> attackers;
        for (Object *source : selected)
            if (!canAttack(*source) || !isEnemy(*source, *enemy)) continue;
            else
                attackers.push_back(source);
        float centroidX = 0, centroidY = 0;
        float approachSpacing = 0.18f;
        for (const Object *source : attackers) {
            centroidX += source->x;
            centroidY += source->y;
            const float contact =
                collisionRadius(*source) + collisionRadius(*enemy) + 0.08f;
            float approachDistance =
                std::max(contact, attackRange(*source, *enemy) * 0.8f);
            if (source->unit->minRange > 0)
                approachDistance =
                    std::max(approachDistance, source->unit->minRange + 0.2f);
            const float separation = collisionRadius(*source) * 2.0f + 0.12f;
            const float ratio =
                std::min(0.95f, separation / std::max(0.1f, approachDistance * 2.0f));
            approachSpacing =
                std::max(approachSpacing, 2.0f * std::asin(ratio));
        }
        approachSpacing = std::min(0.8f, approachSpacing);
        const float baseAngle =
            attackers.empty()
                ? 0
                : std::atan2(centroidY / attackers.size() - enemy->y,
                             centroidX / attackers.size() - enemy->x);
        for (size_t i = 0; i < attackers.size(); i++) {
            Object *source = attackers[i];
            const float offset =
                ((float)i - ((float)attackers.size() - 1.0f) * 0.5f) *
                approachSpacing;
            issueAttack(*source, *enemy, baseAngle + offset);
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
        object->attackTargetId = 0;
        object->attackAutomatic = false;
    }
    playUnitAcknowledgement(*selected.front(), false);
    issueGroupMove(std::move(selected), targetX, targetY);
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

void Game::issueAttack(Object &source, Object &target, float approachAngle,
                       bool automatic) {
    source.attackTargetId = target.spawnId;
    source.attackRepathTime = 0;
    source.attackApproachAngle = approachAngle;
    source.attackSlotRetries = 0;
    source.attackStallTime = 0;
    source.attackBestDistance = std::numeric_limits<float>::max();
    source.moveGoalActive = false;
    source.moveGroupId = 0;
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
               ? std::max(12.0f, acquisition * 2.0f)
               : std::max(8.0f, acquisition + 3.0f);
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
    const float direction = (source.attackSlotRetries & 1u) ? 1.0f : -1.0f;
    const float magnitude =
        0.28f * (1.0f + std::min(3u, source.attackSlotRetries / 2u));
    source.attackApproachAngle += handedness * direction * magnitude;
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
                if (!isEnemy(source, candidate)) continue;
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

void Game::killObject(Object &object) {
    const bool wasStatic = object.unit->speed <= 0 || object.unit->type == dat::UT_Building;
    object.active = false;
    object.selected = false;
    object.state = State::Idle;
    object.path.clear();
    object.attackTargetId = 0;

    Remains remains;
    remains.dyingGraphic = object.unit->dyingGraphic;
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

    if (playUnitSound_) {
        int soundId = object.unit->dyingSound;
        if (soundId < 0) soundId = graphicSound(object.unit->dyingGraphic);
        if (soundId >= 0) playUnitSound_(soundId, civilizationForPlayer(object.player));
    }
    unitsKilled_++;
    if (wasStatic) rebuildAdjacency();
}

void Game::damageObject(Object &object, int damage, uint32_t attackerId) {
    if (!object.active || damage <= 0) return;
    const float previousDamage =
        100.0f * (1.0f - object.hitPoints / std::max(1.0f, object.maxHitPoints));
    object.hitPoints = std::max(0.0f, object.hitPoints - damage);
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
    if (playUnitSound_ && soundId >= 0)
        playUnitSound_(soundId, civilizationForPlayer(object.player));
}

void Game::launchProjectile(const Object &source, const Object &target, int damage) {
    const dat::Unit *projectileUnit =
        findUnit(civilizationForPlayer(source.player), source.unit->projectileUnitId);
    if (!projectileUnit || projectileUnit->standingGraphic[0] < 0) {
        const int soundId = graphicSound(source.unit->attackGraphic);
        if (playUnitSound_ && soundId >= 0)
            playUnitSound_(soundId, civilizationForPlayer(source.player));
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
    if (playUnitSound_ && soundId >= 0)
        playUnitSound_(soundId, civilizationForPlayer(source.player));
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
        const bool returnToPost = source.attackAutomatic;
        finishAttack(source, returnToPost);
        return;
    }

    float dx = target->x - source.x, dy = target->y - source.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance > 0.0001f) source.facing = std::atan2(dy, dx);
    const float range = attackRange(source, *target);
    if (source.attackAutomatic) {
        const float leash = automaticPursuitLeash(source);
        const float targetHomeDx = target->x - source.homeX;
        const float targetHomeDy = target->y - source.homeY;
        if ((leash <= 0 && distance > range + 0.05f) ||
            (leash > 0 &&
             targetHomeDx * targetHomeDx + targetHomeDy * targetHomeDy >
                 leash * leash)) {
            finishAttack(source, leash > 0);
            return;
        }
    }
    if (distance <= range + 0.05f && distance + 0.05f >= source.unit->minRange) {
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
                if (playUnitSound_ && soundId >= 0)
                    playUnitSound_(soundId, civilizationForPlayer(source.player));
                damageObject(*target, damage, source.spawnId);
            }
            source.attackCooldown = std::max(0.1f, source.unit->reloadTime);
            source.animTime = 0;
            if (!target->active) {
                const bool returnToPost = source.attackAutomatic;
                finishAttack(source, returnToPost);
            }
        }
        return;
    }

    const float contact = collisionRadius(source) + collisionRadius(*target) + 0.08f;
    float desiredDistance = std::max(contact, range * 0.8f);
    if (source.unit->minRange > 0)
        desiredDistance = std::max(desiredDistance, source.unit->minRange + 0.2f);
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
        issueMove(source, destinationX, destinationY) ? 0.5f : 1.5f;
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
                const float halfX = std::max(0.05f, other.unit->collisionSize[0]);
                const float halfY = std::max(0.05f, other.unit->collisionSize[1]);
                const float extentX = halfX + radius, extentY = halfY + radius;
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

bool Game::findPath(const Object &object, float targetX, float targetY,
                    std::vector<std::array<float, 2>> &path) const {
    constexpr int kNodesPerTile = 2;
    constexpr int kStraightCost = 10;
    constexpr int kDiagonalCost = 14;
    const int width = mapSize_ * kNodesPerTile;
    if (width <= 0) return false;
    const int nodeCount = width * width;
    auto nodeX = [](int x) { return (x + 0.5f) / kNodesPerTile; };
    auto nodeY = [](int y) { return (y + 0.5f) / kNodesPerTile; };
    auto clampNode = [width](float value) {
        return std::max(0, std::min(width - 1, (int)std::floor(value * kNodesPerTile)));
    };
    auto indexOf = [width](int x, int y) { return y * width + x; };

    std::vector<uint8_t> passable((size_t)nodeCount, 0);
    const float radius = collisionRadius(object);
    for (int y = 0; y < width; y++)
        for (int x = 0; x < width; x++) {
            const float worldX = nodeX(x), worldY = nodeY(y);
            passable[(size_t)indexOf(x, y)] =
                worldX >= radius && worldY >= radius &&
                worldX < mapSize_ - radius && worldY < mapSize_ - radius &&
                terrainPassable(object, worldX, worldY);
        }
    if (!isAirUnit(object)) {
        for (uint32_t index : staticObstructionIndices_) {
            const Object &other = objects_[(size_t)index];
            if (!other.active || other.hidden) continue;
            const float halfX = std::max(0.05f, other.unit->collisionSize[0]);
            const float halfY = std::max(0.05f, other.unit->collisionSize[1]);
            const int minX = std::max(0, (int)std::floor(
                (other.x - halfX - radius) * kNodesPerTile) - 1);
            const int maxX = std::min(width - 1, (int)std::ceil(
                (other.x + halfX + radius) * kNodesPerTile));
            const int minY = std::max(0, (int)std::floor(
                (other.y - halfY - radius) * kNodesPerTile) - 1);
            const int maxY = std::min(width - 1, (int)std::ceil(
                (other.y + halfY + radius) * kNodesPerTile));
            for (int y = minY; y <= maxY; y++)
                for (int x = minX; x <= maxX; x++)
                    if (std::abs(nodeX(x) - other.x) < halfX + radius &&
                        std::abs(nodeY(y) - other.y) < halfY + radius)
                        passable[(size_t)indexOf(x, y)] = 0;
        }
    }
    for (uint32_t index : mobileObjectIndices_) {
        const Object &other = objects_[(size_t)index];
        if (&other == &object || !other.active || other.hidden ||
            isAirUnit(other) != isAirUnit(object))
            continue;
        const float separation = radius + collisionRadius(other) + 0.04f;
        const int minX = std::max(0, (int)std::floor(
            (other.x - separation) * kNodesPerTile) - 1);
        const int maxX = std::min(width - 1, (int)std::ceil(
            (other.x + separation) * kNodesPerTile));
        const int minY = std::max(0, (int)std::floor(
            (other.y - separation) * kNodesPerTile) - 1);
        const int maxY = std::min(width - 1, (int)std::ceil(
            (other.y + separation) * kNodesPerTile));
        for (int y = minY; y <= maxY; y++)
            for (int x = minX; x <= maxX; x++) {
                const float dx = nodeX(x) - other.x, dy = nodeY(y) - other.y;
                if (dx * dx + dy * dy < separation * separation)
                    passable[(size_t)indexOf(x, y)] = 0;
            }
    }

    const int startX = clampNode(object.x), startY = clampNode(object.y);
    const int desiredX = clampNode(targetX), desiredY = clampNode(targetY);
    const int start = indexOf(startX, startY);
    passable[(size_t)start] = 1;

    struct OpenNode {
        int score;
        int index;
        bool operator<(const OpenNode &other) const { return score > other.score; }
    };
    const int infinity = std::numeric_limits<int>::max();
    std::vector<int> cost((size_t)nodeCount, infinity);
    std::vector<int> parent((size_t)nodeCount, -1);
    std::priority_queue<OpenNode> open;
    auto heuristic = [desiredX, desiredY](int x, int y) {
        const int dx = std::abs(x - desiredX), dy = std::abs(y - desiredY);
        return kDiagonalCost * std::min(dx, dy) +
               kStraightCost * (std::max(dx, dy) - std::min(dx, dy));
    };
    cost[(size_t)start] = 0;
    open.push({heuristic(startX, startY), start});
    int closest = start;
    int closestDistance = heuristic(startX, startY);
    static constexpr int directions[8][2] = {
        {-1, 0}, {1, 0}, {0, -1}, {0, 1},
        {-1, -1}, {1, -1}, {-1, 1}, {1, 1},
    };

    while (!open.empty()) {
        const OpenNode current = open.top();
        open.pop();
        const int cx = current.index % width, cy = current.index / width;
        if (current.score != cost[(size_t)current.index] + heuristic(cx, cy)) continue;
        const int distance = heuristic(cx, cy);
        if (distance < closestDistance) {
            closestDistance = distance;
            closest = current.index;
        }
        if (cx == desiredX && cy == desiredY) {
            closest = current.index;
            break;
        }
        for (const auto &direction : directions) {
            const int nx = cx + direction[0], ny = cy + direction[1];
            if (nx < 0 || ny < 0 || nx >= width || ny >= width) continue;
            const int next = indexOf(nx, ny);
            if (!passable[(size_t)next]) continue;
            const bool diagonal = direction[0] != 0 && direction[1] != 0;
            if (diagonal &&
                (!passable[(size_t)indexOf(cx + direction[0], cy)] ||
                 !passable[(size_t)indexOf(cx, cy + direction[1])]))
                continue;
            const int nextCost = cost[(size_t)current.index] +
                                 (diagonal ? kDiagonalCost : kStraightCost);
            if (nextCost >= cost[(size_t)next]) continue;
            cost[(size_t)next] = nextCost;
            parent[(size_t)next] = current.index;
            open.push({nextCost + heuristic(nx, ny), next});
        }
    }

    if (closest == start) return closestDistance == 0;
    std::vector<int> reverse;
    for (int node = closest; node != start && node >= 0; node = parent[(size_t)node])
        reverse.push_back(node);
    if (reverse.empty() || parent[(size_t)reverse.back()] < 0) return false;
    std::reverse(reverse.begin(), reverse.end());

    path.clear();
    int previousDx = 0, previousDy = 0;
    for (size_t i = 0; i < reverse.size(); i++) {
        const int node = reverse[i];
        const int previous = i == 0 ? start : reverse[i - 1];
        const int dx = node % width - previous % width;
        const int dy = node / width - previous / width;
        if (i > 0 && dx == previousDx && dy == previousDy)
            path.back() = {nodeX(node % width), nodeY(node / width)};
        else
            path.push_back({nodeX(node % width), nodeY(node / width)});
        previousDx = dx;
        previousDy = dy;
    }
    if (closestDistance == 0 && positionPassable(object, targetX, targetY, false)) {
        const auto &last = path.back();
        const float dx = targetX - last[0], dy = targetY - last[1];
        if (dx * dx + dy * dy > 0.0025f) path.push_back({targetX, targetY});
    }
    return !path.empty();
}

bool Game::issueMove(Object &object, float targetX, float targetY) {
    object.targetX = targetX;
    object.targetY = targetY;
    object.pathIndex = 0;
    object.blockedTime = 0;
    const float dx = targetX - object.x, dy = targetY - object.y;
    if (dx * dx + dy * dy < 0.01f) {
        object.path.clear();
        object.state = State::Idle;
        object.moveGoalActive = false;
        return true;
    }
    bool direct = true;
    const float distance = std::sqrt(dx * dx + dy * dy);
    const int samples = std::max(1, (int)std::ceil(distance * 4.0f));
    for (int sample = 1; sample <= samples; sample++) {
        const float amount = (float)sample / samples;
        if (!positionPassable(object, object.x + dx * amount,
                              object.y + dy * amount, false)) {
            direct = false;
            break;
        }
    }
    if (direct) {
        object.path.assign(1, {targetX, targetY});
    } else if (!findPath(object, targetX, targetY, object.path)) {
        object.path.clear();
        object.state = State::Idle;
        return false;
    }
    object.state = State::Walk;
    object.wander = false;
    object.animTime = 0;
    return true;
}

void Game::issueGroupMove(std::vector<Object *> targets, float targetX, float targetY) {
    struct ReservedDestination {
        float x, y, radius;
    };
    std::vector<ReservedDestination> reserved;
    reserved.reserve(targets.size());
    const uint32_t moveGroupId = nextMoveGroupId_++;
    if (nextMoveGroupId_ == 0) nextMoveGroupId_ = 1;
    float slotSpacing = 0.75f;
    for (const Object *object : targets)
        if (!object->hidden && object->unit->speed > 0)
            slotSpacing =
                std::max(slotSpacing, collisionRadius(*object) * 2.0f + 0.25f);
    size_t slot = 0;
    std::vector<Object *> remaining = targets;
    std::sort(remaining.begin(), remaining.end(),
              [&](const Object *a, const Object *b) {
                  const float adx = a->x - targetX, ady = a->y - targetY;
                  const float bdx = b->x - targetX, bdy = b->y - targetY;
                  return adx * adx + ady * ady < bdx * bdx + bdy * bdy;
              });
    for (Object *object : remaining) {
        if (object->hidden || object->unit->speed <= 0) continue;
        object->attackTargetId = 0;
        object->attackAutomatic = false;
        object->moveGroupId = moveGroupId;
        float slotX = targetX, slotY = targetY;
        if (slot > 0) {
            const int ring =
                (int)std::ceil((std::sqrt((float)slot + 1.0f) - 1.0f) * 0.5f);
            const int side = ring * 2;
            const int first = (ring * 2 - 1) * (ring * 2 - 1);
            const int offset = (int)slot - first;
            int sx = ring, sy = ring;
            if (offset < side) sx -= offset;
            else if (offset < side * 2) {
                sx = -ring;
                sy -= offset - side;
            } else if (offset < side * 3) {
                sx = -ring + offset - side * 2;
                sy = -ring;
            } else {
                sy = -ring + offset - side * 3;
            }
            slotX += sx * slotSpacing;
            slotY += sy * slotSpacing;
        }
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
                    std::find(targets.begin(), targets.end(), &other) != targets.end() ||
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
        if (!issueMove(*object, slotX, slotY)) {
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
        if (sourcePlayer >= 0 && (size_t)sourcePlayer < researchedTechs_.size() && technology >= 0)
            researchedTechs_[(size_t)sourcePlayer].insert(technology);
        if (warnedEffects_.insert(effect.type).second)
            log("technology research is tracked; DAT technology modifiers are not applied yet");
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
            object->gate = true;
            object->locked = effect.type == 7;
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
        if (sourcePlayer >= 0 && (size_t)sourcePlayer < disabledTechs_.size())
            disabledTechs_[(size_t)sourcePlayer].erase(technology);
        break;
    case 33:
        if (sourcePlayer >= 0 && (size_t)sourcePlayer < disabledTechs_.size())
            disabledTechs_[(size_t)sourcePlayer].insert(technology);
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
    if (player <= 0) return 16;
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

void Game::update(float dt, const InputState &in) {
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

    float scrollX = in.scrollX, scrollY = in.scrollY;
    if (in.cursorVisible) {
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
    camX_ += scrollX * scrollSpeed * dt - in.dragX / zoom_;
    camY_ += scrollY * scrollSpeed * dt - in.dragY / zoom_;
    // Clamp the camera to the map diamond's bounding box.
    float minX = -mapSize_ * kTileHalfW, maxX = mapSize_ * kTileHalfW;
    float maxY = 2.0f * mapSize_ * kTileHalfH;
    camX_ = std::max(minX, std::min(maxX, camX_));
    camY_ = std::max(0.0f, std::min(maxY, camY_));

    if (in.zoomStep > 0) zoom_ = std::min(1.0f, zoom_ * 1.25f);
    if (in.zoomStep < 0) zoom_ = std::max(0.4f, zoom_ / 1.25f);
    if (in.toggleDebug) debug_ = !debug_;

    updateTriggers(dt);
    rebuildMobileOccupancy();
    commandMarkerTime_ = std::max(0.0f, commandMarkerTime_ - dt);
    selectionClickAge_ += dt;
    for (Object &object : objects_)
        if (object.selected && !isInspectable(object)) object.selected = false;

    cursorVisible_ = in.cursorVisible;
    cursorX_ = in.pointerX;
    cursorY_ = in.pointerY;
    boxSelectActive_ = in.boxSelectActive;
    boxStartX_ = in.boxStartX;
    boxStartY_ = in.boxStartY;
    boxEndX_ = in.boxEndX;
    boxEndY_ = in.boxEndY;
    if (in.boxSelectCommit)
        selectBox(in.boxStartX, in.boxStartY, in.boxEndX, in.boxEndY,
                  in.screenW, in.screenH);
    if (in.selectPressed)
        selectAtScreen(in.pointerX, in.pointerY, in.screenW, in.screenH);
    if (in.cycleAttackMode) cycleSelectedAttackMode();
    if (in.commandPressed)
        commandAtScreen(in.pointerX, in.pointerY, in.screenW, in.screenH);
    if (in.pointerTap) {
        if (hasSelectedAttacker() &&
            enemyAtScreen(in.pointerX, in.pointerY, in.screenW, in.screenH))
            commandAtScreen(in.pointerX, in.pointerY, in.screenW, in.screenH);
        else if (objectAtScreen(in.pointerX, in.pointerY, in.screenW, in.screenH))
            selectAtScreen(in.pointerX, in.pointerY, in.screenW, in.screenH);
        else if (hasSelectedUnit())
            commandAtScreen(in.pointerX, in.pointerY, in.screenW, in.screenH);
        else
            clearSelection();
    }
    cursorMode_ = CursorMode::Normal;
    if (cursorVisible_ && !boxSelectActive_ && hasSelectedUnit())
        cursorMode_ = enemyAtScreen(cursorX_, cursorY_, in.screenW, in.screenH)
                          ? CursorMode::Attack
                          : CursorMode::Move;

    std::uniform_real_distribution<float> r01(0, 1);
    struct CooperativeMove {
        size_t index;
        float x, y;
    };
    std::vector<CooperativeMove> cooperativeMoves;
    std::vector<Object *> cooperativeChain;
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
                if (!positionPassable(object, candidateX, candidateY, true))
                    continue;
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
    auto cooperativeMove = [&](auto &&self, Object &object, float nextX,
                               float nextY, int depth) -> bool {
        if (depth >= 12 || !positionPassable(object, nextX, nextY, false))
            return false;
        if (std::find(cooperativeChain.begin(), cooperativeChain.end(), &object) !=
            cooperativeChain.end())
            return false;
        cooperativeChain.push_back(&object);
        const size_t objectIndex = (size_t)(&object - objects_.data());
        const float dx = nextX - object.x, dy = nextY - object.y;
        const float radius = collisionRadius(object);
        for (uint32_t index : mobileObjectIndices_) {
            Object &other = objects_[(size_t)index];
            if (&other == &object || !other.active || other.hidden ||
                isAirUnit(other) != isAirUnit(object))
                continue;
            const float otherDx = nextX - other.x;
            const float otherDy = nextY - other.y;
            const float separation =
                radius + collisionRadius(other) + 0.04f;
            if (otherDx * otherDx + otherDy * otherDy >=
                separation * separation)
                continue;
            if (object.moveGroupId == 0 ||
                other.moveGroupId != object.moveGroupId ||
                other.attackTargetId ||
                !self(self, other, other.x + dx, other.y + dy, depth + 1)) {
                cooperativeChain.pop_back();
                return false;
            }
        }
        cooperativeMoves.push_back({objectIndex, object.x, object.y});
        object.x = nextX;
        object.y = nextY;
        cooperativeChain.pop_back();
        return true;
    };
    for (Object &o : objects_) {
        if (!o.active) continue;
        o.animTime += dt;
        o.flashTime = std::max(0.0f, o.flashTime - dt);
        updateAttack(o, dt);
        if (!o.active) continue;
        if (o.hidden || o.unit->type < dat::UT_DeadFish || o.unit->speed <= 0 ||
            o.unit->type == dat::UT_Building)
            continue;
        o.stateTime -= dt;
        if (o.state == State::Attack) continue;
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
            if (o.pathIndex >= o.path.size()) {
                o.state = State::Idle;
                o.stateTime = 1.5f + r01(rng_) * 5.0f;
                o.animTime = 0;
                const float goalDx = o.targetX - o.x;
                const float goalDy = o.targetY - o.y;
                const float arrival =
                    std::max(0.20f, collisionRadius(o) * 0.75f);
                if (o.moveGoalActive &&
                    goalDx * goalDx + goalDy * goalDy <= arrival * arrival)
                    o.moveGoalActive = false;
                continue;
            }
            const auto &waypoint = o.path[o.pathIndex];
            float dx = waypoint[0] - o.x, dy = waypoint[1] - o.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            const float step = o.unit->speed * dt;
            const float scale = dist <= step || dist <= 0.0001f ? 1.0f : step / dist;
            float nextX = o.x + dx * scale, nextY = o.y + dy * scale;
            bool moved = positionPassable(o, nextX, nextY, true);
            bool movedCooperatively = false;
            if (!moved) {
                cooperativeMoves.clear();
                cooperativeChain.clear();
                movedCooperatively =
                    cooperativeMove(cooperativeMove, o, nextX, nextY, 0);
                moved = movedCooperatively;
                if (!movedCooperatively)
                    for (auto it = cooperativeMoves.rbegin();
                         it != cooperativeMoves.rend(); ++it) {
                        Object &rollback = objects_[it->index];
                        rollback.x = it->x;
                        rollback.y = it->y;
                    }
            }
            if (!moved && std::abs(dx) > 0.0001f)
                moved = positionPassable(o, nextX, o.y, true), nextY = o.y;
            if (!moved && std::abs(dy) > 0.0001f) {
                nextX = o.x;
                nextY = o.y + dy * scale;
                moved = positionPassable(o, nextX, nextY, true);
            }
            if (!moved && dist > 0.0001f) {
                const float directionX = dx / dist, directionY = dy / dist;
                const float handedness = (o.spawnId & 1u) ? 1.0f : -1.0f;
                static constexpr float turns[8] = {
                    0.39269908f, -0.39269908f, 0.78539816f, -0.78539816f,
                    1.17809725f, -1.17809725f, 1.57079633f, -1.57079633f};
                for (float turn : turns) {
                    const float angle = turn * handedness;
                    const float sidestepX =
                        directionX * std::cos(angle) - directionY * std::sin(angle);
                    const float sidestepY =
                        directionX * std::sin(angle) + directionY * std::cos(angle);
                    nextX = o.x + sidestepX * step;
                    nextY = o.y + sidestepY * step;
                    if (positionPassable(o, nextX, nextY, true)) {
                        moved = true;
                        break;
                    }
                }
            }
            if (moved) {
                if (!movedCooperatively) {
                    o.x = nextX;
                    o.y = nextY;
                }
                o.facing = std::atan2(dy, dx);
                const float newGoalDx = o.targetX - o.x;
                const float newGoalDy = o.targetY - o.y;
                const float newGoalDistance =
                    std::sqrt(newGoalDx * newGoalDx + newGoalDy * newGoalDy);
                if (newGoalDistance + 0.05f < o.moveBestDistance) {
                    o.moveBestDistance = newGoalDistance;
                    o.blockedTime = 0;
                    o.moveStallTime = 0;
                } else {
                    o.moveStallTime += dt;
                }
                if (o.moveGoalActive && !o.attackTargetId &&
                    o.moveStallTime >= 1.25f) {
                    spreadBlockedMoveGoal(o);
                    continue;
                }
                if (dist <= step || dist <= 0.0001f) {
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
                            goalDx * goalDx + goalDy * goalDy <= arrival * arrival)
                            o.moveGoalActive = false;
                    }
                }
            } else {
                o.blockedTime += dt;
                if (o.moveGoalActive && !o.attackTargetId)
                    o.moveStallTime += dt;
                const float targetDx = o.targetX - o.x, targetDy = o.targetY - o.y;
                const float arrival =
                    std::max(0.20f, collisionRadius(o) * 0.75f);
                const bool reachedMoveGoal =
                    o.moveGoalActive &&
                    targetDx * targetDx + targetDy * targetDy <=
                        arrival * arrival;
                const bool reachedLooseGoal =
                    !o.moveGoalActive && !o.attackTargetId &&
                    targetDx * targetDx + targetDy * targetDy <= 2.25f;
                if (o.blockedTime >= 0.5f &&
                    (reachedMoveGoal || reachedLooseGoal)) {
                    o.state = State::Idle;
                    o.stateTime = 1.5f + r01(rng_) * 5.0f;
                    o.animTime = 0;
                    if (reachedMoveGoal) o.moveGoalActive = false;
                    continue;
                }
                if (o.blockedTime >= 1.0f) {
                    if (o.attackTargetId) {
                        retryAttackApproach(o);
                        continue;
                    }
                    if (o.moveGoalActive &&
                        spreadBlockedMoveGoal(o))
                        continue;
                    const bool wander = o.wander;
                    issueMove(o, o.targetX, o.targetY);
                    o.wander = wander;
                }
            }
        }
    }
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

void Game::drawGraphic(Renderer &r, int graphicId, float sx, float sy, float facing, float animTime, int player,
                       int initialFrame, int depth, bool drawShadows, float viewW, float viewH) {
    const dat::Graphic *g = assets_.dat().graphic(graphicId);
    if (!g) return;
    if (!drawShadows && depth > 0 && g->layer == 5) return;
    if (!g->deltas.empty() && depth < 3) {
        for (const auto &d : g->deltas) {
            if (d.graphicId == -1) {
                // -1 means "draw my own SLP here".
                dat::Graphic self = *g;
                self.deltas.clear();
                const SpriteSheet *sh = assets_.sheet(self.slp, playerColorBase(player));
                if (!sh) continue;
                size_t fr; bool flip;
                if (!pickFrame(self, sh->frames.size(), facing, animTime, initialFrame, fr, flip)) continue;
                const SpriteFrame &f = sh->frames[fr];
                float x = sx + d.offsetX - (flip ? f.w - f.hotX : f.hotX), y = sy + d.offsetY - f.hotY;
                if (viewW > 0 && (x + f.w <= 0 || x >= viewW || y + f.h <= 0 || y >= viewH)) continue;
                Quad q{x, y, (float)f.w, (float)f.h, flip ? f.u + f.w : f.u, f.v, flip ? f.u : f.u + f.w, f.v + f.h};
                g_draws.push_back({(int64_t)std::min<int>(g->layer, 20) << 40 | (int64_t)(sy * 16 + 65536) << 8, f.tex, q});
            } else {
                drawGraphic(r, d.graphicId, sx + d.offsetX, sy + d.offsetY, facing, animTime, player,
                            initialFrame, depth + 1, drawShadows, viewW, viewH);
            }
        }
        return;
    }
    const SpriteSheet *sh = assets_.sheet(g->slp, playerColorBase(player));
    if (!sh) return;
    size_t fr;
    bool flip;
    if (!pickFrame(*g, sh->frames.size(), facing, animTime, initialFrame, fr, flip)) return;
    const SpriteFrame &f = sh->frames[fr];
    if (f.w == 0 || f.h == 0) return;
    float x = sx - (flip ? f.w - f.hotX : f.hotX), y = sy - f.hotY;
    if (viewW > 0 && (x + f.w <= 0 || x >= viewW || y + f.h <= 0 || y >= viewH)) return;
    Quad q{x, y, (float)f.w, (float)f.h, flip ? f.u + f.w : f.u, f.v, flip ? f.u : f.u + f.w, f.v + f.h};
    // Sort: graphic layer first (shadows/rubble under units), then screen y.
    int64_t key = (int64_t)std::min<int>(g->layer, 20) << 40 | (int64_t)(sy * 16 + 65536) << 8 | (depth & 0xFF);
    g_draws.push_back({key, f.tex, q});
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
        int gid = o.unit->standingGraphic[0];
        if (o.state == State::Walk && o.unit->walkingGraphic >= 0) gid = o.unit->walkingGraphic;
        if (o.state == State::Attack && o.unit->attackGraphic >= 0) gid = o.unit->attackGraphic;
        drawGraphic(r, gid, sx, sy, o.facing, o.animTime, o.player, o.initialFrame, 0,
                    o.drawShadows && !overview, viewW, viewH);

        const float damagePercent =
            100.0f * (1.0f - o.hitPoints / std::max(1.0f, o.maxHitPoints));
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
                            o.player, 0, 0, false, viewW, viewH);
            }
        }
        if (replacement)
            drawGraphic(r, replacement->graphicId, sx, sy, o.facing, o.animTime,
                        o.player, 0, 0, false, viewW, viewH);
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

        const float halfX = std::max(0.5f, object.unit->collisionSize[0]);
        const float halfY = std::max(0.5f, object.unit->collisionSize[1]);
        const std::array<std::array<float, 2>, 4> points = {{
            {sx + (-halfX + halfY) * kTileHalfW,
             sy + (-halfX - halfY) * kTileHalfH},
            {sx + (halfX + halfY) * kTileHalfW,
             sy + (halfX - halfY) * kTileHalfH},
            {sx + (halfX - halfY) * kTileHalfW,
             sy + (halfX + halfY) * kTileHalfH},
            {sx + (-halfX - halfY) * kTileHalfW,
             sy + (-halfX + halfY) * kTileHalfH},
        }};
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
        for (size_t point = 0; point < points.size(); point++)
            drawEdge(points[point], points[(point + 1) % points.size()],
                     3.0f / zoom_, 0);
        for (size_t point = 0; point < points.size(); point++)
            drawEdge(points[point], points[(point + 1) % points.size()],
                     1.5f / zoom_, 255);
    }

    // --- objects -------------------------------------------------------
    std::stable_sort(g_draws.begin(), g_draws.end(),
                     [](const SpriteDraw &a, const SpriteDraw &b) { return a.key < b.key; });
    for (const SpriteDraw &d : g_draws) r.draw(d.tex, d.q);
    stats_.sprites = (int)g_draws.size();

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
        r.fillRect(sx - barWidth * 0.5f, barY, barWidth * health, barHeight,
                   20, 220, 55, 255);
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

    const Object *selectedAttacker = nullptr;
    bool mixedAttackModes = false;
    for (const Object &object : objects_) {
        if (!object.selected || !isSelectable(object) || !canAttack(object)) continue;
        if (!selectedAttacker)
            selectedAttacker = &object;
        else if (selectedAttacker->attackMode != object.attackMode)
            mixedAttackModes = true;
    }
    if (selectedAttacker) {
        const char *mode = "MIXED";
        if (!mixedAttackModes)
            switch (selectedAttacker->attackMode) {
            case AttackMode::Aggressive:
                mode = "AGGRESSIVE";
                break;
            case AttackMode::Defensive:
                mode = "DEFENSIVE";
                break;
            case AttackMode::StandGround:
                mode = "STAND GROUND";
                break;
            case AttackMode::Passive:
                mode = "PASSIVE";
                break;
            }
        const float invZoom = 1.0f / zoom_;
        const float boxX = 16.0f * invZoom;
        const float boxY = (screenH - 38.0f) * invZoom;
        const std::vector<std::string> line = {
            std::string("STANCE: ") + mode + "  TRIANGLE"};
        r.fillRect(boxX, boxY, 310.0f * invZoom, 26.0f * invZoom,
                   5, 8, 16, 210);
        drawBitmapText(r, line, boxX + 8.0f * invZoom,
                       boxY + 6.0f * invZoom, 1.5f * invZoom);
    }

    if (!currentInstruction_.empty()) {
        const float boxWidthPixels = std::min(720.0f, screenW - 32.0f);
        const int textColumns = std::max(20, (int)((boxWidthPixels - 24.0f) / 12.0f));
        const std::vector<std::string> lines = wrapText(currentInstruction_, textColumns);
        const float invZoom = 1.0f / zoom_;
        const float boxHeightPixels = 24.0f + lines.size() * 18.0f;
        const float boxX = 16.0f * invZoom;
        const float boxY = 16.0f * invZoom;
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

    if (cursorVisible_) {
        const float invZoom = 1.0f / zoom_;
        const float x = cursorX_ * invZoom, y = cursorY_ * invZoom;
        size_t frameIndex = kCursorNormal;
        if (cursorMode_ == CursorMode::Move) frameIndex = kCursorMove;
        if (cursorMode_ == CursorMode::Attack) frameIndex = kCursorAttack;
        if (cursors && frameIndex < cursors->frames.size()) {
            const SpriteFrame &frame = cursors->frames[frameIndex];
            const float width = frame.w * invZoom, height = frame.h * invZoom;
            const bool centered = cursorMode_ == CursorMode::Attack;
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
