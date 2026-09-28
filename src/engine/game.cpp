// SPDX-License-Identifier: GPL-3.0-or-later
#include "game.h"

#include <algorithm>
#include <cmath>

namespace swgb {

namespace {

constexpr float kPi = 3.14159265358979f;

// Terrain ids from genie_x1.dat's terrain table.
enum : uint8_t {
    T_GRASS1 = 0, T_WATER1 = 1, T_SHORE = 2, T_DIRT3 = 3, T_DIRT1 = 6, T_GRASS3 = 9,
    T_DIRT2 = 11, T_GRASS2 = 12, T_SAND = 14, T_WATER2 = 22, T_WATER3 = 23,
};

struct SpriteDraw {
    int64_t key;
    Texture *tex;
    Quad q;
};

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

void Game::generateTerrain(int size) {
    terrain_.assign((size_t)size * size, T_GRASS1);
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
        if (o.unit->type < dat::UT_DeadFish || o.unit->speed <= 0 || o.unit->type == dat::UT_Building) continue;
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
static bool pickFrame(const dat::Graphic &g, size_t slpFrames, float facing, float t, size_t &frame, bool &flip) {
    int angles = std::max<int>(1, g.angleCount);
    int perAngle = std::max<int>(1, g.frameCount);
    // World direction -> screen direction (iso projection), y grows downwards.
    float sdx = (std::cos(facing) - std::sin(facing)) * 2.0f;
    float sdy = (std::cos(facing) + std::sin(facing));
    float theta = std::atan2(sdy, sdx);                 // 0 = screen east
    float rel = theta - kPi / 2;                        // 0 = screen south
    float step = 2 * kPi / angles;
    int a = (int)std::lround(rel / step);
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
    int f = 0;
    if (g.frameDuration > 0 && perAngle > 1) {
        float cycle = perAngle * g.frameDuration + std::max(0.0f, g.replayDelay);
        float tt = std::fmod(t, cycle);
        f = std::min(perAngle - 1, (int)(tt / g.frameDuration));
    }
    frame = (size_t)a * perAngle + f;
    if (frame >= slpFrames) frame = slpFrames ? slpFrames - 1 : 0;
    return slpFrames > 0;
}

static std::vector<SpriteDraw> g_draws; // reused between frames

void Game::drawGraphic(Renderer &r, int graphicId, float sx, float sy, float facing, float animTime, int player,
                       int depth) {
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
                if (!pickFrame(self, sh->frames.size(), facing, animTime, fr, flip)) continue;
                const SpriteFrame &f = sh->frames[fr];
                float x = sx + d.offsetX - (flip ? f.w - f.hotX : f.hotX), y = sy + d.offsetY - f.hotY;
                Quad q{x, y, (float)f.w, (float)f.h, flip ? f.u + f.w : f.u, f.v, flip ? f.u : f.u + f.w, f.v + f.h};
                g_draws.push_back({(int64_t)std::min<int>(g->layer, 20) << 40 | (int64_t)(sy * 16 + 65536) << 8, f.tex, q});
            } else {
                drawGraphic(r, d.graphicId, sx + d.offsetX, sy + d.offsetY, facing, animTime, player, depth + 1);
            }
        }
        return;
    }
    const SpriteSheet *sh = assets_.sheet(g->slp, playerColorBase(player));
    if (!sh) return;
    size_t fr;
    bool flip;
    if (!pickFrame(*g, sh->frames.size(), facing, animTime, fr, flip)) return;
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
    drawGraphic(r, graphicId, sx, sy, facing, t, player, 0);
    std::stable_sort(g_draws.begin(), g_draws.end(),
                     [](const SpriteDraw &a, const SpriteDraw &b) { return a.key < b.key; });
    for (const SpriteDraw &d : g_draws) r.draw(d.tex, d.q);
    g_draws.clear();
}

void Game::render(Renderer &r, int screenW, int screenH) {
    stats_ = FrameStats{};
    r.beginFrame(screenW, screenH, zoom_, 0, 0, 0);
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

    for (int ty = y0; ty <= y1; ty++) {
        for (int tx = x0; tx <= x1; tx++) {
            float sx, sy;
            toScreen((float)tx, (float)ty, sx, sy);
            sx -= ox;
            sy -= oy;
            if (sx + kTileHalfW < 0 || sx - kTileHalfW > viewW || sy > viewH || sy + 2 * kTileHalfH < 0) continue;
            const dat::Terrain &t = terrains[terrainAt(tx, ty)];
            const SpriteSheet *sh = assets_.terrainSheet(t.slp);
            if (!sh || sh->frames.empty()) continue;
            int dim = (int)std::lround(std::sqrt((double)sh->frames.size()));
            if (dim < 1) dim = 1;
            size_t fi = (size_t)((tx % dim) + (ty % dim) * dim);
            if (fi >= sh->frames.size()) fi = 0;
            const SpriteFrame &f = sh->frames[fi];
            Quad q{sx - kTileHalfW, sy, (float)f.w, (float)f.h, f.u, f.v, f.u + f.w, f.v + f.h};
            r.draw(f.tex, q);
            stats_.tiles++;
        }
    }

    // --- objects -------------------------------------------------------
    g_draws.clear();
    for (const Object &o : objects_) {
        float sx, sy;
        toScreen(o.x, o.y, sx, sy);
        sx -= ox;
        sy -= oy;
        if (sx < -400 || sx > viewW + 400 || sy < -100 || sy > viewH + 500) continue;
        int gid = o.unit->standingGraphic[0];
        if (o.state == State::Walk && o.unit->walkingGraphic >= 0) gid = o.unit->walkingGraphic;
        drawGraphic(r, gid, sx, sy, o.facing, o.animTime, o.player, 0);
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
