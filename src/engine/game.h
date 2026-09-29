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
#include <unordered_map>
#include <vector>

namespace swgb {

struct InputState {
    float scrollX = 0, scrollY = 0; // -1..1, from stick / d-pad
    float dragX = 0, dragY = 0;     // screen pixels moved by touch this frame
    float pointerX = 0, pointerY = 0;
    float boxStartX = 0, boxStartY = 0, boxEndX = 0, boxEndY = 0;
    int screenW = 960, screenH = 544;
    int zoomStep = 0;               // -1 zoom out, +1 zoom in (edge-triggered)
    bool toggleDebug = false;
    bool cursorVisible = false;
    bool selectPressed = false;
    bool commandPressed = false;
    bool cycleAttackMode = false;
    bool pointerTap = false;
    bool boxSelectActive = false;
    bool boxSelectCommit = false;
};

struct FrameStats {
    int sprites = 0;
    int tiles = 0;
};

struct MovementStats {
    size_t pathingObjects = 0;
    size_t pendingMoveGoals = 0;
    size_t selectedPendingMoveGoals = 0;
    size_t overlappingPairs = 0;
    uint32_t firstOverlapObject = 0;
    uint32_t secondOverlapObject = 0;
    int firstOverlapUnit = -1;
    int secondOverlapUnit = -1;
    bool firstOverlapSelected = false;
    bool secondOverlapSelected = false;
    size_t terrainViolations = 0;
    size_t staticObstructionViolations = 0;
};

struct CombatStats {
    size_t activeOrders = 0;
    size_t ordersIssued = 0;
    size_t attacksLanded = 0;
    size_t unitsKilled = 0;
    size_t projectilesLaunched = 0;
    size_t activeProjectiles = 0;
    size_t activeRemains = 0;
    size_t attackPathsComputed = 0;
    size_t attackApproachRetries = 0;
    size_t automaticTargetsAcquired = 0;
    size_t retaliationOrders = 0;
    size_t armedBuildingsEngaged = 0;
    size_t attackModeChanges = 0;
};

struct MovingObjectInfo {
    uint32_t spawnId = 0;
    int unitId = -1;
    int player = 0;
    float x = 0, y = 0;
    float targetX = 0, targetY = 0;
    float waypointX = 0, waypointY = 0;
    float blockedTime = 0;
    bool moveGoalActive = false;
    bool selected = false;
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
    void setSoundPlayer(std::function<float(const std::string &)> fn) {
        playSound_ = std::move(fn);
    }
    void setUnitSoundPlayer(std::function<void(int, int)> fn) {
        playUnitSound_ = std::move(fn);
    }

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
    bool researchTechnology(int player, int technologyId);
    const std::string &currentInstruction() const { return currentInstruction_; }
    size_t activeObjectCount() const;
    size_t selectedObjectCount() const;
    size_t selectedMovingObjectCount() const;
    MovementStats movementStats() const;
    CombatStats combatStats() const;
    float objectHitPoints(uint32_t spawnId) const;
    float objectMaxHitPoints(uint32_t spawnId) const;
    int objectAttackDamage(uint32_t sourceId,
                           uint32_t targetId) const;
    bool objectSelected(uint32_t spawnId) const;
    bool objectScreenPosition(uint32_t spawnId, int screenW, int screenH,
                              float &screenX, float &screenY) const;
    std::vector<MovingObjectInfo> movingObjects() const;

    static constexpr int kTileHalfW = 48;
    static constexpr int kTileHalfH = 24;

private:
    enum class State : uint8_t { Idle, Walk, Attack };
    enum class CursorMode : uint8_t { Normal, Move, Attack };
    enum class FormationType : uint8_t { Line, Box, Staggered, Flank };
    enum class AttackMode : uint8_t {
        Aggressive,
        Defensive,
        StandGround,
        Passive,
    };

