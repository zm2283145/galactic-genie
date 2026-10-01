// SPDX-License-Identifier: GPL-3.0-or-later
// Genie-style simulation using the original SWGB data and presentation.
#pragma once

#include "assets.h"
#include "ai_script.h"
#include "pathfinding.h"
#include "skirmish.h"
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
#include <unordered_set>
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
    bool pointerDown = false;
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
    bool pausePressed = false;
    int controlGroup = -1;
    bool controlGroupAssign = false;
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

enum class MatchSaveKind : uint8_t {
    Skirmish = 0,
    Campaign = 1,
};

struct MatchSaveMetadata {
    MatchSaveKind kind = MatchSaveKind::Skirmish;
    SkirmishSettings skirmish;
    std::string campaignArchive;
    uint32_t campaignEntry = 0;
};

class Game {
public:
    explicit Game(Assets &assets) : assets_(assets) {}
    ~Game();

    bool init(uint32_t seed, int mapSize, std::string *err);
    bool initSkirmish(
        const SkirmishSettings &settings,
        std::string *err);
    void clearMatch();
    bool initCompactTestMap(
        uint32_t seed, int mapSize,
        std::string *err);
    bool initScenario(
        const Scenario &scenario, std::string *err,
        const std::string &campaignArchive = {},
        uint32_t campaignEntry = 0,
        int difficulty = 2,
        const std::string &aiDirectory = {});
    static bool supportsTriggerCondition(int type);
    static bool supportsTriggerEffect(int type);
    bool loadAiScript(
        int player, const std::string &path,
        const std::unordered_set<std::string> &defines,
        std::string *err = nullptr);
    bool loadAiSourceForTesting(
        int player, const std::string &name,
        const std::string &source,
        const std::unordered_set<std::string> &defines,
        std::string *err = nullptr,
        const std::unordered_map<
            std::string, std::string>
            *virtualFiles = nullptr);
    size_t aiRuleCountForTesting(
        int player) const;
    bool aiLoadedForTesting(
        int player) const {
        return player > 0 &&
               (size_t)player <
                   aiPlayers_.size() &&
               aiPlayers_[(size_t)player]
                   .loaded;
    }
    size_t aiMissingIncludeCountForTesting(
        int player) const {
        return player > 0 &&
                       (size_t)player <
                           aiPlayers_.size()
                   ? aiPlayers_[(size_t)player]
                         .program.missingFiles
                         .size()
                   : 0;
    }
    const std::vector<std::string> &
    aiMissingIncludesForTesting(
        int player) const {
        static const std::vector<std::string>
            empty;
        return player > 0 &&
                       (size_t)player <
                           aiPlayers_.size()
                   ? aiPlayers_[(size_t)player]
                         .program.missingFiles
                   : empty;
    }
    int aiGoalForTesting(
        int player, int goal) const;
    int aiConstantForTesting(
        int player,
        const std::string &name) const {
        if (player <= 0 ||
            (size_t)player >=
                aiPlayers_.size())
            return 0;
        const AiProgram &program =
            aiPlayers_[(size_t)player]
                .program;
        return program.hasConstant(name)
                   ? program.constant(name)
                   : 0;
    }
    size_t aiActionCountForTesting(
        int player,
        const std::string &actionName,
        const std::string &argument) const {
        if (player <= 0 ||
            (size_t)player >=
                aiPlayers_.size())
            return 0;
        size_t count = 0;
        for (const AiRule &rule :
             aiPlayers_[(size_t)player]
                 .program.rules)
            for (const AiNode &action :
                 rule.actions)
                if (normalizeAiSymbol(
                        action.name()) ==
                        normalizeAiSymbol(
                            actionName) &&
                    action.children.size() >
                        1 &&
                    normalizeAiSymbol(
                        action.children[1]
                            .value) ==
                        normalizeAiSymbol(
                            argument))
                    count++;
        return count;
    }
    size_t aiMatchingActionCountForTesting(
        int player,
        const std::string &actionName,
        const std::string &argument) {
        if (player <= 0 ||
            (size_t)player >=
                aiPlayers_.size())
            return 0;
        AiPlayerState &state =
            aiPlayers_[(size_t)player];
        size_t count = 0;
        for (AiRule &rule :
             state.program.rules) {
            if (!rule.enabled) continue;
            const bool hasAction =
                std::any_of(
                    rule.actions.begin(),
                    rule.actions.end(),
                    [&](const AiNode &action) {
                        return normalizeAiSymbol(
                                   action.name()) ==
                                   normalizeAiSymbol(
                                       actionName) &&
                               action.children.size() >
                                   1 &&
                               normalizeAiSymbol(
                                   action.children[1]
                                       .value) ==
                                   normalizeAiSymbol(
                                       argument);
                    });
            if (!hasAction) continue;
            bool matches = true;
            for (const AiNode &condition :
                 rule.conditions)
                if (evaluateAiCondition(
                        player, state,
                        condition) !=
                    AiTruth::True) {
                    matches = false;
                    break;
                }
            if (matches) count++;
        }
        return count;
    }
    std::string aiActionConditionReportForTesting(
        int player,
        const std::string &actionName,
        const std::string &argument) {
        if (player <= 0 ||
            (size_t)player >=
                aiPlayers_.size())
            return {};
        AiPlayerState &state =
            aiPlayers_[(size_t)player];
        std::string report;
        for (AiRule &rule :
             state.program.rules) {
            const bool hasAction =
                std::any_of(
                    rule.actions.begin(),
                    rule.actions.end(),
                    [&](const AiNode &action) {
                        return normalizeAiSymbol(
                                   action.name()) ==
                                   normalizeAiSymbol(
                                       actionName) &&
                               action.children.size() >
                                   1 &&
                               normalizeAiSymbol(
                                   action.children[1]
                                       .value) ==
                                   normalizeAiSymbol(
                                       argument);
                    });
            if (!hasAction) continue;
            report += "[";
            for (const AiNode &condition :
                 rule.conditions) {
                const AiTruth truth =
                    evaluateAiCondition(
                        player, state,
                        condition);
                report += normalizeAiSymbol(
                    condition.name());
                report += truth == AiTruth::True
                              ? ":T "
                          : truth == AiTruth::False
                              ? ":F "
                              : ":? ";
            }
            report += "]";
        }
        return report;
    }
    int aiStrategicNumberForTesting(
        int player,
        const std::string &name) const;
    int aiTechLevelForTesting(
        int player) const {
        return aiTechLevel(player);
    }
    uint32_t aiAttacksIssuedForTesting(
        int player) const {
        return player > 0 &&
                       (size_t)player <
                           aiPlayers_.size()
                   ? aiPlayers_[(size_t)player]
                         .attacksIssued
                   : 0;
    }
    uint32_t aiFormationOrdersForTesting(
        int player) const {
        return player > 0 &&
                       (size_t)player <
                           aiPlayers_.size()
                   ? aiPlayers_[(size_t)player]
                         .formationOrders
                   : 0;
    }
    uint32_t aiTransportLandingsForTesting(
        int player) const {
        return player > 0 &&
                       (size_t)player <
                           aiPlayers_.size()
                   ? aiPlayers_[(size_t)player]
                         .transportLandings
                   : 0;
    }
    size_t exploredTileCountForTesting(
        int player) const {
        if (player <= 0 ||
            (size_t)player >=
                exploredTiles_.size())
            return 0;
        return (size_t)std::count(
            exploredTiles_[(size_t)player]
                .begin(),
            exploredTiles_[(size_t)player]
                .end(),
            (uint8_t)1);
    }
    bool aiBuildForTesting(
        int player,
        const std::string &symbol,
        bool forward = false) {
        const dat::Unit *unit =
            aiUnit(player, symbol);
        return unit &&
               aiBuild(
                   player, *unit,
                   forward);
    }
    std::array<float, 2>
    newestFoundationPositionForTesting(
        int player) const {
        for (auto found = objects_.rbegin();
             found != objects_.rend();
             ++found)
            if (found->active &&
                found->player == player &&
                found->underConstruction)
                return {found->x, found->y};
        return {-1.0f, -1.0f};
    }
    int aiUnitIdForTesting(
        int player,
        const std::string &symbol) const {
        const dat::Unit *unit =
            aiUnit(player, symbol);
        return unit ? unit->id : -1;
    }
    int civilizationForPlayerForTesting(
        int player) const {
        return civilizationForPlayer(player);
    }
    float populationLimitForTesting(
        int player) const {
        return player > 0 &&
                       (size_t)player <=
                           players_.size()
                   ? players_[(size_t)player - 1]
                         .populationLimit
                   : 0.0f;
    }
    bool aiUnitAvailableForTesting(
        int player,
        const std::string &symbol) const {
        const dat::Unit *unit =
            aiUnit(player, symbol);
        return unit &&
               unitAvailable(
                   player, unit->id);
    }
    size_t aiFreeWorkerCountForTesting(
        int player) const {
        return (size_t)std::count_if(
            objects_.begin(), objects_.end(),
            [&](const Object &object) {
                return object.active &&
                       !object.hidden &&
                       object.player == player &&
                       isWorker(object) &&
                       !object.constructionTargetId &&
                       !object.repairTargetId &&
                       object.garrisonedInId < 0;
            });
    }
    size_t aiFoundationCountForTesting(
        int player) const {
        return (size_t)std::count_if(
            objects_.begin(), objects_.end(),
            [&](const Object &object) {
                return object.active &&
                       object.player == player &&
                       object.underConstruction;
            });
    }
    std::string aiFoundationReportForTesting(
        int player) const {
        std::string report;
        char buffer[256];
        for (const Object &foundation :
             objects_) {
            if (!foundation.active ||
                foundation.player != player ||
                !foundation.underConstruction ||
                !foundation.unit)
                continue;
            const Object *builder =
                findObject(
                    foundation
                        .constructionBuilderId);
            snprintf(
                buffer, sizeof buffer,
                "[%s %.1f/%.1f at %.1f,%.1f "
                "builder=%u state=%d goal=%d "
                "pos=%.1f,%.1f path=%zu/%zu "
                "blocked=%.1f] ",
                foundation.unit->name2.c_str(),
                foundation.constructionRemaining,
                foundation.constructionTotal,
                foundation.x, foundation.y,
                foundation.constructionBuilderId,
                builder ? (int)builder->state
                        : -1,
                builder
                    ? (int)builder
                          ->moveGoalActive
                    : -1,
                builder ? builder->x : -1.0f,
                builder ? builder->y : -1.0f,
                builder ? builder->pathIndex : 0,
                builder ? builder->path.size() : 0,
                builder ? builder->blockedTime : 0.0f);
            report += buffer;
        }
        return report;
    }
    size_t aiKnownResourceCountForTesting(
        int player, int resourceType) const {
        return (size_t)std::count_if(
            objects_.begin(), objects_.end(),
            [&](const Object &object) {
                return isGatherable(object) &&
                       object.resourceType ==
                           resourceType &&
                       objectVisibleToPlayer(
                           object, player);
            });
    }
    size_t aiReachableResourceCountForTesting(
        int player, int resourceType) const {
        size_t count = 0;
        for (const Object &resource :
             objects_) {
            if (!isGatherable(resource) ||
                resource.resourceType !=
                    resourceType ||
                !objectVisibleToPlayer(
                    resource, player))
                continue;
            const bool reachable =
                std::any_of(
                    objects_.begin(),
                    objects_.end(),
                    [&](const Object &worker) {
                        return worker.active &&
                               !worker.hidden &&
                               worker.player ==
                                   player &&
                               isWorker(worker) &&
                               worker.garrisonedInId <
                                   0 &&
                               canReachObject(
                                   worker, resource,
                                   0.1f);
                    });
            if (reachable) count++;
        }
        return count;
    }
    int aiObjectCountForTesting(
        int player,
        const std::string &symbol,
        bool includeFoundations = true) const;
    int aiObjectTotalForTesting(
        int player,
        const std::string &symbol) const;
    size_t aiQueuedUnitCountForTesting(
        int player, int unitId) const;
    size_t aiGathererCountForTesting(
        int player, int resourceType) const;
    int aiForceTargetForTesting(
        int player, int domain) const {
        return player > 0 &&
                       (size_t)player <
                           aiPlayers_.size() &&
                       domain >= 0 &&
                       domain < 3
                   ? aiPlayers_[(size_t)player]
                         .forceTargets[
                             (size_t)domain]
                   : 0;
    }
    int aiForceCountForTesting(
        int player, int domain) const {
        return player > 0 &&
                       (size_t)player <
                           aiPlayers_.size() &&
                       domain >= 0 &&
                       domain < 3
                   ? aiPlayers_[(size_t)player]
                         .forceCounts[
                             (size_t)domain]
                   : 0;
    }
    uint32_t aiReplenishmentQueuedForTesting(
        int player) const {
        return player > 0 &&
                       (size_t)player <
                           aiPlayers_.size()
                   ? aiPlayers_[(size_t)player]
                         .replenishmentQueued
                   : 0;
    }
    uint32_t aiRetreatsForTesting(
        int player) const {
        return player > 0 &&
                       (size_t)player <
                           aiPlayers_.size()
                   ? aiPlayers_[(size_t)player]
                         .retreats
                   : 0;
    }
    uint32_t aiEscortAssignmentsForTesting(
        int player) const {
        return player > 0 &&
                       (size_t)player <
                           aiPlayers_.size()
                   ? aiPlayers_[(size_t)player]
                         .escortAssignments
                   : 0;
    }
    bool aiSurrenderedForTesting(
        int player) const {
        return player > 0 &&
                       (size_t)player <
                           aiPlayers_.size() &&
                       aiPlayers_[(size_t)player]
                           .surrendered;
    }
    int aiGroupTargetUnitForTesting(
        int player) const {
        if (player <= 0 ||
            (size_t)player >=
                aiPlayers_.size() ||
            aiPlayers_[(size_t)player]
                .militaryGroups.empty())
            return -1;
        const Object *target =
            findObject(
                aiPlayers_[(size_t)player]
                    .militaryGroups.front()
                    .targetId);
        return target && target->unit
                   ? target->unit->id
                   : -1;
    }
    int aiGroupPhaseForTesting(
        int player) const {
        return player > 0 &&
                       (size_t)player <
                           aiPlayers_.size() &&
                       !aiPlayers_[(size_t)player]
                            .militaryGroups
                            .empty()
                   ? (int)aiPlayers_[
                             (size_t)player]
                         .militaryGroups.front()
                         .phase
                   : -1;
    }
    size_t aiGroupMemberCountForTesting(
        int player) const {
        return player > 0 &&
                       (size_t)player <
                           aiPlayers_.size() &&
                       !aiPlayers_[(size_t)player]
                            .militaryGroups
                            .empty()
                   ? aiPlayers_[(size_t)player]
                         .militaryGroups.front()
                         .members.size()
                   : 0;
    }
    int victoryStateForTesting() const {
        return victoryState_;
    }
    float victoryCountdownForTesting() const {
        return victoryCountdownRemaining_;
    }
    int victoryCountdownPlayerForTesting() const {
        return victoryCountdownPlayer_;
    }
    void setVictoryParametersForTesting(
        float countdown, float timeLimit,
        int scoreLimit) {
        standardVictoryCountdown_ = countdown;
        timeLimitSeconds_ = timeLimit;
        scoreLimit_ = scoreLimit;
    }
    int difficultyForTesting() const {
        return difficulty_;
    }
    SkirmishVictory victoryConditionForTesting() const {
        return victoryCondition_;
    }
    void setVictoryConditionForTesting(
        SkirmishVictory condition);
    uint64_t mapHashForTesting() const;
    std::array<float, 2>
    playerBasePositionForTesting(int player) const;
    int reachableStartingResourceCountForTesting(
        int player, int resourceType) const;
    bool shorelineShipyardSiteForTesting(
        int player);
    size_t projectileCountForResetTesting() const {
        return projectiles_.size();
    }
    size_t instructionCountForResetTesting() const {
        return instructions_.size() +
               (currentInstruction_.empty() ? 0u : 1u);
    }
    bool playerActiveForTesting(
        int player) const {
        return player > 0 &&
               (size_t)player <=
                   players_.size() &&
               players_[(size_t)player - 1]
                   .active;
    }
    void setPlayerActiveForTesting(
        int player, bool active) {
        if (player > 0 &&
            (size_t)player <=
                players_.size())
            players_[(size_t)player - 1]
                .active = active;
    }
    void eliminatePlayerForTesting(
        int player, bool surrendered = false) {
        eliminatePlayer(
            player, surrendered);
    }
    void update(float dt, const InputState &in);
    void render(Renderer &r, int screenW, int screenH);
    bool saveMatch(
        const std::string &path,
        std::string *err = nullptr) const;
    bool loadMatch(
        const std::string &path,
        std::string *err = nullptr);
    static bool readSaveSettings(
        const std::string &path,
        SkirmishSettings &settings,
        std::string *err = nullptr);
    static bool readSaveMetadata(
        const std::string &path,
        MatchSaveMetadata &metadata,
        std::string *err = nullptr);
    bool generatedMatchForSaving() const {
        return generatedMatch_;
    }
    const SkirmishSettings &currentSkirmishSettings() const {
        return currentSkirmishSettings_;
    }

