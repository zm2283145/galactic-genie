// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone-1 "sandbox" game: a generated map with terrain, buildings and
// units that idle and wander using their real SWGB animations.
#pragma once

#include "assets.h"
#include "../core/scenario.h"

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace swgb {

struct InputState {
    float scrollX = 0, scrollY = 0; // -1..1, from stick / d-pad
    float dragX = 0, dragY = 0;     // screen pixels moved by touch this frame
    int zoomStep = 0;               // -1 zoom out, +1 zoom in (edge-triggered)
    bool toggleDebug = false;
};

struct FrameStats {
    int sprites = 0;
    int tiles = 0;
};

class Game {
public:
    explicit Game(Assets &assets) : assets_(assets) {}

    bool init(uint32_t seed, int mapSize, std::string *err);
    bool initScenario(const Scenario &scenario, std::string *err);
    void update(float dt, const InputState &in);
    void render(Renderer &r, int screenW, int screenH);

    // Centre the camera on a tile (used by tools and at startup).
    void lookAt(float tx, float ty);
    const FrameStats &stats() const { return stats_; }

    // Debug helper: draws one graphic immediately at a screen position.
    void drawGraphicNow(Renderer &r, int graphicId, float sx, float sy, float facing, float t, int player);
    float zoom() const { return zoom_; }
    void setZoom(float z) { zoom_ = z; }
    bool debug() const { return debug_; }

    static constexpr int kTileHalfW = 48;
    static constexpr int kTileHalfH = 24;

private:
    enum class State : uint8_t { Idle, Walk };

    struct Object {
        const dat::Unit *unit = nullptr;
        int player = 0; // 0 = gaia
        float x = 0, y = 0;
        float facing = 0; // radians, 0 = +x world axis
        State state = State::Idle;
        float animTime = 0;
        float stateTime = 0;
        float targetX = 0, targetY = 0;
        float homeX = 0, homeY = 0;
        bool wander = true;
        bool drawShadows = true;
        uint16_t initialFrame = 0;
    };

    void generateTerrain(int size);
    void buildTileElevation();
    void spawnBase(int player, int civ, char civLetter, float cx, float cy);
    const dat::Unit *findUnit(int civ, const std::string &name) const;
    const dat::Unit *findUnit(int civ, int id) const;
    Object *spawn(int civ, const std::string &name, int player, float x, float y, float facing);
    int playerColorBase(int player) const;
    int terrainAt(int x, int y) const { return terrain_[(size_t)y * mapSize_ + x]; }
    float elevationAt(float x, float y) const;

    void drawGraphic(Renderer &r, int graphicId, float sx, float sy, float facing, float animTime, int player,
                     int initialFrame, int depth, bool drawShadows, float viewW, float viewH);

    Assets &assets_;
    std::mt19937 rng_;
    int mapSize_ = 0;
    std::vector<uint8_t> terrain_;
    std::vector<uint8_t> cornerElevation_;
    std::vector<uint8_t> tileElevation_;
    std::vector<uint8_t> tileSlope_;
    std::vector<Object> objects_;
    std::array<ScenarioPlayer, 16> players_{};
    int localPlayer_ = 0;
    float camX_ = 0, camY_ = 0; // world-pixel position of the screen centre
    float zoom_ = 1.0f;
    float cameraMotionTime_ = 0;
    bool debug_ = false;
    FrameStats stats_;
};

} // namespace swgb