    struct Object {
        const dat::Unit *unit = nullptr;
        const dat::Unit *gateClosedUnit = nullptr;
        const dat::Unit *gateOpenUnit = nullptr;
        const dat::Unit *gateEndUnit = nullptr;
        std::deque<const dat::Unit *> productionQueue;
        int player = 0; // 0 = gaia
        float x = 0, y = 0;
        float facing = 0; // radians, 0 = +x world axis
        State state = State::Idle;
        float animTime = 0;
        float stateTime = 0;
        float targetX = 0, targetY = 0;
        float hitPoints = 1, maxHitPoints = 1;
        std::vector<std::array<float, 2>> path;
        size_t pathIndex = 0;
        float blockedTime = 0;
        float moveRetryTime = 0;
        float moveStallTime = 0;
        float moveBestDistance = 0;
        float moveSpeedLimit = 0;
        float attackCooldown = 0;
        float attackRepathTime = 0;
        float attackApproachAngle = 0;
        float attackApproachDistance = 0;
        float attackStallTime = 0;
        float attackBestDistance = 0;
        float autoAcquireTime = 0;
        uint32_t attackTargetId = 0;
        uint32_t moveGroupId = 0;
        uint8_t attackSlotRetries = 0;
        uint8_t moveSpreadRetries = 0;
        float homeX = 0, homeY = 0;
        float moveAnchorX = 0, moveAnchorY = 0;
        AttackMode attackMode = AttackMode::Aggressive;
        bool attackAutomatic = false;
        bool moveGoalActive = false;
        bool wander = true;
        bool drawShadows = true;
        bool active = true;
        bool hidden = false;
        bool draw = true;
        bool locked = false;
        bool gate = false;
        bool selected = false;
        bool triggerAddressable = true;
        float flashTime = 0;
        float gateOpenAmount = 0;
        float productionRemaining = 0;
        uint32_t spawnId = 0;
        int32_t garrisonedInId = -1;
        uint16_t initialFrame = 0;
    };

    struct Projectile {
        const dat::Unit *unit = nullptr;
        int player = 0;
        float x = 0, y = 0, z = 0;
        float targetZ = 0;
        float facing = 0;
        float animTime = 0;
        uint32_t targetId = 0;
        uint32_t sourceId = 0;
        int damage = 0;
    };

    struct Remains {
        const dat::Unit *deadUnit = nullptr;
        int dyingGraphic = -1;
        int player = 0;
        float x = 0, y = 0;
        float facing = 0;
        float age = 0;
        float dyingDuration = 0;
        float remainsDuration = 0;
        bool drawShadows = true;
    };

    struct TriggerRuntime {
        bool enabled = false;
        bool fired = false;
        float elapsed = 0;
    };

    struct PathGridCache {
        int terrainRestriction = -1;
        int radiusHundredths = 0;
        bool air = false;
        std::vector<uint8_t> passable;
    };

    struct Instruction {
        std::string text;
        std::string sound;
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
    void rebuildMobileOccupancy();
    void updateTriggers(float dt);
    bool conditionMet(const ScenarioCondition &condition, float triggerElapsed);
    void executeEffect(const ScenarioEffect &effect);
    void setTriggerEnabled(int id, bool enabled);
    std::vector<Object *> effectTargets(const ScenarioEffect &effect);
    bool objectMatches(const Object &object, int unitId, int player, int group, int type) const;
    bool inSourceArea(const Object &object, int x1, int y1, int x2, int y2) const;
    bool issueMove(Object &object, float targetX, float targetY);
    void issueGroupMove(std::vector<Object *> targets, float targetX, float targetY,
                        FormationType formation = FormationType::Line);
    bool findPath(const Object &object, float targetX, float targetY,
                  std::vector<std::array<float, 2>> &path) const;
    bool positionPassable(const Object &object, float x, float y, bool dynamic) const;
    bool terrainPassable(const Object &object, float x, float y) const;
    bool isAirUnit(const Object &object) const;
    bool isEnemy(const Object &source, const Object &target) const;
    bool canAttack(const Object &object) const;
    float attackRange(const Object &source, const Object &target) const;
    int attackDamage(const Object &source, const Object &target) const;
    int modifiedAttackAmount(
        const Object &source,
        const dat::AttackOrArmor &attack) const;
    int modifiedArmourAmount(const Object &target, int armourClass,
                             bool &present) const;
    bool technologyCommandApplies(
        const dat::EffectCommand &command,
        const Object &object) const;
    int graphicSound(int graphicId) const;
    float collisionRadius(const Object &object) const;
    bool isInspectable(const Object &object) const;
    bool isSelectable(const Object &object) const;
    bool hasSelectedUnit() const;
    bool hasSelectedAttacker() const;
    void clearSelection();
    Object *objectAtScreen(float screenX, float screenY, int screenW, int screenH);
    Object *enemyAtScreen(float screenX, float screenY, int screenW, int screenH);
    void selectAtScreen(float screenX, float screenY, int screenW, int screenH);
    void selectBox(float startX, float startY, float endX, float endY,
                   int screenW, int screenH);
    bool handleSelectionPanelClick(float screenX, float screenY,
                                   int screenW, int screenH);
    bool handleActionMenuClick(float screenX, float screenY,
                               int screenW, int screenH);
    bool openSelectedActionMenu();
    std::vector<const dat::Unit *> productionOptions(
        const Object &building) const;
    void commandAtScreen(float screenX, float screenY, int screenW, int screenH);
    void cycleSelectedAttackMode();
    void issueAttack(Object &source, Object &target, float approachAngle,
                     bool automatic = false,
                     float approachDistance = 0);
    void acquireAutomaticTarget(Object &source);
    void finishAttack(Object &source, bool returnToPost);
    void retryAttackApproach(Object &source);
    float automaticAcquisitionRadius(const Object &source) const;
    float automaticPursuitLeash(const Object &source) const;
    void updateAttack(Object &source, float dt);
    void launchProjectile(const Object &source, const Object &target, int damage);
    void updateProjectiles(float dt);
    void updateRemains(float dt);
    void damageObject(Object &object, int damage, uint32_t attackerId);
    void killObject(Object &object);
    void objectScreenPosition(const Object &object, int screenW, int screenH,
                              float &screenX, float &screenY) const;
    void screenToWorld(float screenX, float screenY, int screenW, int screenH,
                       float &worldX, float &worldY) const;
    void playUnitAcknowledgement(const Object &object, bool attack);
    void playWorldUnitSound(const Object &object, int soundId);
    bool worldSoundAudible(float x, float y) const;
    void startInstruction(Instruction instruction);
    void queueInstruction(const std::string &text, float duration,
                          const std::string &sound = std::string());
    void log(const std::string &message) const;
    int playerColorBase(int player) const;
    int terrainAt(int x, int y) const { return terrain_[(size_t)y * mapSize_ + x]; }
    float elevationAt(float x, float y) const;

