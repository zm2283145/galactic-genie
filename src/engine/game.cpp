// SPDX-License-Identifier: GPL-3.0-or-later
#include "game.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

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
    objects_.clear();
    objects_.reserve(scenario.units.size());
    for (const ScenarioUnit &source : scenario.units) {
        if (source.garrisonedInId >= 0) continue;
        const int civilization = civilizationFor(source.player);
        const dat::Unit *unit = findUnit(civilization, source.unitId);
        if (!unit) continue;
        Object object;
        object.unit = unit;
        object.player = source.player;
        object.x = object.homeX = object.targetX = source.y;
        object.y = object.homeY = object.targetY = mapSize_ - source.x;
        const dat::Graphic *graphic = assets_.dat().graphic(unit->standingGraphic[0]);
        object.facing = graphic && graphic->angleCount == 5
                            ? source.rotation
                            : source.rotation - kPi * 0.5f;
        object.wander = false;
        object.initialFrame = source.initialFrame;
        objects_.push_back(object);
    }
    if (scenario.cameraX >= 0 && scenario.cameraY >= 0)
        lookAt(scenario.cameraY, mapSize_ - scenario.cameraX);
    else
        lookAt(mapSize_ * 0.5f, mapSize_ * 0.5f);
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

Game::Object *Game::spawn(int civ, const std::string &name, int player, float x, float y, float facing) {
    const dat::Unit *u = findUnit(civ, name);
    if (!u) return nullptr;
    Object o;
    o.unit = u;
    o.player = player;
    o.x = o.homeX = o.targetX = x;
    o.y = o.homeY = o.targetY = y;
    o.facing = facing;
    std::uniform_real_distribution<float> r01(0, 1);
    o.animTime = r01(rng_) * 3.0f;
    o.stateTime = 1.0f + r01(rng_) * 4.0f;
    objects_.push_back(o);
    return &objects_.back();
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

int Game::playerColorBase(int player) const {
    const auto &pc = assets_.dat().playerColours;
    if (player <= 0) return 16;
    size_t i = (size_t)(player - 1);
    return i < pc.size() ? pc[i].playerColorBase : 16;
}

void Game::lookAt(float tx, float ty) { toScreen(tx, ty, camX_, camY_); }

void Game::update(float dt, const InputState &in) {
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

    std::uniform_real_distribution<float> r01(0, 1);
    for (Object &o : objects_) {
        o.animTime += dt;
        if (!o.wander || o.unit->type < dat::UT_DeadFish || o.unit->speed <= 0 ||
            o.unit->type == dat::UT_Building)
            continue;
        o.stateTime -= dt;
        if (o.state == State::Idle) {
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
                       int initialFrame, int depth) {
    const dat::Graphic *g = assets_.dat().graphic(graphicId);
    if (!g) return;
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
                Quad q{x, y, (float)f.w, (float)f.h, flip ? f.u + f.w : f.u, f.v, flip ? f.u : f.u + f.w, f.v + f.h};
                g_draws.push_back({(int64_t)std::min<int>(g->layer, 20) << 40 | (int64_t)(sy * 16 + 65536) << 8, f.tex, q});
            } else {
                drawGraphic(r, d.graphicId, sx + d.offsetX, sy + d.offsetY, facing, animTime, player,
                            initialFrame, depth + 1);
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
    Quad q{x, y, (float)f.w, (float)f.h, flip ? f.u + f.w : f.u, f.v, flip ? f.u : f.u + f.w, f.v + f.h};
    // Sort: graphic layer first (shadows/rubble under units), then screen y.
    int64_t key = (int64_t)std::min<int>(g->layer, 20) << 40 | (int64_t)(sy * 16 + 65536) << 8 | (depth & 0xFF);
    g_draws.push_back({key, f.tex, q});
}

void Game::drawGraphicNow(Renderer &r, int graphicId, float sx, float sy, float facing, float t, int player) {
    g_draws.clear();
    drawGraphic(r, graphicId, sx, sy, facing, t, player, 0, 0);
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

    auto frameIndexFor = [](const SpriteSheet *sheet, int tx, int ty) -> size_t {
        if (!sheet || sheet->frames.empty()) return 0;
        int dim = (int)std::lround(std::sqrt((double)sheet->frames.size()));
        if (dim < 1) dim = 1;
        size_t index = (size_t)((tx % dim) + (ty % dim) * dim);
        if (index >= sheet->frames.size()) index = 0;
        return index;
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
            neighborSlopes.fill(-1);
            for (size_t i = 0; i < neighborSlopes.size(); i++) {
                int nx = tx + slopeNeighborX[i], ny = ty + slopeNeighborY[i];
                if (nx >= 0 && ny >= 0 && nx < mapSize_ && ny < mapSize_)
                    neighborSlopes[i] = tileSlope_[(size_t)ny * mapSize_ + nx];
            }
            const int terrainId = terrainAt(tx, ty);
            const dat::Terrain &terrain = terrains[terrainId];
            const dat::Terrain &draw = drawTerrain(terrains, terrainId);
            const SpriteSheet *flat = assets_.terrainSheet(draw.slp);
            assets_.terrainSlopeFrame(draw.slp, slope, frameIndexFor(flat, tx, ty), neighborSlopes);

            if (!assets_.hasBlendMasks()) continue;
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
                assets_.terrainSlopeFrame(drawOverlay.slp, slope, frameIndexFor(flatOverlay, tx, ty),
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
        float sx, sy;
        toScreen(o.x, o.y, sx, sy);
        sy -= elevationAt(o.x, o.y) * assets_.dat().terrainBlock.elevHeight;
        sx -= ox;
        sy -= oy;
        if (sx < -400 || sx > viewW + 400 || sy < -100 || sy > viewH + 500) continue;
        int gid = o.unit->standingGraphic[0];
        if (o.state == State::Walk && o.unit->walkingGraphic >= 0) gid = o.unit->walkingGraphic;
        drawGraphic(r, gid, sx, sy, o.facing, o.animTime, o.player, o.initialFrame, 0);
    }
    g_draws.clear();
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
            neighborSlopes.fill(-1);
            for (size_t i = 0; i < neighborSlopes.size(); i++) {
                int nx = tx + slopeNeighborX[i], ny = ty + slopeNeighborY[i];
                if (nx >= 0 && ny >= 0 && nx < mapSize_ && ny < mapSize_)
                    neighborSlopes[i] = tileSlope_[(size_t)ny * mapSize_ + nx];
            }
            const SpriteSheet *flatBase = assets_.terrainSheet(draw.slp);
            const size_t terrainFrame = frameIndexFor(flatBase, tx, ty);
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

            if (!assets_.hasBlendMasks()) continue;
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
                const size_t overlayFrame = frameIndexFor(flatOverlay, tx, ty);
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
    g_draws.clear();
    for (const Object &o : objects_) {
        float sx, sy;
        toScreen(o.x, o.y, sx, sy);
        sy -= elevationAt(o.x, o.y) * assets_.dat().terrainBlock.elevHeight;
        sx -= ox;
        sy -= oy;
        if (sx < -400 || sx > viewW + 400 || sy < -100 || sy > viewH + 500) continue;
        int gid = o.unit->standingGraphic[0];
        if (o.state == State::Walk && o.unit->walkingGraphic >= 0) gid = o.unit->walkingGraphic;
        drawGraphic(r, gid, sx, sy, o.facing, o.animTime, o.player, o.initialFrame, 0);
    }
    std::stable_sort(g_draws.begin(), g_draws.end(),
                     [](const SpriteDraw &a, const SpriteDraw &b) { return a.key < b.key; });
    for (const SpriteDraw &d : g_draws) r.draw(d.tex, d.q);
    stats_.sprites = (int)g_draws.size();

    if (debug_) {
        // Simple minimap in the corner: one pixel per tile, terrain colours.
        const float s = 2.0f / zoom_;
        const float mx = viewW - mapSize_ * s - 8 / zoom_, my = 8 / zoom_;
        r.fillRect(mx - 2 / zoom_, my - 2 / zoom_, mapSize_ * s + 4 / zoom_, mapSize_ * s + 4 / zoom_, 0, 0, 0, 180);
        for (int ty = 0; ty < mapSize_; ty++)
            for (int tx = 0; tx < mapSize_; tx++) {
                const dat::Terrain &t = terrains[terrainAt(tx, ty)];
                r.fillRect(mx + tx * s, my + ty * s, s, s, t.colors[0] ? assets_.palette()[t.colors[0]].r : 60,
                           t.colors[0] ? assets_.palette()[t.colors[0]].g : 120,
                           t.colors[0] ? assets_.palette()[t.colors[0]].b : 60, 255);
            }
    }
    r.endFrame();
}

} // namespace swgb
