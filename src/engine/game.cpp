// SPDX-License-Identifier: GPL-3.0-or-later
#include "game.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

namespace swgb {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr uint8_t kSequenceAnimated = 0x1;

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
    nextSpawnId_ = 1;
    victoryState_ = -1;
    warnedEffects_.clear();
    warnedConditions_.clear();
    localPlayer_ = 0;
    for (size_t i = 0; i < players_.size(); i++) players_[i].color = (uint32_t)i;
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
    object.x = object.homeX = object.targetX = x;
    object.y = object.homeY = object.targetY = y;
    object.facing = facing;
    object.spawnId = spawnId;
    object.initialFrame = initialFrame;
    object.hidden = hidden;
    object.draw = unit->name.rfind("ENGINE-", 0) != 0 &&
                  unit->name.rfind("OBJ-FLAG", 0) != 0;
    object.garrisonedInId = garrisonedInId;
    object.triggerAddressable = triggerAddressable;
    objects_.push_back(object);
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
    for (Object &object : objects_)
        if (object.spawnId == spawnId) return &object;
    return nullptr;
}

const Game::Object *Game::findObject(uint32_t spawnId) const {
    if (!spawnId) return nullptr;
    for (const Object &object : objects_)
        if (object.spawnId == spawnId) return &object;
    return nullptr;
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
    if (!instruction.sound.empty() && playSound_) playSound_(instruction.sound);
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
    case 7:
        for (Object *object : effectTargets(effect)) object->locked = effect.type == 7;
        break;
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
        if (hasTarget) {
            for (Object *object : effectTargets(effect)) {
                if (object->hidden || object->unit->speed <= 0) continue;
                object->targetX = targetX;
                object->targetY = targetY;
                object->state = State::Walk;
                object->wander = false;
                object->animTime = 0;
            }
        }
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

    const float scrollSpeed = 900.0f / zoom_;
    camX_ += in.scrollX * scrollSpeed * dt - in.dragX / zoom_;
    camY_ += in.scrollY * scrollSpeed * dt - in.dragY / zoom_;
    // Clamp the camera to the map diamond's bounding box.
    float minX = -mapSize_ * kTileHalfW, maxX = mapSize_ * kTileHalfW;
    float maxY = 2.0f * mapSize_ * kTileHalfH;
    camX_ = std::max(minX, std::min(maxX, camX_));
    camY_ = std::max(0.0f, std::min(maxY, camY_));

    if (in.zoomStep > 0) zoom_ = std::min(1.0f, zoom_ * 1.25f);
    if (in.zoomStep < 0) zoom_ = std::max(0.4f, zoom_ / 1.25f);
    if (in.toggleDebug) debug_ = !debug_;

    updateTriggers(dt);

    std::uniform_real_distribution<float> r01(0, 1);
    for (Object &o : objects_) {
        if (!o.active) continue;
        o.animTime += dt;
        o.flashTime = std::max(0.0f, o.flashTime - dt);
        if (o.hidden || o.unit->type < dat::UT_DeadFish || o.unit->speed <= 0 ||
            o.unit->type == dat::UT_Building)
            continue;
        o.stateTime -= dt;
        if (o.state == State::Idle) {
            if (!o.wander) continue;
            if (o.stateTime <= 0) {
                // Wander to a nearby point around home.
                float a = r01(rng_) * 2 * kPi, d = 1.0f + r01(rng_) * 4.0f;
                o.targetX = std::max(1.0f, std::min(mapSize_ - 2.0f, o.homeX + std::cos(a) * d));
                o.targetY = std::max(1.0f, std::min(mapSize_ - 2.0f, o.homeY + std::sin(a) * d));
                o.state = State::Walk;
                o.animTime = 0;
            }
        } else {
            float dx = o.targetX - o.x, dy = o.targetY - o.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            float step = o.unit->speed * dt;
            if (dist <= step) {
                o.x = o.targetX;
                o.y = o.targetY;
                o.state = State::Idle;
                o.stateTime = 1.5f + r01(rng_) * 5.0f;
                o.animTime = 0;
            } else {
                o.x += dx / dist * step;
                o.y += dy / dist * step;
                o.facing = std::atan2(dy, dx);
            }
        }
    }
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
        drawGraphic(r, gid, sx, sy, o.facing, o.animTime, o.player, o.initialFrame, 0,
                    o.drawShadows && !overview, viewW, viewH);
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

    // --- objects -------------------------------------------------------
    std::stable_sort(g_draws.begin(), g_draws.end(),
                     [](const SpriteDraw &a, const SpriteDraw &b) { return a.key < b.key; });
    for (const SpriteDraw &d : g_draws) r.draw(d.tex, d.q);
    stats_.sprites = (int)g_draws.size();

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

    if (!currentInstruction_.empty()) {
        const std::vector<std::string> lines = wrapText(currentInstruction_, 72);
        const float invZoom = 1.0f / zoom_;
        const float boxHeightPixels = 24.0f + lines.size() * 18.0f;
        const float boxX = 20.0f * invZoom;
        const float boxY = (screenH - boxHeightPixels - 16.0f) * invZoom;
        r.fillRect(boxX, boxY, (screenW - 40.0f) * invZoom, boxHeightPixels * invZoom,
                   5, 8, 16, 220);
        r.fillRect(boxX, boxY, (screenW - 40.0f) * invZoom, 2.0f * invZoom,
                   210, 210, 190, 255);
        drawBitmapText(r, lines, 32.0f * invZoom, boxY + 12.0f * invZoom, 2.0f * invZoom);
    }
    r.endFrame();
}

} // namespace swgb
