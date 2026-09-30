// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone-1 "sandbox" game: a generated map with terrain, buildings and
// units that idle and wander using their real SWGB animations.
#pragma once

#include "assets.h"
#include "pathfinding.h"
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
    bool toggleCheatMenu = false;
    bool menuUp = false;
    bool menuDown = false;
    bool menuLeft = false;
    bool menuRight = false;
    bool menuActivate = false;
    bool menuBack = false;
    bool actionTabLeft = false;
    bool actionTabRight = false;
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
    bool initCompactTestMap(
        uint32_t seed, int mapSize,
        std::string *err);
    bool initScenario(const Scenario &scenario, std::string *err);
    void update(float dt, const InputState &in);
    void render(Renderer &r, int screenW, int screenH);

    // Centre the camera on a tile (used by tools and at startup).
    void lookAt(float tx, float ty);
    bool lookAtObject(uint32_t spawnId);
    bool selectObjectForTesting(uint32_t spawnId);
    bool selectObjectsForTesting(
        const std::vector<uint32_t> &spawnIds);
    // Group move of the given units, as a right-click with them selected.
    std::string describeObjectForTesting(uint32_t spawnId) const;
    // Places a gate at world (x, y) the way the cursor would; returns the
    // chosen variant id, or -(id + 2) if it could not be placed.
    int placeGateForTesting(uint32_t workerId, int civilization, float x, float y);
    bool garrisonForTesting(uint32_t unitId, uint32_t buildingId) {
        Object *u = findObject(unitId);
        Object *b = findObject(buildingId);
        if (!u || !b || !canGarrison(*u, *b)) return false;
        u->garrisonTargetId = buildingId;
        float tx = 0, ty = 0;
        interactionPoint(*u, *b, 0.35f, tx, ty);
        issueMove(*u, tx, ty, b, 0.35f);
        return true;
    }
    int garrisonVolleyForTesting(uint32_t buildingId) const {
        const Object *b = findObject(buildingId);
        return b ? garrisonVolleySize(*b) : 0;
    }
    size_t projectilesLaunchedForTesting() const { return projectilesLaunched_; }
    // Mean distance of marching members from their moving slots (-1 if no
    // group is marching).
    float marchShapeErrorForTesting() const;
    // Drags a wall of unitId from tile (x1,y1) to (x2,y2) for the worker;
    // returns the new foundation ids.
    std::vector<uint32_t> placeWallForTesting(uint32_t workerId, int civilization, int unitId,
                                              int x1, int y1, int x2, int y2);
    float objectFacing(uint32_t spawnId) const {
        const Object *o = findObject(spawnId);
        return o ? o->facing : 0.0f;
    }
    void groupMoveForTesting(const std::vector<uint32_t> &spawnIds, float x,
                             float y, int formation = -1);
    const FrameStats &stats() const { return stats_; }
    void setLogger(std::function<void(const std::string &)> fn) { log_ = std::move(fn); }
    void setSoundPlayer(std::function<float(const std::string &)> fn) {
        playSound_ = std::move(fn);
    }
    // Plays an interface sound by sounds-DRS resource id.
    void setInterfaceSoundPlayer(std::function<void(int)> fn) {
        playInterfaceSound_ = std::move(fn);
    }
    void setUnitSoundPlayer(std::function<void(int, int)> fn) {
        playUnitSound_ = std::move(fn);
    }
    void setAmbientSoundPlayer(
        std::function<float(const std::string &)> fn) {
        playAmbientSound_ = std::move(fn);
    }

    // Debug helper: draws one graphic immediately at a screen position.
    void drawGraphicNow(Renderer &r, int graphicId, float sx, float sy, float facing, float t, int player);
    float zoom() const { return zoom_; }
    void setZoom(float z) { zoom_ = z; }
    bool debug() const { return debug_; }
    bool triggerEnabled(size_t id) const;
    bool triggerFired(size_t id) const;
    bool gateLocked(uint32_t spawnId) const;
    size_t gateCount() const;
    void setLocalPlayerForTesting(int player) { localPlayer_ = player; }
    size_t garrisonedCount(uint32_t spawnId) const;
    bool garrisonCursorActive() const {
        return garrisonCursorActive_;
    }
    bool objectActive(uint32_t spawnId) const;
    float resource(int player, int resourceId) const;
    bool researchTechnology(int player, int technologyId);
    const std::string &currentInstruction() const { return currentInstruction_; }
    size_t activeObjectCount() const;
    size_t underConstructionObjectCount() const;
    size_t selectedObjectCount() const;
    size_t selectedMovingObjectCount() const;
    MovementStats movementStats() const;
    CombatStats combatStats() const;
    float objectHitPoints(uint32_t spawnId) const;
    float objectMaxHitPoints(uint32_t spawnId) const;
    float objectShieldPoints(uint32_t spawnId) const;
    float objectMaxShieldPoints(uint32_t spawnId) const;
    bool objectShielded(uint32_t spawnId) const;
    uint32_t spawnObjectForTesting(int civilization, int unitId, int player,
                                   float x, float y);
    uint32_t spawnFoundationForTesting(
        int civilization, int unitId, int player,
        float x, float y,
        const std::vector<uint32_t> &builderIds);
    bool completeFoundationForTesting(uint32_t spawnId);
    bool setConstructionProgressForTesting(
        uint32_t spawnId, float progress);
    bool damageObjectForTesting(uint32_t spawnId, int damage);
    bool moveObjectForTesting(uint32_t spawnId, float x, float y);
    int objectPlayer(uint32_t spawnId) const;
    bool setGateLockedForTesting(
        uint32_t spawnId, bool locked);
    bool positionPassableForTesting(
        uint32_t spawnId, float x, float y) const;
    bool issueRepairForTesting(
        uint32_t workerId, uint32_t targetId);
    bool issueGatherForTesting(
        uint32_t workerId, uint32_t targetId);
    bool issueDropOffForTesting(
        uint32_t workerId, uint32_t buildingId);
    bool objectPoweredForTesting(uint32_t spawnId) const;
    bool destroyLastSelectedForTesting();
    bool ejectGarrisonedUnitForTesting(
        uint32_t buildingId, uint32_t unitId);
    void setResourceForTesting(
        int player, int resourceType,
        float amount);
    void setDiplomacyForTesting(
        int sourcePlayer, int targetPlayer,
        uint32_t stance);
    bool actionMenuOpenForTesting() const {
        return actionMenuOpen_;
    }
    size_t productionQueueSizeForTesting(uint32_t spawnId) const {
        const Object *object = findObject(spawnId);
        return object ? object->productionQueue.size() : 0;
    }
    float productionRemainingForTesting(uint32_t spawnId) const {
        const Object *object = findObject(spawnId);
        return object ? object->productionRemaining : 0.0f;
    }
    int objectUnitId(uint32_t spawnId) const;
    int objectAttackDamage(uint32_t sourceId,
                           uint32_t targetId) const;
    bool objectSelected(uint32_t spawnId) const;
    std::vector<uint32_t> selectedObjectIds() const;
    std::vector<int> productionOptionIds(
        uint32_t spawnId) const;
    std::vector<int> researchOptionIds(
        uint32_t spawnId) const;
    std::vector<int> buildingOptionIds(
        uint32_t spawnId) const;
    std::vector<int> buildingOptionIds(
        uint32_t spawnId, int interfaceKind) const;
    std::vector<uint32_t> underConstructionObjectIds() const;
    uint32_t constructionBuilderId(
        uint32_t spawnId) const;
    bool objectIsBuilder(uint32_t spawnId) const;
    bool objectGatheringTarget(
        uint32_t spawnId, uint32_t targetId) const;
    bool objectBuildingTarget(
        uint32_t spawnId, uint32_t targetId) const;
    float objectCarriedAmount(uint32_t spawnId) const;
    float objectResourceAmount(uint32_t spawnId) const;
    float objectStashForTesting(uint32_t spawnId, int type) const {
        const Object *o = findObject(spawnId);
        return o && type >= 0 && type < 4 ? o->stash[(size_t)type] : 0.0f;
    }
    void setTextureBudget(size_t bytes) { textureBudget_ = bytes; }
    int reseedQueueForTesting(int player) const { return reseedQueue_[(size_t)player]; }
    bool issueAttackForTesting(uint32_t sourceId, uint32_t targetId) {
        Object *a = findObject(sourceId);
        Object *b = findObject(targetId);
        if (!a || !b || !isEnemy(*a, *b) ||
            !canAttackTarget(*a, *b))
            return false;
        issueAttack(*a, *b, std::atan2(a->y - b->y, a->x - b->x));
        return true;
    }
    bool canAttackTargetForTesting(
        uint32_t sourceId, uint32_t targetId) const {
        const Object *source = findObject(sourceId);
        const Object *target = findObject(targetId);
        return source && target &&
               canAttackTarget(*source, *target);
    }
    void setAttackModeForTesting(uint32_t spawnId, int mode) {
        if (Object *o = findObject(spawnId)) o->attackMode = (AttackMode)mode;
    }
    size_t projectileCountForTesting() const { return projectiles_.size(); }
    int firstProjectileUnitForTesting() const {
        return projectiles_.empty() || !projectiles_.front().unit
                   ? -1
                   : projectiles_.front().unit->id;
    }
    uint32_t attackTargetForTesting(
        uint32_t spawnId) const {
        const Object *object =
            findObject(spawnId);
        return object
                   ? object->attackTargetId
                   : 0;
    }
    float minimumRangeForTesting(uint32_t id) const { const Object *o = findObject(id); return o ? minimumRange(*o) : -1.0f; }
    void clearSelectionForTesting() { clearSelection(); }
    int terrainAtForTesting(int x, int y) const { return terrainAt(x, y); }
    bool setTerrainForTesting(int x, int y, int terrain) {
        if (x < 0 || y < 0 || x >= mapSize_ ||
            y >= mapSize_ || terrain < 0 ||
            terrain > 255)
            return false;
        terrain_[(size_t)y * mapSize_ + x] =
            (uint8_t)terrain;
        return true;
    }
    bool setCornerElevationForTesting(
        int x, int y, int elevation) {
        if (x < 0 || y < 0 || x > mapSize_ ||
            y > mapSize_ || elevation < 0 ||
            elevation > 255)
            return false;
        cornerElevation_[
            (size_t)y * (mapSize_ + 1) + x] =
            (uint8_t)elevation;
        buildTileElevation();
        return true;
    }
    std::array<float, 2>
    snappedBuildingPositionForTesting(
        int civilization, int unitId,
        float x, float y) const;
    bool placementValidForTesting(
        int civilization, int unitId,
        float x, float y);
    int placementFailureForTesting() const {
        return placementFailureCode_;
    }
    void setObjectResourceForTesting(uint32_t spawnId, float amount) {
        if (Object *o = findObject(spawnId)) o->resourceAmount = amount;
    }
    bool objectFelled(uint32_t spawnId) const {
        const Object *o = findObject(spawnId);
        return o && o->felled;
    }
    std::array<float, 2> objectPosition(
        uint32_t spawnId) const;
    bool technologyResearched(int player, int technologyId) const {
        return player >= 0 &&
               (size_t)player < researchedTechs_.size() &&
               researchedTechs_[(size_t)player].count(technologyId);
    }
    bool researchTechnologyForTesting(
        int player, int technologyId) {
        return researchTechnology(
            player, technologyId);
    }
    bool technologyRequirementsMetForTesting(
        int player, int technologyId) const {
        if (technologyId < 0 ||
            (size_t)technologyId >=
                assets_.dat().techs.size())
            return false;
        return technologyRequirementsMet(
            player,
            assets_.dat().techs[
                (size_t)technologyId]);
    }
    bool fullTechTreeUnlocked() const {
        return fullTechTreeCheat_;
    }
    bool objectScreenPosition(uint32_t spawnId, int screenW, int screenH,
                              float &screenX, float &screenY) const;
    std::vector<MovingObjectInfo> movingObjects() const;

    static constexpr int kTileHalfW = 48;
    static constexpr int kTileHalfH = 24;

