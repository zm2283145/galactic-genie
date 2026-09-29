// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone-1 "sandbox" game: a generated map with terrain, buildings and
// units that idle and wander using their real SWGB animations.
#pragma once

#include "assets.h"
#include "../core/scenario.h"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <random>
#include <set>
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
    void setLogger(std::function<void(const std::string &)> fn) { log_ = std::move(fn); }

    // Debug helper: draws one graphic immediately at a screen position.
    void drawGraphicNow(Renderer &r, int graphicId, float sx, float sy, float facing, float t, int player);
    float zoom() const { return zoom_; }
    void setZoom(float z) { zoom_ = z; }
    bool debug() const { return debug_; }
    bool triggerEnabled(size_t id) const;
    bool triggerFired(size_t id) const;
    bool gateLocked(uint32_t spawnId) const;
    bool objectActive(uint32_t spawnId) const;
    float resource(int player, int resourceId) const;
    const std::string &currentInstruction() const { return currentInstruction_; }
    size_t activeObjectCount() const;

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
        bool active = true;
        bool hidden = false;
        bool draw = true;
        bool locked = false;
        bool triggerAddressable = true;
        float flashTime = 0;
        uint32_t spawnId = 0;
        int32_t garrisonedInId = -1;
        uint16_t initialFrame = 0;
    };

    struct TriggerRuntime {
        bool enabled = false;
        bool fired = false;
        float elapsed = 0;
    };

    struct Instruction {
        std::string text;
        float duration = 0;
    };

    void generateTerrain(int size);
    void buildTileElevation();
    void spawnBase(int player, int civ, char civLetter, float cx, float cy);
    const dat::Unit *findUnit(int civ, const std::string &name) const;
    const dat::Unit *findUnit(int civ, int id) const;
    Object *spawn(int civ, const std::string &name, int player, float x, float y, float facing);
    Object *addObject(const dat::Unit *unit, int player, float x, float y, float facing,
                      uint32_t spawnId, uint16_t initialFrame = 0, bool hidden = false,
                      int32_t garrisonedInId = -1, bool triggerAddressable = true);
    Object *findObject(uint32_t spawnId);
    const Object *findObject(uint32_t spawnId) const;
    int civilizationForPlayer(int player) const;
    void rebuildAdjacency();
    void updateTriggers(float dt);
    bool conditionMet(const ScenarioCondition &condition, float triggerElapsed);
    void executeEffect(const ScenarioEffect &effect);
    void setTriggerEnabled(int id, bool enabled);
    std::vector<Object *> effectTargets(const ScenarioEffect &effect);
    bool objectMatches(const Object &object, int unitId, int player, int group, int type) const;
    bool inSourceArea(const Object &object, int x1, int y1, int x2, int y2) const;
    void queueInstruction(const std::string &text, float duration);
    void log(const std::string &message) const;
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
    std::array<std::map<int, float>, 17> resources_{};
    std::array<std::set<int>, 17> researchedTechs_{};
    std::array<std::set<int>, 17> disabledTechs_{};
    std::vector<ScenarioTrigger> triggers_;
    std::vector<uint32_t> triggerOrder_;
    std::vector<TriggerRuntime> triggerRuntime_;
    std::deque<Instruction> instructions_;
    std::string currentInstruction_;
    float instructionTime_ = 0;
    uint32_t nextSpawnId_ = 1;
    int difficulty_ = 2;
    int victoryState_ = -1;
    std::set<int> warnedEffects_;
    std::set<int> warnedConditions_;
    int localPlayer_ = 0;
    float camX_ = 0, camY_ = 0; // world-pixel position of the screen centre
    float zoom_ = 1.0f;
    bool debug_ = false;
    FrameStats stats_;
    std::function<void(const std::string &)> log_;
};

} // namespace swgb