    void drawGraphic(Renderer &r, int graphicId, float sx, float sy, float facing, float animTime, int player,
                     int initialFrame, int depth, bool drawShadows, float viewW, float viewH,
                     int sortLayerOverride = -1, int sortBias = 0);
    int graphicSortLayer(int graphicId, int depth = 0) const;

    Assets &assets_;
    std::mt19937 rng_;
    int mapSize_ = 0;
    std::vector<uint8_t> terrain_;
    std::vector<uint8_t> cornerElevation_;
    std::vector<uint8_t> tileElevation_;
    std::vector<uint8_t> tileSlope_;
    std::vector<Object> objects_;
    std::unordered_map<uint32_t, size_t> objectIndices_;
    std::vector<Projectile> projectiles_;
    std::vector<Remains> remains_;
    std::vector<uint32_t> mobileObjectIndices_;
    std::vector<std::vector<uint32_t>> mobileObjectCells_;
    std::vector<uint32_t> combatObjectIndices_;
    std::vector<std::vector<uint32_t>> combatObjectCells_;
    int mobileObjectGridWidth_ = 0;
    float maxMobileCollisionRadius_ = 0;
    std::vector<uint32_t> staticObstructionIndices_;
    std::vector<std::vector<uint32_t>> staticObstructionCells_;
    mutable std::vector<PathGridCache> pathGridCache_;
    mutable std::vector<int> pathCostScratch_;
    mutable std::vector<int> pathParentScratch_;
    mutable std::vector<uint32_t> pathSearchStamp_;
    mutable uint32_t pathSearchGeneration_ = 0;
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
    uint32_t nextMoveGroupId_ = 1;
    int difficulty_ = 2;
    int victoryState_ = -1;
    std::set<int> warnedEffects_;
    std::set<int> warnedConditions_;
    int localPlayer_ = 0;
    float camX_ = 0, camY_ = 0; // world-pixel position of the screen centre
    float zoom_ = 1.0f;
    float cursorX_ = 0, cursorY_ = 0;
    float boxStartX_ = 0, boxStartY_ = 0, boxEndX_ = 0, boxEndY_ = 0;
    float commandMarkerX_ = 0, commandMarkerY_ = 0, commandMarkerTime_ = 0;
    float selectionClickAge_ = 1000.0f;
    float lastSelectionX_ = 0, lastSelectionY_ = 0;
    int lastSelectionUnitId_ = -1;
    bool cursorVisible_ = false;
    CursorMode cursorMode_ = CursorMode::Normal;
    FormationType selectedFormation_ = FormationType::Line;
    bool actionMenuOpen_ = false;
    uint32_t actionMenuObjectId_ = 0;
    std::string statusMessage_;
    float statusTime_ = 0;
    bool boxSelectActive_ = false;
    bool debug_ = false;
    FrameStats stats_;
    size_t attackOrdersIssued_ = 0;
    size_t attacksLanded_ = 0;
    size_t unitsKilled_ = 0;
    size_t projectilesLaunched_ = 0;
    size_t attackPathsComputed_ = 0;
    size_t attackApproachRetries_ = 0;
    size_t automaticTargetsAcquired_ = 0;
    size_t retaliationOrders_ = 0;
    size_t armedBuildingsEngaged_ = 0;
    size_t attackModeChanges_ = 0;
    std::function<void(const std::string &)> log_;
    std::function<float(const std::string &)> playSound_;
    std::function<void(int, int)> playUnitSound_;
};

} // namespace swgb