    // Centre the camera on a tile (used by tools and at startup).
    void lookAt(float tx, float ty);
    bool lookAtObject(uint32_t spawnId);
    bool selectObjectForTesting(uint32_t spawnId);
    bool selectObjectsForTesting(
        const std::vector<uint32_t> &spawnIds);
    bool assignControlGroup(int group);
    bool recallControlGroup(
        int group, bool centerCamera = false);
    const std::vector<uint32_t> &controlGroupForTesting(
        int group) const {
        static const std::vector<uint32_t> empty;
        return group >= 0 &&
                       group < (int)controlGroups_.size()
                   ? controlGroups_[(size_t)group]
                   : empty;
    }
    static std::array<float, 2> minimapWorldToPoint(
        float worldX, float worldY, int mapSize,
        float left, float top, float size);
    static bool minimapPointToWorld(
        float pointX, float pointY, int mapSize,
        float left, float top, float size,
        float &worldX, float &worldY);
    std::array<float, 2> cameraCenterForTesting() const {
        return {camX_, camY_};
    }
    float simulationTimeForTesting() const {
        return simulationTime_;
    }
    size_t minimapAlertCountForTesting() const {
        return minimapAlerts_.size();
    }
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
    bool canGarrisonForTesting(
        uint32_t unitId,
        uint32_t buildingId) const {
        const Object *unit =
            findObject(unitId);
        const Object *building =
            findObject(buildingId);
        return unit && building &&
               canGarrison(*unit, *building);
    }
    int objectHealthForTesting(
        uint32_t spawnId) const {
        const Object *object =
            findObject(spawnId);
        return object
                   ? (int)std::round(
                         object->hitPoints)
                   : 0;
    }
    int objectMaxHealthForTesting(
        uint32_t spawnId) const {
        const Object *object =
            findObject(spawnId);
        return object
                   ? (int)std::round(
                         object->maxHitPoints)
                   : 0;
    }
    int garrisonVolleyForTesting(uint32_t buildingId) const {
        const Object *b = findObject(buildingId);
        return b ? garrisonVolleySize(*b) : 0;
    }
    size_t projectilesLaunchedForTesting() const { return projectilesLaunched_; }
    // Mean distance of marching members from their moving slots (-1 if no
    // group is marching).
    float marchShapeErrorForTesting() const;
    size_t formationMemberCountForTesting() const;
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
    void setOiiaSoundPlayer(std::function<void()> fn) {
        playOiiaSound_ = std::move(fn);
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
    float animalNurseryFoodRateForTesting(
        uint32_t spawnId) const {
        const Object *nursery = findObject(spawnId);
        return nursery
                   ? animalNurseryFoodRate(*nursery)
                   : 0.0f;
    }
    bool garrisonCursorActive() const {
        return garrisonCursorActive_;
    }
    bool objectActive(uint32_t spawnId) const;
    float resource(int player, int resourceId) const;
    bool researchTechnology(int player, int technologyId);
    const std::string &currentInstruction() const { return currentInstruction_; }
    const std::string &statusMessageForTesting() const {
        return statusMessage_;
    }
    bool statusMessageIsAttackAlertForTesting() const {
        return !attackAlertMessage_.empty() &&
               statusMessage_ ==
                   attackAlertMessage_;
    }
    void setEnemyIntelligenceForTesting(
        bool enabled) {
        enemyIntelligenceCheat_ = enabled;
    }
    bool canInspectProductionForTesting(
        uint32_t spawnId) const {
        const Object *object =
            findObject(spawnId);
        return object &&
               canInspectProduction(*object);
    }
    int currentInstructionPlayerForTesting() const {
        return currentInstructionPlayer_;
    }
    void queueInstructionForTesting(
        const std::string &text, int player = -1) {
        queueInstruction(text, 3.0f, std::string(), player);
    }
    float populationUsedForTesting(int player) const {
        return populationUsed(player);
    }
    float populationCapacityForTesting(int player) const {
        return populationCapacity(player);
    }
    size_t activeObjectCount() const;
    bool invariantsForTesting() const;
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
    static float shieldRegenerationForTesting(
        float shieldPoints) {
        return shieldRegeneration(
            shieldPoints);
    }
    float objectUnitAttributeForTesting(
        uint32_t spawnId, int attribute,
        float baseValue) const {
        const Object *object = findObject(spawnId);
        return object
                   ? modifiedUnitAttribute(
                         *object, attribute,
                         baseValue)
                   : baseValue;
    }
    uint32_t spawnObjectForTesting(int civilization, int unitId, int player,
                                   float x, float y);
    uint32_t spawnFoundationForTesting(
        int civilization, int unitId, int player,
        float x, float y,
        const std::vector<uint32_t> &builderIds);
    bool completeFoundationForTesting(uint32_t spawnId);
    bool setConstructionProgressForTesting(
        uint32_t spawnId, float progress);
    bool damageObjectForTesting(
        uint32_t spawnId, int damage,
        uint32_t attackerId = 0);
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
    uint32_t diplomacyForTesting(
        int sourcePlayer, int targetPlayer) const {
        if (sourcePlayer <= 0 ||
            (size_t)sourcePlayer > players_.size() ||
            targetPlayer < 0 || targetPlayer >= 16)
            return UINT32_MAX;
        return players_[(size_t)sourcePlayer - 1]
            .diplomacy[(size_t)targetPlayer];
    }
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
    size_t objectCountForTesting(
        int player, int unitId) const;
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
    float farthestProjectileDistanceForTesting(
        uint32_t sourceId) const;
    int firstProjectileUnitForTesting() const {
        return projectiles_.empty() || !projectiles_.front().unit
                   ? -1
                   : projectiles_.front().unit->id;
    }
    int projectileUnitForTesting(size_t index) const {
        return index >= projectiles_.size() ||
                       !projectiles_[index].unit
                   ? -1
                   : projectiles_[index].unit->id;
    }
    std::array<float, 2>
    projectileAimForTesting(size_t index) const {
        return index < projectiles_.size()
                   ? std::array<float, 2>{
                         projectiles_[index].aimX,
                         projectiles_[index].aimY}
                   : std::array<float, 2>{-1.0f,
                                          -1.0f};
    }
    bool projectileUsesFixedAimForTesting(
        size_t index) const {
        return index < projectiles_.size() &&
               projectiles_[index].groundAimed;
    }
    void applyBlastForTesting(
        uint32_t sourceId, float x, float y,
        uint32_t primaryId, float width,
        int level, int damage) {
        const Object *source =
            findObject(sourceId);
        applyBlast(
            sourceId,
            source ? source->player : 0,
            x, y, primaryId,
            width, level, damage);
    }
    bool attackShotPendingForTesting(
        uint32_t spawnId) const {
        const Object *object =
            findObject(spawnId);
        return object &&
               object->attackShotPending;
    }
    uint32_t spawnOiiaCatForTesting(
        int player, float x, float y);
    bool objectIsOiiaCatForTesting(
        uint32_t spawnId) const {
        const Object *object =
            findObject(spawnId);
        return object &&
               object->customKind == 1;
    }
    uint32_t attackTargetForTesting(
        uint32_t spawnId) const {
        const Object *object =
            findObject(spawnId);
        return object
                   ? object->attackTargetId
                   : 0;
    }
    bool canAttackGroundForTesting(
        uint32_t spawnId) const {
        const Object *object =
            findObject(spawnId);
        return object &&
               canAttackGround(*object);
    }
    bool issueAttackGroundForTesting(
        uint32_t spawnId, float x, float y) {
        Object *object = findObject(spawnId);
        return object &&
               issueAttackGround(
                   *object, x, y);
    }
    bool attackGroundActiveForTesting(
        uint32_t spawnId) const {
        const Object *object =
            findObject(spawnId);
        return object &&
               object->attackGroundActive;
    }
    bool attackGroundCursorActiveForTesting() const {
        return attackGroundCursorActive_;
    }
    bool issuePatrolForTesting(
        uint32_t spawnId, float x, float y);
    bool issueGuardForTesting(
        uint32_t spawnId,
        uint32_t targetId);
    bool issueFollowForTesting(
        uint32_t spawnId,
        uint32_t targetId);
    bool stopUnitForTesting(uint32_t spawnId);
    bool patrolActiveForTesting(
        uint32_t spawnId) const {
        const Object *object =
            findObject(spawnId);
        return object &&
               object->patrolActive;
    }
    uint32_t guardTargetForTesting(
        uint32_t spawnId) const {
        const Object *object =
            findObject(spawnId);
        return object
                   ? object->guardTargetId
                   : 0;
    }
    uint32_t followTargetForTesting(
        uint32_t spawnId) const {
        const Object *object =
            findObject(spawnId);
        return object
                   ? object->followTargetId
                   : 0;
    }
    bool selectedCanAttackGroundForTesting() const {
        return selectedCanAttackGround();
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
    void setResourceTypeAmountsForTesting(
        int type, float amount) {
        for (Object &object : objects_)
            if (object.resourceType == type)
                object.resourceAmount = amount;
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
    bool tileExploredForTesting(int player, int x, int y) const {
        return tileExplored(player, x, y);
    }
    bool tileVisibleForTesting(int player, int x, int y) const {
        return tileVisible(player, x, y);
    }
    bool objectVisibleForTesting(
        int player, uint32_t spawnId) const {
        const Object *object = findObject(spawnId);
        return object &&
               objectVisibleToPlayer(*object, player);
    }
    void updateVisibilityForTesting() {
        updateVisibility();
    }
    void setVisibilityCheatsForTesting(
        bool explore, bool sight) {
        forceExploreCheat_ = explore;
        forceSightCheat_ = sight;
    }
    bool issueConversionForTesting(
        uint32_t converterId,
        uint32_t targetId);
    uint32_t spawnConverterForTesting(
        int player, float x, float y);
    float conversionProgressForTesting(
        uint32_t converterId) const;
    float conversionChargeForTesting(
        uint32_t converterId) const;
    float conversionPowerPercentForTesting(
        uint32_t converterId) const;
    int convertCommandIconForTesting() const;
    int carriedHolocronGraphicForTesting(
        uint32_t carrierId) const;
    bool objectStealthedForTesting(
        uint32_t spawnId) const;
    bool objectDetectedForTesting(
        int player, uint32_t spawnId) const;
    uint32_t spawnDetectorForTesting(
        int player, float x, float y);
    bool setStealthedForTesting(
        int player, bool enabled);
    uint32_t spawnHolocronForTesting(
        float x, float y);
    uint32_t spawnTempleForTesting(
        int player, float x, float y);
    bool issueHolocronOrderForTesting(
        uint32_t carrierId,
        uint32_t targetId);
    uint32_t carriedHolocronForTesting(
        uint32_t carrierId) const;
    int holocronHolderForTesting(
        uint32_t holocronId) const;
    int storedHolocronCountForTesting(
        int player) const;
    int holocronCountForTesting() const;
    int playerScoreForTesting(int player) const;

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
        Convert,
    };
    enum class CursorMode : uint8_t {
        Normal,
        Attack,
        Garrison,
        ContextWork,
        RepairCommand,
        Guard,
        Follow,
        AttackGround,
        Convert,
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
    enum class UnitCommand : uint8_t {
        Stop,
        Patrol,
        Guard,
        Follow,
        AttackGround,
        Convert,
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
        int triggerAttack = -1;
        std::string triggerName;
        float shieldPoints = 0, maxShieldPoints = 0;
        float shieldRegenerationTime = 0;
        float shieldDrainTime = 0;
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
        bool attackShotPending = false;
        bool attackGroundActive = false;
        float attackGroundX = 0;
        float attackGroundY = 0;
        bool patrolActive = false;
        bool patrolTowardEnd = true;
        float patrolStartX = 0;
        float patrolStartY = 0;
        float patrolEndX = 0;
        float patrolEndY = 0;
        uint32_t guardTargetId = 0;
        uint32_t followTargetId = 0;
        uint32_t conversionTargetId = 0;
        float conversionProgress = 0;
        float conversionRecharge = 0;
        uint32_t holocronTargetId = 0;
        uint32_t carriedHolocronId = 0;
        uint32_t carriedById = 0;
        bool moveGoalActive = false;
        bool wander = false;
        bool drawShadows = true;
        bool active = true;
        bool hidden = false;
        bool draw = true;
        bool frozen = false;
        bool locked = false;
        bool gate = false;
        bool underConstruction = false;
        bool manualDropOff = false;
        bool selected = false;
        bool triggerAddressable = true;
        uint8_t customKind = 0;
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
        bool productionPopulationBlocked = false;
        float constructionRemaining = 0;
        float constructionTotal = 0;
        uint32_t constructionBuilderId = 0;
        uint32_t constructionTargetId = 0;
        uint32_t gatherTargetId = 0;
        uint32_t dropOffTargetId = 0;
        uint32_t repairTargetId = 0;
        uint32_t garrisonTargetId = 0;
        bool garrisonDamageLocked = false;
        uint32_t spawnId = 0;
        uint32_t discoveredByPlayers = 0;
        int32_t garrisonedInId = -1;
        uint16_t initialFrame = 0;
    };

    enum class AiTruth : uint8_t {
        False,
        True,
        Unknown,
    };

    enum class AiGroupPhase : uint8_t {
        Advance,
        EmbarkMove,
        Boarding,
        Crossing,
        Unloading,
        Engage,
        Retreat,
    };

    struct AiMilitaryGroup {
        uint32_t id = 0;
        uint32_t targetId = 0;
        std::vector<uint32_t> members;
        std::vector<uint32_t> transports;
        std::vector<uint32_t> escorts;
        std::unordered_map<uint32_t, uint32_t>
            boardingAssignments;
        AiGroupPhase phase =
            AiGroupPhase::Advance;
        FormationType formation =
            FormationType::Line;
        float destinationX = 0.0f;
        float destinationY = 0.0f;
        float retryTime = 0.0f;
        float initialStrength = 0.0f;
    };

    struct AiPlayerState {
        AiProgram program;
        std::unordered_map<int, int> goals;
        std::unordered_map<std::string, int>
            strategicNumbers;
        std::unordered_map<int, float> timers;
        std::array<int, 4> escrowPercent{};
        std::array<float, 4> escrowResources{};
        std::unordered_set<std::string>
            warnedFacts;
        std::unordered_set<std::string>
            warnedActions;
        std::string currentRuleSource;
        int currentRuleLine = 0;
        int randomNumber = 0;
        std::unordered_set<int> events;
        std::unordered_set<int> signals;
        size_t ruleCursor = 0;
        float ruleTime = 0.0f;
        float economyTime = 0.0f;
        float militaryTime = 0.0f;
        float defenseTime = 0.0f;
        float strategyTime = 0.0f;
        float townSafeTime = 0.0f;
        float scoutTime = 0.0f;
        float surrenderTime = 0.0f;
        float rebuildUntil = 0.0f;
        float gameTime = 0.0f;
        float ageTime = 0.0f;
        int age = 1;
        uint32_t nextMilitaryGroupId = 1;
        uint32_t attacksIssued = 0;
        uint32_t formationOrders = 0;
        uint32_t transportLandings = 0;
        uint32_t replenishmentQueued = 0;
        uint32_t retreats = 0;
        uint32_t regroupOrders = 0;
        uint32_t escortAssignments = 0;
        std::array<int, 3> forceTargets{};
        std::array<int, 3> forceCounts{};
        std::vector<AiMilitaryGroup>
            militaryGroups;
        std::unordered_set<uint32_t>
            shelteredWorkers;
        bool surrendered = false;
        bool loaded = false;
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
        int player = -1;
    };

    bool initGenerated(
        const SkirmishSettings &settings,
        bool addStartingResources,
        std::string *err);
    void resetMatchState();
    void generateTerrain(
        int size, SkirmishMapStyle style);
    void spawnStartingResources(
        int player, float baseX, float baseY,
        float inlandDirection);
    void spawnFishingResources();
    void spawnHolocrons();
    void buildTileElevation();
    void resetVisibility();
    void updateVisibility();
    void updateMinimapTexture(Renderer &renderer);
    void drawFogOverlay(
        Renderer &renderer, int screenW,
        int screenH, float originX,
        float originY);
    bool handleMinimapInput(const InputState &input);
    void clampCamera();
    void syncControlGroups();
    bool tileExplored(int player, int x, int y) const;
    bool tileVisible(int player, int x, int y) const;
    bool objectCurrentlyVisibleToPlayer(
        const Object &object, int player) const;
    bool objectVisibleToPlayer(
        const Object &object, int player) const;
    bool isStealthed(
        const Object &object) const;
    bool isDetector(
        const Object &object) const;
    bool detectedByPlayer(
        const Object &object, int player) const;
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
    bool hasAnyGarrisonTask(const Object &unit) const;
    bool hasGarrisonTask(const Object &unit, const Object &container) const;
    bool isTransport(const Object &object) const;
    bool isFarmUnit(const dat::Unit &unit) const;
    bool isFoodProcessingCenter(const Object &building) const;
    bool queueFarmReseed(const Object &building, const dat::Unit &farm);
    void playInterfaceFeedback(int soundId);
    void showCannotDo(int languageId, const std::string &fallback);
    void showResourceShortage(int resourceType);
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
    bool canReachObject(
        const Object &mover, const Object &target,
        float clearance = 0.0f) const;
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
    bool isPassiveAnimal(const Object &object) const;
    bool isHostileGaiaAnimal(
        const Object &object) const;
    bool isEnemy(const Object &source, const Object &target) const;
    bool canAttack(const Object &object) const;
    bool canAttackTarget(
        const Object &source, const Object &target) const;
    bool canAttackGround(
        const Object &source) const;
    const dat::Task *conversionTask(
        const Object &converter) const;
    float conversionChargeFraction(
        const Object &converter) const;
    bool canConvert(
        const Object &converter,
        const Object &target) const;
    bool issueConversion(
        Object &converter, Object &target);
    void updateConversion(float dt);
    bool isHolocron(
        const Object &object) const;
    bool isTemple(
        const Object &object) const;
    bool canCarryHolocron(
        const Object &object) const;
    bool issueHolocronOrder(
        Object &carrier, Object &target);
    void updateHolocrons(float dt);
    void dropHolocron(Object &carrier);
    void releaseHolocronsForRemoval(
        Object &object);
    int storedHolocronCount(int player) const;
    bool selectedCanAttackGround() const;
    bool issueAttackGround(
        Object &source, float x, float y);
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
    int effectiveGarrisonCapacity(
        const Object &object) const;
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
    std::vector<UnitCommand> unitCommands(const Object &unit) const;
    int unitCommandIcon(UnitCommand command) const;
    std::string unitCommandTitle(UnitCommand command) const;
    std::string unitCommandHelp(UnitCommand command) const;
    void executeUnitCommand(UnitCommand command);
    void clearUnitCommandOrder(Object &unit);
    void stopUnit(Object &unit);
    void updateUnitCommandOrder(Object &unit, float dt);
    int buildingCommandIcon(const Object &building, BuildingCommand command) const;
    std::string buildingCommandTitle(const Object &building, BuildingCommand command) const;
    std::string buildingCommandHelp(const Object &building, BuildingCommand command) const;
    void executeBuildingCommand(Object &building, BuildingCommand command);
    bool canSetGatherPoint(const Object &building) const;
    void setGatherPoint(Object &building, float screenX, float screenY, int screenW, int screenH);
    void sendToGatherPoint(Object &building, Object &unit);
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
    const std::vector<int> &researchOptions(
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
    void initializeCivilizationRestrictions();
    void refreshAutomaticTechnologies(int player);
    bool technologyVisible(int player, const dat::Tech &technology) const;
    void refreshAllAutomaticTechnologies();
    std::string unitDisplayName(const dat::Unit &unit) const;
    std::string ownershipLabel(int player) const;
    std::string factionName(int civilization) const;
    std::string playerDisplayName(int player) const;
    char factionAbbreviation(int civilization) const;
    bool canInspectProduction(
        const Object &object) const;
    int civilizationGraphic(int graphicId, int player) const;
    bool isWorker(const Object &object) const;
    bool isGatherer(const Object &object) const;
    bool isBuilder(const Object &object) const;
    bool isRepairer(const Object &object) const;
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
    static float shieldRegeneration(
        float shieldPoints);
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
    float animalNurseryFoodRate(
        const Object &nursery) const;
    bool issueGarrisonCommand(Object &building);
    bool issueGarrisonOrder(
        Object &unit, Object &container);
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
    void updateAttackGround(
        Object &source, float dt);
    bool attackReleaseReady(
        Object &source);
    bool attackAnimationPlaying(
        const Object &source) const;
    const dat::Unit *projectileUnitForTarget(
        const Object &source, const Object &target) const;
    void launchProjectile(const Object &source, const Object &target, int damage);
    void launchGroundProjectile(
        const Object &source,
        float targetX, float targetY);
    void updateProjectiles(float dt);
    void updateRemains(float dt);
    void damageObject(Object &object, int damage, uint32_t attackerId);
    void killObject(Object &object, bool countKill = true);
    bool transferOwnership(
        Object &object, int player,
        bool clearOrders = true);
    void objectScreenPosition(const Object &object, int screenW, int screenH,
                              float &screenX, float &screenY) const;
    void screenToWorld(float screenX, float screenY, int screenW, int screenH,
                       float &worldX, float &worldY) const;
    void playUnitAcknowledgement(const Object &object, bool attack);
    void playWorldUnitSound(const Object &object, int soundId);
    bool worldSoundAudible(float x, float y) const;
    void updateAmbience(float dt, int screenW, int screenH);
    void updateAi(float dt);
    void updateAiPlayer(
        int player, AiPlayerState &state);
    void updateAiGatherers(
        int player, AiPlayerState &state);
    void updateAiWorkerShelter(
        int player, AiPlayerState &state,
        float dt);
    void updateAiScouting(
        int player, AiPlayerState &state);
    void updateAiMilitaryGroups(
        int player, AiPlayerState &state,
        float dt);
    void updateAiStrategy(
        int player, AiPlayerState &state);
    bool aiAttackNow(
        int player, AiPlayerState &state);
    bool aiStartGroupAdvance(
        AiPlayerState &state,
        AiMilitaryGroup &group,
        Object &target);
    void aiMoveTransports(
        AiPlayerState &state,
        const std::vector<Object *> &transports,
        float x, float y);
    void aiMoveEscorts(
        AiPlayerState &state,
        const std::vector<Object *> &escorts,
        float x, float y);
    int aiMilitaryDomain(
        const dat::Unit &unit) const;
    float aiObjectStrength(
        const Object &object) const;
    float aiGroupStrength(
        const AiMilitaryGroup &group) const;
    bool aiBasePoint(
        int player, float &x, float &y) const;
    int aiTargetPriority(
        const Object &target) const;
    bool aiFindTransportPoint(
        const Object &transport,
        const std::vector<Object *> &members,
        const Object *target,
        bool unloading,
        float &x, float &y) const;
    AiTruth evaluateAiCondition(
        int player, AiPlayerState &state,
        const AiNode &condition);
    bool evaluateAiFactValue(
        int player, AiPlayerState &state,
        const AiNode &condition,
        size_t argumentEnd, int &value);
    int resolveAiValue(
        int player, const AiPlayerState &state,
        const std::string &value,
        bool &known) const;
    bool executeAiAction(
        int player, AiPlayerState &state,
        AiRule &rule, const AiNode &action);
    const dat::Unit *aiUnit(
        int player,
        const std::string &symbol) const;
    int aiTechnology(
        const std::string &symbol) const;
    int aiTechLevel(int player) const;
    int aiObjectCount(
        int player, const dat::Unit &unit,
        bool includeFoundations) const;
    bool aiCanAfford(
        int player,
        const dat::Unit &unit) const;
    bool aiCanAfford(
        int player,
        const dat::Tech &technology) const;
    bool aiBuild(
        int player, const dat::Unit &unit,
        bool forward = false);
    bool aiTrain(
        int player, const dat::Unit &unit);
    bool aiResearch(
        int player, int technologyId);
    void activateCheat(size_t index, int screenW, int screenH);
    bool spawnCheatUnit(int unitId, bool requireWater,
                        int screenW, int screenH,
                        uint8_t customKind = 0);
    void defeatCheatPlayer(int player);
    void updateConquest(float dt);
    void updateVictoryConditions(float dt);
    int playerScore(int player) const;
    bool playersShareVictory(
        int first, int second) const;
    void eliminatePlayer(
        int player, bool surrendered);
    void setMatchOutcome(int outcome);
    void startInstruction(Instruction instruction);
    void queueInstruction(const std::string &text, float duration,
                          const std::string &sound = std::string(),
                          int player = -1);
    int instructionPlayer(
        const std::string &text, int fallbackPlayer) const;
    float populationUse(const dat::Unit &unit) const;
    float populationUsed(int player) const;
    float populationCapacity(int player) const;
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
    std::array<std::vector<uint8_t>, 17> exploredTiles_;
    std::array<std::vector<uint8_t>, 17> visibleTiles_;
    float visibilityTime_ = 0.0f;
    uint64_t visibilityGeneration_ = 0;
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
    std::array<AiPlayerState, 17> aiPlayers_{};
    std::array<std::set<int>, 17> disabledTechs_{};
    std::array<std::set<int>, 17> disabledUnits_{};
    std::vector<ScenarioTrigger> triggers_;
    std::vector<uint32_t> triggerOrder_;
    std::vector<TriggerRuntime> triggerRuntime_;
    std::deque<Instruction> instructions_;
    std::string currentInstruction_;
    int currentInstructionPlayer_ = -1;
    float instructionTime_ = 0;
    float attackAlertCooldown_ = 0;
    float simulationTime_ = 0;
    uint32_t nextSpawnId_ = 1;
    uint32_t nextMoveGroupId_ = 1;
    int difficulty_ = 2;
    SkirmishVictory victoryCondition_ =
        SkirmishVictory::Standard;
    int victoryState_ = -1;
    bool conquestEnabled_ = false;
    float conquestCheckTime_ = 0.0f;
    float standardVictoryCountdown_ = 600.0f;
    float timeLimitSeconds_ = 3600.0f;
    int scoreLimit_ = 4000;
    int victoryCountdownPlayer_ = -1;
    int victoryCountdownKind_ = 0;
    float victoryCountdownRemaining_ = 0.0f;
    std::array<float, 17>
        monumentVictoryCountdowns_{};
    std::array<float, 17>
        holocronVictoryCountdowns_{};
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
    std::array<std::vector<uint32_t>, 10> controlGroups_;
    int lastRecalledControlGroup_ = -1;
    float controlGroupRecallAge_ = 1000.0f;
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
    bool queueTechnologyForTesting(
        uint32_t buildingId, int technologyId);
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
    struct ResearchMenuCacheEntry {
        uint64_t techGeneration = 0;
        uint64_t queueSignature = 0;
        int unitId = -1;
        std::vector<int> options;
        bool hasTab = false;
    };
    mutable std::unordered_map<
        uint32_t, ResearchMenuCacheEntry>
        researchMenuCache_;
    bool forceExploreCheat_ = false;
    bool forceSightCheat_ = false;
    bool enemyIntelligenceCheat_ = false;
    bool garrisonCursorActive_ = false;
    bool repairCursorActive_ = false;
    bool attackGroundCursorActive_ = false;
    bool attackGroundJustBegun_ = false;
    bool unitCommandCursorActive_ = false;
    bool unitCommandJustBegun_ = false;
    UnitCommand pendingUnitCommand_ =
        UnitCommand::Stop;
    float ambienceTime_ = 2.0f;
    uint32_t ambienceSequence_ = 0;
    std::string statusMessage_;
    std::string attackAlertMessage_;
    float statusTime_ = 0;
    bool boxSelectActive_ = false;
    bool debug_ = false;
    struct MinimapAlert {
        float x = 0;
        float y = 0;
        float age = 0;
        bool attack = false;
    };
    std::vector<MinimapAlert> minimapAlerts_;
    Texture *minimapTexture_ = nullptr;
    Renderer *minimapRenderer_ = nullptr;
    std::vector<uint8_t> minimapPixels_;
    int minimapTextureSize_ = 0;
    float minimapRefreshTime_ = 0;
    Texture *fogTexture_ = nullptr;
    Renderer *fogRenderer_ = nullptr;
    std::vector<uint8_t> fogPixels_;
    int fogTextureWidth_ = 0;
    int fogTextureHeight_ = 0;
    float fogOriginX_ = 0.0f;
    float fogOriginY_ = 0.0f;
    float fogZoom_ = 0.0f;
    int fogPlayer_ = -1;
    uint64_t fogVisibilityGeneration_ =
        UINT64_MAX;
    bool fogForceExplore_ = false;
    bool minimapDragging_ = false;
    SkirmishSettings currentSkirmishSettings_{};
    bool generatedMatch_ = false;
    MatchSaveKind saveKind_ =
        MatchSaveKind::Skirmish;
    std::string campaignArchive_;
    uint32_t campaignEntry_ = 0;
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
    std::function<void()> playOiiaSound_;
    std::function<float(const std::string &)> playAmbientSound_;
};

} // namespace swgb