private:
    enum class State : uint8_t {
        Idle,
        Walk,
        Attack,
        Build,
        Gather,
        Repair,
    };
    enum class CursorMode : uint8_t {
        Normal,
        Move,
        Attack,
        Garrison,
        Gather,
        Repair,
        Placement,
        GatherPoint,
    };
    enum class ActionMenuTab : uint8_t {
        Units,
        Research,
        Commands,
        Stances,
        Economy,
        Military,
        Defense,
    };
    enum class FormationType : uint8_t { Line, Box, Staggered, Flank };
    enum class AttackMode : uint8_t {
        Aggressive,
        Defensive,
        StandGround,
        Passive,
    };

    struct ProductionItem {
        const dat::Unit *unit = nullptr;
        int technologyId = -1;
        float duration = 0;
    };

    struct Object {
        const dat::Unit *unit = nullptr;
        const dat::Unit *gateClosedUnit = nullptr;
        const dat::Unit *gateOpenUnit = nullptr;
        const dat::Unit *gateEndUnit = nullptr;
        std::deque<ProductionItem> productionQueue;
        int player = 0; // 0 = gaia
        float x = 0, y = 0;
        float facing = 0; // radians, 0 = +x world axis
        State state = State::Idle;
        float animTime = 0;
        float stateTime = 0;
        float targetX = 0, targetY = 0;
        float hitPoints = 1, maxHitPoints = 1;
        float shieldPoints = 0, maxShieldPoints = 0;
        float resourceAmount = 0;
        float carriedAmount = 0;
        // Resources of other types kept when a worker switches jobs; a drop
        // site that accepts them takes them along with the carried load.
        std::array<float, 4> stash{};
        int resourceType = -1;
        int carriedResourceType = -1;
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
        bool underConstruction = false;
        bool manualDropOff = false;
        bool selected = false;
        bool triggerAddressable = true;
        float flashTime = 0;
        float gateOpenAmount = 0;
        float gateCloseTimer = 0;
        bool felled = false; // carbon tree cut down, still holding resources
        // Hunted/slaughtered animals leave a carcass object holding their food,
        // which decays at the animal's resource decay rate (per second).
        // A worker keeps the look and title of its last job (builder, ore
        // miner, ...) until given a different one, like the original's
        // variant units.
        const dat::Unit *jobUnit = nullptr;
        int jobKind = 0; // 0 none, 1 builder, 2 repairer, 10 + resource type
        uint32_t annexParentId = 0; // gate posts: the gate they belong to
        uint32_t carcassId = 0;   // on the dead animal: its carcass
        int carcassClass = -1;    // on a carcass: the animal's class
        float carcassDecay = 0.0f;
        float carcassHidden = 0.0f; // not drawn while the dying animation plays
        float huntTimer = 0.0f;
        uint32_t resourceWorkTargetId = 0;
        float resourceWorkTime = 0.0f;
        float damageSoundTime = 0.0f; // next fire/damage graphic sound
        int farmStage = -1; // farm terrain applied: 0 build, 1 grown, 2 dead
        std::vector<uint8_t> farmUnderlay; // terrain under a farm foundation
        float farmMoveTime = 0.0f;
        uint8_t farmMoveStep = 0;
        bool farmMoveActive = false;
        uint32_t pathGoalId = 0;      // object approached (region goal), 0 = point
        float pathGoalClearance = 0;
        uint8_t repathCount = 0;
        float detourTime = 0;
        float approachRetry = 0;
        // Gather (rally) point, command 0x78: building +0x214..0x228.
        bool rallyActive = false;
        float rallyX = 0, rallyY = 0;
        uint32_t rallyTargetId = 0;
        // Remaining garrison bolts of the current volley (0x55be20).
        int volleyRemaining = 0;
        float volleyTimer = 0;
        uint32_t volleyTargetId = 0;
        // Marching formation (group update 0x47e780): the group this unit
        // marches with, its steering speed and re-path throttle.
        // Last resource worked, so the worker can move on to the same kind
        // nearby when it runs out.
        int lastGatherClass = -1, lastGatherType = -1;
        float lastGatherX = 0, lastGatherY = 0;
        uint32_t marchGroupId = 0;
        float marchSpeed = 0;
        float marchRepath = 0;
        uint32_t blockerId = 0;     // unit that blocked the last step
        float productionRemaining = 0;
        float constructionRemaining = 0;
        float constructionTotal = 0;
        uint32_t constructionBuilderId = 0;
        uint32_t constructionTargetId = 0;
        uint32_t gatherTargetId = 0;
        uint32_t dropOffTargetId = 0;
        uint32_t repairTargetId = 0;
        uint32_t garrisonTargetId = 0;
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
        // Garrison bolts are aimed at a scattered point near the target and
        // only hurt it if they land on it.
        bool groundAimed = false;
        float aimX = 0, aimY = 0;
        float blastWidth = 0;   // splash radius (attacker or projectile)
        int blastLevel = 3;     // 3 = target only; <= 2 also hits friends
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
        bool air = false;
        int player = -1;
        int radiusHundredths = 0;
        TilePathfinder finder;
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
    bool isFlatFootprint(const Object &object) const;
    const dat::Unit *repairerUnit(const Object &worker) const;
    Object *objectAtScreenRaw(float screenX, float screenY, int screenW, int screenH, bool includeGatherables);
    void applyBlast(uint32_t sourceId, int sourcePlayer, float x, float y, uint32_t primaryId,
                    float width, int level, int fallbackDamage);
    float minimumRange(const Object &source) const;
    bool isLiveAnimal(const Object &object) const;
    int gatherClass(const Object &target) const {
        return target.carcassClass >= 0 ? target.carcassClass : (target.unit ? target.unit->cls : -1);
    }
    bool hasGarrisonTask(const Object &unit, const Object &container) const;
    bool isTransport(const Object &object) const;
    bool isFoodProcessingCenter(const Object &building) const;
    bool queueFarmReseed(const Object &building, const dat::Unit &farm);
    int carryTypeForSite(const Object &worker) const;
    bool siteAcceptsType(const Object &worker, const Object &building, int type) const;
    bool hasCarry(const Object &worker) const;
    bool depositAt(Object &worker, const Object &building);

    bool engagedWithCurrentTarget(const Object &object) const;
    bool footprintContainsScreen(const Object &object, float screenX, float screenY,
                                 int screenW, int screenH) const;
    void syncFarmTerrain(Object &farm, bool dying = false);
    float playerAttribute(int player, int attribute) const;
    const Object *findObject(uint32_t spawnId) const;
    int civilizationForPlayer(int player) const;
    void rebuildAdjacency();
    void classifyObject(size_t index);
    void rebuildObjectClassification();
    bool configureGate(Object &object);
    bool gateBlocks(const Object &gate, const Object &mover) const;
    void rebuildMobileOccupancy();
    void updateLivestockOwnership();
    void updateTriggers(float dt);
    bool conditionMet(const ScenarioCondition &condition, float triggerElapsed);
    void executeEffect(const ScenarioEffect &effect);
    void setTriggerEnabled(int id, bool enabled);
    std::vector<Object *> effectTargets(const ScenarioEffect &effect);
    bool objectMatches(const Object &object, int unitId, int player, int group, int type) const;
    bool inSourceArea(const Object &object, int x1, int y1, int x2, int y2) const;
    bool issueMove(Object &object, float targetX, float targetY,
                   const Object *goalObject = nullptr, float clearance = 0.0f);
    void issueGroupMove(std::vector<Object *> targets, float targetX, float targetY,
                        FormationType formation = FormationType::Line);
    bool findPath(const Object &object, float targetX, float targetY,
                  std::vector<std::array<float, 2>> &path,
                  const Object *goalObject = nullptr, float clearance = 0.0f) const;
    bool segmentClear(const Object &object, float ax, float ay, float bx, float by) const;
    bool staticPassableAt(const Object &object, float x, float y) const;
    const Object *unitBlockerAt(const Object &object, float x, float y) const;
    bool detourAround(Object &object, float goalX, float goalY);
    bool approach(Object &object, const Object &target, float clearance);
    bool positionPassable(
        const Object &object, float x, float y,
        bool dynamic, int *failure = nullptr) const;
    bool terrainPassable(const Object &object, float x, float y) const;
    bool isAirUnit(const Object &object) const;
    bool isEnemy(const Object &source, const Object &target) const;
    bool canAttack(const Object &object) const;
    bool canAttackTarget(
        const Object &source, const Object &target) const;
    void attackApproachPoint(
        const Object &source, const Object &target,
        float angle, float centerDistance,
        float &x, float &y) const;
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
    float modifiedUnitAttribute(const Object &object,
                                int attribute,
                                float baseValue) const;
    int graphicSound(int graphicId) const;
    float collisionRadius(const Object &object) const;
    bool workingOn(const Object &unit, uint32_t targetId) const;
    bool selfShielded(const Object &object) const;
    Object *nextResourceLike(const Object &worker);
    // Job acknowledgement: the original plays the attack sound (master
    // +0x120) of the worker's task variant (forager, lumberjack, miner...).
    void playJobAcknowledgement(const Object &worker, const Object *target, bool build,
                                bool repair);
    // Original rollover help (FUN_004d1520): creation/description string
    // + 20000 with <cost>/<hp>/<attack>/... tags expanded. Returns the body
    // (the part after the "Create <b>Name<b> (<cost>)" first line).
    std::string originalHelpText(int stringId, const dat::Unit *unit,
                                 const dat::Tech *technology) const;
    void unmetVisibleRequirements(int player, int technologyId, std::vector<int> &out,
                                  int depth = 0) const;
    float garrisonFirePower(const dat::Unit &unit) const;
    float buildingProjectileTotal(const Object &building) const;
    int garrisonVolleySize(const Object &building) const;
    void launchVolleyBolt(Object &source, const Object &target);
    enum class BuildingCommand : uint8_t { Eject, ToggleGate, SetGatherPoint, RemoveGatherPoint };
    std::vector<BuildingCommand> buildingCommands(const Object &building) const;
    int buildingCommandIcon(const Object &building, BuildingCommand command) const;
    std::string buildingCommandTitle(const Object &building, BuildingCommand command) const;
    std::string buildingCommandHelp(const Object &building, BuildingCommand command) const;
    void executeBuildingCommand(Object &building, BuildingCommand command);
    bool canSetGatherPoint(const Object &building) const;
    void setGatherPoint(Object &building, float screenX, float screenY, int screenW, int screenH);
    void sendToGatherPoint(const Object &building, Object &unit);
    struct WallTile {
        int x, y, frame;
    };
    std::vector<WallTile> wallLine(int x1, int y1, int x2, int y2) const;
    Object *createFoundation(const dat::Unit &unit, int player, float x, float y);
    bool placeWallLine(int x1, int y1, int x2, int y2);
    bool placeBuildingWorld(float worldX, float worldY);
    bool isWallPlacement() const;
    // Gate placement (0x60c100): picks the A/B/C/D gate from the walls
    // around the cursor tile; returns the unit id or -1 to keep the current.
    int gateVariantAt(int tileX, int tileY) const;
    bool isGateFoundation(const dat::Unit &unit) const {
        // 487/490/665/673 and their upgraded variants (all class 8).
        return unit.id == 487 || unit.id == 490 || unit.id == 665 || unit.id == 673 ||
               (unit.cls == 8 && unit.type == dat::UT_Building);
    }
    // Placement check that lets a gate replace the player's own wall pieces.
    bool placementValid(const dat::Unit &unit, float x, float y,
                        std::vector<Object *> *replacedWalls = nullptr);
    bool hasResearchTab(const Object &building) const;
    bool overlapsWorkingUnit(const Object &object, uint32_t targetId) const;
    // Closest spot around the target's footprint that no other unit is
    // standing on (the original treats the whole perimeter as the goal).
    bool freeInteractionPoint(const Object &source, const Object &target,
                              float clearance, float &x, float &y) const;
    void interactionPoint(const Object &source, const Object &target,
                          float clearance, float &x, float &y) const;
    bool withinInteractionRange(const Object &source, const Object &target,
                                float clearance) const;
    bool isInspectable(const Object &object) const;
    bool isSelectable(const Object &object) const;
    bool hasSelectedUnit() const;
    bool hasSelectedAttacker() const;
    void clearSelection();
    void selectObject(Object &object, bool first = false);
    void syncSelectionOrder();
    std::vector<Object *> selectedObjectsInOrder(bool selectableOnly);
    Object *objectAtScreen(float screenX, float screenY, int screenW,
                           int screenH, bool includeGatherables = false);
    Object *gatherableAtScreen(float screenX, float screenY,
                               int screenW, int screenH);
    Object *enemyAtScreen(float screenX, float screenY, int screenW, int screenH);
    void selectAtScreen(float screenX, float screenY, int screenW, int screenH);
    void selectBox(float startX, float startY, float endX, float endY,
                   int screenW, int screenH);
    bool handleSelectionPanelClick(float screenX, float screenY,
                                   int screenW, int screenH);
    bool handleActionMenuClick(float screenX, float screenY,
                               int screenW, int screenH);
    bool cancelProductionItem(Object &building, size_t index);
    bool openSelectedActionMenu();
    std::vector<const dat::Unit *> productionOptions(
        const Object &building) const;
    bool buildingMatchesLocation(
        const Object &building, int locationId) const;
    std::vector<int> researchOptions(
        const Object &building) const;
    std::string technologyDisplayName(
        int technologyId) const;
    std::vector<std::string> technologyRequirementLines(
        int player, int technologyId) const;
    std::vector<std::string> technologyEffectLines(
        int technologyId) const;
    std::vector<const dat::Unit *> buildingOptions(
        const Object &worker,
        ActionMenuTab category) const;
    bool unitAvailable(int player, int unitId) const;
    const dat::Unit *effectiveUnitForPlayer(
        int player, const dat::Unit *unit) const;
    void applyUnitUpgrades(int player);
    bool technologyRequirementsMet(int player,
                                  const dat::Tech &technology) const;
    void refreshAutomaticTechnologies(int player);
    bool technologyVisible(int player, const dat::Tech &technology) const;
    void refreshAllAutomaticTechnologies();
    std::string unitDisplayName(const dat::Unit &unit) const;
    std::string ownershipLabel(int player) const;
    std::string factionName(int civilization) const;
    char factionAbbreviation(int civilization) const;
    int civilizationGraphic(int graphicId, int player) const;
    bool isWorker(const Object &object) const;
    bool isPowerCore(const Object &object) const;
    bool isPowerSource(const Object &object) const;
    bool isShieldGenerator(const Object &object) const;
    bool graphicHasPowerIndicator(
        int graphicId, int depth = 0) const;
    bool requiresPower(const Object &building) const;
    bool isPowered(const Object &building) const;
    const Object *shieldGeneratorFor(
        const Object &object) const;
    bool isShielded(const Object &object) const;
    void updateShields(float dt);
    const dat::Unit *builderUnit(
        const Object &worker) const;
    int builderWorkingGraphic(
        const Object &worker) const;
    int repairWorkingGraphic(
        const Object &worker) const;
    bool isGatherable(const Object &object) const;
    const dat::Unit *gathererUnit(
        const Object &worker) const;
    const dat::Task *gatherTask(
        const Object &worker,
        const dat::Unit &gatherer) const;
    bool issueGatherCommand(
        Object &worker, Object &resource);
    bool issueDropOffCommand(
        Object &worker, Object &building);
    Object *nearestDropSite(
        const Object &worker,
        const dat::Unit &gatherer);
    bool buildingAcceptsResource(
        const Object &building,
        const dat::Unit &gatherer) const;
    bool assignAutomaticWorkerTask(
        Object &worker,
        const Object &completedBuilding);
    void updateGathering(float dt);
    bool isFriendlyPlayer(int sourcePlayer,
                          int targetPlayer) const;
    bool isRepairableBy(
        const Object &worker,
        const Object &target,
        bool requireDamage = true) const;
    bool issueRepairCommand(
        Object &worker, Object &target);
    void updateRepairing(float dt);
    bool canGarrison(const Object &unit,
                     const Object &building) const;
    bool findProductionExit(
        const Object &building, const dat::Unit &unit,
        const std::vector<std::array<float, 4>> &reserved,
        float &x, float &y) const;
    uint8_t garrisonCategory(const Object &unit) const;
    size_t garrisonedCount(const Object &building,
                           bool includeIncoming) const;
    bool issueGarrisonCommand(Object &building);
    void updateGarrisoning();
    bool ejectGarrisonedUnit(
        Object &building, Object &unit,
        size_t placementOffset = 0);
    size_t ejectGarrisoned(Object &building);
    bool destroyLastSelected();
    void setGateLocked(Object &gate, bool locked);
    bool beginBuildingPlacement(Object &worker,
                               const dat::Unit &building);
    void clearConstructionAssignment(Object &worker);
    bool assignBuilder(Object &worker, Object &building);
    bool placeBuilding(float screenX, float screenY,
                       int screenW, int screenH);
    void updateConstruction(float dt);
    void commandAtScreen(float screenX, float screenY, int screenW, int screenH);
    void cycleSelectedAttackMode();
    void setSelectedAttackMode(AttackMode mode);
    void issueAttack(Object &source, Object &target, float approachAngle,
                     bool automatic = false,
                     float approachDistance = 0);
    void acquireAutomaticTarget(Object &source);
    void finishAttack(Object &source, bool returnToPost);
    void retryAttackApproach(Object &source);
    float automaticAcquisitionRadius(const Object &source) const;
    float automaticPursuitLeash(const Object &source) const;
    void updateAttack(Object &source, float dt);
    const dat::Unit *projectileUnitForTarget(
        const Object &source, const Object &target) const;
    void launchProjectile(const Object &source, const Object &target, int damage);
    void updateProjectiles(float dt);
    void updateRemains(float dt);
    void damageObject(Object &object, int damage, uint32_t attackerId);
    void killObject(Object &object, bool countKill = true);
    void objectScreenPosition(const Object &object, int screenW, int screenH,
                              float &screenX, float &screenY) const;
    void screenToWorld(float screenX, float screenY, int screenW, int screenH,
                       float &worldX, float &worldY) const;
    void playUnitAcknowledgement(const Object &object, bool attack);
    void playWorldUnitSound(const Object &object, int soundId);
    bool worldSoundAudible(float x, float y) const;
    void updateAmbience(float dt, int screenW, int screenH);
    void activateCheat(size_t index, int screenW, int screenH);
    bool spawnCheatUnit(int unitId, bool requireWater,
                        int screenW, int screenH);
    void defeatCheatPlayer(int player);
    void startInstruction(Instruction instruction);
    void queueInstruction(const std::string &text, float duration,
                          const std::string &sound = std::string());
    void log(const std::string &message) const;
    int playerColorBase(int player) const;
    int terrainAt(int x, int y) const { return terrain_[(size_t)y * mapSize_ + x]; }
    float elevationAt(float x, float y) const;

    void drawGraphic(Renderer &r, int graphicId, float sx, float sy, float facing, float animTime, int player,
                     int initialFrame, int depth, bool drawShadows, float viewW, float viewH,
                     int sortLayerOverride = -1, int sortBias = 0,
                     float sortYOverride = -1000000000.0f,
                     uint32_t ownerId = 0,
                     bool outlineCandidate = false,
                     int powerState = -1,
                     int frameOverride = -1);
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
    mutable uint32_t pathSearches_ = 0;
    mutable std::vector<uint32_t> shieldGeneratorIndices_;
    mutable size_t shieldGeneratorCacheSize_ = (size_t)-1;
    mutable std::map<std::pair<int, int>, int>
        civilizationGraphicCache_;
    std::array<ScenarioPlayer, 16> players_{};
    std::array<std::map<int, float>, 17> resources_{};
    std::array<std::set<int>, 17> researchedTechs_{};
    std::array<std::set<int>, 17> disabledTechs_{};
    std::array<std::set<int>, 17> disabledUnits_{};
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
    uint32_t commandTargetId_ = 0;   // object an order was issued on
    float commandTargetTime_ = 0.0f; // its green acknowledgement blink
    void flashCommandTarget(const Object &target) {
        commandTargetId_ = target.spawnId;
        commandTargetTime_ = 1.0f;
        commandMarkerTime_ = 0.0f; // object orders show no ground marker
    }
    float selectionClickAge_ = 1000.0f;
    float animClock_ = 0.0f;
    size_t textureBudget_ = 64u * 1024u * 1024u;
    static constexpr int kReseedQueueMax = 40;
    std::array<int, 17> reseedQueue_{}; // prepaid farm reseeds per player
    struct PendingCarcass {
        const dat::Unit *deadUnit = nullptr;
        float amount = 0, decay = 0, x = 0, y = 0, facing = 0, dying = 0;
        int animalClass = -1, owner = 0;
        uint32_t animalId = 0;
    };
    std::vector<PendingCarcass> pendingCarcasses_;
    float lastSelectionX_ = 0, lastSelectionY_ = 0;
    int lastSelectionUnitId_ = -1;
    std::vector<uint32_t> selectionOrder_;
    bool cursorVisible_ = false;
    CursorMode cursorMode_ = CursorMode::Normal;
    FormationType selectedFormation_ = FormationType::Line;
    // Destination of each active group move, so a formation change can
    // re-form the group on the way there instead of stopping it.
    std::unordered_map<uint32_t, std::array<float, 2>> moveGroupDestinations_;
    // A formation on the march: an invisible leader follows the group path
    // at the slowest member's speed and leaves a trail; each row's anchor
    // is its depth back along that trail, and members steer to their slot.
    struct MarchGroup {
        uint32_t id = 0;
        std::vector<uint32_t> members;
        std::vector<std::array<float, 2>> offsets; // lateral, forward (<= 0)
        std::vector<std::array<float, 2>> path;
        size_t pathIndex = 0;
        float x = 0, y = 0, dirX = 1, dirY = 0;
        std::vector<std::array<float, 2>> trail; // newest first
        float spacing = 0.5f;
        bool straggling = false;
    };
    std::vector<MarchGroup> marchGroups_;
    void updateMarchGroups(float dt);
    std::array<float, 2> marchTrailPoint(const MarchGroup &group, float depth) const;
    bool actionMenuOpen_ = false;
    uint32_t actionMenuObjectId_ = 0;
    ActionMenuTab actionMenuTab_ = ActionMenuTab::Units;
    size_t actionMenuSelection_ = 0;
    size_t actionMenuScroll_ = 0;
    const dat::Unit *placementUnit_ = nullptr;
    int placementFailureCode_ = 0;
    uint32_t placementBuilderId_ = 0;
    // Set when placement starts from the build menu, so the same press
    // that picked the item cannot also place it.
    bool placementJustBegun_ = false;
    const std::vector<Object *> *placementIgnore_ = nullptr; // walls a gate replaces
    uint32_t gatherPointBuildingId_ = 0; // "click an area to set gather point" mode
    bool gatherPointJustBegun_ = false;
public:
    bool setGatherPointForTesting(uint32_t buildingId, float x, float y, uint32_t targetId = 0) {
        Object *b = findObject(buildingId);
        if (!b || !canSetGatherPoint(*b)) return false;
        b->rallyActive = true;
        b->rallyX = x;
        b->rallyY = y;
        b->rallyTargetId = targetId;
        return true;
    }
    bool queueUnitForTesting(uint32_t buildingId, int unitId);
private:
    // Wall placement (mouse mode 0x15 in the original): first press sets
    // the start tile, the preview follows the cursor, the next press
    // commits the L-shaped line of foundations.
    bool wallDragActive_ = false;
    int wallStartTileX_ = 0, wallStartTileY_ = 0;
    bool cheatMenuOpen_ = false;
    size_t cheatMenuSelection_ = 0;
    bool forceBuildCheat_ = false;
    bool fullTechTreeCheat_ = false;
    // Researched-tech derived caches (see unitAvailable/effectiveUnitForPlayer).
    uint64_t techGeneration_ = 1;
    mutable std::array<std::vector<int8_t>, 17> availableCache_{};
    mutable std::array<uint64_t, 17> availableCacheGeneration_{};
    mutable std::array<std::map<int, int>, 17> upgradeCache_{};
    mutable std::array<uint64_t, 17> upgradeCacheGeneration_{};
    bool forceExploreCheat_ = false;
    bool forceSightCheat_ = false;
    bool garrisonCursorActive_ = false;
    bool repairCursorActive_ = false;
    float ambienceTime_ = 2.0f;
    uint32_t ambienceSequence_ = 0;
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
    std::function<void(int)> playInterfaceSound_;
    std::function<void(int, int)> playUnitSound_;
    std::function<float(const std::string &)> playAmbientSound_;
};

} // namespace swgb
