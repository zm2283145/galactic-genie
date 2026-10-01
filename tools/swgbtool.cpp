// SPDX-License-Identifier: GPL-3.0-or-later
// PC-side tool for inspecting SWGB data and rendering engine scenes headless.
//
//   swgbtool info   <DataDir>
//   swgbtool drs    <file.drs>
//   swgbtool slp    <DataDir> <slpId> <out.png> [playerBase]
//   swgbtool slopes <DataDir> <slpId> <out.png> [frame]
//   swgbtool render <DataDir> <out.png> [seed] [seconds] [zoom]
#include "../src/core/cpx.h"
#include "../src/core/drs.h"
#include "../src/core/audio.h"
#include "../src/core/genie_dat.h"
#include "../src/core/scenario.h"
#include "../src/engine/assets.h"
#include "../src/engine/ai_script.h"
#include "../src/engine/frontend.h"
#include "../src/engine/game.h"
#include "../src/engine/startup.h"
#include "../src/render/soft_renderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <unordered_set>

using namespace swgb;

static int usage() {
    fprintf(stderr,
            "usage:\n"
            "  swgbtool info   <DataDir>\n"
            "  swgbtool terrain <DataDir> <terrainId>\n"
            "  swgbtool restriction <DataDir> <restrictionId>\n"
            "  swgbtool unit <DataDir> <unitId>\n"
            "  swgbtool string <DataDir> <firstId> [lastId]\n"
            "  swgbtool units <DataDir> <name-fragment>\n"
            "  swgbtool graphics <DataDir> <name-fragment>\n"
            "  swgbtool tech <DataDir> <techId>\n"
            "  swgbtool techs <DataDir> <name-fragment>\n"
            "  swgbtool effect-refs <DataDir> <value>\n"
            "  swgbtool effect <DataDir> <effectId>\n"
            "  swgbtool attack-ground-candidates <DataDir>\n"
            "  swgbtool options <DataDir> <civId> <buildingId>\n"
            "  swgbtool sound <DataDir> <soundId>\n"
            "  swgbtool drs    <file.drs>\n"
            "  swgbtool drs-slps <file.drs>\n"
            "  swgbtool slp    <DataDir> <slpId> <out.png> [playerBase]\n"
            "  swgbtool slopes <DataDir> <slpId> <out.png> [frame]\n"
            "  swgbtool render <DataDir> <out.png> [seed] [seconds] [zoom]\n"
            "  swgbtool render-compact <DataDir> <out.png> [seed] [seconds] [zoom]\n"
            "  swgbtool campaign <file.cpx>\n"
            "  swgbtool scenario <file.cpx> <entry>\n"
            "  swgbtool scenario-units <DataDir> <file.cpx> <entry> [unitId]\n"
            "  swgbtool render-scenario <DataDir> <file.cpx> <entry> <out.png> [x] [y] [zoom]\n"
            "  swgbtool stress-scenario <DataDir> <file.cpx> <entry> [zoom]\n"
            "  swgbtool simulate-scenario <DataDir> <file.cpx> <entry> [seconds] [out.png]\n"
            "  swgbtool test-controls <DataDir> <file.cpx> <entry> [out.png]\n"
            "  swgbtool test-combat <DataDir> [out.png]\n"
            "  swgbtool ai-script <entry.per>\n"
            "  swgbtool test-ai <DataDir> <AiDir>\n"
            "  swgbtool test-skirmish <DataDir>\n"
            "  swgbtool test-maps-modes <DataDir>\n"
            "  swgbtool test-interface <DataDir>\n"
            "  swgbtool test-core-gameplay <DataDir>\n"
            "  swgbtool test-major-mechanics <DataDir>\n"
            "  swgbtool test-fidelity <DataDir>\n"
            "  swgbtool test-campaign <DataDir> <CampaignDir>\n"
            "  swgbtool mp3 <file.mp3>\n");
    return 2;
}

static int cmdInfo(const char *dataDir) {
    SoftRenderer r;
    Assets a(&r);
    a.setLogger([](const std::string &s) { printf("  %s\n", s.c_str()); });
    std::string err;
    if (!a.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }

    const auto &d = a.dat();
    size_t g = 0, u = 0;
    for (auto &x : d.graphics) g += x.exists;
    for (auto &x : d.civs[1].units) u += x.exists;
    int minBlendType = d.terrainBlock.terrains.empty() ? 0 : d.terrainBlock.terrains[0].blendType;
    int maxBlendType = minBlendType;
    for (const auto &terrain : d.terrainBlock.terrains) {
        minBlendType = std::min(minBlendType, terrain.blendType);
        maxBlendType = std::max(maxBlendType, terrain.blendType);
    }
    printf("dat %s: %zu civs, %zu graphics (%zu used), %zu units/civ (%zu used), %zu techs, %zu effects, %zu sounds\n",
           d.version.c_str(), d.civs.size(), d.graphics.size(), g, d.civs[1].units.size(), u, d.techs.size(),
           d.effects.size(), d.sounds.size());
    printf("tile %dx%d, %zu terrains (blend types %d..%d), %zu player colours\n", d.terrainBlock.tileWidth,
           d.terrainBlock.tileHeight, d.terrainBlock.terrains.size(), minBlendType, maxBlendType,
           d.playerColours.size());
    printf("elevation height %d; slope sizes:", d.terrainBlock.elevHeight);
    for (size_t i = 0; i < d.terrainBlock.tileSizes.size(); i++) {
        const auto &size = d.terrainBlock.tileSizes[i];
        printf(" %zu=%dx%dx%d", i, size.width, size.height, size.deltaY);
    }
    printf("\n");
    for (size_t i = 0; i < d.playerColours.size(); i++) {
        const auto &color = d.playerColours[i];
        printf("  player color %zu: id %d, palette base %d, minimap %d, statistics %d\n",
               i, color.id, color.playerColorBase, color.minimapColor, color.statisticsText);
    }
    for (size_t i = 0; i < d.civs.size(); i++) {
        const auto &civ = d.civs[i];
        printf("  civilization %zu: %-24s icon set %u\n", i, civ.name.c_str(), civ.iconSet);
    }
    for (size_t i = 0; i < d.terrainBlock.terrains.size(); i++) {
        const auto &terrain = d.terrainBlock.terrains[i];
        if (terrain.blendType >= 8)
            printf("  terrain %zu %-16s blend type %d, priority %d\n", i, terrain.name2.c_str(),
                   terrain.blendType, terrain.blendPriority);
    }
    return 0;
}

static int cmdGraphics(const char *dataDir,
                       const char *fragment) {
    SoftRenderer renderer;
    Assets assets(&renderer);
    std::string err;
    if (!assets.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    for (size_t id = 0;
         id < assets.dat().graphics.size(); id++) {
        const dat::Graphic &graphic =
            assets.dat().graphics[id];
        if (!graphic.exists ||
            (graphic.name.find(fragment) ==
                 std::string::npos &&
             graphic.fileName.find(fragment) ==
                 std::string::npos))
            continue;
        printf("%5zu %-32s file %-20s slp %d "
               "frames %d angles %d duration %.3f "
               "sequence 0x%02x deltas %zu\n",
               id, graphic.name.c_str(),
               graphic.fileName.c_str(),
               graphic.slp, graphic.frameCount,
               graphic.angleCount,
               graphic.frameDuration,
               graphic.sequenceType,
               graphic.deltas.size());
    }
    return 0;
}

static int cmdTerrain(const char *dataDir, int id) {
        SoftRenderer renderer;
        Assets assets(&renderer);
        std::string err;
        if (!assets.init(dataDir, &err)) {
            fprintf(stderr, "error: %s\n", err.c_str());
            return 1;
        }
        const auto &terrains = assets.dat().terrainBlock.terrains;
        if (id < 0 || (size_t)id >= terrains.size()) {
            fprintf(stderr, "error: terrain ID out of range\n");
            return 1;
        }
        const auto &terrain = terrains[(size_t)id];
        printf("terrain %d: '%s' / '%s', slp %d, draw %d, blend type %d, priority %d, dimensions %dx%d\n",
               id, terrain.name.c_str(), terrain.name2.c_str(), terrain.slp, terrain.terrainToDraw,
               terrain.blendType, terrain.blendPriority, terrain.terrainDimensions[0],
               terrain.terrainDimensions[1]);
        return 0;
}

static int cmdString(const char *dataDir, int firstId, int lastId) {
        SoftRenderer renderer;
        Assets assets(&renderer);
        std::string err;
        if (!assets.init(dataDir, &err)) {
            fprintf(stderr, "error: %s\n", err.c_str());
            return 1;
        }
        for (int id = firstId; id <= lastId; id++) {
            const std::string &text = assets.localizedString(id);
            if (!text.empty())
                printf("%d: %s\n", id, text.c_str());
        }
        return 0;
}

static int cmdAttackGroundCandidates(const char *dataDir) {
        SoftRenderer renderer;
        Assets assets(&renderer);
        std::string err;
        if (!assets.init(dataDir, &err)) {
            fprintf(stderr, "error: %s\n", err.c_str());
            return 1;
        }
        const size_t civilization =
            assets.dat().civs.size() > 1 ? 1 : 0;
        const auto &units =
            assets.dat().civs[civilization].units;
        for (const dat::Unit &unit : units) {
            if (!unit.exists ||
                unit.type < dat::UT_Combatant ||
                unit.projectileUnitId < 0 ||
                unit.attackGraphic < 0 ||
                (unit.blastWidth <= 0 &&
                 unit.accuracyPercent >= 100 &&
                 unit.accuracyDispersion <= 0 &&
                 !unit.specialAbility))
                continue;
            printf(
                "%d '%s' type %u class %d hidden %u hero %u "
                "blast %.2f/%u accuracy %d dispersion %.2f "
                "projectile %d/%d count %.2f/%u area %.2f,%.2f,%.2f "
                "special %u break %u\n",
                unit.id, unit.name.c_str(), unit.type,
                unit.cls, unit.hideInEditor, unit.heroMode,
                unit.blastWidth, unit.blastAttackLevel,
                unit.accuracyPercent,
                unit.accuracyDispersion,
                unit.projectileUnitId,
                unit.secondaryProjectileUnit,
                unit.totalProjectiles,
                unit.maxTotalProjectiles,
                unit.projectileSpawningArea[0],
                unit.projectileSpawningArea[1],
                unit.projectileSpawningArea[2],
                unit.specialAbility,
                unit.breakOffCombat);
        }
        return 0;
}

static int cmdUnit(const char *dataDir, int id) {
        SoftRenderer renderer;
        Assets assets(&renderer);
        std::string err;
        if (!assets.init(dataDir, &err)) {
            fprintf(stderr, "error: %s\n", err.c_str());
            return 1;
        }

        for (size_t civ = 0; civ < assets.dat().civs.size(); civ++) {
            const auto &units = assets.dat().civs[civ].units;
            if (id < 0 || (size_t)id >= units.size() || !units[(size_t)id].exists) continue;
            const auto &unit = units[(size_t)id];
            const int graphicId = unit.standingGraphic[0];
            const auto *graphic = assets.dat().graphic(graphicId);
            printf("civ %zu %-24s unit '%s', type %u, class %d, hidden %u, hero %u, graphic %d, slp %d, frames %d, angles %d, "
                   "duration %.3f, sequence 0x%02x, mirror %u, deltas %zu, special graphic %d, "
                   "special ability %u, adjacent mode %u, graphics angle %d, speed %.2f, "
                   "restriction %d, fly %u, obstruction %u/%u, collision %.2f,%.2f, "
                   "outline %.2f,%.2f,%.2f, enabled/disabled %u/%u, unit civ %u, "
                   "minimap mode/color %u/%u, sounds select/move/attack %d/%d/%d\n", civ,
                   assets.dat().civs[civ].name.c_str(), unit.name.c_str(), unit.type, unit.cls,
                   unit.hideInEditor, unit.heroMode, graphicId,
                   graphic ? graphic->slp : -1, graphic ? graphic->frameCount : 0,
                   graphic ? graphic->angleCount : 0, graphic ? graphic->frameDuration : 0,
                   graphic ? graphic->sequenceType : 0, graphic ? graphic->mirroringMode : 0,
                   graphic ? graphic->deltas.size() : 0, unit.specialGraphic, unit.specialAbility,
                   unit.adjacentMode, unit.graphicsAngle, unit.speed, unit.terrainRestriction,
                   unit.flyMode, unit.obstructionType, unit.obstructionClass,
                   unit.collisionSize[0], unit.collisionSize[1],
                   unit.outlineSize[0], unit.outlineSize[1], unit.outlineSize[2],
                   unit.enabled, unit.disabled, unit.civilization,
                   unit.minimapMode, unit.minimapColor,
                   unit.selectionSound, unit.moveSound, unit.attackSound);
            printf("  combat hp %d base armor %d level %u range %.2f..%.2f reload %.2f "
                   "garrison capacity/type/heal %.0f/%u/%.2f "
                   "attack graphic %d projectile %d/%d frame delay %d displacement %.2f,%.2f,%.2f "
                   "projectiles %.2f/%u spawn %.2f,%.2f,%.2f displayed attack/armor %d/%d\n",
                   unit.hitPoints, unit.baseArmor, unit.combatLevel,
                   unit.minRange, unit.maxRange,
                   unit.reloadTime, (float)unit.garrisonCapacity,
                   unit.garrisonType, unit.garrisonHealRate,
                   unit.attackGraphic, unit.projectileUnitId,
                   unit.secondaryProjectileUnit,
                   unit.frameDelay, unit.graphicDisplacement[0], unit.graphicDisplacement[1],
                   unit.graphicDisplacement[2],
                   unit.totalProjectiles,
                   unit.maxTotalProjectiles,
                   unit.projectileSpawningArea[0],
                   unit.projectileSpawningArea[1],
                   unit.projectileSpawningArea[2],
                   unit.displayedAttack, unit.displayedMeleeArmour);
            if (graphic)
                for (const auto &delta : graphic->deltas) {
                   const auto *child =
                       assets.dat().graphic(delta.graphicId);
                   printf("  delta graphic %d offset %d,%d frames %d "
                          "duration %.3f sequence 0x%02x slp %d\n",
                          delta.graphicId, delta.offsetX, delta.offsetY,
                          child ? child->frameCount : 0,
                          child ? child->frameDuration : 0.0f,
                          child ? child->sequenceType : 0,
                          child ? child->slp : -1);
                }
            printf("  interface name id %d '%s', internal '%s', icon %d, portrait %d, kind %u, line %d\n",
                   unit.languageDllName,
                   assets.localizedString(unit.languageDllName).c_str(),
                   unit.name2.c_str(), unit.iconId, unit.oldPortraitPict,
                   unit.interfaceKind, unit.unitLine);
            printf("  placement side terrain %d/%d terrain %d/%d clearance %.2f,%.2f "
                   "hill %u build-on %u foundation terrain %d\n",
                   unit.placementSideTerrain[0], unit.placementSideTerrain[1],
                   unit.placementTerrain[0], unit.placementTerrain[1],
                   unit.clearanceSize[0], unit.clearanceSize[1],
                   unit.hillMode, unit.canBeBuiltOn,
                   unit.foundationTerrainId);
            printf("  creation location %d button %u time %d construction graphic %d transform %d sounds train/transform/construction %d/%d/%d costs",
                   unit.trainLocationId, unit.buttonId, unit.trainTime,
                   unit.constructionGraphic, unit.transformUnit,
                   unit.trainSound, unit.transformSound,
                   unit.constructionSound);
            for (const auto &cost : unit.costs)
                if (cost.flag && cost.type >= 0)
                    printf(" %d=%d", cost.type, cost.amount);
            printf("; default task header %d, capacity %d, work rate %.3f, "
                   "line of sight %.2f, search %.2f, "
                   "storage",
                   unit.defaultTaskId, unit.resourceCapacity,
                   unit.workRate, unit.lineOfSight,
                   unit.searchRadius);
            for (const auto &storage :
                 unit.resourceStorages)
                if (storage.type >= 0 &&
                    storage.amount != 0)
                    printf(" %d=%.1f/%u", storage.type,
                           storage.amount, storage.flag);
            printf("\n");
            if ((size_t)unit.id <
                assets.dat().unitHeaders.size()) {
                for (const auto &task :
                     assets.dat()
                         .unitHeaders[(size_t)unit.id]
                         .tasks)
                    printf("    task %d type %d action %d class %d unit %d "
                           "terrain %d default %u target %u/%u build %u "
                           "resource %d*%d->%d gather %d "
                           "work %.3f/%.3f range %.2f "
                           "graphics %d/%d/%d/%d sounds %d/%d\n",
                           task.id, task.taskType, task.actionType,
                           task.classId, task.unitId, task.terrainId,
                           task.isDefault, task.enableTargeting,
                           task.combatLevelFlag,
                           task.pickForConstruction,
                           task.resourceIn,
                           task.resourceMultiplier,
                           task.resourceOut,
                           task.gatherType,
                           task.workValue1,
                           task.workValue2,
                           task.workRange,
                           task.movingGraphic,
                           task.proceedingGraphic,
                           task.workingGraphic,
                           task.carryingGraphic,
                           task.resourceGatheringSound,
                           task.resourceDepositSound);
            }
            const auto *attackGraphic = assets.dat().graphic(unit.attackGraphic);
            const auto *dyingGraphic = assets.dat().graphic(unit.dyingGraphic);
            const auto *projectile = unit.projectileUnitId >= 0 &&
                                             (size_t)unit.projectileUnitId < units.size()
                                         ? &units[(size_t)unit.projectileUnitId]
                                         : nullptr;
            const auto *projectileGraphic =
                projectile ? assets.dat().graphic(projectile->standingGraphic[0]) : nullptr;
            const auto *deadUnit =
                unit.deadUnitId >= 0 && (size_t)unit.deadUnitId < units.size()
                    ? &units[(size_t)unit.deadUnitId]
                    : nullptr;
            const auto *deadGraphic =
                deadUnit ? assets.dat().graphic(deadUnit->standingGraphic[0]) : nullptr;
            printf("  sounds damage/dying %d/%d, graphics stand/stand2/walk/dying %d/%d/%d/%d, "
                   "dead unit %d, graphic sounds stand/attack/projectile/dying %d/%d/%d/%d\n",
                   unit.damageSound, unit.dyingSound, unit.standingGraphic[0],
                   unit.standingGraphic[1], unit.walkingGraphic,
                   unit.dyingGraphic, unit.deadUnitId,
                   graphic ? graphic->soundId : -1,
                   attackGraphic ? attackGraphic->soundId : -1,
                   projectileGraphic ? projectileGraphic->soundId : -1,
                   dyingGraphic ? dyingGraphic->soundId : -1);
            printf("  death animation frames/duration/sequence %d/%.3f/0x%02x, "
                   "remains graphic %d frames/duration/sequence %d/%.3f/0x%02x\n",
                   dyingGraphic ? dyingGraphic->frameCount : 0,
                   dyingGraphic ? dyingGraphic->frameDuration : 0,
                   dyingGraphic ? dyingGraphic->sequenceType : 0,
                   deadUnit ? deadUnit->standingGraphic[0] : -1,
                   deadGraphic ? deadGraphic->frameCount : 0,
                   deadGraphic ? deadGraphic->frameDuration : 0,
                   deadGraphic ? deadGraphic->sequenceType : 0);
            printf("  damage graphics:");
            for (const auto &damage : unit.damageGraphics) {
                const auto *damageGraphic = assets.dat().graphic(damage.graphicId);
                printf(" %d@%d%% mode %u", damage.graphicId, damage.damagePercent,
                       damage.applyMode);
                if (damageGraphic)
                    printf("(slp %d frames %d layer %u sound %d)", damageGraphic->slp,
                           damageGraphic->frameCount, damageGraphic->layer,
                           damageGraphic->soundId);
            }
            printf("\n");
            printf("  attacks:");
            for (const auto &attack : unit.attacks)
                printf(" %d=%d", attack.cls, attack.amount);
            printf("; armours:");
            for (const auto &armour : unit.armours)
                printf(" %d=%d", armour.cls, armour.amount);
            printf("\n");
            if (graphic)
                for (const auto &delta : graphic->deltas)
                    if (const auto *child = assets.dat().graphic(delta.graphicId))
                        printf("  delta graphic %d slp %d frames %d angles %d layer %u offset %d,%d display angle %d\n",
                               delta.graphicId, child->slp, child->frameCount, child->angleCount,
                               child->layer, delta.offsetX, delta.offsetY, delta.displayAngle);
            if (unit.type == dat::UT_Building)
                for (const auto &annex : unit.annexes)
                    if (annex.unitId >= 0)
                        printf("  annex unit %d offset %.2f,%.2f\n", annex.unitId,
                               annex.misplacementX, annex.misplacementY);
        }

        return 0;
}

static int cmdUnits(const char *dataDir, const char *fragment) {
    SoftRenderer renderer;
    Assets assets(&renderer);
    std::string err;
    if (!assets.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (assets.dat().civs.empty()) return 0;
    const auto &units = assets.dat().civs[0].units;
    for (size_t id = 0; id < units.size(); id++) {
        const auto &unit = units[id];
        const std::string localized =
            assets.localizedString(unit.languageDllName);
        if (!unit.exists ||
            (unit.name.find(fragment) == std::string::npos &&
             unit.name2.find(fragment) == std::string::npos &&
             localized.find(fragment) == std::string::npos))
            continue;
        const auto *graphic =
            assets.dat().graphic(unit.standingGraphic[0]);
        printf("%4zu %-24s %-24s '%s' type %u class %d line %d graphic %d slp %d "
               "frames %d duration %.3f sequence 0x%02x "
               "copy/base %d/%d collision %.2f,%.2f clearance %.2f,%.2f "
               "terrain %d/%d side %d/%d hill %u restriction %d adjacent %u\n",
               id, unit.name.c_str(), unit.name2.c_str(),
               localized.c_str(),
               unit.type, unit.cls, unit.unitLine,
               unit.standingGraphic[0], graphic ? graphic->slp : -1,
               graphic ? graphic->frameCount : 0,
               graphic ? graphic->frameDuration : 0,
               graphic ? graphic->sequenceType : 0,
               unit.copyId, unit.baseId,
               unit.collisionSize[0], unit.collisionSize[1],
               unit.clearanceSize[0], unit.clearanceSize[1],
               unit.placementTerrain[0], unit.placementTerrain[1],
               unit.placementSideTerrain[0], unit.placementSideTerrain[1],
               unit.hillMode, unit.terrainRestriction,
               unit.adjacentMode);
    }
    return 0;
}

static int cmdTech(const char *dataDir, int id) {
    SoftRenderer renderer;
    Assets assets(&renderer);
    std::string err;
    if (!assets.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    const auto &techs = assets.dat().techs;
    if (id < 0 || (size_t)id >= techs.size()) {
        fprintf(stderr, "error: technology %d is unavailable\n", id);
        return 1;
    }
    const dat::Tech &tech = techs[(size_t)id];
    printf("tech %d '%s' internal '%s' civ %d location %d time %d effect %d icon %d button %u\n",
           id, assets.localizedString(tech.languageDllName).c_str(),
           tech.name2.c_str(), tech.civ, tech.locationId,
           tech.researchTime, tech.effectId,
           tech.iconId, tech.buttonId);
    printf("  required (%d):", tech.requiredTechCount);
    for (int required : tech.requiredTechs)
        if (required >= 0) printf(" %d", required);
    printf("; costs");
    for (const auto &cost : tech.costs)
        if (cost.flag && cost.type >= 0)
            printf(" %d=%d", cost.type, cost.amount);
    printf("\n");
    printf("  description '%s'\n  help '%s'\n  tree '%s'\n",
           assets.localizedString(
               tech.languageDllDescription).c_str(),
           assets.localizedString(
               tech.languageDllHelp).c_str(),
           assets.localizedString(
               tech.languageDllTechTree).c_str());
    if (tech.effectId >= 0 &&
        (size_t)tech.effectId < assets.dat().effects.size()) {
        const dat::Effect &effect =
            assets.dat().effects[(size_t)tech.effectId];
        printf("  effect '%s'\n", effect.name.c_str());
        for (const dat::EffectCommand &command : effect.commands)
            printf("    type %u a %d b %d c %d d %.3f\n",
                   command.type, command.a, command.b,
                   command.c, command.d);
    }
    return 0;
}

static int cmdTechs(const char *dataDir, const char *fragment) {
    SoftRenderer renderer;
    Assets assets(&renderer);
    std::string err;
    if (!assets.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    for (size_t id = 0; id < assets.dat().techs.size(); id++) {
        const dat::Tech &tech = assets.dat().techs[id];
        const std::string localized =
            assets.localizedString(tech.languageDllName);
        if (tech.name.find(fragment) == std::string::npos &&
            tech.name2.find(fragment) == std::string::npos &&
            localized.find(fragment) == std::string::npos)
            continue;
        printf("%4zu location %d effect %d time %d civ %d button %u "
               "'%s' '%s' '%s'\n",
               id, tech.locationId, tech.effectId, tech.researchTime,
               tech.civ, tech.buttonId, tech.name.c_str(),
               tech.name2.c_str(), localized.c_str());
    }
    return 0;
}

static int cmdEffectRefs(
    const char *dataDir, int value) {
    SoftRenderer renderer;
    Assets assets(&renderer);
    std::string err;
    if (!assets.init(dataDir, &err)) {
        fprintf(
            stderr, "error: %s\n",
            err.c_str());
        return 1;
    }
    for (size_t technologyId = 0;
         technologyId < assets.dat().techs.size();
         technologyId++) {
        const dat::Tech &technology =
            assets.dat().techs[technologyId];
        if (technology.effectId < 0 ||
            (size_t)technology.effectId >=
                assets.dat().effects.size())
            continue;
        const dat::Effect &effect =
            assets.dat().effects[
                (size_t)technology.effectId];
        for (const dat::EffectCommand &command :
             effect.commands) {
            const int rounded =
                (int)std::lround(command.d);
            if (command.a != value &&
                command.b != value &&
                command.c != value &&
                rounded != value)
                continue;
            printf(
                "tech %zu '%s' effect %d '%s': "
                "type %u a %d b %d c %d d %.3f\n",
                technologyId,
                assets.localizedString(
                    technology.languageDllName)
                    .c_str(),
                technology.effectId,
                effect.name.c_str(),
                command.type, command.a,
                command.b, command.c,
                command.d);
        }
    }
    return 0;
}

static int cmdEffect(
    const char *dataDir, int effectId) {
    SoftRenderer renderer;
    Assets assets(&renderer);
    std::string err;
    if (!assets.init(dataDir, &err)) {
        fprintf(
            stderr, "error: %s\n",
            err.c_str());
        return 1;
    }
    if (effectId < 0 ||
        (size_t)effectId >=
            assets.dat().effects.size()) {
        fprintf(
            stderr,
            "error: effect %d is unavailable\n",
            effectId);
        return 1;
    }
    const dat::Effect &effect =
        assets.dat().effects[(size_t)effectId];
    printf(
        "effect %d '%s' (%zu commands)\n",
        effectId, effect.name.c_str(),
        effect.commands.size());
    for (const dat::EffectCommand &command :
         effect.commands)
        printf(
            "  type %u a %d b %d c %d "
            "d %.3f\n",
            command.type, command.a,
            command.b, command.c,
            command.d);
    return 0;
}

static int cmdOptions(const char *dataDir, int civId,
                      int buildingId) {
    SoftRenderer renderer;
    Assets assets(&renderer);
    std::string err;
    if (!assets.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (civId < 0 ||
        (size_t)civId >= assets.dat().civs.size()) {
        fprintf(stderr, "error: civilization %d is unavailable\n",
                civId);
        return 1;
    }
    const auto &civ = assets.dat().civs[(size_t)civId];
    printf("units at building %d for civ %d '%s' "
           "tech-tree %d\n",
           buildingId, civId, civ.name.c_str(),
           civ.techTreeId);
    for (const dat::Unit &unit : civ.units) {
        if (!unit.exists ||
            unit.trainLocationId != buildingId)
            continue;
        printf("  unit %d button %u enabled/disabled %u/%u "
               "hidden %u hero %u unit-civ %u '%s' '%s'\n",
               unit.id, unit.buttonId, unit.enabled,
               unit.disabled, unit.hideInEditor, unit.heroMode,
               unit.civilization, unit.name.c_str(),
               assets.localizedString(
                   unit.languageDllName).c_str());
    }
    printf("technologies at building %d\n", buildingId);
    for (size_t id = 0; id < assets.dat().techs.size(); id++) {
        const dat::Tech &tech = assets.dat().techs[id];
        if (tech.locationId != buildingId)
            continue;
        printf("  tech %zu button %u civ %d time %d effect %d "
               "required %d '%s' '%s'\n",
               id, tech.buttonId, tech.civ,
               tech.researchTime, tech.effectId,
               tech.requiredTechCount, tech.name2.c_str(),
               assets.localizedString(
                   tech.languageDllName).c_str());
    }
    return 0;
}

static int cmdRestriction(const char *dataDir, int id) {
    SoftRenderer renderer;
    Assets assets(&renderer);
    std::string err;
    if (!assets.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    const auto &restrictions = assets.dat().terrainRestrictions;
    if (id < 0 || (size_t)id >= restrictions.size()) {
        fprintf(stderr, "error: restriction ID out of range\n");
        return 1;
    }
    const auto &values = restrictions[(size_t)id].passableBuildableDmgMultiplier;
    const auto &terrains = assets.dat().terrainBlock.terrains;
    for (size_t terrain = 0; terrain < values.size() && terrain < terrains.size(); terrain++)
        printf("%3zu %8.3f %s\n", terrain, values[terrain], terrains[terrain].name.c_str());
    return 0;
}

static int cmdSound(const char *dataDir, int id) {
    SoftRenderer renderer;
    Assets assets(&renderer);
    std::string err;
    if (!assets.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    const auto &sounds = assets.dat().sounds;
    if (id == -1) {
        for (size_t soundId = 0;
             soundId < sounds.size(); soundId++) {
            const auto &sound = sounds[soundId];
            for (const auto &item : sound.items)
                printf("%4zu %5d %s\n", soundId,
                       item.resourceId,
                       item.fileName.c_str());
        }
        return 0;
    }
    if (id < 0 || (size_t)id >= sounds.size()) {
        fprintf(stderr, "error: sound ID out of range\n");
        return 1;
    }
    const auto &sound = sounds[(size_t)id];
    printf("sound %d stored id %d delay %d cache %d, %zu items\n",
           id, sound.id, sound.playDelay, sound.cacheTime, sound.items.size());
    for (const auto &item : sound.items)
        printf("  resource %d probability %d civ %d icon %d '%s'\n",
               item.resourceId, item.probability, item.civilization, item.iconSet,
               item.fileName.c_str());
    std::vector<uint8_t> data;
    int resourceId = -1;
    std::string fileName;
    AudioClip clip;
    if (assets.readSound(id, -1, 0, data, &resourceId, &fileName) &&
        decodeWav(data, clip, &err))
        printf("  decoded resource %d '%s': %zu frames, %.2f seconds\n",
               resourceId, fileName.c_str(), clip.frameCount(),
               clip.frameCount() / (double)AudioClip::kSampleRate);
    return 0;
}

static int cmdDrs(const char *path) {
    std::string err;
    auto a = DrsArchive::open(path, &err);
    if (!a) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    for (auto &e : a->entries()) {
        const char *t = e.type == DrsType::Slp ? "slp" : e.type == DrsType::Wav ? "wav" : e.type == DrsType::Bina ? "bina" : "?";
        printf("%6d %-4s %10u %9u\n", e.id, t, e.offset, e.size);
    }
    return 0;
}

static int cmdDrsSlps(const char *path) {
    std::string err;
    auto archive = DrsArchive::open(path, &err);
    if (!archive) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    for (const DrsEntry &entry : archive->entries()) {
        if (entry.type != DrsType::Slp) continue;
        std::vector<uint8_t> data;
        Slp slp;
        if (!archive->read(entry, data) || !slp.parse(std::move(data), &err)) {
            fprintf(stderr, "warning: could not parse SLP %d: %s\n", entry.id, err.c_str());
            continue;
        }
        int maxWidth = 0, maxHeight = 0;
        for (size_t frame = 0; frame < slp.frameCount(); frame++) {
            maxWidth = std::max(maxWidth, slp.frame(frame).width);
            maxHeight = std::max(maxHeight, slp.frame(frame).height);
        }
        const SlpFrameInfo &first = slp.frame(0);
        printf("%6d %4zu frames %4dx%-4d hotspot %4d,%4d %9u bytes\n",
               entry.id, slp.frameCount(), maxWidth, maxHeight,
               first.hotspotX, first.hotspotY, entry.size);
    }
    return 0;
}

static std::unique_ptr<CpxArchive> openCampaign(const char *path) {
    std::string err;
    auto campaign = CpxArchive::open(path, &err);
    if (!campaign) fprintf(stderr, "error: %s\n", err.c_str());
    return campaign;
}

static int cmdCampaign(const char *path) {
    auto campaign = openCampaign(path);
    if (!campaign) return 1;
    printf("%s: version %s, %zu entries\n", campaign->name().c_str(), campaign->version().c_str(),
           campaign->entries().size());
    for (size_t i = 0; i < campaign->entries().size(); i++) {
        const CpxEntry &entry = campaign->entries()[i];
        printf("%3zu  %-32s %8u bytes  %s\n", i + 1, entry.filename.c_str(), entry.size,
               entry.identifier.c_str());
    }
    return 0;
}

static bool loadScenario(const char *path, int entryNumber, Scenario &scenario, std::string &err) {
    auto campaign = CpxArchive::open(path, &err);
    if (!campaign) return false;
    if (entryNumber < 1 || (size_t)entryNumber > campaign->entries().size()) {
        err = "campaign entry must be between 1 and " + std::to_string(campaign->entries().size());
        return false;
    }
    std::vector<uint8_t> scx;
    if (!campaign->read((size_t)entryNumber - 1, scx, &err)) return false;
    return scenario.load(scx, &err);
}

static const char *effectName(int type) {
    static const char *names[] = {
        "None", "Change Diplomacy", "Research Technology", "Send Chat", "Play Sound",
        "Send Tribute", "Unlock Gate", "Lock Gate", "Activate Trigger", "Deactivate Trigger",
        "AI Script Goal", "Create Object", "Task Object", "Declare Victory", "Kill Object",
        "Remove Object", "Change View", "Unload", "Change Ownership", "Patrol",
        "Display Instructions", "Clear Instructions", "Freeze Unit", "Advanced Buttons",
        "Damage Object", "Place Foundation", "Change Object Name", "Change Object HP",
        "Change Object Attack", "Stop Unit", "Snap View", "Unknown 31", "Enable Tech",
        "Disable Tech", "Enable Unit", "Disable Unit", "Flash Objects"
    };
    return type >= 0 && (size_t)type < sizeof(names) / sizeof(names[0]) ? names[type] : "Unknown";
}

static const char *conditionName(int type) {
    static const char *names[] = {
        "None", "Bring Object to Area", "Bring Object to Object", "Own Objects",
        "Own Fewer Objects", "Objects in Area", "Destroy Object", "Capture Object",
        "Accumulate Attribute", "Research Technology", "Timer", "Object Selected",
        "AI Signal", "Player Defeated", "Object Has Target", "Object Visible",
        "Object Not Visible", "Researching Technology", "Units Garrisoned",
        "Difficulty Level", "Own Fewer Foundations", "Selected Objects in Area",
        "Powered Objects in Area", "Units Queued Past Pop Cap"
    };
    return type >= 0 && (size_t)type < sizeof(names) / sizeof(names[0]) ? names[type] : "Unknown";
}

static int cmdScenario(const char *path, int entryNumber) {
    Scenario scenario;
    std::string err;
    if (!loadScenario(path, entryNumber, scenario, err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    printf("%s: SCX %s, player data %.2f, %ux%u map, %u players, %zu units, next unit %u\n",
           scenario.originalFilename.c_str(), scenario.version.c_str(), scenario.playerDataVersion,
           scenario.map.width, scenario.map.height, scenario.enabledPlayerCount, scenario.units.size(),
           scenario.nextUnitId);
    printf("camera player %.1f,%.1f, map %.1f,%.1f\n", scenario.cameraX, scenario.cameraY,
           scenario.mapCameraX, scenario.mapCameraY);
    printf("instructions: %s\n", scenario.instructions.c_str());
    for (size_t i = 0; i < 8; i++) {
        const ScenarioPlayer &player = scenario.players[i];
        printf("player %zu: active %d, human %d, civ %u, color %u, camera %.1f,%.1f, resources "
               "%.0f/%.0f/%.0f/%.0f/%.0f, population %.0f, allied victory %d, name '%s', "
               "ai %u '%s' personality '%s' files '%s'/'%s'/%zu bytes, disables %zu/%zu/%zu, "
               "age %d, diplomacy",
               i + 1, player.active, player.human, player.civilization, player.color,
               player.cameraX, player.cameraY,
               player.resources[0], player.resources[1], player.resources[2],
               player.resources[3], player.resources[4], player.populationLimit,
               player.alliedVictory, player.name.c_str(), player.aiType,
               player.aiName.c_str(), player.personalityName.c_str(),
               player.aiFilename.c_str(), player.cityFilename.c_str(),
               player.personality.size(), player.disabledTechnologies.size(),
               player.disabledUnits.size(), player.disabledBuildings.size(),
               player.startingAge);
        for (size_t other = 0; other < 9; other++) printf(" %u", player.diplomacy[other]);
        printf("\n");
    }
    printf("triggers: %zu, system %.2f, objective state %u\n", scenario.triggers.size(),
           scenario.triggerSystemVersion, scenario.objectiveState);
    for (size_t i = 0; i < scenario.triggers.size(); i++) {
        const ScenarioTrigger &trigger = scenario.triggers[i];
        printf("  %zu: enabled %d, loop %d, objective %d/%d, %zu effects, %zu conditions, '%s'\n",
               i, trigger.enabled, trigger.looping, trigger.objective, trigger.objectiveOrder,
               trigger.effects.size(), trigger.conditions.size(), trigger.name.c_str());
        for (const ScenarioEffect &effect : trigger.effects) {
            printf("    effect %d %-23s fields", effect.type, effectName(effect.type));
            for (int32_t field : effect.fields) printf(" %d", field);
            printf(" selected");
            for (uint32_t id : effect.selectedUnitIds) printf(" %u", id);
            printf(" message '%s' sound '%s'\n", effect.message.c_str(), effect.sound.c_str());
        }
        for (const ScenarioCondition &condition : trigger.conditions) {
            printf("    condition %d %-23s fields", condition.type, conditionName(condition.type));
            for (int32_t field : condition.fields) printf(" %d", field);
            printf("\n");
        }
    }
    return 0;
}

static int cmdScenarioUnits(const char *dataDir, const char *path, int entryNumber, int detailId) {
    Scenario scenario;
    std::string err;
    if (!loadScenario(path, entryNumber, scenario, err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    SoftRenderer renderer;
    Assets assets(&renderer);
    if (!assets.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (detailId >= -1) {
        for (const ScenarioUnit &unit : scenario.units) {
            if (detailId >= 0 && unit.unitId != detailId) continue;
            printf("player %u spawn %u unit %u at %.2f,%.2f rotation %.6f frame %u garrison %d\n",
                   unit.player, unit.spawnId, unit.unitId, unit.x, unit.y, unit.rotation,
                   unit.initialFrame, unit.garrisonedInId);
        }
        return 0;
    }
    std::map<std::pair<uint8_t, uint16_t>, size_t> counts;
    for (const ScenarioUnit &unit : scenario.units) counts[{unit.player, unit.unitId}]++;
    for (const auto &[key, count] : counts) {
        const uint8_t player = key.first;
        const uint16_t id = key.second;
        const size_t civ = player > 0 && player <= scenario.civilizations.size()
                               ? scenario.civilizations[player - 1]
                               : 0;
        const dat::Unit *unit = nullptr;
        if (civ < assets.dat().civs.size() && id < assets.dat().civs[civ].units.size() &&
            assets.dat().civs[civ].units[id].exists)
            unit = &assets.dat().civs[civ].units[id];
        const dat::Graphic *graphic = unit ? assets.dat().graphic(unit->standingGraphic[0]) : nullptr;
        printf("player %u civ %zu unit %u count %zu '%s' adjacent %u angles %u\n",
               player, civ, id, count, unit ? unit->name.c_str() : "?",
               unit ? unit->adjacentMode : 0, graphic ? graphic->angleCount : 0);
    }
    return 0;
}

static int cmdSlp(const char *dataDir, int id, const char *out, int base) {
    SoftRenderer r;
    Assets a(&r);
    std::string err;
    if (!a.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    const SpriteSheet *sh =
        id >= 50000 ? a.interfaceSheet(id)
                    : a.sheet(id, base);
    if (!sh) sh = a.interfaceSheet(id);
    if (!sh) sh = a.terrainSheet(id);
    if (!sh) {
        fprintf(stderr, "slp %d not found\n", id);
        return 1;
    }
    // Lay frames out in a grid with a checkerboard-free grey background.
    int maxW = 1, maxH = 1;
    for (auto &f : sh->frames) { maxW = std::max(maxW, f.w); maxH = std::max(maxH, f.h); }
    int cols = std::max(1, std::min<int>((int)sh->frames.size(), 2048 / (maxW + 2)));
    int rows = ((int)sh->frames.size() + cols - 1) / cols;
    r.beginFrame(cols * (maxW + 2), rows * (maxH + 2), 1.0f, 90, 90, 100);
    for (size_t i = 0; i < sh->frames.size(); i++) {
        const SpriteFrame &f = sh->frames[i];
        float x = (float)(i % cols) * (maxW + 2), y = (float)(i / cols) * (maxH + 2);
        r.draw(f.tex, Quad{x, y, (float)f.w, (float)f.h, f.u, f.v, f.u + f.w, f.v + f.h});
    }
    r.endFrame();
    r.savePng(out);
    printf("slp %d: %zu frames, %zu pages, %zu KB -> %s\n", id, sh->frames.size(), sh->pages.size(), sh->bytes / 1024, out);
    for (size_t i = 0; i < sh->frames.size(); i++)
        printf("  frame %zu: %dx%d hot %d,%d\n", i,
               sh->frames[i].w, sh->frames[i].h,
               sh->frames[i].hotX, sh->frames[i].hotY);
    return 0;
}

static int cmdSlopes(const char *dataDir, int id, const char *out, size_t frame) {
    SoftRenderer r;
    Assets a(&r);
    std::string err;
    if (!a.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    constexpr int cellW = 105, cellH = 81;
    r.beginFrame(9 * cellW, 2 * cellH, 1.0f, 70, 70, 80);
    std::array<int8_t, 8> neighbors;
    neighbors.fill(-1);
    for (int slope = 0; slope < (int)kSlopeCount; slope++) {
        const SpriteFrame *frameData = a.terrainSlopeFrame(id, slope, frame, neighbors);
        if (!frameData) {
            fprintf(stderr, "slope %d for terrain SLP %d could not be generated\n", slope, id);
            return 1;
        }
        const SpriteFrame &f = *frameData;
        const float x = (float)(slope % 9) * cellW + (cellW - f.w) * 0.5f;
        const float y = (float)(slope / 9) * cellH + (cellH - f.h) * 0.5f;
        r.draw(f.tex, Quad{x, y, (float)f.w, (float)f.h, f.u, f.v, f.u + f.w, f.v + f.h});
    }
    r.endFrame();
    r.savePng(out);
    printf("rendered 17 slopes for terrain SLP %d frame %zu -> %s\n", id, frame, out);
    return 0;
}

static int cmdRender(const char *dataDir, const char *out, uint32_t seed,
                     float seconds, float zoom, bool compact = false,
                     const char *menu = nullptr) {
    SoftRenderer r;
    Assets a(&r);
    std::string err;
    auto t0 = std::chrono::steady_clock::now();
    if (!a.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    Game g(a);
    if (!(compact
              ? g.initCompactTestMap(seed, 64, &err)
              : g.init(seed, 64, &err))) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    g.setZoom(zoom);
    InputState in;
    for (float t = 0; t < seconds; t += 1.0f / 30) g.update(1.0f / 30, in);
    if (compact && menu) {
        const bool research =
            !strcmp(menu, "research");
        if (g.selectObjectForTesting(
                research ? 1u : 6u)) {
            in = {};
            in.cycleAttackMode = true;
            g.update(0.001f, in);
            if (research) {
                in = {};
                in.actionTabRight = true;
                g.update(0.001f, in);
            }
        }
    }
    g.render(r, 960, 544);
    auto t1 = std::chrono::steady_clock::now();
    r.savePng(out);
    printf("rendered %d tiles, %d sprites; %zu sheets, %.1f MB textures; %.0f ms -> %s\n", g.stats().tiles,
           g.stats().sprites, a.sheetCount(), a.textureBytes() / 1048576.0,
           std::chrono::duration<double, std::milli>(t1 - t0).count(), out);
    return 0;
}

static int cmdRenderScenario(const char *dataDir, const char *campaignPath, int entryNumber, const char *out,
                             float x, float y, float zoom, bool moving) {
    Scenario scenario;
    std::string err;
    if (!loadScenario(campaignPath, entryNumber, scenario, err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    SoftRenderer renderer;
    Assets assets(&renderer);
    auto start = std::chrono::steady_clock::now();
    if (!assets.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    Game game(assets);
    if (!game.initScenario(scenario, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (x >= 0 && y >= 0) game.lookAt(x, y);
    game.setZoom(zoom);
    if (moving) {
        InputState input;
        input.scrollX = 1;
        game.update(0, input);
    }
    game.render(renderer, 960, 544);
    renderer.savePng(out);
    auto end = std::chrono::steady_clock::now();
    printf("rendered %s (%ux%u) at %.1f,%.1f: %d tiles, %d sprites, %d quads, "
           "%.1f MB textures, %.0f ms -> %s\n",
           scenario.originalFilename.c_str(), scenario.map.width, scenario.map.height,
           x >= 0 ? x : scenario.map.width * 0.5f, y >= 0 ? y : scenario.map.height * 0.5f,
           game.stats().tiles, game.stats().sprites, renderer.drawCalls(),
           assets.textureBytes() / 1048576.0,
           std::chrono::duration<double, std::milli>(end - start).count(), out);
    return 0;
}

static int cmdStressScenario(const char *dataDir, const char *campaignPath, int entryNumber, float zoom) {
    Scenario scenario;
    std::string err;
    if (!loadScenario(campaignPath, entryNumber, scenario, err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }

    SoftRenderer renderer;
    Assets assets(&renderer);
    if (!assets.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    Game game(assets);
    if (!game.initScenario(scenario, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    game.setZoom(zoom);
    constexpr int steps = 5;
    size_t peakBytes = 0, peakSheets = 0;
    for (int row = 0; row < steps; row++) {
        for (int column = 0; column < steps; column++) {
            const int orderedColumn = row & 1 ? steps - 1 - column : column;
            const float x = 12.0f + orderedColumn * (scenario.map.width - 24.0f) / (steps - 1);
            const float y = 12.0f + row * (scenario.map.height - 24.0f) / (steps - 1);
            game.lookAt(x, y);
            InputState moving;
            moving.scrollX = 1;
            game.update(0, moving);
            game.render(renderer, 960, 544);
            peakBytes = std::max(peakBytes, assets.textureBytes());
            peakSheets = std::max(peakSheets, assets.sheetCount());
        }
    }
    printf("stress rendered %d views: final %.1f MB/%zu sheets, peak %.1f MB/%zu sheets\n",
           steps * steps, assets.textureBytes() / 1048576.0, assets.sheetCount(),
           peakBytes / 1048576.0, peakSheets);
    if (peakBytes > 70u * 1024u * 1024u) {
        fprintf(stderr, "error: terrain texture cache exceeded stress limit\n");
        return 1;
    }
    return 0;
}

static int cmdMp3(const char *path) {
    AudioClip clip;
    std::string err;
    if (!loadMp3(path, clip, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    printf("decoded %s: %zu frames, %.2f seconds, %u Hz stereo\n", path, clip.frameCount(),
           clip.frameCount() / (double)AudioClip::kSampleRate, AudioClip::kSampleRate);
    return 0;
}

static int cmdSimulateScenario(const char *dataDir, const char *campaignPath, int entryNumber,
                               float seconds, const char *out) {
    Scenario scenario;
    std::string err;
    if (!loadScenario(campaignPath, entryNumber, scenario, err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    SoftRenderer renderer;
    Assets assets(&renderer);
    if (!assets.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    Game game(assets);
    float simulationElapsed = 0;
    std::vector<std::array<float, 2>> unitSounds;
    game.setLogger([](const std::string &message) { printf("runtime: %s\n", message.c_str()); });
    game.setSoundPlayer([](const std::string &name) {
        printf("sound: %s\n", name.c_str());
        return 2.0f;
    });
    game.setUnitSoundPlayer([&](int soundId, int civilization) {
        if (unitSounds.size() < 64)
            unitSounds.push_back(
                {simulationElapsed, (float)(soundId * 100 + civilization)});
    });
    if (!game.initScenario(scenario, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    InputState input;
    const float step = 1.0f / 30.0f;
    for (; simulationElapsed < seconds; simulationElapsed += step)
        game.update(std::min(step, seconds - simulationElapsed), input);
    if (out) {
        game.render(renderer, 960, 544);
        if (!renderer.savePng(out)) {
            fprintf(stderr, "error: could not write %s\n", out);
            return 1;
        }
    }

    printf("simulated %.2f seconds: %zu active objects, gate 13861 %s, instruction '%s'\n",
           seconds, game.activeObjectCount(), game.gateLocked(13861) ? "locked" : "unlocked",
           game.currentInstruction().c_str());
    const MovementStats movement = game.movementStats();
    printf("  movement: %zu pathing, %zu overlapping pairs, %zu terrain violations, "
           "%zu static obstruction violations\n",
           movement.pathingObjects, movement.overlappingPairs, movement.terrainViolations,
           movement.staticObstructionViolations);
    const CombatStats combat = game.combatStats();
    printf("  combat: %zu active orders, %zu automatic targets, %zu hits, "
           "%zu kills, %zu projectiles\n",
           combat.activeOrders, combat.automaticTargetsAcquired,
           combat.attacksLanded, combat.unitsKilled, combat.projectilesLaunched);
    for (const auto &sound : unitSounds) {
        const int packed = (int)sound[1];
        printf("    unit sound %.2fs: id %d civilization %d\n", sound[0],
               packed / 100, packed % 100);
    }
    for (const MovingObjectInfo &object : game.movingObjects())
        printf("    object %u unit %d player %d at %.2f,%.2f -> %.2f,%.2f "
               "via %.2f,%.2f blocked %.2f\n",
               object.spawnId, object.unitId, object.player, object.x, object.y,
               object.targetX, object.targetY, object.waypointX, object.waypointY,
               object.blockedTime);
    for (size_t i = 0; i < scenario.triggers.size(); i++)
        if (game.triggerEnabled(i) || game.triggerFired(i))
            printf("  trigger %zu: enabled %d fired %d '%s'\n", i, game.triggerEnabled(i),
                   game.triggerFired(i), scenario.triggers[i].name.c_str());
    printf("  player 1 resources: food %.0f wood %.0f stone %.0f gold %.0f "
           "contact %.0f rescued %.0f\n",
           game.resource(1, 0), game.resource(1, 1), game.resource(1, 2),
           game.resource(1, 3), game.resource(1, 200), game.resource(1, 201));
    return 0;
}

static int cmdTestControls(const char *dataDir, const char *campaignPath, int entryNumber,
                           const char *out) {
    Scenario scenario;
    std::string err;
    if (!loadScenario(campaignPath, entryNumber, scenario, err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    SoftRenderer renderer;
    Assets assets(&renderer);
    if (!assets.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    Game game(assets);
    std::vector<int> acknowledgementSounds;
    game.setUnitSoundPlayer([&](int soundId, int) {
        acknowledgementSounds.push_back(soundId);
    });
    if (!game.initScenario(scenario, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }

    InputState input;
    for (int i = 0; i < 30; i++) game.update(1.0f / 30.0f, input);
    const CombatStats startupCombat = game.combatStats();
    const bool quietStartup =
        startupCombat.automaticTargetsAcquired == 0 &&
        startupCombat.attacksLanded == 0 &&
        startupCombat.unitsKilled == 0 &&
        startupCombat.projectilesLaunched == 0;
    const size_t configuredGates = game.gateCount();
    const std::vector<int> troopOptions =
        game.productionOptionIds(13818);
    const std::vector<int> nurseryUnits =
        game.productionOptionIds(23978);
    const std::vector<int> commandOptions =
        game.productionOptionIds(13780);
    const std::vector<int> commandResearch =
        game.researchOptionIds(13780);
    const bool productionFiltered =
        std::find(troopOptions.begin(), troopOptions.end(), 23) ==
            troopOptions.end() &&
        std::find(troopOptions.begin(), troopOptions.end(), 307) !=
            troopOptions.end() &&
        nurseryUnits.empty() &&
        std::find(commandOptions.begin(), commandOptions.end(), 83) !=
            commandOptions.end() &&
        std::find(commandResearch.begin(),
                  commandResearch.end(),
                  33) != commandResearch.end();
    if (!productionFiltered) {
        auto printIds = [](const char *label,
                           const std::vector<int> &ids) {
            fprintf(stderr, "  %s:", label);
            for (int id : ids) fprintf(stderr, " %d", id);
            fprintf(stderr, "\n");
        };
        printIds("troop options", troopOptions);
        printIds("nursery units", nurseryUnits);
        printIds("command options", commandOptions);
        printIds("command research", commandResearch);
    }
    input.pointerX = 480;
    input.pointerY = 272;
    input.selectPressed = true;
    game.update(0.001f, input);
    const size_t singleSelected = game.selectedObjectCount();
    if (out) {
        game.render(renderer, 960, 544);
        std::string singleOut(out);
        const size_t extension = singleOut.find_last_of('.');
        singleOut.insert(extension == std::string::npos ? singleOut.size()
                                                        : extension,
                         "-single");
        if (!renderer.savePng(singleOut)) {
            fprintf(stderr, "error: could not write %s\n", singleOut.c_str());
            return 1;
        }
    }

    input = {};
    input.pointerX = 340;
    input.pointerY = 195;
    input.selectPressed = true;
    game.update(0.001f, input);
    const std::vector<uint32_t> firstClickOrder =
        game.selectedObjectIds();
    const size_t soundsBeforeDoubleClick = acknowledgementSounds.size();
    input = {};
    input.pointerX = 340;
    input.pointerY = 195;
    input.selectPressed = true;
    game.update(0.001f, input);
    const size_t doubleSelected = game.selectedObjectCount();
    const std::vector<uint32_t> doubleClickOrder =
        game.selectedObjectIds();
    const bool clickedUnitLeadsDoubleSelection =
        !firstClickOrder.empty() && !doubleClickOrder.empty() &&
        doubleClickOrder.front() == firstClickOrder.front();
    const bool doubleClickPlayedOnce =
        acknowledgementSounds.size() == soundsBeforeDoubleClick;

    input = {};
    input.boxSelectCommit = true;
    input.boxStartX = 0;
    input.boxStartY = 0;
    input.boxEndX = 959;
    input.boxEndY = 543;
    game.update(0.001f, input);
    const size_t boxSelected = game.selectedObjectCount();
    const std::vector<uint32_t> boxSelectionOrder =
        game.selectedObjectIds();

    input = {};
    input.pointerX = 118;
    input.pointerY = 452;
    input.selectPressed = true;
    game.update(0.001f, input);
    const size_t portraitSelected = game.selectedObjectCount();
    const std::vector<uint32_t> portraitSelectionOrder =
        game.selectedObjectIds();
    const bool firstPortraitSelectedLeader =
        !boxSelectionOrder.empty() &&
        portraitSelectionOrder.size() == 1 &&
        portraitSelectionOrder.front() == boxSelectionOrder.front();

    input = {};
    input.boxSelectCommit = true;
    input.boxStartX = 0;
    input.boxStartY = 0;
    input.boxEndX = 959;
    input.boxEndY = 543;
    game.update(0.001f, input);

    input = {};
    input.cycleAttackMode = true;
    game.update(0.001f, input);
    const size_t attackModeChanges = game.combatStats().attackModeChanges;

    input = {};
    input.pointerX = 620;
    input.pointerY = 360;
    input.commandPressed = true;
    const size_t soundsBeforeMove = acknowledgementSounds.size();
    game.update(0.001f, input);
    const bool movePlayedOnce =
        acknowledgementSounds.size() == soundsBeforeMove + 1;
    const size_t commanded = game.selectedMovingObjectCount();
    input = {};
    int movementFrames = 0;
    for (; movementFrames < 1800 &&
           game.movementStats().selectedPendingMoveGoals > 0;
         movementFrames++) {
        game.update(1.0f / 30.0f, input);
        if (std::getenv("SWGB_TRACE_PENDING") && movementFrames % 30 == 0)
            fprintf(stderr, "t=%.1f pending=%zu\n", movementFrames / 30.0f,
                    game.movementStats().selectedPendingMoveGoals);
    }

    const float foodBeforeCheat = game.resource(1, 0);
    input = {};
    input.toggleCheatMenu = true;
    game.update(0.001f, input);
    input = {};
    input.menuActivate = true;
    game.update(0.001f, input);
    const float foodAfterCheat = game.resource(1, 0);
    const bool forceFoodGranted =
        std::abs(foodAfterCheat - foodBeforeCheat - 1000.0f) <
        0.01f;
    input = {};
    input.menuBack = true;
    game.update(0.001f, input);
    input = {};
    input.toggleCheatMenu = true;
    game.update(0.001f, input);
    for (int i = 0; i < 5; ++i) {
        input = {};
        input.menuDown = true;
        game.update(0.001f, input);
    }
    input = {};
    input.menuActivate = true;
    game.update(0.001f, input);
    const bool fullTechTreeUnlocked =
        game.fullTechTreeUnlocked();
    input = {};
    input.menuBack = true;
    game.update(0.001f, input);
    constexpr uint32_t gateId = 13861;
    const bool gateProductionHidden =
        game.productionOptionIds(gateId).empty() &&
        game.researchOptionIds(gateId).empty();
    bool gateMenuToggled = false;
    bool gateSelected = false;
    float gateX = 0, gateY = 0;
    game.setLocalPlayerForTesting(5);
    if (game.lookAtObject(gateId) &&
        game.objectScreenPosition(
            gateId, 960, 544, gateX, gateY)) {
        const bool initiallyLocked =
            game.gateLocked(gateId);
        input = {};
        input.pointerX = gateX;
        input.pointerY = gateY;
        input.selectPressed = true;
        game.update(0.001f, input);
        gateSelected = game.objectSelected(gateId);
        input = {};
        input.cycleAttackMode = true;
        game.update(0.001f, input);
        input = {};
        input.pointerX = 214.0f;
        input.pointerY = 190.0f;
        input.selectPressed = true;
        game.update(0.001f, input);
        gateMenuToggled =
            game.gateLocked(gateId) !=
            initiallyLocked;
    }
    game.setLocalPlayerForTesting(1);

    if (out) {
        game.render(renderer, 960, 544);
        if (!renderer.savePng(out)) {
            fprintf(stderr, "error: could not write %s\n", out);
            return 1;
        }
    }
    const MovementStats movement = game.movementStats();
    printf("controls: single %zu, double %zu, box %zu, portrait %zu, commanded %zu, sounds %zu, move %.2fs, "
           "leader double/portrait %d/%d, stance changes %zu, single audio %d/%d, quiet startup %d, gates %zu/%d/%d/%d, production %d, cheats %d/%d, "
           "pending goals %zu/%zu, overlaps %zu (%u:u%d:s%d/%u:u%d:s%d), terrain violations %zu\n",
           singleSelected, doubleSelected, boxSelected, portraitSelected,
           commanded, acknowledgementSounds.size(),
           movementFrames / 30.0f,
           clickedUnitLeadsDoubleSelection ? 1 : 0,
           firstPortraitSelectedLeader ? 1 : 0,
           attackModeChanges,
           doubleClickPlayedOnce ? 1 : 0, movePlayedOnce ? 1 : 0,
           quietStartup ? 1 : 0,
           configuredGates,
           gateProductionHidden ? 1 : 0,
           gateSelected ? 1 : 0,
           gateMenuToggled ? 1 : 0,
           productionFiltered ? 1 : 0,
           forceFoodGranted ? 1 : 0,
           fullTechTreeUnlocked ? 1 : 0,
           movement.selectedPendingMoveGoals, movement.pendingMoveGoals,
           movement.overlappingPairs,
           movement.firstOverlapObject, movement.firstOverlapUnit,
           movement.firstOverlapSelected ? 1 : 0,
           movement.secondOverlapObject, movement.secondOverlapUnit,
           movement.secondOverlapSelected ? 1 : 0,
           movement.terrainViolations);
    if (singleSelected != 1 || doubleSelected <= 1 ||
        boxSelected < doubleSelected || portraitSelected != 1 ||
        commanded == 0 || acknowledgementSounds.size() < 4 ||
        !clickedUnitLeadsDoubleSelection ||
        !firstPortraitSelectedLeader ||
        attackModeChanges != 1 ||
        !doubleClickPlayedOnce || !movePlayedOnce ||
        !quietStartup || configuredGates < 3 ||
        !gateProductionHidden || !gateSelected ||
        !gateMenuToggled ||
        !productionFiltered ||
        !forceFoodGranted ||
        !fullTechTreeUnlocked ||
        movement.selectedPendingMoveGoals != 0 ||
        movement.overlappingPairs != 0 ||
        movement.terrainViolations != 0) {
        for (const MovingObjectInfo &object : game.movingObjects())
            if (object.selected && object.moveGoalActive)
                fprintf(stderr,
                        "  pending object %u unit %d at %.2f,%.2f -> %.2f,%.2f "
                        "via %.2f,%.2f blocked %.2f\n",
                        object.spawnId, object.unitId, object.x, object.y,
                        object.targetX, object.targetY, object.waypointX,
                        object.waypointY, object.blockedTime);
        fprintf(stderr, "error: control validation failed\n");
        return 1;
    }
    return 0;
}

static int cmdTestCombat(const char *dataDir, const char *out) {
    std::string err;
    SoftRenderer renderer;
    Assets assets(&renderer);
    if (!assets.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }

    Game game(assets);
    std::vector<int> acknowledgementSounds;
    game.setUnitSoundPlayer([&](int soundId, int) {
        acknowledgementSounds.push_back(soundId);
    });
    constexpr int mapSize = 96;
    if (!game.init(7, mapSize, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    // This broad compatibility fixture predates fog and intentionally
    // exercises commands against objects across the whole map. Dedicated
    // fog regressions below cover visibility-gated interaction.
    game.setVisibilityCheatsForTesting(
        true, true);
    auto saveStage = [&](const char *suffix) {
        if (!out) return true;
        game.render(renderer, 960, 544);
        std::string path = out;
        const size_t extension = path.rfind('.');
        path.insert(
            extension == std::string::npos
                ? path.size()
                : extension,
            suffix);
        return renderer.savePng(path);
    };
    const std::vector<int> workerBuildings =
        game.buildingOptionIds(6);
    const bool workerCanBuild =
        std::find(workerBuildings.begin(),
                  workerBuildings.end(),
                  70) != workerBuildings.end() &&
        std::find(workerBuildings.begin(),
                  workerBuildings.end(),
                  87) != workerBuildings.end();
    const std::vector<int> economyBuildings =
        game.buildingOptionIds(6, 2);
    const std::vector<int> militaryBuildings =
        game.buildingOptionIds(6, 10);
    const std::vector<int> defenseBuildings =
        game.buildingOptionIds(6, 11);
    const bool buildingPagesSeparated =
        std::find(economyBuildings.begin(),
                  economyBuildings.end(), 70) !=
            economyBuildings.end() &&
        std::find(economyBuildings.begin(),
                  economyBuildings.end(), 87) ==
            economyBuildings.end() &&
        std::find(militaryBuildings.begin(),
                  militaryBuildings.end(), 87) !=
            militaryBuildings.end() &&
        std::find(defenseBuildings.begin(),
                  defenseBuildings.end(), 598) !=
            defenseBuildings.end();

    constexpr int screenW = 960, screenH = 544;
    InputState input;
    input.toggleCheatMenu = true;
    game.update(0.001f, input);
    for (int cheat = 0; cheat < 4; cheat++) {
        if (cheat > 0) {
            input = {};
            input.menuDown = true;
            game.update(0.001f, input);
        }
        input = {};
        input.menuActivate = true;
        game.update(0.001f, input);
    }
    input = {};
    input.toggleCheatMenu = true;
    game.update(0.001f, input);

    constexpr uint32_t commandCenterId = 1;
    constexpr int basicTrainingId = 33;
    const std::vector<int> commandCenterResearch =
        game.researchOptionIds(commandCenterId);
    const auto basicTraining =
        std::find(commandCenterResearch.begin(),
                  commandCenterResearch.end(),
                  basicTrainingId);
    bool researchQueued = false;
    bool researchCancelled = false;
    bool researchCompleted = false;
    bool researchCostDeducted = false;
    bool lockedResearchHidden = false;
    bool researchChainAdvanced = false;
    float centerX = 0, centerY = 0;
    if (basicTraining != commandCenterResearch.end() &&
        game.objectScreenPosition(commandCenterId, screenW, screenH,
                                  centerX, centerY)) {
        input = {};
        input.pointerX = centerX;
        input.pointerY = centerY;
        input.selectPressed = true;
        game.update(0.001f, input);
        input = {};
        input.cycleAttackMode = true;
        game.update(0.001f, input);
        input = {};
        input.actionTabRight = true;
        game.update(0.001f, input);

        const float novaBeforeResearch = game.resource(1, 3);
        lockedResearchHidden =
            std::none_of(
                commandCenterResearch.begin(),
                commandCenterResearch.end(),
                [&](int technologyId) {
                    // Tech-level buttons (1-3) stay visible while their
                    // building prerequisites are unmet, like the original
                    // (pressing them reports string 3062).
                    return technologyId > 3 &&
                           !game.technologyRequirementsMetForTesting(
                               1, technologyId);
                });
        input = {};
        const size_t basicTrainingIndex =
            (size_t)std::distance(
                commandCenterResearch.begin(),
                basicTraining);
        input.pointerX =
            188.0f +
            (float)(basicTrainingIndex % 5) *
                52.0f +
            26.0f;
        input.pointerY =
            164.0f +
            (float)(basicTrainingIndex / 5) *
                52.0f +
            26.0f;
        input.selectPressed = true;
        game.update(0.001f, input);
        const std::vector<int> queuedResearch =
            game.researchOptionIds(commandCenterId);
        researchQueued =
            std::find(queuedResearch.begin(),
                      queuedResearch.end(),
                      basicTrainingId) ==
            queuedResearch.end();
        researchCostDeducted =
            std::abs(game.resource(1, 3) -
                     (novaBeforeResearch - 50.0f)) < 0.01f;
        input = {};
        input.pointerX = 214.0f;
        input.pointerY = 390.0f;
        input.selectPressed = true;
        game.update(0.001f, input);
        const std::vector<int> cancelledResearch =
            game.researchOptionIds(commandCenterId);
        researchCancelled =
            std::find(
                cancelledResearch.begin(),
                cancelledResearch.end(),
                basicTrainingId) !=
                cancelledResearch.end() &&
            std::abs(game.resource(1, 3) -
                     novaBeforeResearch) < 0.01f;
        input = {};
        input.pointerX =
            188.0f +
            (float)(basicTrainingIndex % 5) *
                52.0f +
            26.0f;
        input.pointerY =
            164.0f +
            (float)(basicTrainingIndex / 5) *
                52.0f +
            26.0f;
        input.selectPressed = true;
        game.update(0.001f, input);
        input = {};
        game.update(
            assets.dat().techs[(size_t)basicTrainingId]
                    .researchTime *
                    4.0f +
                0.01f,
            input);
        researchCompleted =
            game.technologyResearched(1, basicTrainingId);
        const std::vector<int> advancedResearch =
            game.researchOptionIds(commandCenterId);
        bool hasBasicTrainingSuccessor = false;
        researchChainAdvanced = true;
        for (size_t technologyId = 0;
             technologyId < assets.dat().techs.size();
             technologyId++) {
            const dat::Tech &technology =
                assets.dat().techs[technologyId];
            if (technology.buttonId !=
                    assets.dat().techs[
                        basicTrainingId]
                        .buttonId ||
                technology.locationId !=
                    assets.dat().techs[
                        basicTrainingId]
                        .locationId ||
                std::find(
                    std::begin(
                        technology.requiredTechs),
                    std::end(
                        technology.requiredTechs),
                    basicTrainingId) ==
                    std::end(
                        technology.requiredTechs))
                continue;
            hasBasicTrainingSuccessor = true;
            researchChainAdvanced =
                std::find(
                    advancedResearch.begin(),
                    advancedResearch.end(),
                    (int)technologyId) !=
                advancedResearch.end();
            break;
        }
        if (!hasBasicTrainingSuccessor)
            researchChainAdvanced = true;
    }
    input = {};
    input.menuBack = true;
    game.update(0.001f, input);

    bool foundationPlaced = false;
    bool constructionCompleted = false;
    bool constructionAssignmentCleared = false;
    bool constructionReassigned = false;
    bool multipleBuildersAssigned = false;
    bool multiWorkerMenuOpened = false;
    bool builderStateEntered = false;
    bool garrisonModeActivated = false;
    bool workerGarrisoned = false;
    bool individualGarrisonEjected = false;
    bool workerEjected = false;
    const auto dwelling =
        std::find(workerBuildings.begin(),
                  workerBuildings.end(), 70);
    float workerX = 0, workerY = 0;
    uint32_t selectedWorkerId = 0;
    for (uint32_t workerId = 6;
         workerId <= 10 && !selectedWorkerId;
         workerId++) {
        if (!game.objectScreenPosition(
                workerId, screenW, screenH,
                workerX, workerY))
            continue;
        input = {};
        input.boxSelectCommit = true;
        input.boxStartX = workerX - 2.0f;
        input.boxStartY = workerY - 2.0f;
        input.boxEndX = workerX + 2.0f;
        input.boxEndY = workerY + 2.0f;
        game.update(0.001f, input);
        if (game.objectSelected(workerId))
            selectedWorkerId = workerId;
    }
    if (selectedWorkerId &&
        game.objectScreenPosition(
            commandCenterId, screenW, screenH,
            centerX, centerY)) {
        input = {};
        input.pointerX = screenW - 280.0f;
        input.pointerY =
            screenH - 82.0f;
        input.selectPressed = true;
        game.update(0.001f, input);
        garrisonModeActivated =
            game.garrisonCursorActive();
        input = {};
        input.pointerX = centerX;
        input.pointerY = centerY;
        input.selectPressed = true;
        game.update(0.001f, input);
        for (int frame = 0;
             frame < 900 &&
             game.garrisonedCount(commandCenterId) == 0;
             ++frame)
            game.update(1.0f / 30.0f, {});
        workerGarrisoned =
            game.garrisonedCount(commandCenterId) == 1;
        const uint32_t secondGarrisonWorkerId =
            selectedWorkerId == 6 ? 7 : 6;
        if (workerGarrisoned &&
            game.selectObjectForTesting(
                secondGarrisonWorkerId)) {
            input = {};
            input.pointerX = screenW - 280.0f;
            input.pointerY =
                screenH - 82.0f;
            input.selectPressed = true;
            game.update(0.001f, input);
            input = {};
            input.pointerX = centerX;
            input.pointerY = centerY;
            input.selectPressed = true;
            game.update(0.001f, input);
            for (int frame = 0;
                 frame < 900 &&
                 game.garrisonedCount(
                     commandCenterId) < 2;
                 ++frame)
                game.update(
                    1.0f / 30.0f, {});
            game.selectObjectForTesting(
                commandCenterId);
            input = {};
            input.pointerX = 477.0f;
            input.pointerY =
                screenH -
                112.0f +
                29.0f;
            input.selectPressed = true;
            game.update(0.001f, input);
            individualGarrisonEjected =
                game.garrisonedCount(
                    commandCenterId) == 1;
        }
        if (individualGarrisonEjected) {
            game.ejectGarrisonedUnitForTesting(
                commandCenterId,
                selectedWorkerId);
            game.ejectGarrisonedUnitForTesting(
                commandCenterId,
                secondGarrisonWorkerId);
        }
        workerEjected =
            game.garrisonedCount(commandCenterId) == 0;
        if (workerEjected &&
            game.objectScreenPosition(
                selectedWorkerId, screenW, screenH,
                workerX, workerY)) {
            const uint32_t menuWorkerId =
                game.spawnObjectForTesting(
                    3, 83, 1,
                    game.objectPosition(
                        selectedWorkerId)[0] +
                        1.0f,
                    game.objectPosition(
                        selectedWorkerId)[1]);
            game.selectObjectsForTesting(
                {selectedWorkerId, menuWorkerId});
            input = {};
            input.cycleAttackMode = true;
            game.update(0.001f, input);
            multiWorkerMenuOpened =
                game.actionMenuOpenForTesting();
            input = {};
            input.menuBack = true;
            game.update(0.001f, input);
            input = {};
            input.boxSelectCommit = true;
            input.boxStartX = workerX - 2.0f;
            input.boxStartY = workerY - 2.0f;
            input.boxEndX = workerX + 2.0f;
            input.boxEndY = workerY + 2.0f;
            game.update(0.001f, input);
        }
    }
    if (dwelling != workerBuildings.end() &&
        selectedWorkerId) {
        input = {};
        input.cycleAttackMode = true;
        game.update(0.001f, input);
        if (!saveStage("-building-pages")) {
            fprintf(
                stderr,
                "error: could not write building pages preview\n");
            return 1;
        }
        input = {};
        const size_t dwellingIndex =
            (size_t)std::distance(
                workerBuildings.begin(), dwelling);
        input.pointerX =
            188.0f +
            (float)(dwellingIndex % 5) *
                52.0f +
            26.0f;
        input.pointerY =
            164.0f +
            (float)(dwellingIndex / 5) *
                52.0f +
            26.0f;
        input.selectPressed = true;
        game.update(0.001f, input);

        const size_t objectsBeforeFoundation =
            game.activeObjectCount();
        float blockedX = 0;
        float blockedY = 0;
        if (game.objectScreenPosition(
                commandCenterId, screenW, screenH,
                blockedX, blockedY)) {
            input = {};
            input.cursorVisible = true;
            input.pointerX = blockedX;
            input.pointerY = blockedY;
            game.update(0.1f, input);
            if (!saveStage("-placement-blocked")) {
                fprintf(
                    stderr,
                    "error: could not write blocked placement preview\n");
                return 1;
            }
        }
        game.lookAt(mapSize * 0.30f + 13.0f,
                    mapSize * 0.35f + 10.0f);
        input = {};
        input.cursorVisible = true;
        input.pointerX = screenW * 0.5f;
        input.pointerY = screenH * 0.5f;
        game.update(0.1f, input);
        if (!saveStage("-placement")) {
            fprintf(stderr,
                    "error: could not write placement preview\n");
            return 1;
        }
        input = {};
        input.pointerX = screenW * 0.5f;
        input.pointerY = screenH * 0.5f;
        input.selectPressed = true;
        game.update(0.001f, input);
        foundationPlaced =
            game.activeObjectCount() ==
                objectsBeforeFoundation + 1 &&
            game.underConstructionObjectCount() == 1;
        const std::vector<uint32_t> foundations =
            game.underConstructionObjectIds();
        const uint32_t foundationId =
            foundations.empty() ? 0 : foundations.front();
        if (foundationId != 0) {
            input = {};
            input.pointerX = screenW - 10.0f;
            input.pointerY = screenH - 10.0f;
            input.commandPressed = true;
            game.update(0.001f, input);
            constructionAssignmentCleared =
                game.constructionBuilderId(
                    foundationId) == 0;

            float foundationX = 0;
            float foundationY = 0;
            if (game.objectScreenPosition(
                    foundationId, screenW, screenH,
                    foundationX, foundationY)) {
                const std::array<float, 2>
                    foundationPosition =
                        game.objectPosition(
                            foundationId);
                const uint32_t secondBuilderId =
                    game.spawnObjectForTesting(
                        3, 83, 1,
                        foundationPosition[0] + 3.0f,
                        foundationPosition[1] + 1.0f);
                game.selectObjectsForTesting(
                    {selectedWorkerId,
                     secondBuilderId});
                input = {};
                input.pointerX = foundationX;
                input.pointerY = foundationY;
                input.commandPressed = true;
                game.update(0.001f, input);
                constructionReassigned =
                    game.constructionBuilderId(
                        foundationId) ==
                    selectedWorkerId;
                multipleBuildersAssigned =
                    game.objectBuildingTarget(
                        selectedWorkerId,
                        foundationId) &&
                    game.objectBuildingTarget(
                        secondBuilderId,
                        foundationId);
                for (int frame = 0;
                     frame < 1500 &&
                     !builderStateEntered;
                     frame++) {
                    game.update(1.0f / 30.0f, {});
                    builderStateEntered =
                        game.objectIsBuilder(
                            selectedWorkerId);
                }
                if (builderStateEntered &&
                    !saveStage("-construction")) {
                    fprintf(
                        stderr,
                        "error: could not write construction preview\n");
                    return 1;
                }
                if (builderStateEntered) {
                    game.setConstructionProgressForTesting(
                        foundationId, 0.5f);
                    if (!saveStage(
                            "-construction-middle")) {
                        fprintf(
                            stderr,
                            "error: could not write middle "
                            "construction preview\n");
                        return 1;
                    }
                    game.setConstructionProgressForTesting(
                        foundationId, 0.85f);
                    if (!saveStage(
                            "-construction-late")) {
                        fprintf(
                            stderr,
                            "error: could not write late "
                            "construction preview\n");
                        return 1;
                    }
                }
            }
        }

        input = {};
        input.toggleCheatMenu = true;
        game.update(0.001f, input);
        input = {};
        input.menuDown = true;
        game.update(0.001f, input);
        input = {};
        input.menuActivate = true;
        game.update(0.001f, input);
        input = {};
        input.toggleCheatMenu = true;
        game.update(0.001f, input);
        game.update(0.001f, {});
        constructionCompleted =
            foundationPlaced &&
            game.underConstructionObjectCount() == 0;
    }

    game.lookAt(mapSize * 0.30f + 2.0f,
                mapSize * 0.35f + 2.0f);
    input = {};
    input.boxSelectCommit = true;
    input.boxStartX = 0;
    input.boxStartY = 0;
    input.boxEndX = screenW - 1.0f;
    input.boxEndY = screenH - 1.0f;
    game.update(0.001f, input);
    if (game.selectedObjectCount() == 0) {
        fprintf(stderr, "error: combat sandbox selected no attackers\n");
        return 1;
    }
    // spawnBase creates five buildings, five workers, then four troopers.
    // The second base therefore starts at spawn 18 and its first trooper is 28.
    constexpr uint32_t targetId = 28;
    constexpr uint32_t sourceTrooperId = 11;
    constexpr uint32_t technologyTargetId = 19;
    const int baseBuildingDamage =
        game.objectAttackDamage(sourceTrooperId,
                                technologyTargetId);
    const bool researchedFocusCoils =
        game.researchTechnology(1, 66);
    const int upgradedBuildingDamage =
        game.objectAttackDamage(sourceTrooperId,
                                technologyTargetId);
    game.lookAt(mapSize * 0.62f + 8.0f, mapSize * 0.60f + 1.0f);
    float screenX = 0, screenY = 0;
    if (!game.objectScreenPosition(targetId, screenW, screenH, screenX, screenY)) {
        fprintf(stderr, "error: combat sandbox target %u is unavailable\n", targetId);
        return 1;
    }
    const float edgeBeforeX = screenX;
    input = {};
    input.pointerX = screenW;
    input.pointerY = screenH * 0.5f;
    input.cursorVisible = true;
    game.update(0.1f, input);
    float edgeAfterX = 0, edgeAfterY = 0;
    game.objectScreenPosition(targetId, screenW, screenH, edgeAfterX, edgeAfterY);
    const bool edgeScrolled = edgeAfterX < edgeBeforeX - 1.0f;
    game.lookAt(mapSize * 0.62f + 8.0f, mapSize * 0.60f + 1.0f);
    game.objectScreenPosition(targetId, screenW, screenH, screenX, screenY);
    const float initialHitPoints = game.objectHitPoints(targetId);
    input = {};
    input.pointerX = screenX;
    input.pointerY = screenY;
    input.commandPressed = true;
    input.cursorVisible = true;
    const size_t soundsBeforeAttack = acknowledgementSounds.size();
    game.update(0.001f, input);
    const bool attackPlayedOnce =
        acknowledgementSounds.size() == soundsBeforeAttack + 1;

    bool offscreenWorldMuted = false;
    {
        Game audioGame(assets);
        std::vector<int> offscreenSounds;
        audioGame.setUnitSoundPlayer(
            [&](int soundId, int) {
                offscreenSounds.push_back(soundId);
            });
        if (!audioGame.init(7, mapSize, &err)) {
            fprintf(stderr, "error: %s\n", err.c_str());
            return 1;
        }
        InputState audioInput;
        audioInput.boxSelectCommit = true;
        audioInput.boxStartX = 0;
        audioInput.boxStartY = 0;
        audioInput.boxEndX = screenW - 1.0f;
        audioInput.boxEndY = screenH - 1.0f;
        audioGame.update(0.001f, audioInput);
        audioGame.lookAt(
            mapSize * 0.62f + 8.0f,
            mapSize * 0.60f + 1.0f);
        float audioTargetX = 0, audioTargetY = 0;
        audioGame.objectScreenPosition(
            targetId, screenW, screenH,
            audioTargetX, audioTargetY);
        audioInput = {};
        audioInput.pointerX = audioTargetX;
        audioInput.pointerY = audioTargetY;
        audioInput.commandPressed = true;
        audioGame.update(0.001f, audioInput);
        audioGame.lookAt(4.0f, 4.0f);
        const size_t soundsBeforeOffscreenCombat =
            offscreenSounds.size();
        const size_t projectilesBefore =
            audioGame.combatStats().projectilesLaunched;
        for (float audioElapsed = 0;
             audioElapsed < 60.0f &&
             audioGame.combatStats().projectilesLaunched ==
                 projectilesBefore;
             audioElapsed += 1.0f / 30.0f)
            audioGame.update(1.0f / 30.0f, {});
        offscreenWorldMuted =
            audioGame.combatStats().projectilesLaunched >
                projectilesBefore &&
            offscreenSounds.size() ==
                soundsBeforeOffscreenCombat;
    }

    input = {};
    constexpr float step = 1.0f / 30.0f;
    float elapsed = 0;
    bool sawProjectile = false;
    bool savedProjectile = false;
    int projectileFrames = 0;
    for (; elapsed < 120.0f; elapsed += step) {
        game.update(step, input);
        if (game.combatStats().activeProjectiles > 0) {
            sawProjectile = true;
            projectileFrames++;
        }
        if (out && !savedProjectile && projectileFrames >= 4 &&
            game.combatStats().activeProjectiles > 0) {
            game.render(renderer, screenW, screenH);
            std::string projectileOut(out);
            const size_t extension = projectileOut.find_last_of('.');
            projectileOut.insert(extension == std::string::npos ? projectileOut.size() : extension,
                                 "-projectile");
            if (!renderer.savePng(projectileOut)) {
                fprintf(stderr, "error: could not write %s\n", projectileOut.c_str());
                return 1;
            }
            savedProjectile = true;
        }
        if (!game.objectActive(targetId)) break;
    }
    const bool sawRemains = game.combatStats().activeRemains > 0;
    if (out && sawRemains) {
        game.render(renderer, screenW, screenH);
        std::string deathOut(out);
        const size_t extension = deathOut.find_last_of('.');
        deathOut.insert(extension == std::string::npos ? deathOut.size() : extension,
                        "-death");
        if (!renderer.savePng(deathOut)) {
            fprintf(stderr, "error: could not write %s\n", deathOut.c_str());
            return 1;
        }
    }
    if (!sawProjectile) {
        fprintf(stderr, "error: combat sandbox rendered no projectile\n");
        return 1;
    }

    // Attack the enemy command center until its first damage graphic is active,
    // then select it to validate inspection and its hostile health bar.
    constexpr uint32_t buildingId = 19;
    const float buildingMaxHitPoints = game.objectMaxHitPoints(buildingId);
    game.lookAt(mapSize * 0.62f + 2.0f, mapSize * 0.60f + 2.0f);
    if (!game.objectScreenPosition(buildingId, screenW, screenH, screenX, screenY)) {
        fprintf(stderr, "error: combat sandbox building %u is unavailable\n", buildingId);
        return 1;
    }
    input = {};
    input.pointerX = screenX;
    input.pointerY = screenY - 36.0f;
    input.commandPressed = true;
    game.update(0.001f, input);
    float buildingElapsed = 0;
    for (;
         buildingElapsed < 120.0f &&
         game.objectHitPoints(buildingId) > buildingMaxHitPoints * 0.74f;
         buildingElapsed += step)
        game.update(step, {});
    const float damagedBuildingHitPoints = game.objectHitPoints(buildingId);
    input = {};
    input.pointerX = screenX;
    input.pointerY = screenY - 36.0f;
    input.selectPressed = true;
    game.update(0.001f, input);
    const bool buildingSelected = game.objectSelected(buildingId);
    if (out) {
        game.render(renderer, screenW, screenH);
        if (!renderer.savePng(out)) {
            fprintf(stderr, "error: could not write %s\n", out);
            return 1;
        }
    }

    constexpr uint32_t destroyedBuildingId = buildingId;
    input = {};
    input.boxSelectCommit = true;
    input.boxStartX = 0;
    input.boxStartY = 0;
    input.boxEndX = screenW - 1.0f;
    input.boxEndY = screenH - 1.0f;
    game.update(0.001f, input);
    game.lookAt(mapSize * 0.62f - 3.0f, mapSize * 0.60f + 1.0f);
    if (!game.objectScreenPosition(
            destroyedBuildingId, screenW, screenH, screenX, screenY)) {
        fprintf(stderr, "error: destruction target %u is unavailable\n",
                destroyedBuildingId);
        return 1;
    }
    input = {};
    input.pointerX = screenX;
    input.pointerY = screenY - 24.0f;
    input.commandPressed = true;
    game.update(0.001f, input);
    float destructionElapsed = 0;
    for (;
         destructionElapsed < 240.0f &&
         game.objectActive(destroyedBuildingId);
         destructionElapsed += step)
        game.update(step, {});
    const bool buildingDestroyed = !game.objectActive(destroyedBuildingId);
    const bool sawBuildingRemains = game.combatStats().activeRemains > 0;
    if (out && sawBuildingRemains) {
        game.render(renderer, screenW, screenH);
        std::string destructionOut(out);
        const size_t extension = destructionOut.find_last_of('.');
        destructionOut.insert(
            extension == std::string::npos ? destructionOut.size() : extension,
            "-destruction");
        if (!renderer.savePng(destructionOut)) {
            fprintf(stderr, "error: could not write %s\n", destructionOut.c_str());
            return 1;
        }
    }
    // Remains last about a minute; later deaths in the ongoing skirmish add
    // their own, so wait for all of them to clear.
    for (float decayElapsed = 0;
         decayElapsed < 140.0f && (decayElapsed < 65.0f || game.combatStats().activeRemains);
         decayElapsed += step)
        game.update(step, {});
    const bool remainsDecayed = game.combatStats().activeRemains == 0;
    const int commandCenterBeforeAge =
        game.objectUnitId(commandCenterId);
    const uint32_t troopCenterId =
        game.spawnObjectForTesting(
            3, 87, 1,
            mapSize * 0.30f + 8.0f,
            mapSize * 0.35f + 8.0f);
    const bool buildingUpgraded =
        game.researchTechnology(1, 1) &&
        commandCenterBeforeAge == 109 &&
        game.objectUnitId(commandCenterId) == 71;
    const bool upgradedProductionMenusWork =
        !game.productionOptionIds(
                 commandCenterId).empty() &&
        !game.productionOptionIds(
                 troopCenterId).empty();

    Game systems(assets);
    std::vector<int> systemsSounds;
    systems.setUnitSoundPlayer(
        [&](int soundId, int) {
            systemsSounds.push_back(soundId);
        });
    systems.setInterfaceSoundPlayer([&](int resourceId) {
        systemsSounds.push_back(resourceId);
    });
    if (!systems.init(0x51E1D, mapSize, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    const uint32_t powerCoreId =
        systems.spawnObjectForTesting(
            3, 12, 1, 6.0f, 48.0f);
    const uint32_t shieldGeneratorId =
        systems.spawnObjectForTesting(
            3, 335, 1, 11.0f, 48.0f);
    const bool shieldFixturesCreated =
        powerCoreId != 0 &&
        shieldGeneratorId != 0;
    const uint32_t shieldBuildingId =
        systems.spawnObjectForTesting(
            3, 70, 1, 13.0f, 48.0f);
    const uint32_t shieldWorkerId =
        systems.spawnObjectForTesting(
            3, 83, 1, 12.0f, 46.0f);
    systems.update(20.0f, {});
    const float chargedBuildingShield =
        systems.objectShieldPoints(
            shieldBuildingId);
    const float chargedWorkerShield =
        systems.objectShieldPoints(
            shieldWorkerId);
    const float buildingHealthBeforeShieldHit =
        systems.objectHitPoints(
            shieldBuildingId);
    const float workerHealthBeforeShieldHit =
        systems.objectHitPoints(
            shieldWorkerId);
    systems.damageObjectForTesting(
        shieldBuildingId, 10);
    systems.damageObjectForTesting(
        shieldWorkerId, 10);
    const bool buildingShieldAbsorbed =
        chargedBuildingShield >= 39.9f &&
        std::abs(
            systems.objectHitPoints(
                shieldBuildingId) -
            buildingHealthBeforeShieldHit) <
            0.01f &&
        systems.objectShieldPoints(
            shieldBuildingId) <=
            chargedBuildingShield - 9.9f;
    const bool mobileShieldAbsorbed =
        chargedWorkerShield >= 39.9f &&
        std::abs(
            systems.objectHitPoints(
                shieldWorkerId) -
            workerHealthBeforeShieldHit) <
            0.01f;
    const float workerShieldBeforeOverflow =
        systems.objectShieldPoints(
            shieldWorkerId);
    const float workerHealthBeforeOverflow =
        systems.objectHitPoints(
            shieldWorkerId);
    systems.damageObjectForTesting(
        shieldWorkerId, 50);
    const float expectedOverflow =
        50.0f -
        workerShieldBeforeOverflow;
    const bool shieldOverflowDamagedHealth =
        systems.objectShieldPoints(
            shieldWorkerId) == 0 &&
        std::abs(
            systems.objectHitPoints(
                shieldWorkerId) -
            (workerHealthBeforeOverflow -
             expectedOverflow)) <
            0.01f;
    systems.moveObjectForTesting(
        shieldBuildingId, 40.0f, 48.0f);
    const float shieldBeforeLeaving =
        systems.objectShieldPoints(
            shieldBuildingId);
    systems.update(0.5f, {});
    const bool shieldRetainedOutsideRadius =
        std::abs(
            systems.objectShieldPoints(
                shieldBuildingId) -
            shieldBeforeLeaving) < 0.01f &&
        systems.objectMaxShieldPoints(
            shieldBuildingId) > 0;
    systems.update(0.5f, {});
    const bool shieldDrainedOnTick =
        std::abs(
            systems.objectShieldPoints(
                shieldBuildingId) -
            std::max(
                0.0f,
                shieldBeforeLeaving -
                    40.0f)) < 0.01f;
    systems.moveObjectForTesting(
        shieldBuildingId, 13.0f, 48.0f);
    systems.update(400.0f, {});
    const float poweredShield =
        systems.objectShieldPoints(
            shieldBuildingId);
    const bool shieldChargedFully =
        poweredShield >=
        systems.objectMaxShieldPoints(
            shieldBuildingId) - 0.001f;
    systems.moveObjectForTesting(
        powerCoreId, 40.0f, 40.0f);
    systems.update(0.5f, {});
    const bool noPartialDrain =
        std::abs(
            systems.objectShieldPoints(
                shieldBuildingId) -
            poweredShield) < 0.01f;
    systems.update(0.5f, {});
    const bool unpoweredShieldDrained =
        systems.objectShieldPoints(
            shieldBuildingId) <=
            poweredShield - 39.9f;

    const float testBaseX = mapSize * 0.30f;
    const float testBaseY = mapSize * 0.35f;
    constexpr uint32_t gatherWorkerId = 6;
    systems.moveObjectForTesting(
        gatherWorkerId,
        testBaseX + 4.0f,
        testBaseY + 2.0f);
    const uint32_t foodId =
        systems.spawnObjectForTesting(
            0, 59, 0,
            testBaseX + 5.5f,
            testBaseY + 2.0f);
    const float foodBeforeGathering =
        systems.resource(1, 0);
    const uint32_t secondGatherWorkerId =
        systems.spawnObjectForTesting(
            3, 83, 1,
            testBaseX + 3.5f,
            testBaseY + 2.8f);
    systems.lookAt(
        testBaseX + 4.75f,
        testBaseY + 2.0f);
    const bool gatherWorkerSelected =
        systems.selectObjectsForTesting(
            {gatherWorkerId,
             secondGatherWorkerId});
    float foodScreenX = 0.0f;
    float foodScreenY = 0.0f;
    const bool foodOnScreen =
        systems.objectScreenPosition(
            foodId, screenW, screenH,
            foodScreenX, foodScreenY);
    InputState gatherInput{};
    gatherInput.commandPressed = true;
    gatherInput.pointerX = foodScreenX;
    gatherInput.pointerY = foodScreenY;
    if (foodOnScreen)
        systems.update(0.001f, gatherInput);
    const bool gatherInputAccepted =
        systems.objectGatheringTarget(
            gatherWorkerId, foodId);
    const bool multipleGatherersAssigned =
        gatherInputAccepted &&
        systems.objectGatheringTarget(
            secondGatherWorkerId, foodId);
    for (int frame = 0; frame < 2400 &&
         systems.resource(1, 0) <=
             foodBeforeGathering; frame++)
        systems.update(1.0f / 30.0f, {});
    const bool workerGatheredAndDeposited =
        gatherWorkerSelected &&
        foodOnScreen &&
        multipleGatherersAssigned &&
        systems.resource(1, 0) >
            foodBeforeGathering;
    const uint32_t manualWorkerId =
        systems.spawnObjectForTesting(
            3, 83, 1,
            testBaseX + 1.0f,
            testBaseY + 1.0f);
    const uint32_t manualFoodId =
        systems.spawnObjectForTesting(
            0, 59, 0,
            testBaseX + 1.8f,
            testBaseY + 1.0f);
    bool manualDropOffWorked = false;
    if (manualWorkerId && manualFoodId &&
        systems.issueGatherForTesting(
            manualWorkerId, manualFoodId)) {
        for (int frame = 0;
             frame < 600 &&
             systems.objectCarriedAmount(
                 manualWorkerId) < 2.0f;
             frame++)
            systems.update(1.0f / 30.0f, {});
        const float foodBeforeDrop =
            systems.resource(1, 0);
        const bool orderAccepted =
            systems.issueDropOffForTesting(
                manualWorkerId, commandCenterId);
        for (int frame = 0;
             frame < 900 &&
             systems.objectCarriedAmount(
                 manualWorkerId) > 0.001f;
             frame++)
            systems.update(1.0f / 30.0f, {});
        manualDropOffWorked =
            orderAccepted &&
            systems.objectCarriedAmount(
                manualWorkerId) <= 0.001f &&
            systems.resource(1, 0) >
                foodBeforeDrop;
    }

    const uint32_t mobilePowerId =
        systems.spawnObjectForTesting(
            3, 1009, 1, 34.0f, 20.0f);
    const uint32_t mobilePoweredBuildingId =
        systems.spawnObjectForTesting(
            3, 335, 1, 38.0f, 20.0f);
    const bool mobilePowerWorked =
        mobilePowerId &&
        mobilePoweredBuildingId &&
        systems.objectPoweredForTesting(
            mobilePoweredBuildingId);
    if (mobilePowerId)
        systems.moveObjectForTesting(
            mobilePowerId, 15.0f, 15.0f);
    const bool mobilePowerRemoved =
        mobilePoweredBuildingId &&
        !systems.objectPoweredForTesting(
            mobilePoweredBuildingId);

    const uint32_t destroyFirstId =
        systems.spawnObjectForTesting(
            3, 83, 1, 46.0f, 20.0f);
    const uint32_t destroyLastId =
        systems.spawnObjectForTesting(
            3, 83, 1, 47.0f, 20.0f);
    const bool destroyReverseOrder =
        destroyFirstId && destroyLastId &&
        systems.selectObjectsForTesting(
            {destroyFirstId, destroyLastId}) &&
        systems.destroyLastSelectedForTesting() &&
        systems.objectActive(destroyFirstId) &&
        !systems.objectActive(destroyLastId) &&
        systems.destroyLastSelectedForTesting() &&
        !systems.objectActive(destroyFirstId);
    const uint32_t stanceUnitId =
        systems.spawnObjectForTesting(
            3, 460, 1, 48.0f, 20.0f);
    bool stanceMenuWorked = false;
    if (systems.selectObjectForTesting(
            stanceUnitId)) {
        InputState stanceInput;
        stanceInput.screenW = screenW;
        stanceInput.screenH = screenH;
        stanceInput.pointerX = 851.0f;
        stanceInput.pointerY = 473.0f;
        stanceInput.selectPressed = true;
        systems.update(0.001f, stanceInput);
        const bool opened =
            systems.actionMenuOpenForTesting();
        if (opened)
            systems.render(
                renderer, screenW, screenH);
        stanceInput = {};
        stanceInput.screenW = screenW;
        stanceInput.screenH = screenH;
        stanceInput.menuDown = true;
        systems.update(0.001f, stanceInput);
        stanceInput = {};
        stanceInput.screenW = screenW;
        stanceInput.screenH = screenH;
        stanceInput.menuActivate = true;
        systems.update(0.001f, stanceInput);
        stanceMenuWorked =
            opened &&
            !systems.actionMenuOpenForTesting();
    }

    const uint32_t automaticWorkerId =
        systems.spawnObjectForTesting(
            3, 83, 1, 49.0f, 54.0f);
    const uint32_t automaticFoodId =
        systems.spawnObjectForTesting(
            0, 59, 0, 50.5f, 54.0f);
    const uint32_t automaticDropSiteId =
        systems.spawnFoundationForTesting(
            3, 109, 1, 45.0f, 54.0f,
            {automaticWorkerId});
    const bool automaticGatheringAssigned =
        automaticFoodId != 0 &&
        automaticDropSiteId != 0 &&
        systems.completeFoundationForTesting(
            automaticDropSiteId) &&
        (systems.update(0.001f, {}),
         systems.objectGatheringTarget(
             automaticWorkerId,
             0));

    const uint32_t chainingWorkerId =
        systems.spawnObjectForTesting(
            3, 83, 1, 53.0f, 58.0f);
    const uint32_t completedFoundationId =
        systems.spawnFoundationForTesting(
            3, 70, 1, 54.0f, 58.0f,
            {chainingWorkerId});
    const uint32_t nextFoundationId =
        systems.spawnFoundationForTesting(
            3, 70, 1, 58.0f, 58.0f, {});
    const bool automaticConstructionChained =
        completedFoundationId != 0 &&
        nextFoundationId != 0 &&
        systems.completeFoundationForTesting(
            completedFoundationId) &&
        (systems.update(0.001f, {}),
         systems.objectBuildingTarget(
             chainingWorkerId,
             nextFoundationId));
    if (!workerGatheredAndDeposited) {
        const std::array<float, 2> workerPosition =
            systems.objectPosition(gatherWorkerId);
        fprintf(
            stderr,
            "gather input debug: selected %d screen %d "
            "accepted %d resource %.1f -> %.1f at %.1f,%.1f "
            "carried %.1f remaining %.1f moving %zu target %d "
            "position %.2f,%.2f\n",
            gatherWorkerSelected ? 1 : 0,
            foodOnScreen ? 1 : 0,
            gatherInputAccepted ? 1 : 0,
            foodBeforeGathering,
            systems.resource(1, 0),
            foodScreenX, foodScreenY,
            systems.objectCarriedAmount(
                gatherWorkerId),
            systems.objectResourceAmount(foodId),
            systems.selectedMovingObjectCount(),
            systems.objectGatheringTarget(
                gatherWorkerId, foodId) ? 1 : 0,
            workerPosition[0], workerPosition[1]);
    }
    const std::vector<int> farmEconomyBuildings =
        systems.buildingOptionIds(
            gatherWorkerId, 2);
    const bool farmAvailable =
        std::find(
            farmEconomyBuildings.begin(),
            farmEconomyBuildings.end(),
            50) != farmEconomyBuildings.end();

    const uint32_t repairWorkerId =
        systems.spawnObjectForTesting(
            3, 83, 1, 29.0f, 48.0f);
    const uint32_t repairBuildingId =
        systems.spawnObjectForTesting(
            3, 70, 1, 31.0f, 48.0f);
    systems.setResourceForTesting(
        1, 1, 1000.0f);
    systems.damageObjectForTesting(
        repairBuildingId, 100);
    const float repairHealthBefore =
        systems.objectHitPoints(
            repairBuildingId);
    const float repairCarbonBefore =
        systems.resource(1, 1);
    const bool repairOrderIssued =
        systems.issueRepairForTesting(
            repairWorkerId,
            repairBuildingId);
    for (int frame = 0; frame < 600;
         frame++)
        systems.update(1.0f / 30.0f, {});
    const float repairHealthAfter =
        systems.objectHitPoints(
            repairBuildingId);
    const float repairCarbonAfter =
        systems.resource(1, 1);
    const bool workerRepairedBuilding =
        repairOrderIssued &&
        repairHealthAfter >
            repairHealthBefore &&
        repairCarbonAfter <
            repairCarbonBefore;

    systems.setDiplomacyForTesting(
        1, 3, 0);
    const uint32_t alliedRepairWorkerId =
        systems.spawnObjectForTesting(
            3, 83, 1, 35.0f, 48.0f);
    const uint32_t alliedMechId =
        systems.spawnObjectForTesting(
            3, 244, 3, 37.0f, 48.0f);
    for (int resourceType = 0;
         resourceType < 4; resourceType++)
        systems.setResourceForTesting(
            1, resourceType, 1000.0f);
    systems.damageObjectForTesting(
        alliedMechId, 50);
    const float alliedMechHealthBefore =
        systems.objectHitPoints(alliedMechId);
    float alliedRepairResourcesBefore = 0;
    for (int resourceType = 0;
         resourceType < 4; resourceType++)
        alliedRepairResourcesBefore +=
            systems.resource(1, resourceType);
    const bool alliedRepairOrderIssued =
        systems.issueRepairForTesting(
            alliedRepairWorkerId,
            alliedMechId);
    for (int frame = 0; frame < 600;
         frame++)
        systems.update(1.0f / 30.0f, {});
    float alliedRepairResourcesAfter = 0;
    for (int resourceType = 0;
         resourceType < 4; resourceType++)
        alliedRepairResourcesAfter +=
            systems.resource(1, resourceType);
    const bool workerRepairedAlliedMech =
        alliedRepairOrderIssued &&
        systems.objectHitPoints(alliedMechId) >
            alliedMechHealthBefore &&
        alliedRepairResourcesAfter <
            alliedRepairResourcesBefore;

    const uint32_t nerfId =
        systems.spawnObjectForTesting(
            0, 594, 0,
            testBaseX + 7.0f,
            testBaseY + 7.0f);
    systems.moveObjectForTesting(
        11, testBaseX + 7.5f,
        testBaseY + 7.0f);
    systems.update(0.1f, {});
    const bool livestockCaptured =
        systems.objectPlayer(nerfId) == 1;

    const uint32_t gateTestId =
        systems.spawnObjectForTesting(
            3, 64, 1, 50.0f, 10.0f);
    systems.moveObjectForTesting(
        7, 49.0f, 10.0f);
    systems.lookAt(50.0f, 10.0f);
    systemsSounds.clear();
    const bool gateLocked =
        systems.setGateLockedForTesting(
            gateTestId, true);
    const bool gateSoundPlayed =
        std::find(
            systemsSounds.begin(),
            systemsSounds.end(),
            50362) != systemsSounds.end(); // gatel.wav
    const bool gatePositionPassable =
        systems.positionPassableForTesting(
            7, 50.0f, 10.0f);
    const bool lockedGateBlocks =
        gateLocked && !gatePositionPassable &&
        gateSoundPlayed;

    systems.selectObjectForTesting(2);
    InputState emptyMenuInput;
    emptyMenuInput.screenW = screenW;
    emptyMenuInput.screenH = screenH;
    emptyMenuInput.cycleAttackMode = true;
    systems.update(0.001f, emptyMenuInput);
    const bool emptyMenuHidden =
        !systems.actionMenuOpenForTesting();

    systems.moveObjectForTesting(
        11, 42.0f, 20.0f);
    systems.moveObjectForTesting(
        12, 44.0f, 20.0f);
    const bool formationSelectionReady =
        systems.selectObjectsForTesting(
            {11, 12});
    InputState formationInput;
    formationInput.screenW = screenW;
    formationInput.screenH = screenH;
    formationInput.pointerX = 20.0f;
    formationInput.pointerY =
        screenH - 92.0f;
    formationInput.selectPressed = true;
    systems.update(0.001f, formationInput);
    const bool formationMovedImmediately =
        formationSelectionReady &&
        systems.selectedMovingObjectCount() >
            0;

    Game targeting(assets);
    if (!targeting.init(7, 96, &err)) {
        fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    targeting.setLocalPlayerForTesting(1);
    targeting.setDiplomacyForTesting(1, 2, 3);
    targeting.setDiplomacyForTesting(2, 1, 3);
    const uint32_t groundTrooper =
        targeting.spawnObjectForTesting(
            1, 460, 1, 74.0f, 72.0f);
    const uint32_t antiAirMobile =
        targeting.spawnObjectForTesting(
            1, 651, 1, 74.0f, 74.0f);
    const uint32_t assaultMech =
        targeting.spawnObjectForTesting(
            1, 603, 1, 74.0f, 76.0f);
    const uint32_t fighter =
        targeting.spawnObjectForTesting(
            5, 158, 2, 79.0f, 74.0f);
    const uint32_t enemyTrooper =
        targeting.spawnObjectForTesting(
            3, 460, 2, 79.0f, 76.0f);
    const bool groundRejectsAir =
        !targeting.canAttackTargetForTesting(
            groundTrooper, fighter) &&
        !targeting.issueAttackForTesting(
            groundTrooper, fighter);
    const bool antiAirTargetsOnlyAir =
        targeting.canAttackTargetForTesting(
            antiAirMobile, fighter) &&
        !targeting.canAttackTargetForTesting(
            antiAirMobile, enemyTrooper);
    const bool walkerLockedBeforeResearch =
        !targeting.canAttackTargetForTesting(
            assaultMech, fighter);
    targeting.setAttackModeForTesting(
        groundTrooper, 3);
    targeting.setAttackModeForTesting(
        antiAirMobile, 3);
    targeting.setAttackModeForTesting(
        enemyTrooper, 3);
    targeting.setAttackModeForTesting(
        fighter, 3);
    const bool walkerResearchApplied =
        targeting.researchTechnologyForTesting(
            1, 164) &&
        targeting.canAttackTargetForTesting(
            assaultMech, fighter) &&
        targeting.issueAttackForTesting(
            assaultMech, fighter);
    targeting.lookAtObject(assaultMech);
    for (int frame = 0;
         frame < 300 &&
         targeting.projectileCountForTesting() ==
             0;
         frame++)
        targeting.update(1.0f / 30.0f, {});
    const bool walkerAirProjectile =
        targeting.firstProjectileUnitForTesting() ==
        992;
    for (int frame = 0; frame < 60; frame++)
        targeting.update(1.0f / 30.0f, {});
    const bool invalidAutomaticTargetIgnored =
        targeting.attackTargetForTesting(
            groundTrooper) != fighter;

    Game attackGround(assets);
    if (!attackGround.init(7, 96, &err)) {
        fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    attackGround.setLocalPlayerForTesting(1);
    attackGround.setDiplomacyForTesting(1, 2, 3);
    attackGround.setDiplomacyForTesting(2, 1, 3);
    const uint32_t heavyArtillery =
        attackGround.spawnObjectForTesting(
            1, 192, 1, 48.0f, 48.0f);
    const uint32_t blastTarget =
        attackGround.spawnObjectForTesting(
            3, 460, 2, 56.0f, 48.0f);
    const float blastHealthBefore =
        attackGround.objectHitPoints(blastTarget);
    const int eligibleAttackGroundUnits[] = {
        108, 192, 249, 292, 1273, 1275,
        1580, 1586, 1587,
    };
    const int ineligibleAttackGroundUnits[] = {
        460, 545, 1204, 1314,
    };
    bool attackGroundEligibility = true;
    float candidateY = 52.0f;
    for (int unitId : eligibleAttackGroundUnits) {
        const uint32_t id =
            attackGround.spawnObjectForTesting(
                1, unitId, 1, 40.0f,
                candidateY);
        attackGroundEligibility =
            attackGroundEligibility &&
            id != 0 &&
            attackGround.canAttackGroundForTesting(id);
        candidateY += 1.0f;
    }
    for (int unitId : ineligibleAttackGroundUnits) {
        const uint32_t id =
            attackGround.spawnObjectForTesting(
                1, unitId, 1, 44.0f,
                candidateY);
        attackGroundEligibility =
            attackGroundEligibility &&
            id != 0 &&
            !attackGround.canAttackGroundForTesting(id);
        candidateY += 1.0f;
    }
    attackGround.clearSelectionForTesting();
    attackGround.selectObjectForTesting(
        heavyArtillery);
    InputState attackGroundButton;
    attackGroundButton.screenW = screenW;
    attackGroundButton.screenH = screenH;
    attackGroundButton.pointerX =
        screenW - 300.0f + 230.0f;
    attackGroundButton.pointerY =
        screenH - 82.0f;
    attackGroundButton.selectPressed = true;
    attackGround.update(
        0.001f, attackGroundButton);
    attackGroundButton = {};
    attackGroundButton.screenW = screenW;
    attackGroundButton.screenH = screenH;
    attackGroundButton.pointerX =
        176.0f + 12.0f +
        4.0f * 52.0f + 26.0f;
    attackGroundButton.pointerY =
        164.0f + 26.0f;
    attackGroundButton.selectPressed = true;
    attackGround.update(
        0.001f, attackGroundButton);
    const bool attackGroundButtonWorked =
        attackGround
            .attackGroundCursorActiveForTesting();
    attackGround.clearSelectionForTesting();
    const bool attackGroundSelectionCancelled =
        !attackGround
             .attackGroundCursorActiveForTesting();
    const bool attackGroundOrderIssued =
        attackGround.issueAttackGroundForTesting(
            heavyArtillery, 56.0f, 48.0f);
    attackGround.update(1.0f / 30.0f, {});
    const bool heavyArtilleryVolley =
        attackGround.projectileCountForTesting() ==
            2 &&
        attackGround.projectileUnitForTesting(0) ==
            656 &&
        attackGround.projectileUnitForTesting(1) ==
            369;
    for (int frame = 0; frame < 330; frame++)
        attackGround.update(1.0f / 30.0f, {});
    const bool attackGroundDamagedPoint =
        attackGround.objectHitPoints(
            blastTarget) <
        blastHealthBefore;
    const bool attackGroundRepeated =
        attackGround.combatStats()
                .projectilesLaunched >=
            4;
    const uint32_t replacementTarget =
        attackGround.spawnObjectForTesting(
            3, 460, 2, 54.0f, 50.0f);
    const bool attackGroundReplaced =
        attackGround.issueAttackForTesting(
            heavyArtillery,
            replacementTarget) &&
        !attackGround.attackGroundActiveForTesting(
            heavyArtillery);

    Game delayedAttack(assets);
    if (!delayedAttack.init(7, 96, &err)) {
        fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    delayedAttack.setLocalPlayerForTesting(1);
    const uint32_t delayedMech =
        delayedAttack.spawnObjectForTesting(
            1, 603, 1, 48.0f, 48.0f);
    const bool delayedOrderIssued =
        delayedAttack.issueAttackGroundForTesting(
            delayedMech, 55.0f, 48.0f);
    delayedAttack.update(1.0f / 30.0f, {});
    const bool attackReleaseDelayed =
        delayedOrderIssued &&
        delayedAttack.attackShotPendingForTesting(
            delayedMech) &&
        delayedAttack.projectileCountForTesting() ==
            0;
    for (int frame = 0;
         frame < 300 &&
         delayedAttack.combatStats()
                 .projectilesLaunched == 0;
         frame++)
        delayedAttack.update(
            1.0f / 30.0f, {});
    const bool delayedProjectileReleased =
        delayedAttack.combatStats()
                .projectilesLaunched >
            0;

    Game buildingAttack(assets);
    if (!buildingAttack.init(7, 96, &err)) {
        fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    buildingAttack.setLocalPlayerForTesting(1);
    buildingAttack.setDiplomacyForTesting(1, 2, 3);
    buildingAttack.setDiplomacyForTesting(2, 1, 3);
    const uint32_t fortress =
        buildingAttack.spawnObjectForTesting(
            3, 84, 2, 80.0f, 80.0f);
    const float fortressHealthBefore =
        buildingAttack.objectHitPoints(fortress);
    std::vector<uint32_t> buildingAttackers;
    for (int index = 0; index < 10; index++) {
        const uint32_t attacker =
            buildingAttack.spawnObjectForTesting(
                1, 460, 1, 71.0f,
                75.5f + index);
        if (attacker &&
            buildingAttack.issueAttackForTesting(
                attacker, fortress))
            buildingAttackers.push_back(attacker);
    }
    for (int frame = 0; frame < 30 * 24;
         frame++)
        buildingAttack.update(
            1.0f / 30.0f, {});
    size_t attackersStillEngaged = 0;
    float minimumSeparation =
        std::numeric_limits<float>::max();
    float minimumY =
        std::numeric_limits<float>::max();
    float maximumY =
        -std::numeric_limits<float>::max();
    for (size_t i = 0;
         i < buildingAttackers.size(); i++) {
        if (!buildingAttack.objectActive(
                buildingAttackers[i]))
            continue;
        const auto a =
            buildingAttack.objectPosition(
                buildingAttackers[i]);
        minimumY = std::min(minimumY, a[1]);
        maximumY = std::max(maximumY, a[1]);
        if (buildingAttack.attackTargetForTesting(
                buildingAttackers[i]) ==
            fortress)
            attackersStillEngaged++;
        for (size_t j = i + 1;
             j < buildingAttackers.size(); j++) {
            if (!buildingAttack.objectActive(
                    buildingAttackers[j]))
                continue;
            const auto b =
                buildingAttack.objectPosition(
                    buildingAttackers[j]);
            const float dx = a[0] - b[0];
            const float dy = a[1] - b[1];
            minimumSeparation =
                std::min(
                    minimumSeparation,
                    std::sqrt(
                        dx * dx + dy * dy));
        }
    }
    const bool buildingAttackSlotsWorked =
        buildingAttackers.size() == 10 &&
        buildingAttack.objectHitPoints(fortress) <
            fortressHealthBefore &&
        attackersStillEngaged >= 8 &&
        minimumSeparation >= 0.34f &&
        maximumY - minimumY >= 2.0f;

    if (out) {
        systems.selectObjectForTesting(
            repairWorkerId);
        systems.lookAtObject(repairWorkerId);
        systems.render(
            renderer, screenW, screenH);
        std::string repairUiOut(out);
        const size_t extension =
            repairUiOut.find_last_of('.');
        repairUiOut.insert(
            extension == std::string::npos
                ? repairUiOut.size()
                : extension,
            "-repair-ui");
        if (!renderer.savePng(repairUiOut)) {
            fprintf(
                stderr,
                "error: could not write %s\n",
                repairUiOut.c_str());
            return 1;
        }
        systems.selectObjectForTesting(2);
        systems.lookAtObject(2);
        systems.render(
            renderer, screenW, screenH);
        std::string emptyStatsOut(out);
        const size_t statsExtension =
            emptyStatsOut.find_last_of('.');
        emptyStatsOut.insert(
            statsExtension == std::string::npos
                ? emptyStatsOut.size()
                : statsExtension,
            "-empty-stats");
        if (!renderer.savePng(
                emptyStatsOut)) {
            fprintf(
                stderr,
                "error: could not write %s\n",
                emptyStatsOut.c_str());
            return 1;
        }
    }

    const CombatStats combat = game.combatStats();
    const MovementStats movement = game.movementStats();
    const float finalHitPoints = game.objectHitPoints(targetId);
    printf("combat: target %u hp %.0f -> %.0f, orders %zu, hits %zu, "
           "kills %zu, projectiles %zu, paths %zu, sounds %zu, elapsed %.2f; "
           "building %.0f -> %.0f selected %d, remains %d -> %zu, overlaps %zu, "
           "building destroyed/remains/decayed %d/%d/%d, edge scroll %d, "
           "attack audio %d, offscreen muted %d, approach retries %zu, "
           "automatic/retaliation/armed %zu/%zu/%zu, tech damage %d -> %d, "
           "worker builds/pages/farm %d/%d/%d, research queued/cost/cancelled/completed/upgrade/menus/hidden/chain %d/%d/%d/%d/%d/%d/%d/%d, "
           "garrison mode/entered/individual/all %d/%d/%d/%d, "
           "foundation placed/reassigned/multi/builder/completed/multi-menu %d/%d/%d/%d/%d/%d, "
           "shields building/mobile/overflow/leave/full/drain %d/%d/%d/%d/%d/%d, "
           "gather/multi/auto-gather/auto-build/repair/ally-mech/livestock/gate/empty-menu %d/%d/%d/%d/%d/%d/%d/%d/%d, "
           "formation immediate %d, "
           "repair order/hp/carbon %d/%.1f->%.1f/%.1f->%.1f gate locked/passable %d/%d, "
           "building damage/destroy %.2f/%.2fs\n",
           targetId, initialHitPoints, finalHitPoints,
           combat.ordersIssued, combat.attacksLanded, combat.unitsKilled,
           combat.projectilesLaunched, combat.attackPathsComputed,
           acknowledgementSounds.size(), elapsed, buildingMaxHitPoints,
           damagedBuildingHitPoints, buildingSelected ? 1 : 0,
           sawRemains ? 1 : 0, combat.activeRemains, movement.overlappingPairs,
           buildingDestroyed ? 1 : 0, sawBuildingRemains ? 1 : 0,
           remainsDecayed ? 1 : 0, edgeScrolled ? 1 : 0,
           attackPlayedOnce ? 1 : 0,
           offscreenWorldMuted ? 1 : 0,
           combat.attackApproachRetries,
           combat.automaticTargetsAcquired, combat.retaliationOrders,
           combat.armedBuildingsEngaged,
           baseBuildingDamage, upgradedBuildingDamage,
           workerCanBuild ? 1 : 0,
           buildingPagesSeparated ? 1 : 0,
           farmAvailable ? 1 : 0,
           researchQueued ? 1 : 0,
           researchCostDeducted ? 1 : 0,
           researchCancelled ? 1 : 0,
           researchCompleted ? 1 : 0,
           buildingUpgraded ? 1 : 0,
           upgradedProductionMenusWork ? 1 : 0,
           lockedResearchHidden ? 1 : 0,
           researchChainAdvanced ? 1 : 0,
           garrisonModeActivated ? 1 : 0,
           workerGarrisoned ? 1 : 0,
           individualGarrisonEjected ? 1 : 0,
           workerEjected ? 1 : 0,
           foundationPlaced ? 1 : 0,
           constructionAssignmentCleared &&
                   constructionReassigned
               ? 1
               : 0,
           multipleBuildersAssigned ? 1 : 0,
           builderStateEntered ? 1 : 0,
           constructionCompleted ? 1 : 0,
           multiWorkerMenuOpened ? 1 : 0,
           buildingShieldAbsorbed ? 1 : 0,
           mobileShieldAbsorbed ? 1 : 0,
           shieldOverflowDamagedHealth ? 1 : 0,
           shieldRetainedOutsideRadius &&
                   shieldDrainedOnTick
               ? 1
               : 0,
           shieldChargedFully ? 1 : 0,
           shieldFixturesCreated &&
                   noPartialDrain &&
                   unpoweredShieldDrained
               ? 1
               : 0,
           workerGatheredAndDeposited ? 1 : 0,
           multipleGatherersAssigned ? 1 : 0,
           automaticGatheringAssigned ? 1 : 0,
           automaticConstructionChained ? 1 : 0,
           workerRepairedBuilding ? 1 : 0,
           workerRepairedAlliedMech ? 1 : 0,
           livestockCaptured ? 1 : 0,
           lockedGateBlocks ? 1 : 0,
           emptyMenuHidden ? 1 : 0,
           formationMovedImmediately ? 1 : 0,
           repairOrderIssued ? 1 : 0,
           repairHealthBefore,
           repairHealthAfter,
           repairCarbonBefore,
           repairCarbonAfter,
           gateLocked ? 1 : 0,
           gatePositionPassable ? 1 : 0,
           buildingElapsed, destructionElapsed);
    printf(
        "compatibility: manual drop-off %d, mobile power on/off %d/%d, "
        "reverse destroy %d, stance menu %d, "
        "air ground/aa/walker/projectile/auto %d/%d/%d/%d/%d, "
        "building slots %d engaged %zu separation %.2f spread %.2f\n",
        manualDropOffWorked ? 1 : 0,
        mobilePowerWorked ? 1 : 0,
        mobilePowerRemoved ? 1 : 0,
        destroyReverseOrder ? 1 : 0,
        stanceMenuWorked ? 1 : 0,
        groundRejectsAir ? 1 : 0,
        antiAirTargetsOnlyAir ? 1 : 0,
        walkerLockedBeforeResearch &&
                walkerResearchApplied
            ? 1
            : 0,
        walkerAirProjectile ? 1 : 0,
        invalidAutomaticTargetIgnored ? 1 : 0,
        buildingAttackSlotsWorked ? 1 : 0,
        attackersStillEngaged,
        minimumSeparation,
        maximumY - minimumY);
    printf(
        "attack ground: eligibility/button/cancel/order %d/%d/%d/%d, "
        "volley/damage/repeat/replace %d/%d/%d/%d, "
        "delay/pending-release %d/%d\n",
        attackGroundEligibility ? 1 : 0,
        attackGroundButtonWorked ? 1 : 0,
        attackGroundSelectionCancelled ? 1 : 0,
        attackGroundOrderIssued ? 1 : 0,
        heavyArtilleryVolley ? 1 : 0,
        attackGroundDamagedPoint ? 1 : 0,
        attackGroundRepeated ? 1 : 0,
        attackGroundReplaced ? 1 : 0,
        attackReleaseDelayed ? 1 : 0,
        delayedProjectileReleased ? 1 : 0);
    const bool heardBlaster =
        std::find(acknowledgementSounds.begin(), acknowledgementSounds.end(), 71) !=
        acknowledgementSounds.end();
    const bool heardDeath =
        std::find(acknowledgementSounds.begin(), acknowledgementSounds.end(), 298) !=
        acknowledgementSounds.end();
    if (combat.ordersIssued != 3 || combat.attacksLanded == 0 ||
        game.objectActive(targetId) || combat.unitsKilled == 0 ||
        combat.projectilesLaunched == 0 || !heardBlaster || !heardDeath ||
        damagedBuildingHitPoints > buildingMaxHitPoints * 0.75f ||
        !buildingSelected || !sawRemains || combat.activeRemains != 0 ||
        movement.overlappingPairs != 0 || !buildingDestroyed ||
        !sawBuildingRemains || !remainsDecayed || !edgeScrolled ||
        !attackPlayedOnce || !offscreenWorldMuted ||
        combat.automaticTargetsAcquired == 0 ||
        // Idle sandbox units no longer wander out of combat lanes. Allow
        // bounded replans around those persistent blockers while still
        // rejecting a retry loop.
        combat.attackApproachRetries >
            std::max<size_t>(
                8,
                combat.attackPathsComputed / 2) ||
        !researchedFocusCoils ||
        !workerCanBuild ||
        !buildingPagesSeparated ||
        !farmAvailable ||
        !researchQueued || !researchCostDeducted ||
        !researchCancelled || !researchCompleted ||
        !lockedResearchHidden ||
        !researchChainAdvanced ||
        !buildingUpgraded ||
        !upgradedProductionMenusWork ||
        !garrisonModeActivated || !workerGarrisoned ||
        !individualGarrisonEjected ||
        !workerEjected ||
        !foundationPlaced ||
        !constructionAssignmentCleared ||
        !constructionReassigned ||
        !multipleBuildersAssigned ||
        !builderStateEntered ||
        !constructionCompleted ||
        !multiWorkerMenuOpened ||
        !buildingShieldAbsorbed ||
        !mobileShieldAbsorbed ||
        !shieldOverflowDamagedHealth ||
        !shieldRetainedOutsideRadius ||
        !shieldDrainedOnTick ||
        !shieldChargedFully ||
        !shieldFixturesCreated ||
        !noPartialDrain ||
        !unpoweredShieldDrained ||
        !workerGatheredAndDeposited ||
        !multipleGatherersAssigned ||
        !manualDropOffWorked ||
        !mobilePowerWorked ||
        !mobilePowerRemoved ||
        !destroyReverseOrder ||
        !stanceMenuWorked ||
        !groundRejectsAir ||
        !antiAirTargetsOnlyAir ||
        !walkerLockedBeforeResearch ||
        !walkerResearchApplied ||
        !walkerAirProjectile ||
        !invalidAutomaticTargetIgnored ||
        !attackGroundEligibility ||
        !attackGroundButtonWorked ||
        !attackGroundSelectionCancelled ||
        !attackGroundOrderIssued ||
        !heavyArtilleryVolley ||
        !attackGroundDamagedPoint ||
        !attackGroundRepeated ||
        !attackGroundReplaced ||
        !attackReleaseDelayed ||
        !delayedProjectileReleased ||
        !buildingAttackSlotsWorked ||
        !automaticGatheringAssigned ||
        !automaticConstructionChained ||
        !workerRepairedBuilding ||
        !workerRepairedAlliedMech ||
        !livestockCaptured ||
        !lockedGateBlocks ||
        !emptyMenuHidden ||
        !formationMovedImmediately ||
        upgradedBuildingDamage <= baseBuildingDamage) {
        fprintf(stderr, "error: combat validation failed\n");
        return 1;
    }
    return 0;
}

// Renders one graphic at 8 world facings (0 = +x, then +45 deg steps) in a row.
static int cmdAngles(const char *dataDir, int gid, const char *out) {
    SoftRenderer r;
    Assets a(&r);
    std::string err;
    if (!a.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (const dat::Graphic *graphic = a.dat().graphic(gid)) {
        printf("graphic %d '%s' file '%s': slp %d frames %d angles %d duration %.3f "
               "sequence 0x%02x layer %u deltas %zu\n",
               gid, graphic->name.c_str(), graphic->fileName.c_str(),
               graphic->slp, graphic->frameCount,
               graphic->angleCount, graphic->frameDuration,
               graphic->sequenceType, graphic->layer,
               graphic->deltas.size());
        for (const dat::GraphicDelta &delta :
             graphic->deltas)
            printf("  delta graphic %d offset %d,%d display-angle %d\n",
                   delta.graphicId, delta.offsetX, delta.offsetY,
                   delta.displayAngle);
        if (graphic->slp < 0) {
            const size_t suffix = graphic->name.rfind('-');
            const std::string prefix =
                graphic->name.substr(0, suffix == std::string::npos
                                            ? graphic->name.size()
                                            : suffix + 1);
            for (size_t i = 0; i < a.dat().graphics.size(); i++) {
                const dat::Graphic &candidate = a.dat().graphics[i];
                if (candidate.exists && candidate.slp >= 0 &&
                    candidate.name.rfind(prefix, 0) == 0)
                    printf("  variant %zu '%s': slp %d\n", i,
                           candidate.name.c_str(), candidate.slp);
            }
        }
    }
    Game g(a);
    r.beginFrame(8 * 120, 140, 1.0f, 60, 110, 60);
    for (int i = 0; i < 8; i++) {
        float sx = 60 + i * 120.0f, sy = 90;
        g.drawGraphicNow(r, gid, sx, sy, i * 3.14159265f / 4, 0.0f, 1);
        // Marker showing the expected screen direction of travel.
        float wx = cosf(i * 3.14159265f / 4), wy = sinf(i * 3.14159265f / 4);
        float dx = (wx - wy) * 2, dy = (wx + wy);
        float len = sqrtf(dx * dx + dy * dy);
        for (int k = 0; k < 40; k += 2) r.fillRect(sx + dx / len * k, sy + 30 + dy / len * k * 0.5f, 2, 2, 255, 0, 0, 255);
    }
    r.endFrame();
    r.savePng(out);
    return 0;
}

// Regression checks for the Sept 2026 compatibility fixes. Each check prints
// PASS/FAIL; optional PNG crops are written with the given prefix.
static int cmdTestFixes(const char *dataDir, const char *outPrefix) {
    std::string err;
    SoftRenderer renderer;
    Assets assets(&renderer);
    if (!assets.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    int failures = 0;
    auto report = [&](const char *name, bool ok, const std::string &detail) {
        printf("%s %s %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
        if (!ok) failures++;
    };
    auto shot = [&](Game &g, const std::string &suffix) {
        if (!outPrefix) return;
        g.render(renderer, 960, 544);
        renderer.savePng(std::string(outPrefix) + suffix + ".png");
    };
    auto step = [](Game &g, float seconds) {
        for (float t = 0; t < seconds; t += 1.0f / 30.0f) g.update(1.0f / 30.0f, {});
    };

    // 1) A worker performs the first carbon work animation before the tree
    //    falls, then gathers from the felled pile.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        const uint32_t worker = g.spawnObjectForTesting(3, 83, 1, 27.0f, 40.5f);
        const uint32_t tree = g.spawnObjectForTesting(0, 348, 0, 27.5f, 40.5f);
        const bool standing = tree && !g.objectFelled(tree);
        bool ordered = g.issueGatherForTesting(worker, tree);
        g.lookAtObject(tree);
        shot(g, "_tree_before");
        step(g, 0.5f);
        const bool delayed =
            !g.objectFelled(tree) &&
            g.objectCarriedAmount(worker) <
                0.001f;
        bool felled = false;
        for (int f = 0; f < 600 && !felled; f++) {
            g.update(1.0f / 30.0f, {});
            felled = g.objectFelled(tree);
        }
        step(g, 2.0f);
        shot(g, "_tree_felled");
        const float left = g.objectResourceAmount(tree);
        const auto wp = g.objectPosition(worker);
        printf("  worker at %.2f,%.2f carried %.2f gathering %d\n", wp[0], wp[1],
               g.objectCarriedAmount(worker), (int)g.objectGatheringTarget(worker, tree));
        report("tree-felled", standing && ordered && delayed && felled && left > 0.0f,
               "ordered=" + std::to_string(ordered) + " felled=" + std::to_string(felled) +
                   " delayed=" + std::to_string(delayed) +
                   " remaining=" + std::to_string(left));
    }

    // 2) Gates open instantly for friendly units in the corridor and close
    //    0.5s after it empties; enemies cannot pass a closed gate.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        const float gx = 50.0f, gy = 50.5f;
        const uint32_t gate = g.spawnObjectForTesting(3, 64, 1, gx, gy);
        const int closedId = g.objectUnitId(gate);
        const uint32_t trooper = g.spawnObjectForTesting(3, 460, 1, gx, gy - 3.0f);
        g.update(1.0f / 30.0f, {});
        const bool startsClosed = g.objectUnitId(gate) == closedId;
        g.moveObjectForTesting(trooper, gx, gy - 0.4f);
        g.update(1.0f / 30.0f, {});
        const bool opened = g.objectUnitId(gate) != closedId;
        g.lookAtObject(gate);
        g.selectObjectForTesting(gate);
        g.update(0.001f, {});
        shot(g, "_gate_open");
        g.moveObjectForTesting(trooper, gx, gy - 4.0f);
        step(g, 0.25f);
        const bool stillOpen = g.objectUnitId(gate) != closedId;
        step(g, 0.6f);
        const bool closed = g.objectUnitId(gate) == closedId;
        shot(g, "_gate_closed");
        const uint32_t enemy = g.spawnObjectForTesting(1, 460, 2, gx, gy - 3.0f);
        g.setDiplomacyForTesting(1, 2, 3);
        g.setDiplomacyForTesting(2, 1, 3);
        g.update(1.0f / 30.0f, {});
        const bool enemyBlocked = !g.positionPassableForTesting(enemy, gx, gy);
        const bool friendPasses = g.positionPassableForTesting(trooper, gx, gy);
        report("gate", startsClosed && opened && stillOpen && closed && enemyBlocked && friendPasses,
               "closed0=" + std::to_string(startsClosed) + " open=" + std::to_string(opened) +
                   " hold=" + std::to_string(stillOpen) + " close=" + std::to_string(closed) +
                   " enemyBlocked=" + std::to_string(enemyBlocked) +
                   " friendPasses=" + std::to_string(friendPasses));
    }

    // 3) Command Centre keeps its research after reaching Tech Level 2.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        const uint32_t cc = g.spawnObjectForTesting(3, 109, 1, 70.0f, 70.0f);
        g.update(1.0f / 30.0f, {});
        const size_t before = g.researchOptionIds(cc).size();
        g.researchTechnology(1, 1); // TECH-AGE2
        step(g, 0.2f);
        const std::vector<int> after = g.researchOptionIds(cc);
        std::string ids;
        for (int id : after) ids += std::to_string(id) + ",";
        const bool nextLevelShown =
            std::find(after.begin(), after.end(), 2) != after.end();
        report("cc-research-tl2", before > 0 && !after.empty() && nextLevelShown,
               "unit=" + std::to_string(g.objectUnitId(cc)) + " before=" + std::to_string(before) +
                   " after=" + std::to_string(after.size()) + " [" + ids + "]");
    }
    // 4) Stance menu uses the original icons/strings and shows the active one.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        const uint32_t trooper = g.spawnObjectForTesting(3, 460, 1, 26.0f, 40.0f);
        g.selectObjectForTesting(trooper);
        g.lookAtObject(trooper);
        g.update(0.001f, {});
        shot(g, "_stance_panel");
        InputState in;
        in.screenW = 960; in.screenH = 544;
        in.pointerX = 851.0f; in.pointerY = 473.0f; in.selectPressed = true;
        g.update(0.001f, in);
        const bool opened = g.actionMenuOpenForTesting();
        shot(g, "_stance_menu");
        report("stance-menu", opened, "opened=" + std::to_string(opened));
    }
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) {
            fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        g.setLocalPlayerForTesting(1);
        const uint32_t worker =
            g.spawnObjectForTesting(
                3, 83, 1, 26.0f, 40.0f);
        g.selectObjectForTesting(worker);
        g.lookAtObject(worker);
        g.update(0.001f, {});
        InputState in;
        in.screenW = 960;
        in.screenH = 544;
        in.pointerX = 851.0f;
        in.pointerY = 473.0f;
        in.selectPressed = true;
        g.update(0.001f, in);
        report(
            "worker-no-stance",
            !g.actionMenuOpenForTesting(),
            "opened=" +
                std::to_string(
                    g.actionMenuOpenForTesting()));
    }
    // 5) Group pathing: 16 troopers route through a one-tile gap in a wall
    //    and around a building, arriving without overlaps or stragglers.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        for (int y = 28; y <= 52; y++)
            if (y != 40) g.spawnObjectForTesting(3, 117, 1, 40.5f, y + 0.5f);
        g.spawnObjectForTesting(3, 70, 1, 46.0f, 40.0f); // shelter in the way
        std::vector<uint32_t> troops;
        for (int i = 0; i < 16; i++)
            troops.push_back(g.spawnObjectForTesting(3, 460, 1, 29.0f + (i % 4) * 0.8f,
                                                     37.5f + (i / 4) * 0.8f));
        g.update(1.0f / 30.0f, {});
        const size_t staticBefore = g.movementStats().staticObstructionViolations;
        g.groupMoveForTesting(troops, 50.0f, 40.5f);
        int frames = 0;
        for (; frames < 30 * 60; frames++) {
            g.update(1.0f / 30.0f, {});
            if (g.movementStats().pendingMoveGoals == 0) break;
            if (std::getenv("SWGB_TRACE_PENDING") && frames % 60 == 0) {
                printf("  t=%.0f pending=%zu", frames / 30.0f, g.movementStats().pendingMoveGoals);
                for (uint32_t id : troops) {
                    const auto p = g.objectPosition(id);
                    printf(" %.1f,%.1f", p[0], p[1]);
                }
                printf("\n");
            }
        }
        const MovementStats ms = g.movementStats();
        int across = 0;
        for (uint32_t id : troops) {
            const auto p = g.objectPosition(id);
            if (p[0] > 41.5f) across++;
            if (p[0] <= 41.5f || g.describeObjectForTesting(id).find("goal=1") != std::string::npos) printf("  stuck %u at %.2f,%.2f %s\n", id, p[0], p[1],
                        g.describeObjectForTesting(id).c_str());
        }
        g.lookAtObject(troops[0]);
        shot(g, "_group_path");
        report("group-pathing",
               across == (int)troops.size() && ms.overlappingPairs == 0 &&
                   ms.terrainViolations == 0 && ms.staticObstructionViolations <= staticBefore,
               "across=" + std::to_string(across) + "/16 time=" +
                   std::to_string(frames / 30.0f) + " overlaps=" +
                   std::to_string(ms.overlappingPairs) + " pending=" +
                   std::to_string(ms.pendingMoveGoals) + " static=" +
                   std::to_string(ms.staticObstructionViolations));
    }
    // 6) Formations (Line/Box/Staggered/Flank) settle without overlaps.
    for (int formation = 0; formation < 4; formation++) {
        static const char *names[] = {"line", "box", "staggered", "flank"};
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        std::vector<uint32_t> troops;
        const int mix[] = {460, 460, 460, 460, 460, 460, 460, 460, 460, 460, 460, 460};
        for (int i = 0; i < 12; i++)
            troops.push_back(g.spawnObjectForTesting(3, mix[i], 1, 29.0f + (i % 4) * 0.8f,
                                                     37.5f + (i / 4) * 0.8f));
        g.update(1.0f / 30.0f, {});
        g.groupMoveForTesting(troops, 37.0f, 38.5f, formation);
        int frames = 0;
        bool reported = false;
        for (; frames < 30 * 40; frames++) {
            g.update(1.0f / 30.0f, {});
            const MovementStats now = g.movementStats();
            if (std::getenv("SWGB_TRACE_FORMATION") && !reported)
                for (size_t a = 0; a < troops.size() && !reported; a++)
                    for (size_t b = a + 1; b < troops.size(); b++) {
                        const auto pa = g.objectPosition(troops[a]), pb = g.objectPosition(troops[b]);
                        const float dx = pa[0] - pb[0], dy = pa[1] - pb[1];
                        if (dx * dx + dy * dy < 0.44f * 0.44f) {
                            reported = true;
                            printf("   overlap at frame %d d=%.3f: %u %.2f,%.2f %s | %u %.2f,%.2f %s\n", frames,
                                   std::sqrt(dx * dx + dy * dy), troops[a], pa[0], pa[1],
                                   g.describeObjectForTesting(troops[a]).substr(0, 110).c_str(), troops[b], pb[0], pb[1],
                                   g.describeObjectForTesting(troops[b]).substr(0, 110).c_str());
                            break;
                        }
                    }
            if (now.pendingMoveGoals == 0) break;
        }
        step(g, 1.0f);
        const MovementStats ms = g.movementStats();
        if (std::getenv("SWGB_TRACE_FORMATION"))
            for (uint32_t id : troops) {
                const auto p = g.objectPosition(id);
                printf("   %u %.2f,%.2f %s\n", id, p[0], p[1], g.describeObjectForTesting(id).c_str());
            }
        g.lookAtObject(troops[0]);
        shot(g, std::string("_formation_") + names[formation]);
        report((std::string("formation-") + names[formation]).c_str(),
               ms.pendingMoveGoals == 0 && ms.overlappingPairs == 0,
               "time=" + std::to_string(frames / 30.0f) + " overlaps=" +
                   std::to_string(ms.overlappingPairs) + " (" + std::to_string(ms.firstOverlapObject) + "/" +
                   std::to_string(ms.secondOverlapObject) + ")");
    }
    // 7) Builders route around a building to reach a foundation behind it
    //    and do not jam on each other.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        g.spawnObjectForTesting(3, 109, 1, 40.0f, 44.0f); // command centre in the way
        std::vector<uint32_t> workers;
        for (int i = 0; i < 4; i++)
            workers.push_back(g.spawnObjectForTesting(3, 83, 1, 43.5f + (i % 2) * 0.6f,
                                                      43.4f + (i / 2) * 0.8f));
        g.update(1.0f / 30.0f, {});
        const uint32_t site = g.spawnFoundationForTesting(3, 70, 1, 36.0f, 44.0f, workers);
        const float hp0 = g.objectHitPoints(site);
        int started = -1, frames = 0;
        for (; frames < 30 * 60; frames++) {
            g.update(1.0f / 30.0f, {});
            if (started < 0 && g.objectHitPoints(site) > hp0 + 0.5f) started = frames;
            if (started >= 0 && frames > started + 30 * 8) break;
        }
        int building = 0;
        for (uint32_t id : workers) {
            const auto p = g.objectPosition(id);
            const float dx = p[0] - 36.0f, dy = p[1] - 44.0f;
            if (std::getenv("SWGB_TRACE_BUILDERS"))
                printf("  worker %u at %.2f,%.2f %s\n", id, p[0], p[1],
                       g.describeObjectForTesting(id).substr(0, 150).c_str());
            if (std::sqrt(dx * dx + dy * dy) < 2.6f) building++;
            else printf("  far worker %u at %.2f,%.2f %s\n", id, p[0], p[1],
                        g.describeObjectForTesting(id).substr(0, 120).c_str());
        }
        const MovementStats ms = g.movementStats();
        g.lookAtObject(site);
        shot(g, "_builders");
        report("builders-route", started >= 0 && started < 30 * 15 && building == 4 &&
                                     ms.overlappingPairs == 0,
               "start=" + std::to_string(started / 30.0f) + " atSite=" + std::to_string(building) +
                   " overlaps=" + std::to_string(ms.overlappingPairs));
    }
    // 8) A worker wedged in a gap between two buildings gets out and builds.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        g.spawnObjectForTesting(3, 70, 1, 38.0f, 40.0f);
        g.spawnObjectForTesting(3, 70, 1, 40.3f, 40.0f);
        const uint32_t worker = g.spawnObjectForTesting(3, 83, 1, 39.15f, 40.0f);
        g.update(1.0f / 30.0f, {});
        const uint32_t site = g.spawnFoundationForTesting(3, 70, 1, 39.0f, 45.0f, {worker});
        const float hp0 = g.objectHitPoints(site);
        int started = -1;
        for (int f = 0; f < 30 * 20 && started < 0; f++) {
            g.update(1.0f / 30.0f, {});
            if (g.objectHitPoints(site) > hp0 + 0.5f) started = f;
        }
        const auto p = g.objectPosition(worker);
        report("wedged-worker", started >= 0,
               "start=" + std::to_string(started / 30.0f) + " at " + std::to_string(p[0]) + "," +
                   std::to_string(p[1]) + " " + g.describeObjectForTesting(worker).substr(0, 100));
    }
    // 9) Wall drag lines: L-shape, diagonal and straight, with the original
    //    connection frames (posts at ends and corners).
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        g.setResourceForTesting(1, 2, 5000);
        const uint32_t worker = g.spawnObjectForTesting(3, 83, 1, 28.0f, 36.0f);
        g.update(1.0f / 30.0f, {});
        std::vector<uint32_t> walls;
        auto add = [&](int x1, int y1, int x2, int y2) {
            auto ids = g.placeWallForTesting(worker, 3, 117, x1, y1, x2, y2);
            walls.insert(walls.end(), ids.begin(), ids.end());
            return ids;
        };
        // Find a clear 16x16 patch.
        int bx = -1, by = -1;
        for (int cy = 10; cy < 80 && bx < 0; cy += 4)
            for (int cx = 10; cx < 80 && bx < 0; cx += 4) {
                bool clear = true;
                for (int y = cy; y < cy + 16 && clear; y++)
                    for (int x = cx; x < cx + 16 && clear; x++)
                        clear = g.positionPassableForTesting(worker, x + 0.5f, y + 0.5f);
                if (clear) { bx = cx; by = cy; }
            }
        printf("  wall patch %d,%d\n", bx, by);
        g.moveObjectForTesting(worker, bx - 1.0f, by + 8.0f);
        const auto lWall = add(bx + 1, by + 1, bx + 7, by + 4);
        const auto diag = add(bx + 1, by + 8, bx + 5, by + 12);
        const auto straight = add(bx + 11, by + 1, bx + 11, by + 7);
        // Gap fill: a 2-tile wall ending one tile short of the straight wall
        // is extended by one foundation to close the gap.
        const auto gapWall = add(bx + 8, by + 6, bx + 9, by + 6);
        for (uint32_t id : walls) g.completeFoundationForTesting(id);
        for (int f = 0; f < 10; f++) g.update(1.0f / 30.0f, {});
        auto frameOf = [&](uint32_t id) {
            return (int)std::lround(g.objectFacing(id) / (2.0f * 3.14159265f / 5.0f));
        };
        std::string frames;
        for (uint32_t id : lWall) frames += std::to_string(frameOf(id));
        frames += " ";
        for (uint32_t id : diag) frames += std::to_string(frameOf(id));
        frames += " ";
        for (uint32_t id : straight) frames += std::to_string(frameOf(id));
        g.lookAtObject(lWall.empty() ? worker : lWall[lWall.size() / 2]);
        shot(g, "_walls");
        if (!diag.empty()) {
            g.lookAtObject(diag[diag.size() / 2]);
            shot(g, "_walls_diag");
        }
        // L: 7 tiles along X (ends/corner = 2, middle = 1) then 3 along Y.
        bool gapClosed = false;
        for (uint32_t id : gapWall) {
            const auto p = g.objectPosition(id);
            if (std::abs(p[0] - (bx + 10.5f)) < 0.01f && std::abs(p[1] - (by + 6.5f)) < 0.01f)
                gapClosed = true;
        }
        printf("  gap wall pieces %zu closed=%d\n", gapWall.size(), (int)gapClosed);
        report("wall-gap-fill", gapWall.size() == 3 && gapClosed,
               "pieces=" + std::to_string(gapWall.size()) + " closed=" + std::to_string(gapClosed));
        // Gates follow the wall they are placed on and replace its pieces.
        g.setResourceForTesting(1, 2, 5000);
        const auto alongX = add(bx + 1, by + 14, bx + 9, by + 14);
        for (uint32_t id : alongX) g.completeFoundationForTesting(id);
        for (int f = 0; f < 5; f++) g.update(1.0f / 30.0f, {});
        const int gateX = g.placeGateForTesting(worker, 3, bx + 5.5f, by + 14.5f);
        const int gateY = g.placeGateForTesting(worker, 3, bx + 11.5f, by + 4.5f);
        const int gateDiag = g.placeGateForTesting(worker, 3, bx + 3.5f, by + 10.5f);
        for (int f = 0; f < 5; f++) g.update(1.0f / 30.0f, {});
        g.lookAtObject(alongX[alongX.size() / 2]);
        shot(g, "_gate_on_wall");
        report("gate-orientation", gateX == 490 && gateY == 487 && gateDiag == 673,
               "alongX=" + std::to_string(gateX) + " alongY=" + std::to_string(gateY) +
                   " diagonal=" + std::to_string(gateDiag));
        report("wall-lines", lWall.size() == 10 && diag.size() == 5 && straight.size() == 7 &&
                                 frames == "2111112002 24442 2000022",
               "counts " + std::to_string(lWall.size()) + "/" + std::to_string(diag.size()) + "/" +
                   std::to_string(straight.size()) + " frames " + frames);
        // Gate lifecycle: finish the gate on the X wall, walls can't be laid
        // through it, destroying it removes its posts, and a new gate can go
        // back into the wall.
        uint32_t gateId = 0;
        for (uint32_t id : g.underConstructionObjectIds())
            if (std::abs(g.objectPosition(id)[0] - (bx + 5.5f)) < 1.1f &&
                std::abs(g.objectPosition(id)[1] - (by + 14.5f)) < 1.1f)
                gateId = id;
        g.issueRepairForTesting(worker, gateId);
        for (int f = 0; f < 30 * 120; f++) {
            g.update(1.0f / 30.0f, {});
            bool building = false;
            for (uint32_t id : g.underConstructionObjectIds()) building |= id == gateId;
            if (!building) break;
        }
        const auto gp = g.objectPosition(gateId);
        auto countNear = [&](float x, float y, float r) {
            int n = 0;
            for (uint32_t id = 1; id < 4000; id++)
                if (g.objectActive(id) && id != worker) {
                    const auto p = g.objectPosition(id);
                    if (std::abs(p[0] - x) <= r && std::abs(p[1] - y) <= r) n++;
                }
            return n;
        };
        const int beforeCross = countNear(gp[0], gp[1], 2.6f);
        const auto cross = g.placeWallForTesting(worker, 3, 117, bx + 5, by + 11, bx + 5, by + 17);
        bool throughGate = false;
        for (uint32_t id : cross) {
            const auto p = g.objectPosition(id);
            if (std::abs(p[0] - gp[0]) < 2.0f && std::abs(p[1] - gp[1]) < 0.5f) throughGate = true;
        }
        const int around = countNear(bx + 5.5f, by + 14.5f, 2.6f) - (int)cross.size();
        g.damageObjectForTesting(gateId, 100000);
        for (int f = 0; f < 5; f++) g.update(1.0f / 30.0f, {});
        int left = 0; // anything but wall pieces left where the gate stood
        for (uint32_t id = 1; id < 4000; id++)
            if (g.objectActive(id) && id != worker && g.objectUnitId(id) != 117) {
                const auto p = g.objectPosition(id);
                if (std::abs(p[0] - gp[0]) < 2.6f && std::abs(p[1] - gp[1]) < 1.0f) left++;
            }
        const int again = g.placeGateForTesting(worker, 3, gp[0], gp[1]);
        report("gate-lifecycle", gateId && !throughGate && left == 0 && again > 0,
               "gate " + std::to_string(gateId) + " before " + std::to_string(beforeCross) +
                   " through " + std::to_string(throughGate) + " around " + std::to_string(around) +
                   " leftover " + std::to_string(left) + " replaced " + std::to_string(again));
    }
    // 10) Production uses distinct immediate perimeter exits, blocks at
    //     100% when they are full, and resumes after one becomes available.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        const float bx = 64.0f, by = 64.0f;
        const uint32_t center =
            g.spawnObjectForTesting(
                3, 87, 1, bx, by);
        for (int index = 0; index < 20;
             index++)
            g.spawnObjectForTesting(
                1, 70, 1,
                6.0f + (index % 5) * 2.5f,
                6.0f + (index / 5) * 2.5f);
        std::vector<uint32_t> before;
        for (uint32_t id = 1; id < 20000; id++)
            if (g.objectActive(id) &&
                g.objectUnitId(id) == 460)
                before.push_back(id);
        for (int i = 0; i < 64; i++)
            g.queueUnitForTesting(center, 460);
        // Keep produced combat units passive so this test observes exit
        // placement rather than their later automatic engagement orders.
        std::vector<uint32_t> seen = before;
        for (int frame = 0; frame < 30 * 15; frame++) {
            g.update(1.0f / 30.0f, {});
            for (uint32_t id = 1; id < 20000; id++)
                if (g.objectActive(id) &&
                    g.objectUnitId(id) == 460 &&
                    std::find(
                        seen.begin(), seen.end(), id) ==
                        seen.end()) {
                    const auto position =
                        g.objectPosition(id);
                    g.setAttackModeForTesting(id, 3);
                    g.moveObjectForTesting(
                        id, position[0], position[1]);
                    seen.push_back(id);
                }
        }
        std::vector<uint32_t> produced;
        for (uint32_t id = 1; id < 20000; id++) {
            if (!g.objectActive(id) ||
                g.objectUnitId(id) != 460 ||
                std::find(before.begin(), before.end(), id) !=
                    before.end())
                continue;
            produced.push_back(id);
        }
        bool immediate = !produced.empty();
        bool separate = true;
        float closest = 1000.0f;
        for (size_t i = 0; i < produced.size(); i++) {
            const auto a =
                g.objectPosition(produced[i]);
            immediate &=
                std::abs(a[0] - bx) <= 2.01f &&
                std::abs(a[1] - by) <= 2.01f;
            for (size_t j = i + 1;
                 j < produced.size(); j++) {
                const auto b =
                    g.objectPosition(produced[j]);
                const float dx = a[0] - b[0];
                const float dy = a[1] - b[1];
                closest = std::min(
                    closest,
                    std::sqrt(dx * dx + dy * dy));
                separate &=
                    dx * dx + dy * dy >=
                    0.44f * 0.44f;
            }
        }
        const size_t blockedQueue =
            g.productionQueueSizeForTesting(center);
        const bool blocked =
            blockedQueue > 0 &&
            g.productionRemainingForTesting(center) <=
                0.001f;
        g.lookAtObject(center);
        shot(g, "_production_blocked");
        bool resumed = false;
        if (!produced.empty()) {
            g.moveObjectForTesting(
                produced.front(),
                bx + 8.0f, by + 8.0f);
            step(g, 0.5f);
            resumed =
                g.productionQueueSizeForTesting(center) <
                blockedQueue;
        }
        report(
            "production-exits",
            immediate && separate && blocked && resumed,
            "produced=" +
                std::to_string(produced.size()) +
                " queued=" +
                std::to_string(blockedQueue) +
                " immediate=" +
                std::to_string(immediate) +
                " separate=" +
                std::to_string(separate) +
                " closest=" +
                std::to_string(closest) +
                " blocked=" +
                std::to_string(blocked) +
                " resumed=" +
                std::to_string(resumed));
    }

    // Population is DAT resource storage type 4. A completed queue waits
    // until a finished shelter adds capacity.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        std::vector<int> interfaceSounds;
        g.setInterfaceSoundPlayer(
            [&](int soundId) {
                interfaceSounds.push_back(soundId);
            });
        const uint32_t center =
            g.spawnObjectForTesting(
                1, 109, 1, 68.0f, 20.0f);
        float used =
            g.populationUsedForTesting(1);
        const float initialCapacity =
            g.populationCapacityForTesting(1);
        for (int index = 0;
             used + 0.001f < initialCapacity &&
             index < 250;
             index++) {
            g.spawnObjectForTesting(
                1, 83, 1,
                72.0f + (index % 8) * 0.7f,
                18.0f + (index / 8) * 0.7f);
            used = g.populationUsedForTesting(1);
        }
        const bool queued =
            g.queueUnitForTesting(center, 83);
        for (int frame = 0; frame < 10;
             frame++)
            g.update(1.0f / 30.0f, {});
        const bool blocked =
            g.productionQueueSizeForTesting(
                center) == 1 &&
            g.productionRemainingForTesting(
                center) <= 0.001f &&
            std::find(
                interfaceSounds.begin(),
                interfaceSounds.end(),
                50354) != interfaceSounds.end();
        g.spawnObjectForTesting(
            1, 70, 1, 76.0f, 16.0f);
        for (int frame = 0; frame < 10;
             frame++)
            g.update(1.0f / 30.0f, {});
        const bool resumed =
            g.productionQueueSizeForTesting(
                center) == 0;
        report(
            "population-cap",
            queued && initialCapacity > 0.0f &&
                blocked && resumed,
            "used=" + std::to_string(used) +
                " capacity=" +
                std::to_string(initialCapacity) +
                " blocked=" +
                std::to_string(blocked) +
                " resumed=" +
                std::to_string(resumed));
    }
    // Local hostile damage uses the original atakwarn.wav and a cooldown.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        g.setDiplomacyForTesting(1, 2, 3);
        g.setDiplomacyForTesting(2, 1, 3);
        std::vector<int> interfaceSounds;
        g.setInterfaceSoundPlayer(
            [&](int soundId) {
                interfaceSounds.push_back(soundId);
            });
        const uint32_t victim =
            g.spawnObjectForTesting(
                1, 83, 1, 70.0f, 70.0f);
        const uint32_t attacker =
            g.spawnObjectForTesting(
                3, 460, 2, 10.0f, 10.0f);
        g.damageObjectForTesting(
            victim, 1, attacker);
        g.damageObjectForTesting(
            victim, 1, attacker);
        const size_t firstAlerts =
            std::count(
                interfaceSounds.begin(),
                interfaceSounds.end(),
                50315);
        g.update(10.1f, {});
        g.damageObjectForTesting(
            victim, 1, attacker);
        const std::string attackMessage =
            g.statusMessageForTesting();
        const bool originalMessage =
            attackMessage ==
                "YOUR ARMIES ARE UNDER ATTACK BY "
                "Rebel Alliance" &&
            g.statusMessageIsAttackAlertForTesting();
        const size_t laterAlerts =
            std::count(
                interfaceSounds.begin(),
                interfaceSounds.end(),
                50315);
        report(
            "under-attack-alert",
            firstAlerts == 1 &&
                laterAlerts == 2 &&
                originalMessage,
            "first=" +
                std::to_string(firstAlerts) +
                " later=" +
                std::to_string(laterAlerts) +
                " message=[" +
                attackMessage + "]");
    }
    // Other players expose only Tech Level completion; ordinary research and
    // production queues remain private unless enemy intelligence is enabled.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) {
            fprintf(
                stderr, "%s\n",
                err.c_str());
            return 1;
        }
        g.setLocalPlayerForTesting(1);
        std::vector<int> interfaceSounds;
        g.setInterfaceSoundPlayer(
            [&](int soundId) {
                interfaceSounds.push_back(
                    soundId);
            });
        const uint32_t enemyCenter =
            g.spawnObjectForTesting(
                3, 109, 2, 60.0f, 60.0f);
        const uint32_t enemyProducer =
            g.spawnObjectForTesting(
                3, 87, 2, 64.0f, 60.0f);
        const uint32_t localCenter =
            g.spawnObjectForTesting(
                1, 109, 1, 36.0f, 36.0f);
        const bool ordinaryQueued =
            g.queueTechnologyForTesting(
                enemyCenter, 10);
        g.update(0.2f, {});
        const bool ordinarySilent =
            g.statusMessageForTesting()
                .empty() &&
            std::count(
                interfaceSounds.begin(),
                interfaceSounds.end(),
                50325) == 0;
        const bool levelQueued =
            g.queueTechnologyForTesting(
                enemyCenter, 1);
        g.update(0.2f, {});
        const std::string levelMessage =
            g.statusMessageForTesting();
        const bool levelAnnounced =
            levelMessage.find(
                "Rebel Alliance ADVANCED TO") ==
                    0 &&
            std::count(
                interfaceSounds.begin(),
                interfaceSounds.end(),
                50325) == 1;
        const bool localQueued =
            g.queueTechnologyForTesting(
                localCenter, 11);
        g.update(0.2f, {});
        const bool localAnnounced =
            g.statusMessageForTesting().find(
                " COMPLETE") !=
            std::string::npos;
        const bool enemyProductionQueued =
            g.queueUnitForTesting(
                enemyProducer, 460);
        const bool privateQueue =
            enemyProductionQueued &&
            !g.canInspectProductionForTesting(
                enemyProducer) &&
            g.canInspectProductionForTesting(
                localCenter);
        g.setEnemyIntelligenceForTesting(
            true);
        const bool cheatReveals =
            g.canInspectProductionForTesting(
                enemyProducer);
        report(
            "player-notification-privacy",
            ordinaryQueued &&
                ordinarySilent &&
                levelQueued &&
                levelAnnounced &&
                localQueued &&
                localAnnounced &&
                privateQueue &&
                cheatReveals,
            "ordinary silent " +
                std::to_string(
                    ordinarySilent) +
                " level [" +
                levelMessage +
                "] local " +
                std::to_string(
                    localAnnounced) +
                " queue private/cheat " +
                std::to_string(
                    privateQueue) +
                "/" +
                std::to_string(
                    cheatReveals));
    }
    // Display-instruction speakers inherit the owning player's color.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        g.spawnObjectForTesting(
            3, 1083, 2, 60.0f, 60.0f);
        g.queueInstructionForTesting(
            "Lord Vader : Move out.", -1);
        report(
            "instruction-speaker-color",
            g.currentInstructionPlayerForTesting() ==
                2,
            "player=" +
                std::to_string(
                    g.currentInstructionPlayerForTesting()));
    }

    // 11) Gather point: a trained unit walks to it; removing it works.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        const uint32_t center = g.spawnObjectForTesting(3, 87, 1, 44.0f, 16.0f);
        g.update(1.0f / 30.0f, {});
        const bool set = g.setGatherPointForTesting(center, 50.0f, 22.0f);
        const size_t before = g.selectedObjectIds().size();
        (void)before;
        g.queueUnitForTesting(center, 460);
        float bestDistance = 1e9f;
        for (int f = 0; f < 30 * 20; f++) {
            g.update(1.0f / 30.0f, {});
        }
        // Find the trooper nearest the gather point.
        for (uint32_t id = 1; id < 20000; id++) {
            if (g.objectUnitId(id) != 460) continue;
            const auto p = g.objectPosition(id);
            const float dx = p[0] - 50.0f, dy = p[1] - 22.0f;
            bestDistance = std::min(bestDistance, std::sqrt(dx * dx + dy * dy));
        }
        report("gather-point", set && bestDistance < 1.0f,
               "set=" + std::to_string(set) + " nearest=" + std::to_string(bestDistance));
    }
    // 12) Command Center fires only when garrisoned: +1 bolt per trooper.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        const uint32_t cc = g.spawnObjectForTesting(3, 109, 1, 46.0f, 18.0f);
        const uint32_t enemy = g.spawnObjectForTesting(1, 460, 2, 46.0f, 23.5f);
        g.setDiplomacyForTesting(1, 2, 3);
        g.setDiplomacyForTesting(2, 1, 3);
        g.setDiplomacyForTesting(2, 1, 3);
        g.update(1.0f / 30.0f, {});
        const size_t shots0 = g.projectilesLaunchedForTesting();
        for (int f = 0; f < 60; f++) g.update(1.0f / 30.0f, {});
        const size_t emptyShots = g.projectilesLaunchedForTesting() - shots0;
        std::vector<uint32_t> troops;
        for (int i = 0; i < 5; i++) {
            troops.push_back(g.spawnObjectForTesting(3, 460, 1, 44.0f + i * 0.5f, 15.6f));
            g.garrisonForTesting(troops.back(), cc);
        }
        const float hp0 = g.objectHitPoints(enemy);
        const size_t shots1 = g.projectilesLaunchedForTesting();
        const int emptyVolley = g.garrisonVolleyForTesting(cc);
        for (int f = 0; f < 180; f++) g.update(1.0f / 30.0f, {});
        g.lookAtObject(cc);
        shot(g, "_garrisoned_cc");
        const int volley = g.garrisonVolleyForTesting(cc);
        const size_t garrisonShots = g.projectilesLaunchedForTesting() - shots1;
        printf("  cc %s\n  enemy %s\n", g.describeObjectForTesting(cc).substr(0, 200).c_str(),
               g.describeObjectForTesting(enemy).substr(0, 120).c_str());
        // Troopers (class 52) add 4 dmg / 2 s / 2.5 = 1.6 bolts each: 1 + 8 = 9.
        (void)emptyShots;
        report("garrison-fire", emptyVolley == 1 && volley == 9 && garrisonShots >= 5 &&
                                    g.objectHitPoints(enemy) < hp0,
               "emptyVolley=" + std::to_string(emptyVolley) + " volley=" + std::to_string(volley) +
                   " shots=" + std::to_string(garrisonShots) + " enemyHp " + std::to_string(hp0) +
                   "->" + std::to_string(g.objectHitPoints(enemy)));
    }
    // 13) Research/unit help uses the original rollover text.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        const uint32_t cc = g.spawnObjectForTesting(3, 109, 1, 46.0f, 18.0f);
        g.update(1.0f / 30.0f, {});
        g.selectObjectForTesting(cc);
        g.lookAtObject(cc);
        InputState in;
        in.screenW = 960; in.screenH = 544;
        in.cycleAttackMode = true;
        g.update(0.001f, in);
        const bool opened = g.actionMenuOpenForTesting();
        shot(g, "_menu_units");
        in = {};
        in.screenW = 960; in.screenH = 544;
        in.actionTabRight = true;
        g.update(0.001f, in);
        shot(g, "_menu_research");
        in = {};
        in.screenW = 960; in.screenH = 544;
        in.cursorVisible = true;
        in.pointerX = 960 - 8 - 94 * 3 + 20; in.pointerY = 20; // carbon field
        in.menuBack = true;
        g.update(0.001f, in);
        shot(g, "_tooltip");
        report("original-help", opened, "opened=" + std::to_string(opened));
    }
    // 13) Resources can be selected and show what is left.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        const uint32_t worker =
            g.spawnObjectForTesting(1, 83, 1, 46.0f, 18.0f);
        g.update(1.0f / 30.0f, {});
        g.selectObjectForTesting(worker);
        g.lookAtObject(worker);
        InputState in;
        in.screenW = 960;
        in.screenH = 544;
        in.cycleAttackMode = true;
        g.update(0.001f, in);
        const bool opened =
            g.actionMenuOpenForTesting();
        shot(g, "_worker_build_menu");
        in = {};
        in.screenW = 960;
        in.screenH = 544;
        in.cursorVisible = true;
        in.pointerX = 315;
        in.pointerY = 186;
        g.update(0.001f, in);
        in.menuActivate = true;
        g.update(0.001f, in);
        shot(g, "_farm_placement");
        report(
            "worker-build-menu",
            opened &&
                !g.actionMenuOpenForTesting(),
            "opened=" +
                std::to_string(opened) +
                " placement=" +
                std::to_string(
                    !g.actionMenuOpenForTesting()));
    }
    // 14) Resources can be selected and show what is left.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        const uint32_t tree = g.spawnObjectForTesting(0, 348, 0, 44.5f, 16.5f);
        g.setVisibilityCheatsForTesting(true, false);
        g.update(1.0f / 30.0f, {});
        const bool selected = g.selectObjectForTesting(tree);
        g.lookAtObject(tree);
        g.update(0.001f, {});
        {
            InputState hover;
            hover.screenW = 960; hover.screenH = 544;
            hover.cursorVisible = true;
            hover.pointerX = 126; hover.pointerY = 503;
            g.update(0.001f, hover);
        }
        shot(g, "_resource_panel");
        report("resource-select", selected && g.objectSelected(tree),
               "selected=" + std::to_string(selected) + " amount=" +
                   std::to_string(g.objectResourceAmount(tree)));
    }
    // 15) Sight moves with units, exploration persists, and the two
    // visibility cheats retain their distinct original meanings.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) {
            fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        g.setLocalPlayerForTesting(1);
        const uint32_t scout =
            g.spawnObjectForTesting(
                1, 83, 1, 5.5f, 5.5f);
        const bool initiallyVisible =
            g.tileVisibleForTesting(1, 5, 5);
        g.moveObjectForTesting(
            scout, 90.5f, 90.5f);
        const bool persisted =
            g.tileExploredForTesting(1, 5, 5) &&
            !g.tileVisibleForTesting(1, 5, 5) &&
            g.tileVisibleForTesting(1, 90, 90);
        const uint32_t enemy =
            g.spawnObjectForTesting(
                3, 460, 2, 50.5f, 50.5f);
        const uint32_t enemyBuilding =
            g.spawnObjectForTesting(
                3, 87, 2, 52.5f, 50.5f);
        const uint32_t unseenBuilding =
            g.spawnObjectForTesting(
                3, 87, 2, 72.5f, 50.5f);
        const bool hiddenEnemy =
            !g.objectVisibleForTesting(1, enemy);
        g.moveObjectForTesting(
            scout, 51.0f, 50.5f);
        const bool discovered =
            g.objectVisibleForTesting(1, enemy) &&
            g.objectVisibleForTesting(
                1, enemyBuilding);
        g.moveObjectForTesting(
            scout, 90.5f, 90.5f);
        const bool buildingMemory =
            !g.objectVisibleForTesting(1, enemy) &&
            g.objectVisibleForTesting(
                1, enemyBuilding);
        g.lookAt(40.0f, 42.0f);
        shot(g, "_shroud");
        g.lookAt(5.0f, 90.0f);
        shot(g, "_deep_shroud");
        g.lookAt(40.0f, 42.0f);
        g.setVisibilityCheatsForTesting(
            true, false);
        const bool exploreOnly =
            g.tileExploredForTesting(1, 50, 50) &&
            !g.tileVisibleForTesting(1, 50, 50) &&
            !g.objectVisibleForTesting(1, enemy) &&
            g.objectVisibleForTesting(
                1, unseenBuilding);
        shot(g, "_fog_of_war");
        g.setVisibilityCheatsForTesting(
            true, true);
        const bool forceSight =
            g.tileVisibleForTesting(1, 50, 50) &&
            g.objectVisibleForTesting(1, enemy);
        const bool playerOnly =
            !g.tileExploredForTesting(2, 5, 5) &&
            !g.tileVisibleForTesting(2, 5, 5);
        report(
            "fog-of-war-state",
            initiallyVisible && persisted &&
                hiddenEnemy && discovered &&
                buildingMemory && exploreOnly &&
                forceSight && playerOnly,
            "initial=" +
                std::to_string(initiallyVisible) +
                " persistent=" +
                std::to_string(persisted) +
                " hidden=" +
                std::to_string(hiddenEnemy) +
                " discovered=" +
                std::to_string(discovered) +
                " memory=" +
                std::to_string(
                    buildingMemory) +
                " explore=" +
                std::to_string(exploreOnly) +
                " sight=" +
                std::to_string(forceSight) +
                " local-only=" +
                std::to_string(playerOnly));
    }
    // 16) AI gatherers know only resources their own player has explored.
    {
        Game g(assets);
        if (!g.initCompactTestMap(
                7, 96, &err)) {
            fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        g.setResourceTypeAmountsForTesting(1, 0);
        const uint32_t hiddenCarbon =
            g.spawnObjectForTesting(
                0, 348, 0, 30.5f, 45.5f);
        const char *script =
            "(defrule\n"
            "  (true)\n"
            "=>\n"
            "  (set-strategic-number "
            "sn-food-gatherer-percentage 0)\n"
            "  (set-strategic-number "
            "sn-carbon-gatherer-percentage 100)\n"
            "  (set-strategic-number "
            "sn-metal-gatherer-percentage 0)\n"
            "  (set-strategic-number "
            "sn-nova-gatherer-percentage 0)\n"
            "  (disable-self))\n";
        const bool loaded =
            g.loadAiSourceForTesting(
                2, "fog-test.per", script,
                {}, &err);
        for (int frame = 0;
             loaded && frame < 90; frame++)
            g.update(
                1.0f / 30.0f, {});
        const bool unseen =
            hiddenCarbon &&
            !g.objectVisibleForTesting(
                2, hiddenCarbon);
        g.setLocalPlayerForTesting(2);
        g.setVisibilityCheatsForTesting(
            true, false);
        const bool knownButFogged =
            g.objectVisibleForTesting(
                2, hiddenCarbon) &&
            !g.tileVisibleForTesting(
                2, 30, 45);
        for (int frame = 0;
             loaded && frame < 90; frame++)
            g.update(
                1.0f / 30.0f, {});
        const size_t gatherers =
            g.aiGathererCountForTesting(
                2, 1);
        report(
            "ai-unseen-resource",
            loaded && unseen &&
                knownButFogged &&
                gatherers == 0,
            "loaded=" +
                std::to_string(loaded) +
                " unseen=" +
                std::to_string(unseen) +
                " known=" +
                std::to_string(
                    knownButFogged) +
                " gatherers=" +
                std::to_string(gatherers));
    }
    // 17) Formations keep their shape while marching.
    for (int formation = 0; formation < 4; formation++) {
        static const char *names[] = {"line", "box", "staggered", "flank"};
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        std::vector<uint32_t> troops;
        for (int i = 0; i < 12; i++)
            troops.push_back(g.spawnObjectForTesting(3, 460, 1, 40.0f + (i % 4) * 0.9f,
                                                     11.0f + (i / 4) * 0.9f));
        g.update(1.0f / 30.0f, {});
        g.groupMoveForTesting(troops, 52.0f, 24.0f, formation);
        float worst = 0, sum = 0;
        int samples = 0, frames = 0;
        for (; frames < 30 * 60; frames++) {
            g.update(1.0f / 30.0f, {});
            const float error = g.marchShapeErrorForTesting();
            if (frames > 30 * 5 && error >= 0) {
                worst = std::max(worst, error);
                sum += error;
                samples++;
            }
            if (frames == 30 * 9) {
                g.lookAtObject(troops[5]);
                shot(g, std::string("_march_") + names[formation]);
            }
            if (g.movementStats().pendingMoveGoals == 0) break;
        }
        const MovementStats ms = g.movementStats();
        const float mean = samples ? sum / samples : -1.0f;
        report((std::string("march-") + names[formation]).c_str(),
               samples > 30 && mean < 0.6f && ms.pendingMoveGoals == 0 && ms.overlappingPairs == 0,
               "meanSlotError=" + std::to_string(mean) + " worst=" + std::to_string(worst) +
                   " samples=" + std::to_string(samples) + " time=" + std::to_string(frames / 30.0f) +
                   " overlaps=" + std::to_string(ms.overlappingPairs));
    }
    // 15) Gate appears in the worker build menu from Tech Level 2.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        const uint32_t worker = g.spawnObjectForTesting(3, 83, 1, 44.0f, 16.0f);
        g.update(1.0f / 30.0f, {});
        auto hasGate = [&]() {
            for (int id : g.buildingOptionIds(worker))
                if (id == 487) return true;
            return false;
        };
        const bool before = hasGate();
        g.researchTechnology(1, 1);
        g.update(1.0f / 30.0f, {});
        const bool after = hasGate();
        report("gate-buildable", !before && after,
               "tl1=" + std::to_string(before) + " tl2=" + std::to_string(after));
    }
    // 16) A worker moves on to the next tree when one runs out.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        g.spawnObjectForTesting(3, 109, 1, 44.0f, 14.0f);
        const uint32_t worker = g.spawnObjectForTesting(3, 83, 1, 44.0f, 17.5f);
        const uint32_t tree1 = g.spawnObjectForTesting(0, 348, 0, 44.5f, 19.5f);
        const uint32_t tree2 = g.spawnObjectForTesting(0, 348, 0, 46.5f, 19.5f);
        g.update(1.0f / 30.0f, {});
        g.setObjectResourceForTesting(
            tree1, 3.0f);
        const float carbon0 =
            g.resource(1, 1);
        g.issueGatherForTesting(worker, tree1);
        const float second0 = g.objectResourceAmount(tree2);
        int f = 0;
        for (; f < 30 * 120; f++) {
            g.update(1.0f / 30.0f, {});
            if (g.objectResourceAmount(tree2) < second0 - 5.0f) break;
        }
        printf("  worker at %.2f,%.2f %s\n", g.objectPosition(worker)[0], g.objectPosition(worker)[1], g.describeObjectForTesting(worker).substr(0, 400).c_str());
        report("gather-continues", g.objectResourceAmount(tree1) <= 0.01f &&
                                       g.objectResourceAmount(tree2) < second0 - 5.0f &&
                                       g.objectCarriedAmount(worker) > 7.0f &&
                                       g.resource(1, 1) <= carbon0 + 0.01f,
               "tree1=" + std::to_string(g.objectResourceAmount(tree1)) + " tree2=" +
                   std::to_string(g.objectResourceAmount(tree2)) +
                   " carried=" + std::to_string(g.objectCarriedAmount(worker)) +
                   " bank=" + std::to_string(g.resource(1, 1) - carbon0) +
                   " t=" + std::to_string(f / 30));
    }
    // 17) Self-shielded units charge their own shield (trait 0x40).
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        const uint32_t heavy = g.spawnObjectForTesting(5, 172, 1, 46.0f, 20.0f);
        const uint32_t plain = g.spawnObjectForTesting(5, 174, 1, 48.0f, 20.0f);
        for (int f = 0; f < 30 * 10; f++) g.update(1.0f / 30.0f, {});
        const float heavyShield = g.objectShieldPoints(heavy);
        const float plainShield = g.objectShieldPoints(plain);
        report("self-shield", heavyShield > 1.0f && plainShield <= 0.0f,
               "heavy=" + std::to_string(heavyShield) + "/" +
                   std::to_string(g.objectMaxShieldPoints(heavy)) + " plain=" +
                   std::to_string(plainShield));
    }
    // 18) Farms: terrain-drawn (build/complete), walkable, farmed for food.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        g.spawnObjectForTesting(3, 109, 1, 44.0f, 12.0f);
        const uint32_t worker = g.spawnObjectForTesting(3, 83, 1, 44.0f, 17.0f);
        g.update(1.0f / 30.0f, {});
        const uint32_t farm = g.spawnFoundationForTesting(3, 50, 1, 44.5f, 20.5f, {worker});
        g.update(1.0f / 30.0f, {});
        const int buildTerrain = g.terrainAtForTesting(44, 20);
        int f = 0;
        auto building = [&]() {
            for (uint32_t id : g.underConstructionObjectIds()) if (id == farm) return true;
            return false;
        };
        for (; f < 30 * 120 && building(); f++) g.update(1.0f / 30.0f, {});
        g.update(1.0f / 30.0f, {});
        const int doneTerrain = g.terrainAtForTesting(44, 20);
        const bool walk = g.positionPassableForTesting(worker, 44.5f, 20.5f);
        const float food0 = g.objectResourceAmount(farm);
        const auto farmPosition =
            g.objectPosition(farm);
        float minX = 1e9f, maxX = -1e9f;
        float minY = 1e9f, maxY = -1e9f;
        bool enteredField = false;
        for (int k = 0; k < 30 * 20; k++) {
            g.update(1.0f / 30.0f, {});
            const auto position =
                g.objectPosition(worker);
            minX = std::min(minX, position[0]);
            maxX = std::max(maxX, position[0]);
            minY = std::min(minY, position[1]);
            maxY = std::max(maxY, position[1]);
            enteredField |=
                std::abs(
                    position[0] -
                    farmPosition[0]) <
                    1.4f &&
                std::abs(
                    position[1] -
                    farmPosition[1]) <
                    1.4f;
        }
        for (int k = 0; k < 30 * 40; k++)
            g.update(1.0f / 30.0f, {});
        const float food1 = g.objectResourceAmount(farm);
        const float farmMovement =
            std::hypot(
                maxX - minX, maxY - minY);
        printf("  %s\n", g.describeObjectForTesting(worker).substr(0, 300).c_str());
        g.setObjectResourceForTesting(farm, 0.5f);
        for (int k = 0; k < 30 * 20; k++) g.update(1.0f / 30.0f, {});
        const int deadTerrain = g.terrainAtForTesting(44, 20);
        report("farm", buildTerrain == 29 && doneTerrain == 7 && walk &&
                           enteredField && farmMovement > 0.40f &&
                           food1 < food0 - 1.0f && deadTerrain == 8,
               "terrain " + std::to_string(buildTerrain) + "/" + std::to_string(doneTerrain) + "/" +
                   std::to_string(deadTerrain) + " walk=" + std::to_string(walk) +
                   " field=" + std::to_string(enteredField) +
                   " center=" + std::to_string(farmPosition[0]) + "," +
                   std::to_string(farmPosition[1]) +
                   " movement=" + std::to_string(farmMovement) + " food " +
                   std::to_string(food0) + "->" + std::to_string(food1));
    }
    // 19) Upgrades: build menu shows the upgraded building; HP techs raise
    // the maximum of standing buildings and add to their current HP.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        const uint32_t worker = g.spawnObjectForTesting(3, 83, 1, 44.0f, 16.0f);
        const uint32_t generator = g.spawnObjectForTesting(3, 335, 1, 50.0f, 20.0f);
        g.researchTechnology(1, 1);
        g.researchTechnology(1, 2);
        g.update(1.0f / 30.0f, {});
        const float hp0 = g.objectMaxHitPoints(generator);
        g.researchTechnology(1, 569);
        g.update(1.0f / 30.0f, {});
        const float hp1 = g.objectMaxHitPoints(generator);
        const float cur1 = g.objectHitPoints(generator);
        auto has = [&](int id) {
            for (int option : g.buildingOptionIds(worker)) if (option == id) return true;
            return false;
        };
        const bool before = has(117) && !has(155);
        g.researchTechnology(1, 483);
        g.update(1.0f / 30.0f, {});
        const bool heavy = has(155) && !has(117);
        g.researchTechnology(1, 484);
        g.update(1.0f / 30.0f, {});
        const bool shield = has(195) && !has(155) && has(488) && !has(487);
        report("upgrades", before && heavy && shield && hp1 >= hp0 + 249.0f && cur1 >= hp1 - 0.5f,
               "menu " + std::to_string(before) + std::to_string(heavy) + std::to_string(shield) +
                   " hp " + std::to_string(hp0) + "->" + std::to_string(hp1) + " cur " +
                   std::to_string(cur1));
    }
    // 20) Drop-off: nearest accepting site; switching jobs keeps the old
    // load; a processing center takes only its resource, the CC takes all.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        const uint32_t cc = g.spawnObjectForTesting(3, 109, 1, 44.0f, 8.0f);
        const uint32_t mill = g.spawnObjectForTesting(3, 562, 1, 44.0f, 26.0f);
        const uint32_t worker = g.spawnObjectForTesting(3, 83, 1, 44.0f, 23.0f);
        const uint32_t bush = g.spawnObjectForTesting(0, 59, 0, 42.0f, 22.0f);
        const uint32_t tree = g.spawnObjectForTesting(0, 348, 0, 46.5f, 28.5f);
        g.update(1.0f / 30.0f, {});
        g.issueGatherForTesting(worker, bush);
        for (int f = 0; f < 30 * 12; f++) g.update(1.0f / 30.0f, {});
        const float food = g.objectCarriedAmount(worker);
        g.issueGatherForTesting(worker, tree);
        const float carbon0 = g.resource(1, 1), food0 = g.resource(1, 0);
        float nearest = 1e9f;
        for (int f = 0; f < 30 * 60; f++) {
            g.update(1.0f / 30.0f, {});
            const auto p = g.objectPosition(worker);
            nearest = std::min(nearest, std::abs(p[1] - 8.0f));
        }
        const float stashed = g.objectStashForTesting(worker, 0);
        g.selectObjectForTesting(worker);
        g.lookAtObject(worker);
        shot(g, "_carry");
        const bool millUsed = g.resource(1, 1) > carbon0 + 1.0f && nearest > 6.0f;
        const bool manual = g.issueDropOffForTesting(worker, cc);
        for (int f = 0; f < 30 * 30; f++) g.update(1.0f / 30.0f, {});
        const bool ccTookAll = g.resource(1, 0) >= food0 + stashed - 0.5f &&
                               g.objectStashForTesting(worker, 0) <= 0.01f;
        (void)mill;
        report("drop-off", food > 1.0f && stashed > 1.0f && millUsed && manual && ccTookAll,
               "food carried " + std::to_string(food) + " stashed " + std::to_string(stashed) +
                   " mill " + std::to_string(millUsed) + " nearestCC " + std::to_string(nearest) +
                   " manual " + std::to_string(manual) + " cc " + std::to_string(ccTookAll));
    }
    // 21) Reseed: the Food Proc Ctr queues prepaid farms; a depleted farm
    // becomes a foundation, its farmer rebuilds it and farms again.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        g.spawnObjectForTesting(3, 109, 1, 44.0f, 12.0f);
        const uint32_t mill = g.spawnObjectForTesting(3, 68, 1, 48.0f, 18.0f);
        const uint32_t worker = g.spawnObjectForTesting(3, 83, 1, 44.0f, 17.0f);
        g.update(1.0f / 30.0f, {});
        const uint32_t farm = g.spawnFoundationForTesting(3, 50, 1, 44.5f, 20.5f, {worker});
        for (int f = 0; f < 30 * 40; f++) g.update(1.0f / 30.0f, {});
        bool hasFarm = false;
        for (int id : g.productionOptionIds(mill)) hasFarm |= id == 50;
        g.setResourceForTesting(1, 1, 500.0f);
        g.selectObjectForTesting(mill);
        const bool queued = g.queueUnitForTesting(mill, 50);
        const int queue = g.reseedQueueForTesting(1);
        g.setObjectResourceForTesting(farm, 0.5f);
        for (int f = 0; f < 30 * 10; f++) g.update(1.0f / 30.0f, {});
        const bool rebuilding = g.objectActive(farm) && g.terrainAtForTesting(44, 20) == 29;
        for (int f = 0; f < 30 * 40; f++) g.update(1.0f / 30.0f, {});
        const bool farming = g.objectActive(farm) && g.objectResourceAmount(farm) > 100.0f &&
                             g.objectGatheringTarget(worker, farm) && g.reseedQueueForTesting(1) == 0;
        printf("  farm food %.1f active %d worker %s\n", g.objectResourceAmount(farm), g.objectActive(farm), g.describeObjectForTesting(worker).substr(0, 160).c_str());
        report("reseed", hasFarm && queued && queue == 1 && rebuilding && farming,
               "button " + std::to_string(hasFarm) + " queued " + std::to_string(queue) + " rebuilding " +
                   std::to_string(rebuilding) + " farming " + std::to_string(farming));
    }
    // 22) Menus: Food Proc Ctr units tab with the reseed button; airbase units.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        for (int t : {1, 2, 3}) g.researchTechnology(1, t);
        const uint32_t mill = g.spawnFoundationForTesting(3, 68, 1, 46.0f, 18.0f, {});
        const uint32_t air = g.spawnFoundationForTesting(3, 317, 1, 56.0f, 18.0f, {});
        const uint32_t fort = g.spawnFoundationForTesting(3, 82, 1, 46.0f, 30.0f, {});
        for (uint32_t id : {mill, air, fort}) g.completeFoundationForTesting(id);
        g.update(1.0f / 30.0f, {});
        auto openMenu = [&](uint32_t id, const char *suffix) {
            g.clearSelectionForTesting();
            g.selectObjectForTesting(id);
            g.lookAtObject(id);
            InputState in;
            in.screenW = 960; in.screenH = 544;
            in.cycleAttackMode = true;
            g.update(0.001f, in);
            if (!g.actionMenuOpenForTesting()) g.update(0.001f, in);
            shot(g, suffix);
            return g.actionMenuOpenForTesting();
        };
        const bool millMenu = openMenu(mill, "_menu_mill");
        std::string millIds, airIds;
        for (int id : g.productionOptionIds(mill)) millIds += std::to_string(id) + ",";
        const bool airMenu = openMenu(air, "_menu_air");
        for (int id : g.productionOptionIds(air)) airIds += std::to_string(id) + ",";
        // Like the Vita: select the fortress by pressing X on it, then Triangle.
        bool fortByPress = false;
        {
            g.clearSelectionForTesting();
            InputState none; none.screenW = 960; none.screenH = 544;
            if (g.actionMenuOpenForTesting()) { none.cycleAttackMode = true; g.update(0.001f, none); }
            g.lookAtObject(fort);
            g.update(0.001f, {});
            float fx = 0, fy = 0;
            g.objectScreenPosition(fort, 960, 544, fx, fy);
            for (float dy : {-60.0f, -30.0f, -10.0f}) {
                InputState press; press.screenW = 960; press.screenH = 544; press.cursorVisible = true;
                press.pointerX = fx; press.pointerY = fy + dy; press.selectPressed = true;
                g.update(0.001f, press);
                g.update(0.3f, {});
                InputState tri; tri.screenW = 960; tri.screenH = 544; tri.cursorVisible = true;
                tri.pointerX = fx; tri.pointerY = fy + dy; tri.cycleAttackMode = true;
                g.update(0.001f, tri);
                const bool open = g.actionMenuOpenForTesting();
                printf("  fort press dy %.0f selected %zu open %d\n", dy, g.selectedObjectIds().size(), open);
                fortByPress |= open;
                if (open) { g.update(0.001f, tri); }
            }
        }
        const bool fortMenu = openMenu(fort, "_menu_fort");
        std::string fortIds;
        for (int id : g.productionOptionIds(fort)) fortIds += std::to_string(id) + ",";
        airIds += " fort " + std::to_string(fortMenu) + " [" + fortIds + "]";
        report("menus", fortByPress && fortMenu && !fortIds.empty() && millMenu && millIds.find("50,") != std::string::npos && airMenu && !airIds.empty(),
               "mill " + std::to_string(millMenu) + " [" + millIds + "] air " + std::to_string(airMenu) +
                   " [" + airIds + "]");
    }
    // 23) Upgraded Food Proc Ctr (68 -> 129 at TL2) still takes food.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        g.researchTechnology(1, 1);
        g.spawnObjectForTesting(3, 109, 1, 44.0f, 6.0f);
        const uint32_t mill = g.spawnFoundationForTesting(3, 68, 1, 44.0f, 26.0f, {});
        g.completeFoundationForTesting(mill);
        const uint32_t worker = g.spawnObjectForTesting(3, 83, 1, 44.0f, 23.0f);
        const uint32_t bush = g.spawnObjectForTesting(0, 59, 0, 42.0f, 22.0f);
        g.update(1.0f / 30.0f, {});
        g.issueGatherForTesting(worker, bush);
        const float food0 = g.resource(1, 0);
        float nearest = 1e9f;
        for (int f = 0; f < 30 * 60; f++) {
            g.update(1.0f / 30.0f, {});
            nearest = std::min(nearest, std::abs(g.objectPosition(worker)[1] - 6.0f));
        }
        report("upgraded-drop-site", g.objectUnitId(mill) == 129 && g.resource(1, 0) > food0 + 5.0f && nearest > 8.0f,
               "mill unit " + std::to_string(g.objectUnitId(mill)) + " food +" +
                   std::to_string(g.resource(1, 0) - food0) + " nearestCC " + std::to_string(nearest));
    }
    // 24) Hunting: the worker kills the nerf, gathers its carcass; an
    // untouched carcass rots away.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        g.spawnObjectForTesting(3, 109, 1, 44.0f, 12.0f);
        const uint32_t worker = g.spawnObjectForTesting(3, 83, 1, 44.0f, 17.0f);
        const uint32_t nerf = g.spawnObjectForTesting(0, 594, 0, 44.0f, 19.0f);
        const uint32_t other = g.spawnObjectForTesting(0, 594, 0, 60.0f, 19.0f);
        g.update(1.0f / 30.0f, {});
        const bool ordered = g.issueGatherForTesting(worker, nerf);
        const float food0 = g.resource(1, 0);
        for (int f = 0; f < 30 * 60; f++) g.update(1.0f / 30.0f, {});
        const bool nerfDead = !g.objectActive(nerf);
        const bool gotFood = g.resource(1, 0) > food0 + 5.0f;
        g.damageObjectForTesting(other, 100);
        g.update(1.0f / 30.0f, {});
        g.update(1.0f / 30.0f, {});
        uint32_t carcass = 0;
        for (uint32_t id = other + 1; id < other + 400; id++)
            if (g.objectActive(id) && g.objectUnitId(id) == 595 &&
                std::abs(g.objectPosition(id)[0] - 60.0f) < 0.5f) carcass = id;
        const float c0 = carcass ? g.objectResourceAmount(carcass) : 0.0f;
        for (int f = 0; f < 30 * 20; f++) g.update(1.0f / 30.0f, {});
        const float c1 = carcass ? g.objectResourceAmount(carcass) : 0.0f;
        report("hunting", ordered && nerfDead && gotFood && carcass && c0 > 100.0f && c1 < c0 - 4.0f,
               "ordered " + std::to_string(ordered) + " dead " + std::to_string(nerfDead) + " food " +
                   std::to_string(gotFood) + " carcass " + std::to_string(carcass) + " " +
                   std::to_string(c0) + "->" + std::to_string(c1));
    }
    // 25) Animal Nursery: a captured nerf garrisons and produces food.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        const uint32_t nursery = g.spawnObjectForTesting(3, 319, 1, 44.0f, 20.0f);
        const uint32_t nerf = g.spawnObjectForTesting(0, 594, 1, 44.0f, 24.0f);
        g.update(1.0f / 30.0f, {});
        const bool entered = g.garrisonForTesting(nerf, nursery);
        for (int f = 0; f < 30 * 15; f++) g.update(1.0f / 30.0f, {});
        const float food0 = g.resource(1, 0);
        for (int f = 0; f < 30 * 30; f++) g.update(1.0f / 30.0f, {});
        const float gained = g.resource(1, 0) - food0;
        report("nursery", entered && g.garrisonedCount(nursery) == 1 && gained > 2.0f,
               "entered " + std::to_string(entered) + " inside " + std::to_string(g.garrisonedCount(nursery)) +
                   " food +" + std::to_string(gained));
    }
    // 26) Transports: a trooper boards an assault mech, rides, and unloads.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        const uint32_t mech = g.spawnObjectForTesting(3, 249, 1, 44.0f, 20.0f);
        const uint32_t trooper = g.spawnObjectForTesting(3, 460, 1, 44.0f, 23.0f);
        g.update(1.0f / 30.0f, {});
        const bool entered = g.garrisonForTesting(trooper, mech);
        for (int f = 0; f < 30 * 10; f++) g.update(1.0f / 30.0f, {});
        const size_t inside = g.garrisonedCount(mech);
        g.groupMoveForTesting({mech}, 52.0f, 20.0f);
        for (int f = 0; f < 30 * 30; f++) g.update(1.0f / 30.0f, {});
        const auto ride = g.objectPosition(trooper);
        const bool ejected = g.ejectGarrisonedUnitForTesting(mech, trooper);
        report("transport", entered && inside == 1 && std::abs(ride[0] - g.objectPosition(mech)[0]) < 0.01f &&
                                ride[0] > 48.0f && ejected,
               "entered " + std::to_string(entered) + " inside " + std::to_string(inside) + " ride " +
                   std::to_string(ride[0]) + " ejected " + std::to_string(ejected));
    }
    // 27) Blast damage: a heavy assault mech's shot splashes nearby troops;
    // a mech with a trooper on top backs off to its minimum range to fire.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        g.setDiplomacyForTesting(1, 2, 3);
        g.setDiplomacyForTesting(2, 1, 3);
        const uint32_t mech = g.spawnObjectForTesting(3, 277, 1, 40.0f, 30.0f);
        const uint32_t a = g.spawnObjectForTesting(3, 460, 2, 46.0f, 30.0f);
        const uint32_t b = g.spawnObjectForTesting(3, 460, 2, 46.0f, 30.5f);
        g.update(1.0f / 30.0f, {});
        g.setAttackModeForTesting(a, 3); g.setAttackModeForTesting(b, 3);
        const float hpB = g.objectHitPoints(b);
        g.issueAttackForTesting(mech, a);
        bool shotTaken = false;
        for (int f = 0; f < 30 * 12 && g.objectActive(a); f++) {
            g.update(1.0f / 30.0f, {});
            if (!shotTaken && g.projectileCountForTesting() > 0) {
                g.update(0.1f, {});
                g.lookAtObject(mech);
                shot(g, "_mech_bolt");
                shotTaken = true;
            }
        }
        const bool splashed = !g.objectActive(b) || g.objectHitPoints(b) < hpB;
        // Min range: an enemy right next to the mech.
        const uint32_t close = g.spawnObjectForTesting(3, 460, 2, g.objectPosition(mech)[0] + 0.7f,
                                                       g.objectPosition(mech)[1]);
        g.setAttackModeForTesting(close, 3);
        const float hpC = g.objectHitPoints(close);
        g.issueAttackForTesting(mech, close);
        for (int f = 0; f < 30 * 25 && g.objectActive(close) && g.objectHitPoints(close) >= hpC; f++)
            g.update(1.0f / 30.0f, {});
        const bool firedClose = !g.objectActive(close) || g.objectHitPoints(close) < hpC;
        // The direct target's footprint does not inflate the blast radius;
        // only the secondary candidate's own footprint reduces distance.
        const uint32_t hut = g.spawnObjectForTesting(3, 70, 2, 40.0f, 40.0f);
        const uint32_t guard = g.spawnObjectForTesting(3, 460, 2, 41.9f, 40.0f);
        g.setAttackModeForTesting(guard, 3);
        g.moveObjectForTesting(mech, 40.0f, 34.0f);
        const float hpG = g.objectHitPoints(guard);
        g.issueAttackForTesting(mech, hut);
        for (int f = 0; f < 30 * 20 && g.objectActive(guard) && g.objectHitPoints(guard) >= hpG; f++)
            g.update(1.0f / 30.0f, {});
        const bool besideHit = !g.objectActive(guard) || g.objectHitPoints(guard) < hpG;
        report("blast-minrange", splashed && firedClose && !besideHit,
               "splash " + std::to_string(splashed) + " closeHit " + std::to_string(firedClose) +
                   " primaryFootprintInflated " + std::to_string(besideHit));
    }
    // 28) A worker-built fortress at Tech Level 3 opens its menu (Empire),
    // on the Vita's compact test map.
    {
        Game g(assets);
        if (!g.initCompactTestMap(0x5A17u, 64, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        g.researchTechnology(1, 1);
        g.researchTechnology(1, 2);
        g.setResourceForTesting(1, 1, 5000); g.setResourceForTesting(1, 2, 5000);
        g.setResourceForTesting(1, 3, 5000); g.setResourceForTesting(1, 0, 5000);
        const uint32_t worker = g.spawnObjectForTesting(1, 83, 1, 30.0f, 12.0f);
        g.update(1.0f / 30.0f, {});
        float fx0 = -1, fy0 = -1;
        for (int y = 8; y < 56 && fx0 < 0; y += 2)
            for (int x = 8; x < 56 && fx0 < 0; x += 2) {
                bool clear = true;
                for (int dy = -3; dy <= 3 && clear; dy++)
                    for (int dx = -3; dx <= 3 && clear; dx++)
                        clear = g.positionPassableForTesting(worker, x + dx + 0.5f, y + dy + 0.5f) &&
                                g.terrainAtForTesting(x + dx, y + dy) != 1;
                if (clear) { fx0 = x + 0.5f; fy0 = y + 0.5f; }
            }
        g.moveObjectForTesting(worker, fx0 - 4.0f, fy0);
        const uint32_t fort = g.spawnFoundationForTesting(1, 1211, 1, fx0, fy0, {worker}); // the build-menu Fortress
        for (int f = 0; f < 30 * 400; f++) {
            g.update(1.0f / 30.0f, {});
            bool building = false;
            for (uint32_t id : g.underConstructionObjectIds()) building |= id == fort;
            if (!building) break;
        }
        std::string ids;
        for (int id : g.productionOptionIds(fort)) ids += std::to_string(id) + ",";
        std::string research;
        for (int id : g.researchOptionIds(fort)) research += std::to_string(id) + ",";
        g.clearSelectionForTesting();
        g.selectObjectForTesting(fort);
        InputState tri; tri.screenW = 960; tri.screenH = 544; tri.cycleAttackMode = true;
        g.update(0.001f, tri);
        const bool open = g.actionMenuOpenForTesting();
        g.lookAtObject(fort);
        shot(g, "_fort_tl3");
        if (open) { g.update(0.001f, tri); }
        const bool garrisoned = g.garrisonForTesting(worker, fort);
        for (int f = 0; f < 30 * 10; f++) g.update(1.0f / 30.0f, {});
        report("fortress-tl3", open && !ids.empty() && g.objectUnitId(fort) == 82 && garrisoned &&
                                   g.garrisonedCount(fort) == 1,
               "unit " + std::to_string(g.objectUnitId(fort)) + " open " + std::to_string(open) + " units [" + ids +
                   "] research [" + research + "]");
    }
    // 29) Empire heavy assault mech (min range 3, 1 after Walker Research)
    // attacked by a worker: it backs off and shoots back.
    for (int researched = 0; researched < 2; researched++) {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        g.setDiplomacyForTesting(1, 2, 3);
        g.setDiplomacyForTesting(2, 1, 3);
        if (researched) g.researchTechnology(1, 164);
        const uint32_t mech = g.spawnObjectForTesting(1, 603, 1, 40.0f, 30.0f);
        const uint32_t worker = g.spawnObjectForTesting(3, 83, 2, 41.0f, 30.0f);
        g.update(1.0f / 30.0f, {});
        g.issueAttackForTesting(worker, mech);
        const float hp0 = g.objectHitPoints(worker);
        const auto p0 = g.objectPosition(mech);
        float moved = 0;
        (void)hp0;
        printf("   min range %.2f\n", g.minimumRangeForTesting(mech));
        int f = 0;
        for (; f < 30 * 60 && g.objectActive(worker); f++) {
            g.update(1.0f / 30.0f, {});
            const auto p = g.objectPosition(mech);
            moved = std::max(moved, std::hypot(p[0] - p0[0], p[1] - p0[1]));
            if (f % 150 == 0)
                printf("   t=%d mech %.2f,%.2f hp %.0f sp %.0f worker %.2f,%.2f hp %.0f\n", f / 30, p[0], p[1],
                       g.objectHitPoints(mech), g.objectShieldPoints(mech), g.objectPosition(worker)[0],
                       g.objectPosition(worker)[1], g.objectHitPoints(worker));
        }
        const bool hit = !g.objectActive(worker);
        printf("  mech %s\n", g.describeObjectForTesting(mech).substr(0, 220).c_str());
        report(researched ? "minrange-melee-researched" : "minrange-melee", hit,
               "hit " + std::to_string(hit) + " moved " + std::to_string(moved));
    }
    // 30) Workers repair mechanical units and droid infantry, not organic
    // troopers. Droid identity follows the unit master after conversion.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        for (int r = 0; r < 4; r++) g.setResourceForTesting(1, r, 1000);
        const uint32_t worker = g.spawnObjectForTesting(1, 83, 1, 40.0f, 30.0f);
        const uint32_t mech = g.spawnObjectForTesting(1, 603, 1, 42.0f, 30.0f);
        const uint32_t trooper = g.spawnObjectForTesting(1, 460, 1, 40.0f, 33.0f);
        const uint32_t tradeFederationDroid =
            g.spawnObjectForTesting(
                5, 172, 1, 43.0f, 32.0f);
        const uint32_t confederacyDroid =
            g.spawnObjectForTesting(
                8, 1552, 1, 43.0f, 34.0f);
        g.update(1.0f / 30.0f, {});
        g.damageObjectForTesting(mech, 100);
        g.damageObjectForTesting(trooper, 10);
        g.damageObjectForTesting(
            tradeFederationDroid, 10);
        g.damageObjectForTesting(
            confederacyDroid, 10);
        const bool trooperRefused = !g.issueRepairForTesting(worker, trooper);
        const bool tradeFederationAccepted =
            g.issueRepairForTesting(
                worker,
                tradeFederationDroid);
        const bool confederacyAccepted =
            g.issueRepairForTesting(
                worker,
                confederacyDroid);
        const bool ordered = g.issueRepairForTesting(worker, mech);
        const float hp0 = g.objectHitPoints(mech);
        for (int f = 0; f < 30 * 20; f++) g.update(1.0f / 30.0f, {});
        report("repair-mech", trooperRefused &&
                                  tradeFederationAccepted &&
                                  confederacyAccepted &&
                                  ordered &&
                                  g.objectHitPoints(mech) > hp0 + 5.0f,
               "trooperRefused " + std::to_string(trooperRefused) +
                   " tfDroid " + std::to_string(tradeFederationAccepted) +
                   " confedDroid " + std::to_string(confederacyAccepted) +
                   " ordered " + std::to_string(ordered) +
                   " hp " + std::to_string(hp0) + "->" + std::to_string(g.objectHitPoints(mech)));
    }
    // 31) Worker jobs run for every player. Local ownership controls input
    // and UI, not scenario or AI simulation.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        const uint32_t worker =
            g.spawnObjectForTesting(
                3, 83, 2, 40.0f, 30.0f);
        const uint32_t tree =
            g.spawnObjectForTesting(
                0, 348, 0, 40.5f, 30.0f);
        g.update(1.0f / 30.0f, {});
        const bool ordered =
            g.issueGatherForTesting(
                worker, tree);
        const float amount0 =
            g.objectResourceAmount(tree);
        for (int f = 0; f < 30 * 30; f++)
            g.update(1.0f / 30.0f, {});
        report(
            "nonlocal-worker",
            ordered &&
                g.objectResourceAmount(tree) <
                    amount0 - 0.01f &&
                g.objectCarriedAmount(worker) >
                    0.01f,
            "ordered " +
                std::to_string(ordered) +
                " tree " +
                std::to_string(amount0) +
                "->" +
                std::to_string(
                    g.objectResourceAmount(tree)) +
                " carried " +
                std::to_string(
                    g.objectCarriedAmount(worker)));
    }
    // 32) Continued work uses the worker master's five-tile search radius.
    // Farms keep their builder ahead of nearby foundations, while ordinary
    // completed buildings chain only to foundations inside that radius.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        const uint32_t worker =
            g.spawnObjectForTesting(
                3, 83, 1, 30.0f, 30.0f);
        g.update(1.0f / 30.0f, {});
        const uint32_t completed =
            g.spawnFoundationForTesting(
                3, 70, 1, 31.0f, 30.0f,
                {worker});
        const uint32_t nearby =
            g.spawnFoundationForTesting(
                3, 70, 1, 35.0f, 30.0f,
                {});
        g.completeFoundationForTesting(
            completed);
        g.update(0.001f, {});
        const bool chainedNearby =
            g.objectBuildingTarget(
                worker, nearby);

        Game far(assets);
        if (!far.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        far.setLocalPlayerForTesting(1);
        const uint32_t farWorker =
            far.spawnObjectForTesting(
                3, 83, 1, 30.0f, 30.0f);
        far.update(1.0f / 30.0f, {});
        const uint32_t farCompleted =
            far.spawnFoundationForTesting(
                3, 70, 1, 31.0f, 30.0f,
                {farWorker});
        const uint32_t outside =
            far.spawnFoundationForTesting(
                3, 70, 1, 38.0f, 30.0f,
                {});
        far.completeFoundationForTesting(
            farCompleted);
        far.update(0.001f, {});
        const bool ignoredOutside =
            !far.objectBuildingTarget(
                farWorker, outside);

        Game farmPriority(assets);
        if (!farmPriority.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        farmPriority.setLocalPlayerForTesting(1);
        const uint32_t farmer =
            farmPriority.spawnObjectForTesting(
                3, 83, 1, 30.0f, 30.0f);
        farmPriority.update(
            1.0f / 30.0f, {});
        const uint32_t farm =
            farmPriority.spawnFoundationForTesting(
                3, 50, 1, 31.0f, 30.0f,
                {farmer});
        const uint32_t otherFoundation =
            farmPriority.spawnFoundationForTesting(
                3, 70, 1, 33.0f, 30.0f,
                {});
        farmPriority.completeFoundationForTesting(
            farm);
        farmPriority.update(0.001f, {});
        const bool farmFirst =
            farmPriority.objectGatheringTarget(
                farmer, farm) &&
            !farmPriority.objectBuildingTarget(
                farmer, otherFoundation);
        report(
            "worker-continuation",
            chainedNearby && ignoredOutside &&
                farmFirst,
            "near " +
                std::to_string(chainedNearby) +
                " outside " +
                std::to_string(ignoredOutside) +
                " farmFirst " +
                std::to_string(farmFirst));
    }
    // Building placement uses DAT clearance, terrain requirements and hill
    // modes across the complete snapped footprint.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) {
            fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        g.setLocalPlayerForTesting(1);
        for (int y = 74; y <= 90; y++)
            for (int x = 74; x <= 90; x++)
                g.setTerrainForTesting(x, y, 0);
        const auto farmSnap =
            g.snappedBuildingPositionForTesting(
                3, 50, 80.2f, 80.2f);
        const auto centerSnap =
            g.snappedBuildingPositionForTesting(
                3, 109, 80.2f, 80.2f);
        const bool parity =
            std::fabs(farmSnap[0] - 80.5f) <
                0.001f &&
            std::fabs(farmSnap[1] - 80.5f) <
                0.001f &&
            std::fabs(centerSnap[0] - 80.0f) <
                0.001f &&
            std::fabs(centerSnap[1] - 80.0f) <
                0.001f;
        const bool edgeClearance =
            !g.placementValidForTesting(
                3, 109, 1.1f, 80.0f) &&
            !g.placementValidForTesting(
                3, 665, 1.1f, 80.0f);
        const bool flatValid =
            g.placementValidForTesting(
                3, 109, 80.0f, 80.0f);
        g.setTerrainForTesting(78, 78, 1);
        const bool completeFootprint =
            !g.placementValidForTesting(
                3, 109, 80.0f, 80.0f);
        g.setTerrainForTesting(78, 78, 0);
        g.setCornerElevationForTesting(
            80, 80, 1);
        const bool strictHill =
            !g.placementValidForTesting(
                3, 109, 80.0f, 80.0f);
        const bool gentleHill =
            g.placementValidForTesting(
                3, 68, 80.0f, 80.0f);
        const int gentleFailure =
            g.placementFailureForTesting();
        g.setCornerElevationForTesting(
            80, 80, 2);
        const bool steepHill =
            !g.placementValidForTesting(
                3, 68, 80.0f, 80.0f);
        report(
            "building-placement-footprint",
            parity && edgeClearance && flatValid &&
                completeFootprint && strictHill &&
                gentleHill && steepHill,
            "parity " + std::to_string(parity) +
                " edge " +
                std::to_string(edgeClearance) +
                " footprint " +
                std::to_string(
                    completeFootprint) +
                " hills " +
                std::to_string(strictHill) + "/" +
                std::to_string(gentleHill) + "/" +
                std::to_string(steepHill) +
                " reason " +
                std::to_string(gentleFailure));
    }
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) {
            fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        g.setLocalPlayerForTesting(1);
        for (int y = 76; y <= 84; y++)
            for (int x = 76; x <= 84; x++)
                g.setTerrainForTesting(x, y, 1);
        const bool needsShore =
            !g.placementValidForTesting(
                3, 45, 80.5f, 80.5f);
        for (int tile = 78; tile <= 82;
             tile++) {
            g.setTerrainForTesting(tile, 78, 2);
            g.setTerrainForTesting(tile, 82, 2);
        }
        for (int tile = 79; tile <= 81;
             tile++) {
            g.setTerrainForTesting(78, tile, 2);
            g.setTerrainForTesting(82, tile, 2);
        }
        const bool shoreValid =
            g.placementValidForTesting(
                3, 45, 80.5f, 80.5f);
        const int shoreFailure =
            g.placementFailureForTesting();
        g.setTerrainForTesting(79, 79, 0);
        const bool needsWaterFootprint =
            !g.placementValidForTesting(
                3, 45, 80.5f, 80.5f);
        report(
            "shipyard-placement-terrain",
            needsShore && shoreValid &&
                needsWaterFootprint,
            "shore " +
                std::to_string(needsShore) + "/" +
                std::to_string(shoreValid) +
                " water " +
                std::to_string(
                    needsWaterFootprint) +
                " reason " +
                std::to_string(shoreFailure));
    }
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) {
            fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        g.setTerrainForTesting(70, 70, 0);
        const uint32_t foundation =
            g.spawnFoundationForTesting(
                3, 70, 1, 70.0f, 70.0f, {});
        report(
            "foundation-terrain",
            foundation != 0 &&
                g.terrainAtForTesting(70, 70) ==
                    27,
            "foundation " +
                std::to_string(foundation) +
                " terrain " +
                std::to_string(
                    g.terrainAtForTesting(70, 70)));
    }
    // Unit command state persists through movement/combat and Stop clears it.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        g.setDiplomacyForTesting(1, 2, 3);
        g.setDiplomacyForTesting(2, 1, 3);
        const uint32_t patrol =
            g.spawnObjectForTesting(
                3, 460, 1, 30.0f, 30.0f);
        const bool patrolIssued =
            g.issuePatrolForTesting(
                patrol, 36.0f, 30.0f);
        for (int frame = 0; frame < 30 * 7; frame++)
            g.update(1.0f / 30.0f, {});
        const float patrolDistance =
            std::abs(
                g.objectPosition(patrol)[0] -
                30.0f);
        const bool stopped =
            g.stopUnitForTesting(patrol);
        const auto stoppedAt =
            g.objectPosition(patrol);
        for (int frame = 0; frame < 30 * 2; frame++)
            g.update(1.0f / 30.0f, {});
        const bool stopHeld =
            !g.patrolActiveForTesting(patrol) &&
            std::abs(
                g.objectPosition(patrol)[0] -
                stoppedAt[0]) < 0.2f;

        const uint32_t guarded =
            g.spawnObjectForTesting(
                3, 460, 1, 45.0f, 30.0f);
        const uint32_t guard =
            g.spawnObjectForTesting(
                3, 460, 1, 42.0f, 30.0f);
        const uint32_t enemy =
            g.spawnObjectForTesting(
                3, 460, 2, 49.0f, 30.0f);
        const float enemyHp =
            g.objectHitPoints(enemy);
        const bool guardIssued =
            g.issueGuardForTesting(
                guard, guarded);
        for (int frame = 0; frame < 30 * 8; frame++)
            g.update(1.0f / 30.0f, {});
        const bool guardEngaged =
            g.objectHitPoints(enemy) < enemyHp &&
            g.guardTargetForTesting(guard) ==
                guarded;

        const uint32_t leader =
            g.spawnObjectForTesting(
                3, 460, 1, 24.0f, 45.0f);
        const uint32_t follower =
            g.spawnObjectForTesting(
                3, 460, 1, 16.0f, 45.0f);
        const bool followIssued =
            g.issueFollowForTesting(
                follower, leader);
        for (int frame = 0; frame < 30 * 5; frame++)
            g.update(1.0f / 30.0f, {});
        const auto leaderPosition =
            g.objectPosition(leader);
        const auto followerPosition =
            g.objectPosition(follower);
        const float followDx =
            leaderPosition[0] -
            followerPosition[0];
        const float followDy =
            leaderPosition[1] -
            followerPosition[1];
        const bool followed =
            followDx * followDx +
                    followDy * followDy <
                25.0f &&
            g.followTargetForTesting(follower) ==
                leader;
        report(
            "unit-commands",
            patrolIssued && patrolDistance > 1.0f &&
                stopped && stopHeld &&
                guardIssued && guardEngaged &&
                followIssued && followed,
            "patrol " +
                std::to_string(patrolDistance) +
                " stop " +
                std::to_string(stopHeld) +
                " guard " +
                std::to_string(guardEngaged) +
                " follow " +
                std::to_string(followed));
    }
    // A production rally target may be any compatible building or transport.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        const uint32_t producer =
            g.spawnObjectForTesting(
                3, 87, 1, 30.0f, 16.0f);
        const uint32_t building =
            g.spawnObjectForTesting(
                3, 109, 1, 40.0f, 16.0f);
        g.setGatherPointForTesting(
            producer, 40.0f, 16.0f,
            building);
        const bool buildingQueued =
            g.queueUnitForTesting(
                producer, 460);
        for (int frame = 0; frame < 30 * 25; frame++)
            g.update(1.0f / 30.0f, {});
        const bool ralliedToBuilding =
            g.garrisonedCount(building) == 1;

        const uint32_t transport =
            g.spawnObjectForTesting(
                3, 249, 1, 46.0f, 16.0f);
        g.setGatherPointForTesting(
            producer, 46.0f, 16.0f,
            transport);
        const bool transportQueued =
            g.queueUnitForTesting(
                producer, 460);
        for (int frame = 0; frame < 30 * 35; frame++)
            g.update(1.0f / 30.0f, {});
        const bool ralliedToTransport =
            g.garrisonedCount(transport) == 1;
        report(
            "production-garrison-rally",
            buildingQueued &&
                ralliedToBuilding &&
                transportQueued &&
                ralliedToTransport,
            "building " +
                std::to_string(
                    ralliedToBuilding) +
                " transport " +
                std::to_string(
                    ralliedToTransport));
    }
    // Passive livestock are only valid explicit targets. Hostile Gaia
    // predators acquire enemies and provoke retaliation.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        g.setDiplomacyForTesting(1, 2, 3);
        g.setDiplomacyForTesting(2, 1, 3);
        const uint32_t trooper =
            g.spawnObjectForTesting(
                3, 460, 1, 72.0f, 72.0f);
        const uint32_t nerf =
            g.spawnObjectForTesting(
                0, 594, 2, 76.5f, 72.0f);
        const float nerfHp =
            g.objectHitPoints(nerf);
        step(g, 3.0f);
        const bool ignored =
            g.attackTargetForTesting(trooper) == 0 &&
            std::abs(
                g.objectHitPoints(nerf) -
                nerfHp) < 0.01f;
        const bool explicitAttack =
            g.issueAttackForTesting(
                trooper, nerf);
        step(g, 2.0f);
        const bool explicitlyDamaged =
            !g.objectActive(nerf) ||
            g.objectHitPoints(nerf) < nerfHp;

        Game predatorGame(assets);
        if (!predatorGame.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        predatorGame.setLocalPlayerForTesting(1);
        const uint32_t defender =
            predatorGame.spawnObjectForTesting(
                3, 460, 1, 72.0f, 72.0f);
        const uint32_t nexu =
            predatorGame.spawnObjectForTesting(
                0, 1461, 0, 73.0f, 72.0f);
        step(predatorGame, 3.0f);
        const bool predatorAttacked =
            predatorGame.attackTargetForTesting(
                nexu) == defender ||
            predatorGame.objectHitPoints(
                defender) <
                predatorGame.objectMaxHitPoints(
                    defender);
        const bool defenderRetaliated =
            predatorGame.attackTargetForTesting(
                defender) == nexu ||
            predatorGame.objectHitPoints(
                nexu) <
                predatorGame.objectMaxHitPoints(
                    nexu);
        report(
            "animal-targeting",
            ignored && explicitAttack &&
                explicitlyDamaged &&
                predatorAttacked &&
                defenderRetaliated,
            "ignored " +
                std::to_string(ignored) +
                " explicit " +
                std::to_string(
                    explicitlyDamaged) +
                " predator " +
                std::to_string(
                    predatorAttacked) +
                " retaliation " +
                std::to_string(
                    defenderRetaliated));
    }
    // Capturing livestock plays capsheep.wav once, and uncommanded workers
    // and animals do not perform the old sample wander.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        step(g, 0.1f);
        std::vector<int> sounds;
        g.setInterfaceSoundPlayer(
            [&](int soundId) {
                sounds.push_back(soundId);
            });
        const uint32_t worker =
            g.spawnObjectForTesting(
                3, 83, 1, 82.0f, 82.0f);
        const uint32_t nerf =
            g.spawnObjectForTesting(
                0, 594, 0, 83.0f, 82.0f);
        const auto workerStart =
            g.objectPosition(worker);
        const auto nerfStart =
            g.objectPosition(nerf);
        step(g, 5.0f);
        const size_t captureSounds =
            (size_t)std::count(
                sounds.begin(), sounds.end(),
                50355);
        const auto workerEnd =
            g.objectPosition(worker);
        const auto nerfEnd =
            g.objectPosition(nerf);
        const bool stationary =
            std::hypot(
                workerEnd[0] -
                    workerStart[0],
                workerEnd[1] -
                    workerStart[1]) < 0.01f &&
            std::hypot(
                nerfEnd[0] -
                    nerfStart[0],
                nerfEnd[1] -
                    nerfStart[1]) < 0.01f;
        report(
            "animal-capture-and-idle",
            g.objectPlayer(nerf) == 1 &&
                captureSounds == 1 &&
                stationary,
            "owner " +
                std::to_string(
                    g.objectPlayer(nerf)) +
                " sound " +
                std::to_string(
                    captureSounds) +
                " stationary " +
                std::to_string(
                    stationary));
    }
    // A construction command immediately replaces a worker's gather job.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        const uint32_t worker =
            g.spawnObjectForTesting(
                3, 83, 1, 30.0f, 70.0f);
        const uint32_t carbon =
            g.spawnObjectForTesting(
                0, 348, 0, 30.5f, 70.0f);
        const bool gathering =
            g.issueGatherForTesting(
                worker, carbon);
        step(g, 0.5f);
        const uint32_t foundation =
            g.spawnFoundationForTesting(
                3, 109, 1,
                35.0f, 70.0f,
                {worker});
        const bool retasked =
            foundation != 0 &&
            !g.objectGatheringTarget(
                worker, carbon) &&
            g.objectBuildingTarget(
                worker, foundation);
        step(g, 2.0f);
        report(
            "gather-to-construction",
            gathering && retasked &&
                g.objectBuildingTarget(
                    worker, foundation),
            "gather " +
                std::to_string(gathering) +
                " retasked " +
                std::to_string(retasked));
    }
    // Each Tech Level replaces the current Command Center appearance and
    // plays the original architecture-upgrade cue.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        std::vector<int> sounds;
        g.setInterfaceSoundPlayer(
            [&](int soundId) {
                sounds.push_back(soundId);
            });
        const uint32_t commandCenter =
            g.spawnObjectForTesting(
                3, 109, 1, 60.0f, 70.0f);
        const int expected[] = {
            71, 141, 142};
        bool advanced = true;
        for (int technology = 1;
             technology <= 3;
             ++technology) {
            advanced =
                g.queueTechnologyForTesting(
                    commandCenter,
                    technology) &&
                advanced;
            step(g, 0.2f);
            advanced =
                g.objectUnitId(
                    commandCenter) ==
                    expected[
                        technology - 1] &&
                advanced;
        }
        const size_t levelSounds =
            (size_t)std::count(
                sounds.begin(), sounds.end(),
                50325);
        report(
            "tech-level-command-center",
            advanced && levelSounds == 3,
            "unit " +
                std::to_string(
                    g.objectUnitId(
                        commandCenter)) +
                " sounds " +
                std::to_string(
                    levelSounds));
    }
    // Animal Nursery occupants use the generic clickable passenger strip,
    // and its displayed production rate tracks the animals still inside.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        const uint32_t nursery =
            g.spawnObjectForTesting(
                3, 319, 1, 45.0f, 70.0f);
        const uint32_t first =
            g.spawnObjectForTesting(
                0, 594, 1, 45.0f, 74.0f);
        const uint32_t second =
            g.spawnObjectForTesting(
                0, 594, 1, 46.0f, 74.0f);
        const bool ordered =
            g.garrisonForTesting(
                first, nursery) &&
            g.garrisonForTesting(
                second, nursery);
        for (int frame = 0;
             frame < 900 &&
             g.garrisonedCount(nursery) < 2;
             ++frame)
            g.update(
                1.0f / 30.0f, {});
        const float fullRate =
            g.animalNurseryFoodRateForTesting(
                nursery);
        const float foodBefore =
            g.resource(1, 0);
        step(g, 10.0f);
        const float foodGained =
            g.resource(1, 0) -
            foodBefore;
        g.selectObjectForTesting(nursery);
        g.render(renderer, 960, 544);
        InputState click;
        click.screenW = 960;
        click.screenH = 544;
        click.cursorVisible = true;
        click.pointerX = 477.0f;
        click.pointerY =
            544.0f - 112.0f + 29.0f;
        click.selectPressed = true;
        g.update(0.001f, click);
        const float reducedRate =
            g.animalNurseryFoodRateForTesting(
                nursery);
        report(
            "nursery-rate-and-eject",
            ordered &&
                g.garrisonedCount(
                    nursery) == 1 &&
                fullRate > 0.0f &&
                std::abs(
                    reducedRate * 2.0f -
                    fullRate) < 0.001f &&
                std::abs(
                    foodGained -
                    fullRate * 10.0f) < 0.1f,
            "inside " +
                std::to_string(
                    g.garrisonedCount(
                        nursery)) +
                " rate " +
                std::to_string(fullRate) +
                " -> " +
                std::to_string(
                    reducedRate) +
                " food +" +
                std::to_string(foodGained));
    }
    // Critically damaged garrison buildings evacuate their occupants, reject
    // new entries, and unlock after repairs lift them above the threshold.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        const uint32_t center =
            g.spawnObjectForTesting(
                3, 109, 1, 44.0f, 68.0f);
        const uint32_t first =
            g.spawnObjectForTesting(
                3, 83, 1, 43.0f, 73.0f);
        const uint32_t second =
            g.spawnObjectForTesting(
                3, 83, 1, 45.0f, 73.0f);
        const bool ordered =
            g.garrisonForTesting(
                first, center) &&
            g.garrisonForTesting(
                second, center);
        for (int frame = 0;
             frame < 900 &&
             g.garrisonedCount(center) < 2;
             ++frame)
            g.update(
                1.0f / 30.0f, {});
        const int maxHealth =
            g.objectMaxHealthForTesting(
                center);
        const bool damaged =
            maxHealth > 0 &&
            g.damageObjectForTesting(
                center,
                maxHealth -
                    std::max(
                        1, maxHealth / 5),
                0);
        step(g, 0.25f);
        const bool evacuated =
            g.garrisonedCount(center) == 0;
        const bool rejected =
            !g.canGarrisonForTesting(
                first, center);
        for (int resource = 0;
             resource < 4; ++resource)
            g.setResourceForTesting(
                1, resource, 10000.0f);
        const bool repairing =
            g.issueRepairForTesting(
                first, center);
        for (int frame = 0;
             frame < 1800 &&
             g.objectHealthForTesting(
                 center) * 4 <=
                 maxHealth;
             ++frame)
            g.update(
                1.0f / 30.0f, {});
        const bool unlocked =
            g.canGarrisonForTesting(
                second, center);
        report(
            "critical-garrison-evacuation",
            ordered && damaged &&
                evacuated && rejected &&
                repairing && unlocked,
            "inside " +
                std::to_string(
                    g.garrisonedCount(
                        center)) +
                " hp " +
                std::to_string(
                    g.objectHealthForTesting(
                        center)) +
                "/" +
                std::to_string(maxHealth) +
                " rejected/unlocked " +
                std::to_string(rejected) +
                "/" +
                std::to_string(unlocked));
    }
    // The AI shelters nearby workers while its Command Center is under
    // attack, then releases them after five safe seconds.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        g.setDiplomacyForTesting(3, 1, 3);
        g.setDiplomacyForTesting(1, 3, 3);
        const uint32_t center =
            g.spawnObjectForTesting(
                3, 109, 3, 66.0f, 68.0f);
        for (int offset = -1;
             offset <= 1; ++offset)
            g.spawnObjectForTesting(
                3, 83, 3,
                66.0f + offset * 1.5f,
                73.0f);
        const uint32_t attacker =
            g.spawnObjectForTesting(
                1, 460, 1, 66.0f, 76.0f);
        const char *defenseScript =
            "(defrule\n"
            "  (true)\n"
            "=>\n"
            "  (disable-self))\n";
        const bool loaded =
            g.loadAiSourceForTesting(
                3, "shelter-test.per",
                defenseScript, {}, &err);
        const bool attacked =
            g.issueAttackForTesting(
                attacker, center);
        bool sheltered = false;
        for (int frame = 0;
             frame < 600 && !sheltered;
             ++frame) {
            g.update(
                1.0f / 30.0f, {});
            sheltered =
                g.garrisonedCount(
                    center) > 0;
        }
        g.damageObjectForTesting(
            attacker, 100000, center);
        step(g, 6.0f);
        const bool released =
            g.garrisonedCount(center) == 0;
        report(
            "ai-worker-shelter",
            loaded && attacked &&
                sheltered && released,
            "sheltered/released " +
                std::to_string(sheltered) +
                "/" +
                std::to_string(released));
    }
    // Formation members retain their walking state when they consume the
    // short moving-slot path each frame.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        g.setLocalPlayerForTesting(1);
        std::vector<uint32_t> mechs;
        for (int row = 0; row < 3; ++row)
            for (int column = 0;
                 column < 4; ++column)
                mechs.push_back(
                    g.spawnObjectForTesting(
                        3, 277, 1,
                        18.0f +
                            column * 1.5f,
                        18.0f +
                            row * 1.5f));
        g.selectObjectsForTesting(mechs);
        g.groupMoveForTesting(
            mechs, 72.0f, 72.0f);
        size_t fewestWalking =
            mechs.size();
        for (int frame = 0;
             frame < 60; ++frame) {
            g.update(
                1.0f / 30.0f, {});
            fewestWalking =
                std::min(
                    fewestWalking,
                    g.selectedMovingObjectCount());
        }
        report(
            "march-walking-animation",
            fewestWalking == mechs.size(),
            "walking " +
                std::to_string(
                    fewestWalking) +
                "/" +
                std::to_string(
                    mechs.size()));
    }
    // The Vita compact map is two land masses divided by a full-height
    // channel. Ground units cannot route across it, aircraft can, and the
    // shallow-water/shore bands accept Shipyards on both coasts.
    {
        Game g(assets);
        if (!g.initCompactTestMap(
                0x5A17u, 96, &err)) {
            fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        const uint32_t worker =
            g.spawnObjectForTesting(
                1, 83, 1, 38.0f, 48.0f);
        const uint32_t fighter =
            g.spawnObjectForTesting(
                1, 773, 1, 38.0f, 44.0f);
        g.groupMoveForTesting(
            {worker}, 58.0f, 48.0f);
        g.groupMoveForTesting(
            {fighter}, 58.0f, 44.0f);
        step(g, 20.0f);
        const auto workerPosition =
            g.objectPosition(worker);
        const auto fighterPosition =
            g.objectPosition(fighter);
        float westShipyardX = -1.0f;
        float eastShipyardX = -1.0f;
        for (float x = 42.0f;
             x <= 54.0f; x += 0.5f) {
            if (!g.placementValidForTesting(
                    1, 45, x, 20.0f))
                continue;
            if (x < 48.0f &&
                westShipyardX < 0.0f)
                westShipyardX = x;
            if (x > 48.0f &&
                eastShipyardX < 0.0f)
                eastShipyardX = x;
        }
        const bool fullHeightWater =
            g.terrainAtForTesting(47, 0) == 22 &&
            g.terrainAtForTesting(48, 95) == 22;
        const bool abundantResources =
            g.aiObjectCountForTesting(
                0, "OBJ-VEGETABLE") >= 24 &&
            g.aiObjectCountForTesting(
                0, "OBJ-BULLION") >= 16 &&
            g.aiObjectCountForTesting(
                0, "OBJ-MINERAL") >= 16 &&
            g.aiObjectCountForTesting(
                0, "OBJ-TIMBERA") >= 48;
        report(
            "compact-island-channel",
            workerPosition[0] < 47.0f &&
                fighterPosition[0] > 53.0f &&
                westShipyardX >= 0.0f &&
                eastShipyardX >= 0.0f &&
                fullHeightWater &&
                abundantResources,
            "ground x " +
                std::to_string(workerPosition[0]) +
                " air x " +
                std::to_string(fighterPosition[0]) +
                " shipyards " +
                std::to_string(westShipyardX) +
                "/" +
                std::to_string(eastShipyardX) +
                " resources " +
                std::to_string(
                    abundantResources));
    }
    // Civilization tech-tree effects control research menus. The Empire's
    // effect disables Shield Modifications; the Rebels retain it, and its
    // resource attribute shields every eligible fighter/bomber class.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) {
            fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        const uint32_t empireAirbase =
            g.spawnObjectForTesting(
                1, 317, 1, 28.0f, 18.0f);
        const uint32_t rebelAirbase =
            g.spawnObjectForTesting(
                3, 317, 2, 68.0f, 18.0f);
        for (int technology : {1, 2, 3}) {
            g.researchTechnology(
                1, technology);
            g.researchTechnology(
                2, technology);
        }
        g.update(1.0f / 30.0f, {});
        const std::vector<int> empireResearch =
            g.researchOptionIds(empireAirbase);
        const std::vector<int> rebelResearch =
            g.researchOptionIds(rebelAirbase);
        const bool empireHidden =
            std::find(
                empireResearch.begin(),
                empireResearch.end(), 73) ==
            empireResearch.end();
        const bool rebelVisible =
            std::find(
                rebelResearch.begin(),
                rebelResearch.end(), 73) !=
            rebelResearch.end();
        const uint32_t fighter =
            g.spawnObjectForTesting(
                3, 779, 2, 64.0f, 26.0f);
        const uint32_t bomber =
            g.spawnObjectForTesting(
                3, 769, 2, 66.0f, 26.0f);
        const bool initiallyUnshielded =
            !g.objectShielded(fighter) &&
            !g.objectShielded(bomber);
        g.researchTechnology(2, 73);
        const bool upgradedShielded =
            g.objectShielded(fighter) &&
            g.objectShielded(bomber);
        report(
            "civilization-air-research",
            empireHidden && rebelVisible &&
                initiallyUnshielded &&
                upgradedShielded,
            "empire hidden " +
                std::to_string(empireHidden) +
                " rebel visible " +
                std::to_string(rebelVisible) +
                " shield " +
                std::to_string(
                    initiallyUnshielded) +
                "->" +
                std::to_string(
                    upgradedShielded));
    }
    // Airbases and Shipyards expose and complete their civilization-specific
    // transports through the ordinary DAT-driven production path.
    {
        Game g(assets);
        if (!g.initCompactTestMap(
                0x5A17u, 96, &err)) {
            fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        g.setLocalPlayerForTesting(1);
        const uint32_t empireAirbase =
            g.spawnObjectForTesting(
                1, 317, 1, 28.0f, 18.0f);
        const uint32_t rebelAirbase =
            g.spawnObjectForTesting(
                3, 317, 2, 68.0f, 18.0f);
        const uint32_t empireShipyard =
            g.spawnObjectForTesting(
                1, 45, 1, 44.0f, 20.0f);
        const uint32_t rebelShipyard =
            g.spawnObjectForTesting(
                3, 45, 2, 52.0f, 20.0f);
        for (int index = 0; index < 8;
             ++index) {
            g.spawnObjectForTesting(
                1, 70, 1,
                18.0f + index * 3.0f,
                8.0f);
            g.spawnObjectForTesting(
                3, 70, 2,
                62.0f + (index % 4) * 3.0f,
                8.0f + (index / 4) * 3.0f);
        }
        for (int technology : {1, 2, 3}) {
            g.researchTechnology(
                1, technology);
            g.researchTechnology(
                2, technology);
        }
        g.update(1.0f / 30.0f, {});
        auto hasOption = [&](uint32_t building,
                             int unitId) {
            const std::vector<int> options =
                g.productionOptionIds(building);
            return std::find(
                       options.begin(),
                       options.end(),
                       unitId) != options.end();
        };
        const bool menus =
            hasOption(empireAirbase, 1036) &&
            hasOption(rebelAirbase, 1046) &&
            hasOption(empireShipyard, 838) &&
            hasOption(rebelShipyard, 841);
        const bool queued =
            g.queueUnitForTesting(
                empireAirbase, 1036) &&
            g.queueUnitForTesting(
                rebelAirbase, 1046) &&
            g.queueUnitForTesting(
                empireShipyard, 838) &&
            g.queueUnitForTesting(
                rebelShipyard, 841);
        step(g, 2.0f);
        const bool completed =
            g.objectCountForTesting(1, 1036) > 0 &&
            g.objectCountForTesting(2, 1046) > 0 &&
            g.objectCountForTesting(1, 838) > 0 &&
            g.objectCountForTesting(2, 841) > 0;
        report(
            "transport-production",
            menus && queued && completed,
            "menus " +
                std::to_string(menus) +
                " queued " +
                std::to_string(queued) +
                " completed " +
                std::to_string(completed));
    }
    // Utility Trawlers derive their build and repair controls from their DAT
    // tasks rather than the land-worker class.
    {
        Game g(assets);
        if (!g.initCompactTestMap(
                0x5A17u, 96, &err)) {
            fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        g.setLocalPlayerForTesting(1);
        for (int technology : {1, 2, 3})
            g.researchTechnology(1, technology);
        for (int resource = 0; resource < 4; ++resource)
            g.setResourceForTesting(
                1, resource, 5000.0f);
        const uint32_t shipyard =
            g.spawnObjectForTesting(
                1, 45, 1, 44.0f, 28.0f);
        g.researchTechnology(1, 27);
        const uint32_t trawler =
            g.spawnObjectForTesting(
                1, 13, 1, 47.0f, 28.0f);
        g.update(1.0f / 30.0f, {});
        const std::vector<int> options =
            g.buildingOptionIds(trawler);
        const auto hasOption = [&](int unitId) {
            return std::find(
                       options.begin(),
                       options.end(),
                       unitId) != options.end();
        };
        g.selectObjectForTesting(trawler);
        InputState openMenu;
        openMenu.screenW = 960;
        openMenu.screenH = 544;
        openMenu.cycleAttackMode = true;
        g.update(0.001f, openMenu);
        const bool menuOpened =
            g.actionMenuOpenForTesting();
        const uint32_t foundation =
            g.spawnFoundationForTesting(
                1, 199, 1,
                47.0f, 32.0f,
                {trawler});
        const bool assigned =
            foundation != 0 &&
            g.constructionBuilderId(
                foundation) == trawler;
        const float progressBefore =
            foundation
                ? g.objectHitPoints(foundation)
                : 0.0f;
        step(g, 8.0f);
        const bool building =
            foundation != 0 &&
            (g.objectHitPoints(foundation) >
                 progressBefore ||
             g.objectCountForTesting(
                 1, 199) > 0);
        g.damageObjectForTesting(
            shipyard, 100);
        const float healthBefore =
            g.objectHitPoints(shipyard);
        const bool repairOrdered =
            g.issueRepairForTesting(
                trawler, shipyard);
        step(g, 15.0f);
        const bool repaired =
            g.objectHitPoints(shipyard) >
            healthBefore;
        report(
            "utility-trawler-build-repair",
            hasOption(199) &&
                hasOption(1576) &&
                menuOpened &&
                assigned &&
                building &&
                repairOrdered &&
                repaired,
            "options " +
                std::to_string(options.size()) +
                " aqua " +
                std::to_string(hasOption(199)) +
                " buoy " +
                std::to_string(hasOption(1576)) +
                " menu " +
                std::to_string(menuOpened) +
                " build " +
                std::to_string(
                    assigned && building) +
                " repair " +
                std::to_string(
                    repairOrdered && repaired));
    }
    // Units with their own carrying capacity may still board larger air and
    // sea transports when their original action-3 tasks allow it.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) {
            fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        const uint32_t airTransport =
            g.spawnObjectForTesting(
                1, 1036, 1, 36.0f, 30.0f);
        const uint32_t seaTransport =
            g.spawnObjectForTesting(
                1, 838, 1, 44.0f, 30.0f);
        bool allCanBoard = true;
        std::string failedIds;
        int index = 0;
        for (int unitId :
             {469, 485, 500, 631, 651, 681}) {
            const uint32_t unit =
                g.spawnObjectForTesting(
                    1, unitId, 1,
                    40.0f,
                    36.0f + index * 2.0f);
            const bool air =
                g.canGarrisonForTesting(
                    unit, airTransport);
            const bool sea =
                g.canGarrisonForTesting(
                    unit, seaTransport);
            if (!air || !sea) {
                allCanBoard = false;
                failedIds +=
                    std::to_string(unitId) +
                    "(" +
                    std::to_string(air) +
                    "/" +
                    std::to_string(sea) +
                    ") ";
            }
            ++index;
        }
        const bool transportsDoNotNest =
            !g.canGarrisonForTesting(
                airTransport, seaTransport) &&
            !g.canGarrisonForTesting(
                seaTransport, airTransport);
        report(
            "factory-units-board-transports",
            allCanBoard &&
                transportsDoNotNest,
            "all " +
                std::to_string(allCanBoard) +
                " no nesting " +
                std::to_string(
                    transportsDoNotNest) +
                " failures [" +
                failedIds + "]");
    }
    // AI attack groups use the ordinary formation marcher and carry land
    // armies across disconnected terrain with compatible transports.
    {
        static const char attackScript[] =
            "(defrule\n"
            "  (true)\n"
            "=>\n"
            "  (attack-now)\n"
            "  (disable-self))\n";

        Game formation(assets);
        if (!formation.init(7, 96, &err)) {
            fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        formation.setLocalPlayerForTesting(2);
        formation.setVisibilityCheatsForTesting(
            true, true);
        formation.setDiplomacyForTesting(
            1, 2, 3);
        formation.setDiplomacyForTesting(
            2, 1, 3);
        for (int index = 0; index < 8;
             ++index)
            formation.spawnObjectForTesting(
                3, 460, 2,
                38.0f + (index % 4),
                39.0f + (index / 4));
        formation.spawnObjectForTesting(
            1, 71, 1, 62.0f, 40.0f);
        const bool formationLoaded =
            formation.loadAiSourceForTesting(
                2, "formation-attack.per",
                attackScript, {}, &err);
        for (int frame = 0;
             formationLoaded &&
             frame < 30 * 60 &&
             formation
                     .aiAttacksIssuedForTesting(
                         2) == 0;
             ++frame)
            formation.update(
                1.0f / 30.0f, {});
        const bool formationAttack =
            formationLoaded &&
            formation.aiFormationOrdersForTesting(
                2) > 0 &&
            formation.aiAttacksIssuedForTesting(
                2) >= 4;

        Game invasion(assets);
        if (!invasion.initCompactTestMap(
                0x1A71u, 96, &err)) {
            fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        invasion.setLocalPlayerForTesting(3);
        invasion.setVisibilityCheatsForTesting(
            true, true);
        invasion.setDiplomacyForTesting(
            1, 3, 3);
        invasion.setDiplomacyForTesting(
            3, 1, 3);
        std::vector<uint32_t> landingForce;
        for (int index = 0; index < 6;
             ++index)
            landingForce.push_back(
                invasion.spawnObjectForTesting(
                    1, 460, 3,
                    68.0f + (index % 3),
                    39.0f + (index / 3)));
        const uint32_t transport =
            invasion.spawnObjectForTesting(
                1, 838, 3, 51.5f, 40.5f);
        const uint32_t navalEscort =
            invasion.spawnObjectForTesting(
                1, 868, 3, 51.5f, 43.0f);
        invasion.spawnObjectForTesting(
            1, 773, 3, 68.0f, 36.0f);
        invasion.spawnObjectForTesting(
            1, 71, 1, 27.0f, 40.0f);
        const bool compatible =
            invasion.canGarrisonForTesting(
                landingForce.front(),
                transport);
        const bool invasionLoaded =
            compatible &&
            invasion.loadAiSourceForTesting(
                3, "transport-attack.per",
                attackScript, {}, &err);
        for (int frame = 0;
             invasionLoaded &&
             frame < 30 * 240 &&
             (invasion
                      .aiTransportLandingsForTesting(
                          3) == 0 ||
              invasion
                      .aiAttacksIssuedForTesting(
                          3) == 0);
             ++frame)
            invasion.update(
                1.0f / 30.0f, {});
        bool crossed = true;
        for (uint32_t unit :
             landingForce)
            crossed =
                crossed &&
                invasion.objectPosition(unit)[0] <
                    47.0f;
        const bool transported =
            invasionLoaded &&
            invasion.aiTransportLandingsForTesting(
                3) > 0 &&
            invasion.aiAttacksIssuedForTesting(
                3) > 0 &&
            invasion
                    .aiEscortAssignmentsForTesting(
                        3) >= 2 &&
            crossed;
        report(
            "ai-formation-transport-invasion",
            formationAttack && transported,
            "formation loaded/orders/attacks " +
                std::to_string(
                    formationLoaded) +
                "/" +
                std::to_string(
                    formation
                        .aiFormationOrdersForTesting(
                            2)) +
                "/" +
                std::to_string(
                    formation
                        .aiAttacksIssuedForTesting(
                            2)) +
                " transport compatible/loaded/"
                "landings/crossed " +
                std::to_string(compatible) +
                "/" +
                std::to_string(
                    invasionLoaded) +
                "/" +
                std::to_string(
                    invasion
                        .aiTransportLandingsForTesting(
                            3)) +
                "/" +
                std::to_string(crossed) +
                " attacks " +
                std::to_string(
                    invasion
                        .aiAttacksIssuedForTesting(
                            3)) +
                " escorts " +
                std::to_string(
                    invasion
                        .aiEscortAssignmentsForTesting(
                            3)) +
                " escort-id/phase/members " +
                std::to_string(navalEscort) +
                "/" +
                std::to_string(
                    invasion
                        .aiGroupPhaseForTesting(
                            3)) +
                "/" +
                std::to_string(
                    invasion
                        .aiGroupMemberCountForTesting(
                            3)));
    }
    // The strategic manager replenishes all available military domains,
    // prioritizes Command Centers, and retreats badly outmatched groups.
    {
        static const char noOpScript[] =
            "(defrule\n"
            "  (true)\n"
            "=>\n"
            "  (disable-self))\n";

        Game balance(assets);
        if (!balance.init(7, 96, &err)) {
            fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        balance.setLocalPlayerForTesting(2);
        for (int resource = 0;
             resource < 4; ++resource)
            balance.setResourceForTesting(
                2, resource, 10000.0f);
        balance.researchTechnologyForTesting(
            2, 1);
        balance.researchTechnologyForTesting(
            2, 2);
        balance.researchTechnologyForTesting(
            2, 364);
        balance.researchTechnologyForTesting(
            2, 399);
        balance.spawnObjectForTesting(
            3, 109, 2, 70.0f, 70.0f);
        const uint32_t balanceTroopCenter =
            balance.spawnObjectForTesting(
            3, 87, 2, 72.0f, 66.0f);
        const uint32_t balanceShipyard =
            balance.spawnObjectForTesting(
            3, 45, 2, 52.0f, 40.0f);
        const uint32_t balanceAirbase =
            balance.spawnObjectForTesting(
            3, 317, 2, 74.0f, 66.0f);
        const bool balanceLoaded =
            balance.loadAiSourceForTesting(
                2, "strategy-balance.per",
                noOpScript, {}, &err);
        step(balance, 8.0f);
        const bool balanced =
            balanceLoaded &&
            balance.aiForceTargetForTesting(
                2, 0) > 0 &&
            balance.aiForceTargetForTesting(
                2, 1) > 0 &&
            balance.aiForceTargetForTesting(
                2, 2) > 0 &&
            balance.aiForceCountForTesting(
                2, 0) > 0 &&
            balance.aiForceCountForTesting(
                2, 1) > 0 &&
            balance.aiForceCountForTesting(
                2, 2) > 0 &&
            balance
                    .aiReplenishmentQueuedForTesting(
                        2) >= 3;

        Game priority(assets);
        if (!priority.init(7, 96, &err)) {
            fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        priority.setLocalPlayerForTesting(3);
        priority.setVisibilityCheatsForTesting(
            true, true);
        priority.setDiplomacyForTesting(
            1, 3, 3);
        priority.setDiplomacyForTesting(
            3, 1, 3);
        for (int index = 0;
             index < 6; ++index)
            priority.spawnObjectForTesting(
                3, 460, 3,
                42.0f + index,
                42.0f);
        const uint32_t priorityCenter =
            priority.spawnObjectForTesting(
                1, 109, 1,
                52.0f, 42.0f);
        priority.spawnObjectForTesting(
            1, 83, 1,
            48.0f, 42.0f);
        const bool priorityLoaded =
            priority.loadAiSourceForTesting(
                3, "strategy-priority.per",
                noOpScript, {}, &err);
        step(priority, 2.5f);
        const bool prioritized =
            priorityLoaded &&
            priority.aiGroupTargetUnitForTesting(
                3) ==
                priority.objectUnitId(
                    priorityCenter);

        Game retreat(assets);
        if (!retreat.init(7, 96, &err)) {
            fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        retreat.setLocalPlayerForTesting(3);
        retreat.setVisibilityCheatsForTesting(
            true, true);
        retreat.setDiplomacyForTesting(
            1, 3, 3);
        retreat.setDiplomacyForTesting(
            3, 1, 3);
        retreat.spawnObjectForTesting(
            3, 109, 3, 40.0f, 40.0f);
        for (int index = 0;
             index < 4; ++index)
            retreat.spawnObjectForTesting(
                3, 460, 3,
                44.0f + index,
                44.0f);
        for (int index = 0;
             index < 20; ++index)
            retreat.spawnObjectForTesting(
                1, 460, 1,
                48.0f +
                    (index % 5),
                44.0f +
                    (index / 5));
        const bool retreatLoaded =
            retreat.loadAiSourceForTesting(
                3, "strategy-retreat.per",
                noOpScript, {}, &err);
        step(retreat, 3.0f);
        const bool withdrew =
            retreatLoaded &&
            retreat.aiRetreatsForTesting(
                3) > 0;
        report(
            "ai-strategic-manager",
            balanced && prioritized &&
                withdrew,
            "balanced/priority/retreat " +
                std::to_string(balanced) +
                "/" +
                std::to_string(prioritized) +
                "/" +
                std::to_string(withdrew) +
                " forces " +
                std::to_string(
                    balance
                        .aiForceCountForTesting(
                            2, 0)) +
                "/" +
                std::to_string(
                    balance
                        .aiForceCountForTesting(
                            2, 1)) +
                "/" +
                std::to_string(
                    balance
                        .aiForceCountForTesting(
                            2, 2)) +
                " queued " +
                std::to_string(
                    balance
                        .aiReplenishmentQueuedForTesting(
                            2)) +
                " options " +
                std::to_string(
                    balance
                        .productionOptionIds(
                            balanceTroopCenter)
                        .size()) +
                "/" +
                std::to_string(
                    balance
                        .productionOptionIds(
                            balanceShipyard)
                        .size()) +
                "/" +
                std::to_string(
                    balance
                        .productionOptionIds(
                            balanceAirbase)
                        .size()));
    }
    // Conquest eliminates assetless players, presents local outcomes with the
    // original stream cues, and lets a collapsed AI surrender.
    {
        Game victory(assets);
        if (!victory.init(7, 96, &err)) {
            fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        std::vector<std::string> victorySounds;
        victory.setSoundPlayer(
            [&](const std::string &name) {
                victorySounds.push_back(name);
                return 1.0f;
            });
        victory.eliminatePlayerForTesting(
            2);
        step(victory, 3.5f);
        victory.render(
            renderer, 960, 544);
        const bool won =
            victory.victoryStateForTesting() ==
                1 &&
            std::find(
                victorySounds.begin(),
                victorySounds.end(),
                "won1") !=
                victorySounds.end();

        Game defeat(assets);
        if (!defeat.init(7, 96, &err)) {
            fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        std::vector<std::string> defeatSounds;
        defeat.setSoundPlayer(
            [&](const std::string &name) {
                defeatSounds.push_back(name);
                return 1.0f;
            });
        defeat.eliminatePlayerForTesting(
            1);
        defeat.render(
            renderer, 960, 544);
        const bool lost =
            defeat.victoryStateForTesting() ==
                0 &&
            std::find(
                defeatSounds.begin(),
                defeatSounds.end(),
                "lost") !=
                defeatSounds.end();

        static const char surrenderScript[] =
            "(defrule\n"
            "  (true)\n"
            "=>\n"
            "  (disable-self))\n";
        Game surrender(assets);
        if (!surrender.init(7, 96, &err)) {
            fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        surrender.setDiplomacyForTesting(
            1, 3, 3);
        surrender.setDiplomacyForTesting(
            3, 1, 3);
        surrender.spawnObjectForTesting(
            3, 12, 3, 55.0f, 55.0f);
        const bool surrenderLoaded =
            surrender.loadAiSourceForTesting(
                3, "surrender.per",
                surrenderScript, {}, &err);
        step(surrender, 19.0f);
        const bool surrendered =
            surrenderLoaded &&
            !surrender.playerActiveForTesting(
                3) &&
            surrender.aiSurrenderedForTesting(
                3);
        report(
            "conquest-victory-defeat-surrender",
            won && lost && surrendered,
            "won/lost/surrendered " +
                std::to_string(won) +
                "/" +
                std::to_string(lost) +
                "/" +
                std::to_string(
                    surrendered));
    }
    // The Bongo Marauder's zero target-spread value must not divide its
    // projectile spawning-area offsets into off-screen launch positions.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) {
            fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        g.setDiplomacyForTesting(1, 2, 3);
        g.setDiplomacyForTesting(2, 1, 3);
        const uint32_t marauder =
            g.spawnObjectForTesting(
                1, 1314, 1, 40.0f, 40.0f);
        const uint32_t target =
            g.spawnObjectForTesting(
                3, 838, 2, 46.0f, 40.0f);
        g.issueAttackForTesting(
            marauder, target);
        for (int frame = 0;
             frame < 30 * 10 &&
             g.projectileCountForTesting() < 2;
             ++frame)
            g.update(
                1.0f / 30.0f, {});
        const float farthest =
            g.farthestProjectileDistanceForTesting(
                marauder);
        report(
            "bongo-projectile-origin",
            g.projectileCountForTesting() >= 2 &&
                farthest >= 0.0f &&
                farthest < 4.0f,
            "projectiles " +
                std::to_string(
                    g.projectileCountForTesting()) +
                " farthest " +
                std::to_string(farthest));
    }
    // The secret OIIA cat uses original art and its dedicated synthesized
    // attack cue while retaining the proven cheat-unit combat behavior.
    {
        Game g(assets);
        if (!g.init(7, 96, &err)) {
            fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        g.setLocalPlayerForTesting(1);
        g.setDiplomacyForTesting(1, 2, 3);
        g.setDiplomacyForTesting(2, 1, 3);
        int oiiaSounds = 0;
        g.setOiiaSoundPlayer(
            [&]() {
                ++oiiaSounds;
            });
        const uint32_t cat =
            g.spawnOiiaCatForTesting(
                1, 40.0f, 40.0f);
        const uint32_t target =
            g.spawnObjectForTesting(
                3, 109, 2, 43.0f, 40.0f);
        const float healthBefore =
            g.objectHitPoints(target);
        const bool ordered =
            g.issueAttackForTesting(
                cat, target);
        for (int frame = 0;
             frame < 30 * 10 &&
             oiiaSounds == 0;
             ++frame)
            g.update(
                1.0f / 30.0f, {});
        g.lookAtObject(cat);
        g.selectObjectForTesting(cat);
        g.render(renderer, 960, 544);
        shot(g, "_oiia_cat");
        const bool attacked =
            !g.objectActive(target) ||
            g.objectHitPoints(target) <
                healthBefore;
        report(
            "oiia-cat-cheat-unit",
            cat != 0 &&
                g.objectIsOiiaCatForTesting(cat) &&
                ordered &&
                attacked &&
                oiiaSounds > 0,
            "spawn " +
                std::to_string(cat != 0) +
                " ordered " +
                std::to_string(ordered) +
                " attacked " +
                std::to_string(attacked) +
                " sound " +
                std::to_string(oiiaSounds));
    }
    printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}


// Counts GL-style batches (texture/mask switches) without rasterizing, to
// measure the game-side cost of UI states.
struct CountingRenderer : SoftRenderer {
    Texture *tex = nullptr, *mask = nullptr;
    size_t quads = 0, batches = 0; double area = 0; float scale = 1;
    std::map<std::string, double> big;
    void note(Texture *t, Texture *m, const Quad &q) {
        quads++; area += (double)q.w * q.h * scale * scale;
        if (t != tex || m != mask || !batches) { batches++; tex = t; mask = m; }
    }
    void beginFrame(int, int, float sc, uint8_t, uint8_t, uint8_t) override { quads = batches = 0; area = 0; scale = sc; tex = mask = nullptr; }
    void draw(Texture *t, const Quad &q) override { note(t, nullptr, q); }
    void drawMasked(Texture *t, const Quad &q, Texture *m, const Quad &) override { note(t, m, q); }
    void drawMaskedTinted(Texture *t, const Quad &q, Texture *m, const Quad &, uint8_t, uint8_t, uint8_t, uint8_t) override { note(t, m, q); }
    void drawTinted(Texture *t, const Quad &q, uint8_t, uint8_t, uint8_t, uint8_t) override { note(t, nullptr, q); }
    void drawLine(float x0, float y0, float x1, float y1, float t, uint8_t, uint8_t, uint8_t, uint8_t) override { note((Texture *)1, nullptr, Quad{x0, y0, std::abs(x1 - x0) + t, std::abs(y1 - y0) + t, 0, 0, 0, 0}); }
    void fillRect(float x, float y, float w, float h, uint8_t, uint8_t, uint8_t, uint8_t) override { note((Texture *)1, nullptr, Quad{x, y, w, h, 0, 0, 0, 0}); }
};

static int cmdBenchUi(const char *dataDir) {
    CountingRenderer renderer;
    Assets assets(&renderer);
    std::string err;
    if (!assets.init(dataDir, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
    Game g(assets);
    if (!g.initCompactTestMap(0x5A17u, 64, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
    g.setLocalPlayerForTesting(1);
    const uint32_t cc = g.spawnObjectForTesting(3, 109, 1, 30.0f, 30.0f);
    const uint32_t worker = g.spawnObjectForTesting(3, 83, 1, 30.0f, 34.0f);
    g.update(1.0f / 30.0f, {});
    auto measure = [&](const char *label) {
        using clock = std::chrono::steady_clock;
        for (int i = 0; i < 5; i++) { g.update(1.0f / 60.0f, {}); g.render(renderer, 960, 544); }
        auto t0 = clock::now();
        double updateMs = 0;
        const int frames = 120;
        for (int i = 0; i < frames; i++) {
            auto u0 = clock::now();
            InputState in; in.screenW = 960; in.screenH = 544; in.cursorVisible = true;
            in.pointerX = 480; in.pointerY = 300;
            g.update(1.0f / 60.0f, in);
            updateMs += std::chrono::duration<double, std::milli>(clock::now() - u0).count();
            g.render(renderer, 960, 544);
        }
        const double total = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
        printf("  tex %.1f MB builds %zu\n", assets.textureBytes() / 1048576.0, assets.buildCount());
        printf("%-18s update %.3f ms  render %.3f ms  quads %zu batches %zu  fill %.2f screens\n", label, updateMs / frames,
               (total - updateMs) / frames, renderer.quads, renderer.batches, renderer.area / (960.0 * 544.0));
    };
    g.lookAtObject(cc);
    measure("nothing selected");
    g.selectObjectForTesting(worker);
    measure("worker selected");
    InputState in; in.screenW = 960; in.screenH = 544; in.cycleAttackMode = true;
    g.update(0.001f, in);
    printf("menu open %d\n", g.actionMenuOpenForTesting());
    measure("worker menu");
    g.update(0.001f, in); // close
    g.clearSelectionForTesting();
    g.selectObjectForTesting(cc);
    measure("cc selected");
    g.update(0.001f, in);
    measure("cc menu");
    in = {}; in.screenW = 960; in.screenH = 544; in.actionTabRight = true;
    g.update(0.001f, in);
    measure("cc research");
    for (auto &entry : assets.largestSheets(15)) printf("  slp %d %.2f MB\n", entry.second, entry.first / 1048576.0);
    return 0;
}

static int cmdAiScript(const char *path) {
    AiProgram program;
    std::string err;
    if (!program.load(
            path,
            {"DIFFICULTY-MODERATE"},
            &err)) {
        fprintf(
            stderr, "error: %s\n",
            err.c_str());
        return 1;
    }
    size_t conditions = 0;
    size_t actions = 0;
    for (const AiRule &rule :
         program.rules) {
        conditions +=
            rule.conditions.size();
        actions += rule.actions.size();
    }
    printf(
        "AI: %zu files, %zu constants, "
        "%zu rules, %zu conditions, "
        "%zu actions\n",
        program.files.size(),
        program.constants.size(),
        program.rules.size(),
        conditions, actions);
    return program.rules.empty() ? 1 : 0;
}

static int cmdTestAi(
    const char *dataDir,
    const char *aiDir) {
    std::string err;
    const std::string entry =
        std::string(aiDir) +
        "/Computer Expanded.per";
    AiProgram original;
    const bool parsed =
        original.load(
            entry,
            {"DIFFICULTY-MODERATE"},
            &err);
    if (!parsed) {
        fprintf(
            stderr, "error: %s\n",
            err.c_str());
        return 1;
    }

    SoftRenderer renderer;
    Assets assets(&renderer);
    if (!assets.init(dataDir, &err)) {
        fprintf(
            stderr, "error: %s\n",
            err.c_str());
        return 1;
    }
    Game game(assets);
    if (!game.initCompactTestMap(
            0x5A17u, 96, &err)) {
        fprintf(
            stderr, "error: %s\n",
            err.c_str());
        return 1;
    }
    for (int resource = 0;
         resource < 4; ++resource)
        game.setResourceForTesting(
            2, resource, 2000.0f);
    game.spawnObjectForTesting(
        3, 83, 2, 69.0f, 69.0f);
    game.spawnObjectForTesting(
        0, 348, 0, 70.0f, 69.0f);
    static const char script[] =
        "(defconst test-goal 200)\n"
        "(defrule\n"
        "  (true)\n"
        "=>\n"
        "  (set-goal test-goal 7)\n"
        "  (set-strategic-number "
        "sn-carbon-gatherer-percentage 100)\n"
        "  (set-strategic-number "
        "sn-food-gatherer-percentage 0)\n"
        "  (set-strategic-number "
        "sn-metal-gatherer-percentage 0)\n"
        "  (set-strategic-number "
        "sn-nova-gatherer-percentage 0)\n"
        "  (disable-self))\n"
        "(defrule\n"
        "  (goal test-goal 7)\n"
        "  (building-type-count-total "
        "BLDG-DROPCARBON == 0)\n"
        "  (can-build BLDG-DROPCARBON)\n"
        "=>\n"
        "  (build BLDG-DROPCARBON)\n"
        "  (disable-self))\n"
        "(defrule\n"
        "  (goal test-goal 7)\n"
        "  (building-type-count-total "
        "BLDG-DROPCHOW == 0)\n"
        "  (can-build BLDG-DROPCHOW)\n"
        "=>\n"
        "  (build BLDG-DROPCHOW)\n"
        "  (disable-self))\n"
        "(defrule\n"
        "  (goal test-goal 7)\n"
        "  (can-train UNIT-WORKER)\n"
        "=>\n"
        "  (train UNIT-WORKER)\n"
        "  (disable-self))\n";
    const bool loaded =
        game.loadAiSourceForTesting(
            2, "economy-test.per",
            script, {}, &err);
    if (!loaded) {
        fprintf(
            stderr, "error: %s\n",
            err.c_str());
        return 1;
    }
    for (int frame = 0;
         frame < 30 * 3; ++frame)
        game.update(
            1.0f / 30.0f, {});

    const bool originalParsed =
        original.files.size() >= 35 &&
        original.rules.size() >= 1200 &&
        original.constants.size() >= 250;
    const bool initialized =
        game.aiGoalForTesting(2, 200) == 7 &&
        game.aiStrategicNumberForTesting(
            2,
            "sn-carbon-gatherer-percentage") ==
            100;
    const bool gathered =
        game.aiGathererCountForTesting(
            2, 1) > 0;
    const int carbonBuildings =
        game.aiObjectCountForTesting(
            2, "BLDG-DROPCARBON");
    const int foodBuildings =
        game.aiObjectCountForTesting(
            2, "BLDG-DROPCHOW");
    const size_t queuedWorkers =
        game.aiQueuedUnitCountForTesting(
            2, 83);
    Game openingGame(assets);
    if (!openingGame.initCompactTestMap(
            0x5A17u, 96, &err)) {
        fprintf(
            stderr, "error: %s\n",
            err.c_str());
        return 1;
    }
    bool startingResources = true;
    for (int player = 1;
         player <= 2; ++player)
        for (int resourceType = 0;
             resourceType < 4;
             ++resourceType)
            startingResources =
                startingResources &&
                std::abs(
                    openingGame.resource(
                        player,
                        resourceType) -
                    3000.0f) <
                    0.001f;
    int initialShipyardSites = 0;
    for (float y = 2.0f; y < 94.0f;
         y += 0.5f)
        for (float x = 2.0f;
             x < 94.0f; x += 0.5f)
            if (openingGame
                    .placementValidForTesting(
                        openingGame
                            .civilizationForPlayerForTesting(
                                2),
                        45, x, y))
                initialShipyardSites++;
    if (!openingGame.loadAiScript(
            2, entry,
            {"DIFFICULTY-MODERATE",
             "LAND-SATELLITES-MAP"},
            &err)) {
        fprintf(
            stderr, "error: %s\n",
            err.c_str());
        return 1;
    }
    int enemyProductionSounds = 0;
    const int workerTrainSound =
        assets.dat().civs.size() > 3 &&
                assets.dat().civs[3].units.size() >
                    83
            ? assets.dat().civs[3].units[83]
                  .trainSound
            : -1;
    openingGame.setUnitSoundPlayer(
        [&](int soundId, int) {
            if (soundId == workerTrainSound)
                enemyProductionSounds++;
        });
    for (int frame = 0;
         frame < 30 * 60; ++frame)
        openingGame.update(
            1.0f / 30.0f, {});
    const int openingCarbonPercentage =
        openingGame.aiStrategicNumberForTesting(
            2,
            "sn-carbon-gatherer-percentage");
    const int openingGatherPercentage =
        openingCarbonPercentage +
        openingGame.aiStrategicNumberForTesting(
            2,
            "sn-food-gatherer-percentage") +
        openingGame.aiStrategicNumberForTesting(
            2,
            "sn-metal-gatherer-percentage") +
        openingGame.aiStrategicNumberForTesting(
            2,
            "sn-nova-gatherer-percentage");
    const size_t openingCarbonGatherers =
        openingGame.aiGathererCountForTesting(
            2, 1);
    const bool originalOpening =
        openingGatherPercentage == 100 &&
        openingCarbonGatherers > 0;
    const size_t openingExplored =
        openingGame
            .exploredTileCountForTesting(2);
    for (int step = 0;
         step < 8000; ++step)
        openingGame.update(0.2f, {});
    const int openingAge =
        openingGame.aiTechLevelForTesting(2);
    const int troopCenters =
        openingGame.aiObjectCountForTesting(
            2, "BLDG-TRAINTROOPER");
    const int shipyards =
        openingGame.aiObjectCountForTesting(
            2, "BLDG-TRAINBOAT");
    const int powerCores =
        openingGame.aiObjectCountForTesting(
            2, "BLDG-POWERCORE");
    const int workers =
        openingGame.aiObjectCountForTesting(
            2, "UNIT-WORKER");
    const int militaryUnits =
        openingGame.aiObjectCountForTesting(
            2, "BOAT-LASER-LINE");
    const int transports =
        openingGame.aiObjectCountForTesting(
            2, "BOAT-TRANSPORT");
    const int transportHistory =
        openingGame.aiObjectTotalForTesting(
            2, "BOAT-TRANSPORT");
    const int landForces =
        openingGame.aiForceCountForTesting(
            2, 0);
    const int navalForces =
        openingGame.aiForceCountForTesting(
            2, 1);
    const int airForces =
        openingGame.aiForceCountForTesting(
            2, 2);
    const bool originalProgression =
        openingAge >= 3 &&
        shipyards > 0 &&
        militaryUnits > 0 &&
        transportHistory > 0 &&
        landForces > 0 &&
        navalForces > 0 &&
        airForces > 0;
    const size_t explored =
        openingGame
            .exploredTileCountForTesting(2);
    const uint32_t attacksIssued =
        openingGame
            .aiAttacksIssuedForTesting(2);
    const auto forwardPlacement =
        [&](SkirmishMapStyle style,
            uint32_t seed) {
            SkirmishSettings settings;
            settings.seed = seed;
            settings.mapSize = 96;
            settings.mapStyle = style;
            settings.playerCivilization = 3;
            settings.computerCivilization = 3;
            settings.startingResources = 10000;
            settings.populationCap = 200;
            Game forward(assets);
            if (!forward.initSkirmish(
                    settings, &err))
                return false;
            const auto base =
                forward
                    .playerBasePositionForTesting(
                        2);
            const float direction =
                base[0] < 48.0f
                    ? 1.0f
                    : -1.0f;
            const int enemyUnit =
                forward.aiUnitIdForTesting(
                    1, "BLDG-MAIN1");
            if (enemyUnit < 0)
                return false;
            forward.spawnObjectForTesting(
                forward
                    .civilizationForPlayerForTesting(
                        1),
                enemyUnit, 1,
                base[0] +
                    direction * 6.0f,
                base[1]);
            forward.updateVisibilityForTesting();
            if (!forward.aiBuildForTesting(
                    2, "BLDG-DROPCARBON",
                    true))
                return false;
            const auto foundation =
                forward
                    .newestFoundationPositionForTesting(
                        2);
            return foundation[0] >= 0.0f &&
                   (foundation[0] - base[0]) *
                           direction >
                       0.0f;
        };
    const bool forwardLand =
        forwardPlacement(
            SkirmishMapStyle::Grasslands,
            0xC411u);
    const bool forwardIslands =
        forwardPlacement(
            SkirmishMapStyle::CompactIslands,
            0xC412u);
    printf(
        "AI parser %zu files, %zu constants, "
        "%zu rules; opening %d%% gather/%zu "
        "carbon workers; age %d, workers %d, "
        "troop centers %d, shipyards %d, "
        "power cores %d, frigates %d, "
        "transports %d/%d, forces %d/%d/%d, "
        "explored +%zu, "
        "attacks %u, enemy sounds %d, "
        "forward land/islands %d/%d\n",
        original.files.size(),
        original.constants.size(),
        original.rules.size(),
        openingGatherPercentage,
        openingCarbonGatherers,
        openingAge,
        workers,
        troopCenters,
        shipyards,
        powerCores,
        militaryUnits,
        transports,
        transportHistory,
        landForces,
        navalForces,
        airForces,
        explored - openingExplored,
        attacksIssued,
        enemyProductionSounds,
        forwardLand,
        forwardIslands);
    fflush(stdout);
    if (!originalParsed ||
        !initialized || !gathered ||
        carbonBuildings != 1 ||
        foodBuildings != 1 ||
        queuedWorkers != 1 ||
        !startingResources ||
        initialShipyardSites == 0 ||
        !originalOpening ||
        !originalProgression ||
        explored <= openingExplored ||
        attacksIssued == 0 ||
        powerCores > 1 ||
        enemyProductionSounds != 0 ||
        !forwardLand ||
        !forwardIslands) {
        fprintf(
            stderr,
            "error: AI validation failed\n");
        return 1;
    }
    return 0;
}

static int cmdTestSkirmish(const char *dataDir) {
    std::string err;
    SoftRenderer renderer;
    Assets assets(&renderer);
    if (!assets.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    int failures = 0;
    auto report = [&](const char *name, bool ok,
                      const std::string &detail) {
        printf(
            "%s %s %s\n",
            ok ? "PASS" : "FAIL",
            name, detail.c_str());
        if (!ok) ++failures;
    };

    SkirmishSettings settings;
    settings.seed = 0x1234ABCDu;
    settings.mapSize = 96;
    settings.playerCivilization = 7;
    settings.computerCivilization = 5;
    settings.difficulty = 4;
    settings.personality = AiPersonality::Classic;
    settings.allied = true;
    settings.slots[0].civilization = 7;
    settings.slots[0].team = 1;
    settings.slots[0].alliedVictory = true;
    settings.slots[1].civilization = 5;
    settings.slots[1].difficulty = 4;
    settings.slots[1].personality =
        AiPersonality::Classic;
    settings.slots[1].team = 1;
    settings.slots[1].alliedVictory = true;
    settings.slots[2].type =
        SkirmishSlotType::Computer;
    settings.slots[2].civilization = 3;
    settings.slots[2].team = 2;
    settings.mapStyle = SkirmishMapStyle::Grasslands;
    settings.startingResources = 1000;
    settings.populationCap = 150;
    settings.victory = SkirmishVictory::CommandCenter;

    Game game(assets);
    if (!game.initSkirmish(settings, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    const bool settingsApplied =
        game.civilizationForPlayerForTesting(1) == 7 &&
        game.civilizationForPlayerForTesting(2) == 5 &&
        game.activePlayerCountForTesting() == 3 &&
        game.difficultyForTesting() == 4 &&
        game.populationLimitForTesting(1) == 150.0f &&
        game.populationLimitForTesting(2) == 150.0f &&
        game.resource(1, 0) == 1000.0f &&
        game.resource(2, 3) == 1000.0f &&
        game.diplomacyForTesting(1, 2) == 0 &&
        game.diplomacyForTesting(2, 1) == 0 &&
        game.victoryConditionForTesting() ==
            SkirmishVictory::CommandCenter;
    report(
        "lobby-settings", settingsApplied,
        "civilizations=" +
            std::to_string(
                game.civilizationForPlayerForTesting(1)) +
            "/" +
            std::to_string(
                game.civilizationForPlayerForTesting(2)) +
            " resources=" +
            std::to_string((int)game.resource(1, 0)) +
            " population=" +
            std::to_string(
                (int)game.populationLimitForTesting(1)));

    settings.allied = false;
    settings.slots[0].team = 1;
    settings.slots[0].alliedVictory = false;
    settings.slots[1].team = 2;
    settings.slots[1].alliedVictory = false;
    settings.slots[2].type =
        SkirmishSlotType::Closed;
    settings.playerCivilization = 3;
    settings.computerCivilization = 3;
    settings.slots[0].civilization = 3;
    settings.slots[1].civilization = 3;
    settings.mapStyle = SkirmishMapStyle::Grasslands;
    game.initSkirmish(settings, &err);
    const uint64_t grassHash = game.mapHashForTesting();
    const size_t objectCount = game.activeObjectCount();
    Game same(assets);
    same.initSkirmish(settings, &err);
    const bool deterministic =
        grassHash == same.mapHashForTesting() &&
        objectCount == same.activeObjectCount();
    settings.seed++;
    Game varied(assets);
    varied.initSkirmish(settings, &err);
    const bool seedVaries =
        grassHash != varied.mapHashForTesting();
    settings.seed--;
    settings.mapStyle = SkirmishMapStyle::Archipelago;
    Game islands(assets);
    islands.initSkirmish(settings, &err);
    const bool styleVaries =
        grassHash != islands.mapHashForTesting();
    report(
        "map-determinism-variation",
        deterministic && seedVaries && styleVaries,
        "grass=" + std::to_string(grassHash) +
            " islands=" +
            std::to_string(
                islands.mapHashForTesting()));

    bool fair = true;
    std::string resourceDetail;
    for (int resource = 0; resource < 4;
         ++resource) {
        const int player =
            islands
                .reachableStartingResourceCountForTesting(
                    1, resource);
        const int computer =
            islands
                .reachableStartingResourceCountForTesting(
                    2, resource);
        fair =
            fair && player > 0 &&
            computer > 0 &&
            std::abs(player - computer) <= 4;
        resourceDetail +=
            (resourceDetail.empty() ? "" : ",") +
            std::to_string(player) + "/" +
            std::to_string(computer);
    }
    const bool shore =
        islands.shorelineShipyardSiteForTesting(1) &&
        islands.shorelineShipyardSiteForTesting(2);
    report(
        "map-fairness-reachability",
        fair && shore,
        "reachable=" + resourceDetail +
            " shipyards=" +
            std::to_string((int)shore));

    settings.mapStyle = SkirmishMapStyle::CompactIslands;
    settings.seed = 0x5A17u;
    Game compact(assets);
    compact.initSkirmish(settings, &err);
    const bool compactWater =
        compact.terrainAtForTesting(48, 20) == 22 &&
        compact.terrainAtForTesting(42, 20) == 2 &&
        compact.terrainAtForTesting(53, 20) == 2;
    report(
        "compact-map-selectable",
        compactWater,
        "center=" +
            std::to_string(
                compact.terrainAtForTesting(48, 20)));

    const uint64_t restartHash =
        compact.mapHashForTesting();
    const size_t restartObjects =
        compact.activeObjectCount();
    compact.eliminatePlayerForTesting(1);
    compact.queueInstructionForTesting("STALE");
    const bool dirtied =
        compact.victoryStateForTesting() == 0 &&
        compact.instructionCountForResetTesting() > 0;
    compact.initSkirmish(settings, &err);
    const bool reset =
        dirtied &&
        compact.victoryStateForTesting() == -1 &&
        compact.instructionCountForResetTesting() == 0 &&
        compact.projectileCountForResetTesting() == 0 &&
        compact.activeObjectCount() == restartObjects &&
        compact.mapHashForTesting() == restartHash &&
        compact.selectedObjectCount() == 0;
    report(
        "repeated-match-reset", reset,
        "objects=" +
            std::to_string(
                compact.activeObjectCount()) +
            " outcome=" +
            std::to_string(
                compact.victoryStateForTesting()));

    Frontend frontend;
    InputState input;
    input.menuActivate = true;
    frontend.update(input, -1);
    input = {};
    input.menuActivate = true;
    frontend.update(input, -1);
    input = {};
    input.menuDown = true;
    frontend.update(input, -1);
    input = {};
    input.menuActivate = true;
    frontend.update(input, -1);
    input = {};
    input.menuDown = true;
    for (int row = 0; row < 10; ++row)
        frontend.update(input, -1);
    input = {};
    input.menuActivate = true;
    const FrontendAction start =
        frontend.update(input, -1);
    frontend.loadingFinished(true);
    input = {};
    input.pausePressed = true;
    frontend.update(input, -1);
    input = {};
    input.menuDown = true;
    for (int row = 0; row < 4; ++row)
        frontend.update(input, -1);
    input = {};
    input.menuActivate = true;
    frontend.update(input, -1);
    const FrontendAction restart =
        frontend.update(input, -1);
    frontend.loadingFinished(true);
    frontend.update({}, 1);
    input = {};
    input.menuActivate = true;
    const FrontendAction outcomeRestart =
        frontend.update(input, 1);
    frontend.loadingFinished(true);
    frontend.update({}, 0);
    input = {};
    input.menuDown = true;
    frontend.update(input, 0);
    input = {};
    input.menuActivate = true;
    const FrontendAction outcomeMenu =
        frontend.update(input, 0);
    const bool navigation =
        start == FrontendAction::StartSkirmish &&
        restart == FrontendAction::RestartMatch &&
        outcomeRestart ==
            FrontendAction::RestartMatch &&
        outcomeMenu ==
            FrontendAction::ReturnToMainMenu &&
        frontend.screen() ==
            FrontendScreen::MainMenu;
    report(
        "post-match-navigation", navigation,
        "actions=" +
            std::to_string((int)start) + "/" +
            std::to_string((int)restart) + "/" +
            std::to_string((int)outcomeRestart) +
            "/" +
            std::to_string((int)outcomeMenu));

    return failures ? 1 : 0;
}

static int cmdTestMapsModes(const char *dataDir) {
    std::string err;
    SoftRenderer renderer;
    Assets assets(&renderer);
    if (!assets.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    int failures = 0;
    auto report = [&](const char *name, bool ok,
                      const std::string &detail) {
        printf(
            "%s %s %s\n",
            ok ? "PASS" : "FAIL",
            name, detail.c_str());
        if (!ok) ++failures;
    };
    const auto configure =
        [](SkirmishSettings &settings,
           int players, int teams) {
            for (int slot = 0;
                 slot < kMaxSkirmishSlots;
                 ++slot) {
                SkirmishSlot &player =
                    settings.slots[(size_t)slot];
                player.type =
                    slot >= players
                        ? SkirmishSlotType::Closed
                    : slot == 0
                        ? SkirmishSlotType::Human
                        : SkirmishSlotType::Computer;
                player.name =
                    slot == 0
                        ? "Local Commander"
                        : "Computer " +
                              std::to_string(
                                  slot + 1);
                player.color = (uint8_t)slot;
                player.civilization =
                    (uint8_t)(1 + slot % 8);
                player.difficulty =
                    (uint8_t)(slot % 5);
                player.personality =
                    slot & 1
                        ? AiPersonality::Classic
                        : AiPersonality::Expanded;
                player.team =
                    teams <= 0
                        ? 0
                        : (uint8_t)(
                              1 +
                              slot %
                                  std::max(
                                      1, teams));
                player.alliedVictory =
                    teams > 0;
            }
        };

    SkirmishSettings validation;
    validation.mapSize = 160;
    configure(validation, 8, 4);
    std::string validationError;
    const bool eightValid =
        validateSkirmishSettings(
            validation, &validationError);
    validation.slots[7].color =
        validation.slots[6].color;
    const bool duplicateRejected =
        !validateSkirmishSettings(
            validation, &validationError) &&
        validationError.find("unique") !=
            std::string::npos;
    validation.slots[7].color = 7;
    validation.slots[0].type =
        SkirmishSlotType::Computer;
    const bool noHumanRejected =
        !validateSkirmishSettings(
            validation, &validationError) &&
        validationError.find("human") !=
            std::string::npos;
    validation.slots[0].type =
        SkirmishSlotType::Human;
    validation.slots[1].type =
        SkirmishSlotType::Human;
    const bool multipleHumansRejected =
        !validateSkirmishSettings(
            validation, &validationError) &&
        validationError.find("exactly one") !=
            std::string::npos;
    validation.slots[1].type =
        SkirmishSlotType::Computer;
    validation.mapSize = 64;
    const bool unsafeDensityRejected =
        !validateSkirmishSettings(
            validation, &validationError) &&
        validationError.find("larger") !=
            std::string::npos;
    report(
        "lobby-validation-2-8",
        eightValid && duplicateRejected &&
            noHumanRejected &&
            multipleHumansRejected &&
            unsafeDensityRejected,
        validationError);

    static constexpr SkirmishMapStyle styles[] = {
        SkirmishMapStyle::Grasslands,
        SkirmishMapStyle::Forest,
        SkirmishMapStyle::Desert,
        SkirmishMapStyle::Ice,
        SkirmishMapStyle::Swamp,
        SkirmishMapStyle::InlandWater,
        SkirmishMapStyle::Coastal,
        SkirmishMapStyle::Archipelago,
        SkirmishMapStyle::LandMass,
        SkirmishMapStyle::CompactIslands,
    };
    bool previews = true;
    std::set<uint64_t> previewHashes;
    for (SkirmishMapStyle style : styles) {
        SkirmishSettings settings;
        settings.mapSize = 128;
        configure(settings, 6, 3);
        settings.mapStyle = style;
        settings.seed = 0x4d415000u +
                        (uint32_t)style;
        const SkirmishPreview first =
            generateSkirmishPreview(settings);
        const SkirmishPreview same =
            generateSkirmishPreview(settings);
        settings.seed++;
        const SkirmishPreview varied =
            generateSkirmishPreview(settings);
        previews =
            previews &&
            first.error.empty() &&
            first.hash == same.hash &&
            first.hash != varied.hash;
        previewHashes.insert(first.hash);
    }
    report(
        "preview-determinism-families",
        previews &&
            previewHashes.size() ==
                std::size(styles),
        "distinct=" +
            std::to_string(
                previewHashes.size()));

    bool generated = true;
    int generatedCases = 0;
    size_t maximumAllocation = 0;
    std::set<uint64_t> terrainHashes;
    for (size_t styleIndex = 0;
         styleIndex < std::size(styles);
         ++styleIndex) {
        for (int pass = 0; pass < 2; ++pass) {
            const int players =
                2 + (int)(
                        styleIndex * 2 +
                        pass * 3) %
                        7;
            SkirmishSettings settings;
            settings.mapSize =
                players > 6
                    ? 128
                : players > 4
                    ? 96
                    : 64;
            configure(
                settings, players,
                pass ? 0
                     : std::max(
                           2, players / 2));
            settings.mapStyle =
                styles[styleIndex];
            settings.seed =
                0x51000000u +
                (uint32_t)(styleIndex * 17 +
                           pass);
            settings.startingResources = 1000;
            settings.populationCap = 100;
            Game game(assets);
            const bool initialized =
                game.initSkirmish(
                    settings, &err);
            const bool initialInvariant =
                initialized &&
                game.invariantsForTesting();
            bool fair =
                initialized &&
                game.activePlayerCountForTesting() ==
                    players &&
                game.localPlayerForTesting() == 1 &&
                initialInvariant;
            if (initialized) {
                maximumAllocation =
                    std::max(
                        maximumAllocation,
                        game
                            .allocationBytesForTesting());
                terrainHashes.insert(
                    game.mapHashForTesting());
                for (int player = 1;
                     player <= players;
                     ++player) {
                    const auto base =
                        game
                            .playerBasePositionForTesting(
                                player);
                    fair =
                        fair && base[0] >= 0.0f;
                    for (int resource = 0;
                         resource < 4;
                         ++resource)
                        fair =
                            fair &&
                            game
                                    .reachableStartingResourceCountForTesting(
                                        player,
                                        resource) >
                                0;
                }
                for (int frame = 0;
                     frame < 90; ++frame)
                    game.update(
                        1.0f / 30.0f, {});
                fair =
                    fair &&
                    game.invariantsForTesting();
            }
            generated = generated && fair;
            if (!fair)
                for (int player = 1;
                     player <= players;
                     ++player)
                    printf(
                        "DETAIL player=%d base=%.1f,%.1f resources=%d/%d/%d/%d\n",
                        player,
                        game.playerBasePositionForTesting(
                            player)[0],
                        game.playerBasePositionForTesting(
                            player)[1],
                        game.reachableStartingResourceCountForTesting(
                            player, 0),
                        game.reachableStartingResourceCountForTesting(
                            player, 1),
                        game.reachableStartingResourceCountForTesting(
                            player, 2),
                        game.reachableStartingResourceCountForTesting(
                            player, 3));
            if (!fair)
                printf(
                    "DETAIL generation style=%s players=%d active=%d initial-invariant=%d final-invariant=%d error=%s\n",
                    mapStyleName(
                        settings.mapStyle),
                    players,
                    game.activePlayerCountForTesting(),
                    initialInvariant,
                    game.invariantsForTesting(),
                    err.c_str());
            ++generatedCases;
        }
    }
    SkirmishSettings largestSettings;
    largestSettings.mapSize = 160;
    configure(largestSettings, 8, 4);
    largestSettings.mapStyle =
        SkirmishMapStyle::Forest;
    largestSettings.seed = 0x56495441u;
    largestSettings.startingResources = 1000;
    Game largest(assets);
    const bool largestValid =
        largest.initSkirmish(
            largestSettings, &err) &&
        largest.activePlayerCountForTesting() ==
            8 &&
        largest.invariantsForTesting();
    if (largestValid) {
        maximumAllocation =
            std::max(
                maximumAllocation,
                largest
                    .allocationBytesForTesting());
        terrainHashes.insert(
            largest.mapHashForTesting());
    }
    generated = generated && largestValid;
    ++generatedCases;
    report(
        "generation-property-matrix",
        generated &&
            terrainHashes.size() >= 16,
        "cases=" +
            std::to_string(generatedCases) +
            " hashes=" +
            std::to_string(
                terrainHashes.size()) +
            " max-bytes=" +
            std::to_string(
                maximumAllocation));
    report(
        "vita-map-allocation-cap",
        maximumAllocation > 0 &&
            maximumAllocation <
                64u * 1024u * 1024u,
        "measured-largest=" +
            std::to_string(
                maximumAllocation) +
            " cap=67108864");

    SkirmishSettings teams;
    teams.mapSize = 96;
    configure(teams, 4, 2);
    teams.slots[0].team = 1;
    teams.slots[1].team = 1;
    teams.slots[2].team = 2;
    teams.slots[3].team = 2;
    for (int slot = 0; slot < 4; ++slot)
        teams.slots[(size_t)slot]
            .alliedVictory = true;
    teams.reveal = SkirmishReveal::Explored;
    teams.gameSpeed = SkirmishGameSpeed::Fast;
    teams.startingTechLevel = 2;
    teams.endingTechLevel = 3;
    teams.victory = SkirmishVictory::Conquest;
    Game teamGame(assets);
    bool teamVictory =
        teamGame.initSkirmish(teams, &err);
    if (teamVictory) {
        const char *lockedStanceScript =
            "(defrule\n"
            "  (true)\n"
            "=>\n"
            "  (set-stance 1 3)\n"
            "  (disable-self))\n";
        const bool stanceLoaded =
            teamGame.loadAiSourceForTesting(
                2, "locked-stance.per",
                lockedStanceScript, {}, &err);
        for (int frame = 0;
             stanceLoaded && frame < 90; ++frame)
            teamGame.update(
                1.0f / 30.0f, {});
        teamVictory =
            stanceLoaded &&
            teamGame.diplomacyForTesting(
                2, 1) == 0 &&
            teamGame.difficultyForPlayer(2) ==
                teams.slots[1].difficulty &&
            teamGame.difficultyForPlayer(3) ==
                teams.slots[2].difficulty &&
            teamGame.difficultyForPlayer(4) ==
                teams.slots[3].difficulty;
        teamGame.eliminatePlayerForTesting(3);
        teamGame.eliminatePlayerForTesting(4);
        for (int frame = 0;
             frame < 120; ++frame)
            teamGame.update(
                1.0f / 30.0f, {});
        teamVictory =
            teamVictory &&
            teamGame.victoryStateForTesting() == 1 &&
            teamGame.exploredTileCountForTesting(
                1) ==
                (size_t)teams.mapSize *
                    teams.mapSize &&
            teamGame.aiTechLevelForTesting(1) == 2;
    }
    report(
        "fixed-team-allied-victory",
        teamVictory, err);

    teams.teamsLocked = false;
    Game brokenAlliance(assets);
    bool allyBreak =
        brokenAlliance.initSkirmish(
            teams, &err);
    if (allyBreak) {
        brokenAlliance.setDiplomacyForTesting(
            1, 2, 3);
        brokenAlliance.setDiplomacyForTesting(
            2, 1, 3);
        brokenAlliance
            .eliminatePlayerForTesting(3);
        brokenAlliance
            .eliminatePlayerForTesting(4);
        for (int frame = 0;
             frame < 120; ++frame)
            brokenAlliance.update(
                1.0f / 30.0f, {});
        allyBreak =
            brokenAlliance
                    .victoryStateForTesting() ==
                -1;
        brokenAlliance
            .eliminatePlayerForTesting(2);
        for (int frame = 0;
             frame < 120; ++frame)
            brokenAlliance.update(
                1.0f / 30.0f, {});
        allyBreak =
            allyBreak &&
            brokenAlliance
                    .victoryStateForTesting() ==
                1;
    }
    report(
        "unlocked-team-ally-break",
        allyBreak, err);

    const char *savePath =
        "test-maps-modes.sav";
    std::remove(savePath);
    teams.seed = 0x53415636u;
    teams.mapStyle =
        SkirmishMapStyle::Coastal;
    teams.victory = SkirmishVictory::Score;
    teams.scoreLimit = 5500;
    teams.cheatsEnabled = true;
    Game saved(assets);
    const bool initialized =
        saved.initSkirmish(teams, &err);
    const uint64_t savedHash =
        saved.mapHashForTesting();
    const bool written =
        initialized &&
        saved.saveMatch(savePath, &err);
    MatchSaveMetadata metadata;
    const bool probed =
        written &&
        Game::readSaveMetadata(
            savePath, metadata, &err);
    Game restored(assets);
    const bool restoredInit =
        probed &&
        restored.initSkirmish(
            metadata.skirmish, &err);
    const bool loaded =
        restoredInit &&
        restored.loadMatch(
            savePath, &err);
    std::remove(savePath);
    const bool saveRoundTrip =
        loaded &&
        metadata.skirmish.slots[3].type ==
            SkirmishSlotType::Computer &&
        metadata.skirmish.slots[3].team == 2 &&
        metadata.skirmish.reveal ==
            SkirmishReveal::Explored &&
        metadata.skirmish.scoreLimit == 5500 &&
        restored.mapHashForTesting() ==
            savedHash;
    report(
        "save-v6-settings-roundtrip",
        saveRoundTrip, err);

    return failures ? 1 : 0;
}

static int cmdTestInterface(const char *dataDir) {
    std::string err;
    SoftRenderer renderer;
    Assets assets(&renderer);
    if (!assets.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    int failures = 0;
    auto report = [&](const char *name, bool ok,
                      const std::string &detail) {
        printf(
            "%s %s %s\n",
            ok ? "PASS" : "FAIL",
            name, detail.c_str());
        if (!ok) ++failures;
    };

    bool transforms = true;
    float maximumError = 0.0f;
    for (int mapSize : {48, 96, 192}) {
        for (const auto &world :
             std::array<std::array<float, 2>, 5>{{
                 {{0.1f, 0.1f}},
                 {{(float)mapSize - 0.1f, 0.1f}},
                 {{0.1f, (float)mapSize - 0.1f}},
                 {{mapSize * 0.5f, mapSize * 0.5f}},
                 {{mapSize * 0.25f, mapSize * 0.75f}},
             }}) {
            const auto point =
                Game::minimapWorldToPoint(
                    world[0], world[1], mapSize,
                    17.0f, 9.0f, 168.0f);
            float x = 0.0f, y = 0.0f;
            const bool valid =
                Game::minimapPointToWorld(
                    point[0], point[1], mapSize,
                    17.0f, 9.0f, 168.0f,
                    x, y);
            const float error =
                std::max(
                    std::abs(x - world[0]),
                    std::abs(y - world[1]));
            maximumError =
                std::max(maximumError, error);
            transforms =
                transforms && valid &&
                error < 0.001f;
        }
    }
    float outsideX = 0.0f, outsideY = 0.0f;
    transforms =
        transforms &&
        !Game::minimapPointToWorld(
            17.0f, 9.0f, 96,
            17.0f, 9.0f, 168.0f,
            outsideX, outsideY);
    report(
        "minimap-transform-roundtrip",
        transforms,
        "max-error=" +
            std::to_string(maximumError));

    SkirmishSettings settings;
    settings.seed = 0xC0FFEEu;
    settings.mapSize = 96;
    settings.playerCivilization = 3;
    settings.computerCivilization = 3;
    settings.mapStyle =
        SkirmishMapStyle::Grasslands;
    settings.startingResources = 1000;
    Game game(assets);
    if (!game.initSkirmish(settings, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    const uint32_t first =
        game.spawnObjectForTesting(
            3, 83, 1, 30.0f, 30.0f);
    const uint32_t second =
        game.spawnObjectForTesting(
            3, 83, 1, 31.0f, 30.0f);
    const uint32_t enemy =
        game.spawnObjectForTesting(
            3, 83, 2, 32.0f, 30.0f);
    const bool groupAssigned =
        first && second && enemy &&
        game.selectObjectsForTesting(
            {first, second}) &&
        game.assignControlGroup(0);
    game.damageObjectForTesting(
        second, 100000, enemy);
    const bool groupRecall =
        game.recallControlGroup(0, true) &&
        game.controlGroupForTesting(0).size() ==
            1 &&
        game.selectedObjectIds().size() == 1 &&
        game.selectedObjectIds().front() == first;
    report(
        "control-groups-cleanup-order",
        groupAssigned && groupRecall,
        "members=" +
            std::to_string(
                game.controlGroupForTesting(0)
                    .size()));

    const size_t exploredBefore =
        game.exploredTileCountForTesting(1);
    game.damageObjectForTesting(first, 1, enemy);
    InputState minimapInput;
    minimapInput.screenW = 960;
    minimapInput.screenH = 544;
    minimapInput.pointerX = 866.0f;
    minimapInput.pointerY = 94.0f;
    minimapInput.cursorVisible = true;
    minimapInput.pointerTap = true;
    game.update(0.05f, minimapInput);
    const auto camera =
        game.cameraCenterForTesting();
    const bool minimapNavigation =
        std::abs(camera[0]) < 0.01f &&
        std::abs(
            camera[1] -
            settings.mapSize *
                Game::kTileHalfH) <
            0.1f;
    renderer.beginFrame(
        960, 544, 1.0f,
        0, 0, 0);
    game.render(renderer, 960, 544);
    const int fogDrawCalls =
        renderer.drawCalls();
    game.setVisibilityCheatsForTesting(
        false, true);
    renderer.beginFrame(
        960, 544, 1.0f,
        0, 0, 0);
    game.render(renderer, 960, 544);
    const int clearDrawCalls =
        renderer.drawCalls();
    game.setVisibilityCheatsForTesting(
        false, false);
    const bool boundedFogDraws =
        fogDrawCalls <=
            clearDrawCalls + 1 &&
        fogDrawCalls < 800;
    const bool fogAndAlerts =
        exploredBefore > 0 &&
        exploredBefore <
            (size_t)settings.mapSize *
                settings.mapSize &&
        game.minimapAlertCountForTesting() > 0 &&
        fogDrawCalls > 0 &&
        boundedFogDraws;
    report(
        "minimap-fog-navigation-alerts",
        minimapNavigation && fogAndAlerts,
        "explored=" +
            std::to_string(exploredBefore) +
            " alerts=" +
            std::to_string(
                game.minimapAlertCountForTesting()) +
            " draws=" +
            std::to_string(fogDrawCalls) +
            "/" +
            std::to_string(clearDrawCalls) +
            " camera=" +
            std::to_string(camera[0]) + "," +
            std::to_string(camera[1]));

    const char *settingsPath =
        "swgb-interface-settings.tmp";
    std::remove(settingsPath);
    UserSettings savedOptions;
    savedOptions.masterVolume = 55;
    savedOptions.musicVolume = 35;
    savedOptions.dialogueVolume = 80;
    savedOptions.effectsVolume = 25;
    savedOptions.controls =
        ControlPreset::LeftHanded;
    UserSettings loadedOptions;
    const bool settingsSaved =
        saveSettings(
            settingsPath, savedOptions, &err);
    const bool settingsLoaded =
        settingsSaved &&
        loadSettings(
            settingsPath, loadedOptions, &err) &&
        loadedOptions.masterVolume == 55 &&
        loadedOptions.musicVolume == 35 &&
        loadedOptions.dialogueVolume == 80 &&
        loadedOptions.effectsVolume == 25 &&
        loadedOptions.controls ==
            ControlPreset::LeftHanded;
    FILE *corrupt =
        std::fopen(settingsPath, "wb");
    if (corrupt) {
        std::fwrite("bad", 1, 3, corrupt);
        std::fclose(corrupt);
    }
    std::string corruptError;
    const bool corruptRejected =
        !loadSettings(
            settingsPath, loadedOptions,
            &corruptError) &&
        !corruptError.empty();
    std::remove(settingsPath);
    report(
        "settings-persistence-rejection",
        settingsLoaded && corruptRejected,
        corruptError);

    const char *savePath =
        "swgb-interface-match.tmp";
    std::remove(savePath);
    game.moveObjectForTesting(
        first, 34.0f, 35.0f);
    game.selectObjectForTesting(first);
    game.assignControlGroup(1);
    game.update(0.75f, InputState{});
    const auto savedPosition =
        game.objectPosition(first);
    const int savedHealth =
        game.objectHealthForTesting(first);
    const size_t savedObjects =
        game.activeObjectCount();
    const size_t savedExplored =
        game.exploredTileCountForTesting(1);
    const float savedTime =
        game.simulationTimeForTesting();
    bool roundTrip =
        game.saveMatch(savePath, &err);
    if (roundTrip) {
        game.damageObjectForTesting(
            first, 100000, enemy);
        game.update(1.0f, InputState{});
        roundTrip =
            game.loadMatch(savePath, &err);
    }
    const auto loadedPosition =
        game.objectPosition(first);
    roundTrip =
        roundTrip &&
        game.objectActive(first) &&
        game.objectHealthForTesting(first) ==
            savedHealth &&
        std::abs(
            loadedPosition[0] -
            savedPosition[0]) < 0.001f &&
        std::abs(
            loadedPosition[1] -
            savedPosition[1]) < 0.001f &&
        game.activeObjectCount() ==
            savedObjects &&
        game.exploredTileCountForTesting(1) ==
            savedExplored &&
        std::abs(
            game.simulationTimeForTesting() -
            savedTime) < 0.001f &&
        game.controlGroupForTesting(1).size() ==
            1;
    bool repeated = roundTrip;
    for (int cycle = 0;
         cycle < 3 && repeated; ++cycle) {
        game.update(0.2f, InputState{});
        repeated =
            game.saveMatch(savePath, &err) &&
            game.loadMatch(savePath, &err);
    }
    report(
        "save-load-authoritative-roundtrip",
        roundTrip && repeated,
        roundTrip
            ? "objects=" +
                  std::to_string(savedObjects) +
                  " time=" +
                  std::to_string(savedTime)
            : err);

    FILE *versionFile =
        std::fopen(savePath, "r+b");
    if (versionFile) {
        std::fseek(versionFile, 8, SEEK_SET);
        const uint32_t unsupported = 99;
        std::fwrite(
            &unsupported, 1,
            sizeof unsupported, versionFile);
        std::fclose(versionFile);
    }
    SkirmishSettings probed;
    std::string versionError;
    const bool versionRejected =
        !Game::readSaveSettings(
            savePath, probed, &versionError) &&
        versionError.find("version") !=
            std::string::npos;
    std::remove(savePath);
    report(
        "save-version-rejection",
        versionRejected, versionError);

    Frontend frontend;
    InputState input;
    input.menuActivate = true;
    frontend.update(input, -1);
    input = {};
    input.menuDown = true;
    frontend.update(input, -1);
    input = {};
    input.menuActivate = true;
    frontend.update(input, -1);
    input = {};
    input.menuDown = true;
    for (int row = 0; row < 11; ++row)
        frontend.update(input, -1);
    input = {};
    input.menuActivate = true;
    frontend.update(input, -1);
    frontend.loadingFinished(true);
    game.update(0.1f, InputState{});
    const float beforePause =
        game.simulationTimeForTesting();
    input = {};
    input.pausePressed = true;
    frontend.update(input, -1);
    if (frontend.screen() ==
        FrontendScreen::Gameplay)
        game.update(10.0f, InputState{});
    const bool paused =
        frontend.screen() ==
            FrontendScreen::Pause &&
        game.simulationTimeForTesting() ==
            beforePause;
    report(
        "pause-freezes-simulation",
        paused,
        "time=" +
            std::to_string(beforePause));

    return failures ? 1 : 0;
}

static int cmdTestCoreGameplay(
    const char *dataDir) {
    std::string err;
    SoftRenderer renderer;
    Assets assets(&renderer);
    if (!assets.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n",
                err.c_str());
        return 1;
    }
    int failures = 0;
    auto report =
        [&](const char *name, bool ok,
            const std::string &detail) {
            printf(
                "%s %s %s\n",
                ok ? "PASS" : "FAIL",
                name, detail.c_str());
            if (!ok)
                failures++;
        };
    auto step = [](Game &game,
                   float seconds) {
        for (float elapsed = 0.0f;
             elapsed < seconds;
             elapsed += 1.0f / 30.0f)
            game.update(
                1.0f / 30.0f, {});
    };

    int fishId = -1;
    for (const dat::Unit &unit :
         assets.dat().civs[0].units) {
        if (!unit.exists ||
            (unit.cls != 23 &&
             unit.cls != 24 &&
             unit.cls != 25))
            continue;
        const bool storesFish =
            std::any_of(
                unit.resourceStorages.begin(),
                unit.resourceStorages.end(),
                [](const dat::ResourceStorage
                       &storage) {
                    return storage.type == 17 &&
                           storage.amount > 0.0f;
                });
        if (storesFish) {
            fishId = unit.id;
            break;
        }
    }

    {
        Game game(assets);
        if (!game.initCompactTestMap(
                0xF157u, 96, &err)) {
            fprintf(stderr, "error: %s\n",
                    err.c_str());
            return 1;
        }
        game.setLocalPlayerForTesting(1);
        const int civilization =
            game.civilizationForPlayerForTesting(
                1);
        const uint32_t trawler =
            game.spawnObjectForTesting(
                civilization, 13, 1,
                46.5f, 40.5f);
        const uint32_t fish =
            fishId >= 0
                ? game.spawnObjectForTesting(
                      0, fishId, 0,
                      47.5f, 40.5f)
                : 0;
        const size_t generatedFish =
            fishId >= 0
                ? game.objectCountForTesting(
                      0, fishId)
                : 0;
        const bool ordered =
            trawler && fish &&
            game.issueGatherForTesting(
                trawler, fish);
        step(game, 5.0f);
        const float carried =
            game.objectCarriedAmount(
                trawler);
        const bool fishing =
            ordered && carried > 0.01f &&
            game.objectGatheringTarget(
                trawler, fish);
        const uint32_t shipyard =
            game.spawnObjectForTesting(
                civilization, 45, 1,
                43.5f, 40.5f);
        const float foodBefore =
            game.resource(
                1, 0);
        step(game, 45.0f);
        const float foodAfter =
            game.resource(
                1, 0);
        const bool deposited =
            shipyard &&
            foodAfter > foodBefore + 0.01f;

        game.researchTechnologyForTesting(
            1, 27);
        const std::vector<int> options =
            game.buildingOptionIds(
                trawler);
        const bool harvesterMenu =
            std::find(
                options.begin(),
                options.end(), 199) !=
            options.end();
        const bool buoyMenu =
            std::find(
                options.begin(),
                options.end(), 1576) !=
            options.end();

        const uint32_t aqua =
            game.spawnObjectForTesting(
                civilization, 199, 1,
                45.5f, 44.5f);
        const uint32_t aquaTrawler =
            game.spawnObjectForTesting(
                civilization, 13, 1,
                45.5f, 43.5f);
        const bool aquaOrdered =
            aqua && aquaTrawler &&
            game.issueGatherForTesting(
                aquaTrawler, aqua);
        step(game, 3.0f);
        const bool aquaWorked =
            aquaOrdered &&
            game.objectCarriedAmount(
                aquaTrawler) > 0.01f;

        const uint32_t damaged =
            game.spawnObjectForTesting(
                civilization, 13, 1,
                46.5f, 43.5f);
        game.setResourceForTesting(
            1, 1, 1000.0f);
        const int fullHealth =
            game.objectHealthForTesting(
                damaged);
        game.damageObjectForTesting(
            damaged, 20);
        const int damagedHealth =
            game.objectHealthForTesting(
                damaged);
        const bool repairOrdered =
            game.issueRepairForTesting(
                aquaTrawler, damaged);
        step(game, 4.0f);
        const int repairedHealth =
            game.objectHealthForTesting(
                damaged);

        const char *savePath =
            "swgb-core-fishing.tmp";
        std::remove(savePath);
        const bool saved =
            game.saveMatch(
                savePath, &err);
        const float savedCarry =
            game.objectCarriedAmount(
                trawler);
        const bool loaded =
            saved &&
            game.loadMatch(
                savePath, &err);
        const bool continuity =
            loaded &&
            std::abs(
                game.objectCarriedAmount(
                    trawler) -
                savedCarry) < 0.001f &&
            game.invariantsForTesting();
        const float loadedCarry =
            game.objectCarriedAmount(
                trawler);
        const bool validAfterLoad =
            game.invariantsForTesting();
        std::remove(savePath);

        report(
            "fishing-task-carry",
            fishId >= 0 &&
                generatedFish >= 3 &&
                fishing && deposited,
            "fish=" +
                std::to_string(fishId) +
                " generated=" +
                std::to_string(
                    generatedFish) +
                " carried=" +
                std::to_string(carried) +
                " food=" +
                std::to_string(
                    foodBefore) +
                "->" +
                std::to_string(
                    foodAfter));
        report(
            "trawler-menu-aqua",
            harvesterMenu && buoyMenu &&
                aquaWorked,
            "199=" +
                std::to_string(
                    (int)harvesterMenu) +
                " 1576=" +
                std::to_string(
                    (int)buoyMenu) +
                " aqua-carry=" +
                std::to_string(
                    game.objectCarriedAmount(
                        aquaTrawler)));
        report(
            "trawler-repair-save",
            repairOrdered &&
                repairedHealth >
                    damagedHealth &&
                repairedHealth <=
                    fullHealth &&
                continuity,
            "health=" +
                std::to_string(
                    damagedHealth) +
                "->" +
                std::to_string(
                    repairedHealth) +
                " save=" +
                std::to_string(
                    (int)continuity) +
                " loaded=" +
                std::to_string(
                    (int)loaded) +
                " carry=" +
                std::to_string(
                    savedCarry) +
                "/" +
                std::to_string(
                    loadedCarry) +
                " valid=" +
                std::to_string(
                    (int)validAfterLoad) +
                " error=" + err);
    }

    {
        const int attributes[] = {
            2, 11, 13, 14, 15,
        };
        for (int attribute :
             attributes) {
            Game game(assets);
            game.init(0xA770u +
                          (uint32_t)attribute,
                      48, &err);
            const int civilization =
                game.civilizationForPlayerForTesting(
                    1);
            bool changed = false;
            int testedUnit = -1;
            int testedTech = -1;
            float before = 0.0f;
            float after = 0.0f;
            bool hasApplicableCommand =
                false;
            const auto &units =
                assets.dat()
                    .civs[(size_t)civilization]
                    .units;
            for (const dat::Unit &unit :
                 units) {
                if (!unit.exists)
                    continue;
                float base = 0.0f;
                if (attribute == 2)
                    base =
                        (float)unit
                            .garrisonCapacity;
                else if (attribute == 11)
                    base =
                        (float)unit
                            .accuracyPercent;
                else if (attribute == 13)
                    base = unit.workRate;
                else if (attribute == 14)
                    base =
                        unit.resourceCapacity;
                else if (attribute == 15)
                    base = unit.baseArmor;
                if (base <= 0.0f &&
                    attribute != 15)
                    continue;
                for (size_t technologyId = 0;
                     technologyId <
                         assets.dat()
                             .techs.size();
                     ++technologyId) {
                    const dat::Tech &technology =
                        assets.dat().techs[
                            technologyId];
                    if (technology.effectId < 0 ||
                        (size_t)technology
                                .effectId >=
                            assets.dat()
                                .effects.size())
                        continue;
                    bool applicable = false;
                    for (const dat::EffectCommand
                             &command :
                         assets.dat()
                             .effects[(size_t)
                                          technology
                                              .effectId]
                             .commands) {
                        if (command.c !=
                                attribute ||
                            (command.type != 0 &&
                             command.type != 4 &&
                             command.type != 5) ||
                            (command.a >= 0 &&
                             command.a != unit.id) ||
                            (command.b >= 0 &&
                             command.b !=
                                 unit.cls))
                            continue;
                        hasApplicableCommand =
                            true;
                        applicable = true;
                        break;
                    }
                    if (!applicable)
                        continue;
                    const uint32_t object =
                        game.spawnObjectForTesting(
                            civilization,
                            unit.id, 1,
                            20.0f, 20.0f);
                    if (!object)
                        continue;
                    before =
                        game.objectUnitAttributeForTesting(
                            object, attribute,
                            base);
                    game.researchTechnologyForTesting(
                        1,
                        (int)technologyId);
                    after =
                        game.objectUnitAttributeForTesting(
                            object, attribute,
                            base);
                    if (std::abs(
                            after - before) >
                        0.0001f) {
                        changed = true;
                        testedUnit = unit.id;
                        testedTech =
                            (int)technologyId;
                        break;
                    }
                }
                if (changed)
                    break;
            }
            report(
                ("technology-attribute-" +
                 std::to_string(attribute))
                    .c_str(),
                changed ||
                    !hasApplicableCommand,
                "unit=" +
                    std::to_string(
                        testedUnit) +
                    " tech=" +
                    std::to_string(
                        testedTech) +
                    " value=" +
                    std::to_string(before) +
                    "->" +
                    std::to_string(after) +
                    (!hasApplicableCommand
                         ? " no-applicable-command"
                         : ""));
        }
    }

    {
        int hits = 0;
        int misses = 0;
        bool deterministic = true;
        std::array<float, 2>
            referenceAim{};
        bool haveReference = false;
        for (uint32_t seed = 1;
             seed <= 16; ++seed) {
            Game game(assets);
            if (!game.init(seed, 48,
                           &err)) {
                fprintf(
                    stderr, "error: %s\n",
                    err.c_str());
                return 1;
            }
            game.setDiplomacyForTesting(
                1, 2, 3);
            game.setDiplomacyForTesting(
                2, 1, 3);
            const uint32_t source =
                game.spawnObjectForTesting(
                    7, 6, 1,
                    20.0f, 20.0f);
            const uint32_t target =
                game.spawnObjectForTesting(
                    7, 460, 2,
                    25.0f, 20.0f);
            game.setAttackModeForTesting(
                source, 3);
            game.setAttackModeForTesting(
                target, 3);
            if (!game.issueAttackForTesting(
                    source, target))
                continue;
            for (int frame = 0;
                 frame < 300 &&
                 game.projectileCountForTesting() ==
                     0;
                 ++frame)
                game.update(
                    1.0f / 30.0f, {});
            if (game.projectileCountForTesting() ==
                0)
                continue;
            const auto aim =
                game.projectileAimForTesting(0);
            const bool fixed =
                game.projectileUsesFixedAimForTesting(
                    0);
            const float dx = aim[0] - 25.0f;
            const float dy = aim[1] - 20.0f;
            if (fixed &&
                dx * dx + dy * dy > 0.25f)
                misses++;
            else
                hits++;
            if (seed == 7) {
                referenceAim = aim;
                haveReference = true;
                const char *savePath =
                    "swgb-core-projectile.tmp";
                std::remove(savePath);
                const bool saved =
                    game.saveMatch(
                        savePath, &err);
                const bool loaded =
                    saved &&
                    game.loadMatch(
                        savePath, &err);
                const auto loadedAim =
                    game.projectileAimForTesting(
                        0);
                deterministic =
                    deterministic && loaded &&
                    std::abs(
                        loadedAim[0] -
                        aim[0]) <
                        0.0001f &&
                    std::abs(
                        loadedAim[1] -
                        aim[1]) <
                        0.0001f;
                std::remove(savePath);
            }
        }
        if (haveReference) {
            Game replay(assets);
            replay.init(7, 48, &err);
            replay.setDiplomacyForTesting(
                1, 2, 3);
            replay.setDiplomacyForTesting(
                2, 1, 3);
            const uint32_t source =
                replay.spawnObjectForTesting(
                    7, 6, 1,
                    20.0f, 20.0f);
            const uint32_t target =
                replay.spawnObjectForTesting(
                    7, 460, 2,
                    25.0f, 20.0f);
            replay.setAttackModeForTesting(
                source, 3);
            replay.setAttackModeForTesting(
                target, 3);
            replay.issueAttackForTesting(
                source, target);
            for (int frame = 0;
                 frame < 300 &&
                 replay.projectileCountForTesting() ==
                     0;
                 ++frame)
                replay.update(
                    1.0f / 30.0f, {});
            const auto aim =
                replay.projectileAimForTesting(0);
            deterministic =
                std::abs(
                    aim[0] -
                    referenceAim[0]) <
                    0.0001f &&
                std::abs(
                    aim[1] -
                    referenceAim[1]) <
                    0.0001f;
        }
        report(
            "accuracy-deterministic-misses",
            hits > 0 && misses > 0 &&
                deterministic,
            "hits=" +
                std::to_string(hits) +
                " misses=" +
                std::to_string(misses) +
                " replay=" +
                std::to_string(
                    (int)deterministic));
    }

    {
        Game game(assets);
        game.init(7, 96, &err);
        const int civilization =
            game.civilizationForPlayerForTesting(
                1);
        std::vector<uint32_t> troops;
        for (int index = 0;
             index < 12; ++index)
            troops.push_back(
                game.spawnObjectForTesting(
                    civilization, 460, 1,
                    40.0f +
                        (index % 4) * 0.8f,
                    11.0f +
                        (index / 4) * 0.8f));
        game.groupMoveForTesting(
            troops, 52.0f, 24.0f, 0);
        step(game, 3.0f);
        const char *savePath =
            "swgb-core-formation.tmp";
        std::remove(savePath);
        const bool saved =
            game.saveMatch(
                savePath, &err);
        const float shapeBefore =
            game.marchShapeErrorForTesting();
        const bool loaded =
            saved &&
            game.loadMatch(
                savePath, &err);
        const float shapeAfter =
            game.marchShapeErrorForTesting();
        game.damageObjectForTesting(
            troops.front(), 10000);
        game.update(
            1.0f / 30.0f, {});
        const size_t membersAfterDeath =
            game.formationMemberCountForTesting();
        for (int frame = 0;
             frame < 30 * 90 &&
             game.movementStats()
                     .pendingMoveGoals >
                 0;
             ++frame)
            game.update(
                1.0f / 30.0f, {});
        const MovementStats movement =
            game.movementStats();
        std::remove(savePath);
        report(
            "formation-save-narrow-passage",
            loaded &&
                std::abs(
                    shapeBefore -
                    shapeAfter) < 0.001f &&
                movement.pendingMoveGoals ==
                    0 &&
                movement.overlappingPairs ==
                    0 &&
                movement.terrainViolations ==
                    0 &&
                membersAfterDeath ==
                    troops.size() - 1 &&
                game.invariantsForTesting(),
            "shape=" +
                std::to_string(
                    shapeBefore) +
                "/" +
                std::to_string(
                    shapeAfter) +
                " pending=" +
                std::to_string(
                    movement
                        .pendingMoveGoals) +
                " members=" +
                std::to_string(
                    membersAfterDeath) +
                " overlaps=" +
                std::to_string(
                    movement
                        .overlappingPairs));
    }

    {
        bool soak = true;
        int completed = 0;
        const SkirmishMapStyle styles[] = {
            SkirmishMapStyle::Grasslands,
            SkirmishMapStyle::Archipelago,
            SkirmishMapStyle::CompactIslands,
        };
        for (int index = 0;
             index < 3; ++index) {
            SkirmishSettings settings;
            settings.seed =
                0x500000u +
                (uint32_t)index;
            settings.mapSize = 48;
            settings.playerCivilization =
                1 + index;
            settings.computerCivilization =
                8 - index;
            settings.mapStyle =
                styles[index];
            settings.startingResources =
                1000;
            settings.populationCap = 75;
            Game game(assets);
            bool mapOk =
                game.initSkirmish(
                    settings, &err);
            if (!mapOk)
                printf(
                    "DETAIL soak style=%s error=%s\n",
                    mapStyleName(
                        settings.mapStyle),
                    err.c_str());
            const char *savePath =
                "swgb-core-soak.tmp";
            std::remove(savePath);
            for (int frame = 0;
                 frame < 600 && mapOk;
                 ++frame) {
                game.update(0.2f, {});
                if (frame == 299)
                    mapOk =
                        game.saveMatch(
                            savePath, &err) &&
                        game.loadMatch(
                            savePath, &err);
                mapOk =
                    mapOk &&
                    game.invariantsForTesting();
            }
            std::remove(savePath);
            if (!mapOk)
                printf(
                    "DETAIL soak runtime style=%s invariant=%d error=%s\n",
                    mapStyleName(
                        settings.mapStyle),
                    game.invariantsForTesting(),
                    err.c_str());
            soak = soak && mapOk;
            if (mapOk)
                completed++;
        }
        report(
            "generated-match-soak",
            soak && completed == 3,
            "maps=" +
                std::to_string(
                    completed) +
                "/3");
    }

    return failures ? 1 : 0;
}

static int cmdTestMajorMechanics(
    const char *dataDir) {
    std::string err;
    SoftRenderer renderer;
    Assets assets(&renderer);
    if (!assets.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n",
                err.c_str());
        return 1;
    }
    int failures = 0;
    auto report =
        [&](const char *name, bool ok,
            const std::string &detail) {
            printf(
                "%s %s %s\n",
                ok ? "PASS" : "FAIL",
                name, detail.c_str());
            if (!ok)
                failures++;
        };
    auto step = [](Game &game,
                   float seconds) {
        for (float elapsed = 0.0f;
             elapsed < seconds;
             elapsed += 1.0f / 30.0f)
            game.update(
                1.0f / 30.0f, {});
    };
    auto compact = [&]() {
        Game game(assets);
        if (!game.initCompactTestMap(
                0x4D414A4Fu, 96,
                &err)) {
            fprintf(
                stderr, "error: %s\n",
                err.c_str());
            std::exit(1);
        }
        game.setLocalPlayerForTesting(1);
        return game;
    };

    {
        Game game = compact();
        const int civilization =
            game.civilizationForPlayerForTesting(
                2);
        const uint32_t converter =
            game.spawnConverterForTesting(
                1, 30.0f, 30.0f);
        const uint32_t target =
            game.spawnObjectForTesting(
                civilization, 83, 2,
                30.3f, 30.0f);
        game.setAttackModeForTesting(
            target, 3);
        const bool issued =
            game.issueConversionForTesting(
                converter, target);
        const float initialPower =
            game.conversionPowerPercentForTesting(
                converter);
        step(game, 4.5f);
        const float depletedPower =
            game.conversionPowerPercentForTesting(
                converter);
        const bool converted =
            target &&
            game.objectPlayer(target) == 1 &&
            game.conversionChargeForTesting(
                converter) > 0.0f;
        const bool rechargeBlocks =
            !game.issueConversionForTesting(
                converter, target);
        const uint32_t temple =
            game.spawnTempleForTesting(
                2, 31.0f, 30.0f);
        const uint32_t holocron =
            game.spawnHolocronForTesting(
                31.2f, 30.0f);
        const uint32_t immunityConverter =
            game.spawnConverterForTesting(
                1, 31.0f, 30.3f);
        const bool immuneTargets =
            !game.issueConversionForTesting(
                immunityConverter, temple) &&
            !game.issueConversionForTesting(
                immunityConverter, holocron);
        step(game, 10.0f);
        const float rechargedPower =
            game.conversionPowerPercentForTesting(
                converter);
        report(
            "conversion-lifecycle",
            converter && target && issued &&
                converted && rechargeBlocks &&
                immuneTargets &&
                std::abs(
                    initialPower -
                    100.0f) < 0.01f &&
                depletedPower >= 0.0f &&
                depletedPower < 10.0f &&
                std::abs(
                    rechargedPower -
                    100.0f) < 0.01f &&
                game.convertCommandIconForTesting() ==
                    14 &&
                game.invariantsForTesting(),
            "owner=" +
                std::to_string(
                    game.objectPlayer(target)) +
                " recharge=" +
                std::to_string(
                    game.conversionChargeForTesting(
                        converter)) +
                " power=" +
                std::to_string(
                    initialPower) +
                "->" +
                std::to_string(
                    depletedPower) +
                "->" +
                std::to_string(
                    rechargedPower) +
                " icon=" +
                std::to_string(
                    game.convertCommandIconForTesting()));
    }

    {
        Game game = compact();
        const int civilization =
            game.civilizationForPlayerForTesting(
                2);
        const uint32_t converter =
            game.spawnConverterForTesting(
                1, 33.0f, 33.0f);
        const uint32_t target =
            game.spawnObjectForTesting(
                civilization, 115, 2,
                33.3f, 33.0f);
        game.setAttackModeForTesting(
            target, 3);
        const bool researched =
            game.researchTechnologyForTesting(
                2, 155);
        const bool issued =
            game.issueConversionForTesting(
                converter, target);
        step(game, 4.2f);
        const bool resisted =
            game.objectPlayer(target) == 2;
        step(game, 2.1f);
        const bool completed =
            game.objectPlayer(target) == 1;
        report(
            "conversion-resistance",
            researched && issued &&
                resisted && completed,
            "research/owner-mid/final=" +
                std::to_string(researched) +
                "/" +
                std::to_string(
                    resisted ? 2
                              : game.objectPlayer(
                                    target)) +
                "/" +
                std::to_string(
                    game.objectPlayer(target)));
    }

    {
        Game game = compact();
        const uint32_t carrier =
            game.spawnConverterForTesting(
                1, 32.0f, 32.0f);
        const uint32_t holocron =
            game.spawnHolocronForTesting(
                32.2f, 32.0f);
        const uint32_t temple =
            game.spawnTempleForTesting(
                1, 32.4f, 32.0f);
        const bool pickup =
            game.issueHolocronOrderForTesting(
                carrier, holocron);
        step(game, 0.3f);
        const bool carried =
            game.carriedHolocronForTesting(
                carrier) == holocron &&
            game.carriedHolocronGraphicForTesting(
                carrier) == 5200;
        const int carriedGraphic =
            game.carriedHolocronGraphicForTesting(
                carrier);
        const bool deposit =
            game.issueHolocronOrderForTesting(
                carrier, temple);
        step(game, 0.3f);
        const float novaBefore =
            game.resource(1, 3);
        step(game, 60.0f);
        const float novaGain =
            game.resource(1, 3) -
            novaBefore;
        const bool stored =
            game.storedHolocronCountForTesting(
                1) == 1;

        const uint32_t carrier2 =
            game.spawnConverterForTesting(
                1, 34.0f, 34.0f);
        const uint32_t holocron2 =
            game.spawnHolocronForTesting(
                34.2f, 34.0f);
        game.issueHolocronOrderForTesting(
            carrier2, holocron2);
        step(game, 0.3f);
        game.damageObjectForTesting(
            carrier2, 100000);
        const bool dropped =
            game.holocronHolderForTesting(
                holocron2) == -1;
        report(
            "holocron-lifecycle",
            carrier && holocron && temple &&
                pickup && carried && deposit &&
                stored && novaGain > 40.0f &&
                dropped &&
                game.invariantsForTesting(),
            "stored=" +
                std::to_string(
                    game.storedHolocronCountForTesting(
                        1)) +
                " nova=" +
                std::to_string(novaGain) +
                " graphic=" +
                std::to_string(
                    carriedGraphic) +
                " dropped=" +
                std::to_string(dropped));
    }

    {
        Game game = compact();
        const uint32_t stealthUnit =
            game.spawnConverterForTesting(
                2, 40.0f, 40.0f);
        const bool stealthEnabled =
            game.setStealthedForTesting(
                2, true);
        game.updateVisibilityForTesting();
        const bool concealed =
            game.objectStealthedForTesting(
                stealthUnit) &&
            !game.objectDetectedForTesting(
                1, stealthUnit);
        const uint32_t converter =
            game.spawnConverterForTesting(
                1, 39.7f, 40.0f);
        const bool concealedConversionBlocked =
            !game.issueConversionForTesting(
                converter, stealthUnit);
        game.setDiplomacyForTesting(
            1, 3, 3);
        const uint32_t sharedDetector =
            game.spawnDetectorForTesting(
                3, 40.5f, 40.0f);
        game.updateVisibilityForTesting();
        const bool hostileDetectorHidden =
            !game.objectDetectedForTesting(
                1, stealthUnit);
        game.setDiplomacyForTesting(
            1, 3, 0);
        game.updateVisibilityForTesting();
        const bool alliedDetectorReveal =
            game.objectDetectedForTesting(
                1, stealthUnit);
        const bool revealedConversionAllowed =
            game.issueConversionForTesting(
                converter, stealthUnit);
        game.stopUnitForTesting(converter);
        game.setDiplomacyForTesting(
            1, 3, 3);
        const uint32_t detector =
            game.spawnDetectorForTesting(
                1, 40.5f, 40.0f);
        game.updateVisibilityForTesting();
        const bool revealed =
            game.objectDetectedForTesting(
                1, stealthUnit);
        game.moveObjectForTesting(
            detector, 80.0f, 80.0f);
        game.updateVisibilityForTesting();
        const bool concealedAgain =
            !game.objectDetectedForTesting(
                1, stealthUnit);
        game.setVisibilityCheatsForTesting(
            false, true);
        const bool cheatReveal =
            game.objectDetectedForTesting(
                1, stealthUnit);
        game.setVisibilityCheatsForTesting(
            false, false);
        uint32_t stealthAttacker =
            game.spawnObjectForTesting(
                game
                    .civilizationForPlayerForTesting(
                        2),
                641, 2, 60.0f, 60.0f);
        if (!stealthAttacker)
            stealthAttacker =
                game.spawnObjectForTesting(
                    game
                        .civilizationForPlayerForTesting(
                            2),
                    638, 2,
                    60.0f, 60.0f);
        const uint32_t attackTarget =
            game.spawnObjectForTesting(
                game
                    .civilizationForPlayerForTesting(
                        1),
                109, 1, 60.5f, 60.0f);
        const bool attackIssued =
            game.issueAttackForTesting(
                stealthAttacker,
                attackTarget);
        step(game, 0.5f);
        game.updateVisibilityForTesting();
        const bool attackBreaksStealth =
            attackIssued &&
            !game.objectStealthedForTesting(
                stealthAttacker) &&
            game.objectDetectedForTesting(
                1, stealthAttacker);
        report(
            "stealth-detection",
            stealthUnit && detector &&
                sharedDetector &&
                stealthEnabled && concealed &&
                concealedConversionBlocked &&
                hostileDetectorHidden &&
                alliedDetectorReveal &&
                revealedConversionAllowed &&
                revealed && concealedAgain &&
                cheatReveal &&
                attackBreaksStealth,
            "concealed/convert-blocked/hostile/allied/convert/revealed/reset/cheat/attack=" +
                std::to_string(concealed) +
                "/" +
                std::to_string(
                    concealedConversionBlocked) +
                "/" +
                std::to_string(
                    hostileDetectorHidden) +
                "/" +
                std::to_string(
                    alliedDetectorReveal) +
                "/" +
                std::to_string(
                    revealedConversionAllowed) +
                "/" +
                std::to_string(revealed) +
                "/" +
                std::to_string(
                    concealedAgain) +
                "/" +
                std::to_string(
                    cheatReveal) +
                "/" +
                std::to_string(
                    attackBreaksStealth) +
                " attacker/target/issued/stealthed/detected=" +
                std::to_string(
                    stealthAttacker) +
                "/" +
                std::to_string(
                    attackTarget) +
                "/" +
                std::to_string(
                    attackIssued) +
                "/" +
                std::to_string(
                    game.objectStealthedForTesting(
                        stealthAttacker)) +
                "/" +
                std::to_string(
                    game.objectDetectedForTesting(
                        1,
                        stealthAttacker)));
    }

    {
        Game game = compact();
        const int civilization =
            game.civilizationForPlayerForTesting(
                1);
        int buildingDetector = -1;
        int mobileDetector = -1;
        for (const dat::Unit &unit :
             assets.dat()
                 .civs[(size_t)civilization]
                 .units) {
            if (!unit.exists ||
                (unit.trait & 8u) == 0)
                continue;
            if (unit.type ==
                dat::UT_Building)
                buildingDetector = unit.id;
            else
                mobileDetector = unit.id;
        }
        const uint32_t stealthUnit =
            game.spawnConverterForTesting(
                2, 52.0f, 52.0f);
        game.setStealthedForTesting(
            2, true);
        const uint32_t building =
            buildingDetector >= 0
                ? game.spawnObjectForTesting(
                      civilization,
                      buildingDetector, 1,
                      52.5f, 52.0f)
                : 0;
        game.updateVisibilityForTesting();
        const bool buildingReveal =
            building &&
            game.objectDetectedForTesting(
                1, stealthUnit);
        game.moveObjectForTesting(
            building, 80.0f, 80.0f);
        const uint32_t mobile =
            mobileDetector >= 0
                ? game.spawnObjectForTesting(
                      civilization,
                      mobileDetector, 1,
                      52.5f, 52.0f)
                : 0;
        game.updateVisibilityForTesting();
        const bool mobileReveal =
            mobile &&
            game.objectDetectedForTesting(
                1, stealthUnit);
        report(
            "detector-buildings-and-units",
            buildingDetector >= 0 &&
                mobileDetector >= 0 &&
                buildingReveal &&
                mobileReveal,
            "ids=" +
                std::to_string(
                    buildingDetector) +
                "/" +
                std::to_string(
                    mobileDetector) +
                " reveal=" +
                std::to_string(
                    buildingReveal) +
                "/" +
                std::to_string(
                    mobileReveal));
    }

    {
        Game game = compact();
        const int civ1 =
            game.civilizationForPlayerForTesting(
                1);
        const int civ2 =
            game.civilizationForPlayerForTesting(
                2);
        const uint32_t bomber =
            game.spawnObjectForTesting(
                civ1, 762, 1,
                42.0f, 42.0f);
        const uint32_t fighter =
            game.spawnObjectForTesting(
                civ2, 773, 2,
                43.0f, 42.0f);
        const uint32_t ground =
            game.spawnObjectForTesting(
                civ2, 83, 2,
                43.0f, 43.0f);
        const bool bomberRules =
            !game.canAttackTargetForTesting(
                bomber, fighter) &&
            game.canAttackTargetForTesting(
                bomber, ground);
        const bool fighterRules =
            game.canAttackTargetForTesting(
                fighter, bomber);
        game.researchTechnologyForTesting(
            1, 73);
        const bool shields =
            game.objectMaxShieldPoints(
                bomber) >= 0.0f;
        report(
            "aircraft-class-rules",
            bomber && fighter && ground &&
                bomberRules && fighterRules &&
                shields &&
                game.invariantsForTesting(),
            "bomber-air/ground=" +
                std::to_string(
                    game.canAttackTargetForTesting(
                        bomber, fighter)) +
                "/" +
                std::to_string(
                    game.canAttackTargetForTesting(
                        bomber, ground)) +
                " fighter-air=" +
                std::to_string(
                    fighterRules));
    }

    {
        Game monument = compact();
        monument.setVictoryConditionForTesting(
            SkirmishVictory::Standard);
        monument.setVictoryParametersForTesting(
            0.2f, 3600.0f, 4000);
        monument.spawnObjectForTesting(
            monument
                .civilizationForPlayerForTesting(
                    1),
            276, 1, 45.0f, 45.0f);
        step(monument, 0.5f);

        Game holocron = compact();
        holocron.setVictoryConditionForTesting(
            SkirmishVictory::Standard);
        holocron.setVictoryParametersForTesting(
            0.2f, 3600.0f, 4000);
        const uint32_t carrier =
            holocron
                .spawnConverterForTesting(
                    1, 35.0f, 35.0f);
        const uint32_t relic =
            holocron
                .spawnHolocronForTesting(
                    35.1f, 35.0f);
        const uint32_t temple =
            holocron
                .spawnTempleForTesting(
                    1, 35.2f, 35.0f);
        holocron
            .issueHolocronOrderForTesting(
                carrier, relic);
        step(holocron, 0.3f);
        holocron
            .issueHolocronOrderForTesting(
                carrier, temple);
        step(holocron, 0.6f);

        Game timed = compact();
        timed.setVictoryConditionForTesting(
            SkirmishVictory::TimeLimit);
        timed.setVictoryParametersForTesting(
            600.0f, 0.2f, 4000);
        timed.setResourceForTesting(
            1, 0, 10000.0f);
        timed.setResourceForTesting(
            2, 0, 0.0f);
        step(timed, 0.5f);

        Game timedNoConquest = compact();
        timedNoConquest
            .setVictoryConditionForTesting(
                SkirmishVictory::TimeLimit);
        timedNoConquest
            .setVictoryParametersForTesting(
                600.0f, 100.0f, 4000);
        timedNoConquest
            .eliminatePlayerForTesting(2);
        step(timedNoConquest, 0.5f);

        Game score = compact();
        score.setVictoryConditionForTesting(
            SkirmishVictory::Score);
        score.setVictoryParametersForTesting(
            600.0f, 3600.0f, 8000);
        score.setResourceForTesting(
            1, 0, 10000.0f);
        score.setResourceForTesting(
            2, 0, 0.0f);
        step(score, 0.2f);
        report(
            "victory-modes",
            monument.victoryStateForTesting() ==
                    1 &&
                holocron
                        .victoryStateForTesting() ==
                    1 &&
                timed.victoryStateForTesting() ==
                    1 &&
                timedNoConquest
                        .victoryStateForTesting() ==
                    -1 &&
                score.victoryStateForTesting() ==
                    1,
            "standard-monument=" +
                std::to_string(
                    monument
                        .victoryStateForTesting()) +
                " standard-holocron=" +
                std::to_string(
                    holocron
                        .victoryStateForTesting()) +
                " timed=" +
                std::to_string(
                    timed
                        .victoryStateForTesting()) +
                " timed-no-conquest=" +
                std::to_string(
                    timedNoConquest
                        .victoryStateForTesting()) +
                " score=" +
                std::to_string(
                    score
                        .victoryStateForTesting()));
    }

    {
        Game game = compact();
        game.setVictoryConditionForTesting(
            SkirmishVictory::Standard);
        game.setVictoryParametersForTesting(
            0.5f, 3600.0f, 4000);
        game.spawnObjectForTesting(
            game.civilizationForPlayerForTesting(
                2),
            276, 2, 46.0f, 46.0f);
        step(game, 0.3f);
        game.spawnObjectForTesting(
            game.civilizationForPlayerForTesting(
                1),
            276, 1, 48.0f, 48.0f);
        step(game, 0.3f);
        report(
            "independent-victory-countdowns",
            game.victoryStateForTesting() ==
                0,
            "outcome=" +
                std::to_string(
                    game.victoryStateForTesting()));
    }

    {
        Game game = compact();
        const auto base =
            game.playerBasePositionForTesting(
                2);
        const uint32_t holocron =
            game.spawnHolocronForTesting(
                base[0] + 0.2f, base[1]);
        const uint32_t carrier =
            game.spawnConverterForTesting(
                2, base[0], base[1]);
        game.issueHolocronOrderForTesting(
            carrier, holocron);
        step(game, 0.3f);
        game.eliminatePlayerForTesting(2);
        report(
            "elimination-releases-holocron",
            game.holocronCountForTesting() >=
                    1 &&
                game.holocronHolderForTesting(
                    holocron) == -1,
            "holder=" +
                std::to_string(
                    game.holocronHolderForTesting(
                        holocron)) +
                " total=" +
                std::to_string(
                    game.holocronCountForTesting()));
    }

    {
        SkirmishSettings settings;
        settings.seed = 0x53415645u;
        settings.mapSize = 96;
        settings.playerCivilization = 7;
        settings.computerCivilization = 5;
        settings.victory =
            SkirmishVictory::Standard;
        Game game(assets);
        const bool initialized =
            game.initSkirmish(
                settings, &err);
        game.setVictoryParametersForTesting(
            8.0f, 90.0f, 1200);
        const auto base =
            game.playerBasePositionForTesting(
                1);
        const uint32_t carrier =
            game.spawnConverterForTesting(
                1, base[0], base[1]);
        const uint32_t relic =
            game.spawnHolocronForTesting(
                base[0] + 0.2f, base[1]);
        game.issueHolocronOrderForTesting(
            carrier, relic);
        step(game, 0.3f);
        const std::string savePath =
            "test-major-mechanics.sav";
        const bool saved =
            initialized &&
            game.saveMatch(
                savePath, &err);
        Game loaded(assets);
        const bool loadedInit =
            loaded.initSkirmish(
                settings, &err);
        const bool restored =
            loadedInit &&
            loaded.loadMatch(
                savePath, &err);
        std::remove(savePath.c_str());
        report(
            "save-v5-roundtrip",
            saved && restored &&
                loaded
                        .victoryConditionForTesting() ==
                    SkirmishVictory::Standard &&
                loaded
                        .holocronCountForTesting() ==
                    game.holocronCountForTesting() &&
                loaded.invariantsForTesting(),
            "saved/loaded=" +
                std::to_string(saved) + "/" +
                std::to_string(restored) +
                " holocrons=" +
                std::to_string(
                    loaded
                        .holocronCountForTesting()) +
                (err.empty()
                     ? ""
                     : " error=" + err));
    }

    {
        SkirmishSettings settings;
        settings.seed = 0x41494348u;
        settings.mapSize = 96;
        settings.playerCivilization = 7;
        settings.computerCivilization = 5;
        settings.victory =
            SkirmishVictory::Standard;
        Game game(assets);
        const bool initialized =
            game.initSkirmish(
                settings, &err);
        const auto enemyBase =
            game.playerBasePositionForTesting(
                2);
        const auto localBase =
            game.playerBasePositionForTesting(
                1);
        const uint32_t carrier =
            game.spawnConverterForTesting(
                2, enemyBase[0],
                enemyBase[1]);
        const uint32_t hiddenRelic =
            game.spawnHolocronForTesting(
                localBase[0],
                localBase[1]);
        step(game, 1.0f);
        const bool hiddenSafe =
            game.carriedHolocronForTesting(
                carrier) == 0;
        game.moveObjectForTesting(
            carrier, localBase[0] + 0.2f,
            localBase[1]);
        game.stopUnitForTesting(carrier);
        step(game, 1.0f);
        const bool acquiredWhenVisible =
            game.carriedHolocronForTesting(
                carrier) == hiddenRelic;
        report(
            "ai-holocron-knowledge",
            initialized && carrier &&
                hiddenRelic && hiddenSafe &&
                acquiredWhenVisible,
            "hidden-safe/acquired=" +
                std::to_string(hiddenSafe) +
                "/" +
                std::to_string(
                    acquiredWhenVisible));
    }

    {
        SkirmishSettings land;
        land.seed = 0x484F4C4Fu;
        land.mapSize = 96;
        land.playerCivilization = 7;
        land.computerCivilization = 5;
        land.mapStyle =
            SkirmishMapStyle::Grasslands;
        land.victory =
            SkirmishVictory::Standard;
        Game landGame(assets);
        const bool landInitialized =
            landGame.initSkirmish(
                land, &err);
        SkirmishSettings island = land;
        island.mapStyle =
            SkirmishMapStyle::Archipelago;
        Game islandGame(assets);
        const bool islandInitialized =
            islandGame.initSkirmish(
                island, &err);
        report(
            "generated-victory-prerequisites",
            landInitialized &&
                islandInitialized &&
                landGame
                        .holocronCountForTesting() ==
                    5 &&
                islandGame
                        .holocronCountForTesting() ==
                    5 &&
                landGame.invariantsForTesting() &&
                islandGame.invariantsForTesting(),
            "land/island-holocrons=" +
                std::to_string(
                    landGame
                        .holocronCountForTesting()) +
                "/" +
                std::to_string(
                    islandGame
                        .holocronCountForTesting()));
    }

    return failures ? 1 : 0;
}

static int cmdTestCampaign(
    const char *dataDir, const char *campaignDir) {
    int failures = 0;
    const auto report =
        [&](const char *name, bool ok,
            const std::string &detail = {}) {
            printf(
                "  %-36s %s%s%s\n", name,
                ok ? "PASS" : "FAIL",
                detail.empty() ? "" : " - ",
                detail.c_str());
            if (!ok) ++failures;
        };

    StartupFlow startup;
    startup.validationFinished(false, "missing genie_x1.dat");
    const bool missing =
        startup.stage() == StartupStage::Error &&
        startup.message().find("genie_x1.dat") !=
            std::string::npos;
    startup.retry();
    startup.validationFinished(true);
    startup.dataFinished(true);
    startup.update(0, true);
    startup.update(0, true);
    startup.update(0, true);
    const bool startupTransitions =
        startup.stage() == StartupStage::LoadProfile;
    startup.profileFinished(true);
    report(
        "startup-missing-data-error", missing,
        startup.message());
    report(
        "startup-state-transitions",
        startupTransitions &&
            startup.stage() == StartupStage::Ready);

    SoftRenderer renderer;
    Assets assets(&renderer);
    std::string err;
    if (!assets.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    CampaignCatalog catalog;
    const bool discovered = catalog.discover(
        campaignDir,
        [&](int id, const std::string &fallback) {
            const std::string &localized =
                assets.localizedString(id);
            return localized.empty() ? fallback
                                     : localized;
        },
        &err);
    report(
        "campaign-archive-discovery",
        discovered &&
            catalog.campaigns().size() == 6,
        discovered
            ? std::to_string(catalog.campaigns().size()) +
                  " archives"
            : err);
    report(
        "all-43-scenarios-load-validated",
        discovered && catalog.missionCount() == 43,
        std::to_string(catalog.missionCount()) +
            " missions");
    bool metadata = discovered;
    if (discovered) {
        static constexpr int expectedOrder[] = {
            1, 2, 3, 4, 5, 8};
        metadata =
            catalog.campaigns().size() ==
            sizeof expectedOrder / sizeof expectedOrder[0];
        for (size_t c = 0;
             c < catalog.campaigns().size();
             ++c) {
            const CampaignInfo &campaign =
                catalog.campaigns()[c];
            metadata =
                metadata &&
                c < sizeof expectedOrder /
                        sizeof expectedOrder[0] &&
                campaign.originalNumber ==
                    expectedOrder[c];
            for (const CampaignMission &mission :
                 campaign.missions)
                metadata =
                    metadata && !mission.title.empty() &&
                    !mission.description.empty() &&
                    !mission.faction.empty() &&
                    mission.mapSize >= 48 &&
                    mission.unitCount > 0;
        }
    }
    report("campaign-metadata-order", metadata);

    bool conformance = discovered;
    size_t conformanceRuns = 0;
    size_t conformanceTriggers = 0;
    size_t conformanceConditions = 0;
    size_t conformanceEffects = 0;
    size_t deferredReferences = 0;
    size_t missingAiIncludes = 0;
    std::set<std::string>
        missingAiIncludeNames;
    std::set<std::string>
        unsupportedAiMessages;
    std::string conformanceError;
    const std::filesystem::path aiDirectory =
        std::filesystem::path(campaignDir)
            .parent_path() /
        "AI";
    if (discovered) {
        for (size_t campaignIndex = 0;
             campaignIndex <
                 catalog.campaigns().size() &&
             conformance;
             ++campaignIndex) {
            const CampaignInfo &campaign =
                catalog.campaigns()[
                    campaignIndex];
            for (size_t missionIndex = 0;
                 missionIndex <
                     campaign.missions.size() &&
                 conformance;
                 ++missionIndex) {
                Scenario scenario;
                if (!catalog.loadScenario(
                        campaignIndex,
                        missionIndex,
                        scenario,
                        &conformanceError)) {
                    conformance = false;
                    break;
                }
                std::unordered_set<uint32_t>
                    objectIds;
                for (const ScenarioUnit &unit :
                     scenario.units)
                    objectIds.insert(
                        unit.spawnId);
                bool difficultySensitive =
                    false;
                for (const ScenarioTrigger &trigger :
                     scenario.triggers) {
                    ++conformanceTriggers;
                    for (const ScenarioCondition &condition :
                         trigger.conditions) {
                        ++conformanceConditions;
                        if (!Game::
                                supportsTriggerCondition(
                                    condition.type)) {
                            conformance = false;
                            conformanceError =
                                "unsupported condition " +
                                std::to_string(
                                    condition.type);
                            break;
                        }
                        difficultySensitive =
                            difficultySensitive ||
                            condition.type == 13;
                        const auto field =
                            [&](size_t index) {
                                return index <
                                               condition
                                                   .fields
                                                   .size()
                                           ? condition
                                                 .fields[
                                                     index]
                                           : -1;
                            };
                        for (size_t index :
                             {size_t(2),
                              size_t(3)}) {
                            const int object =
                                field(index);
                            if (object > 0 &&
                                !objectIds.count(
                                    (uint32_t)
                                        object))
                                ++deferredReferences;
                        }
                        const int conditionPlayer =
                            field(5);
                        if (conditionPlayer > 16) {
                            conformance = false;
                            conformanceError =
                                "condition player is out of range";
                        }
                        if (!conformance) break;
                    }
                    if (!conformance) break;
                    for (const ScenarioEffect &effect :
                         trigger.effects) {
                        ++conformanceEffects;
                        if (!Game::
                                supportsTriggerEffect(
                                    effect.type)) {
                            conformance = false;
                            conformanceError =
                                "unsupported effect " +
                                std::to_string(
                                    effect.type);
                            break;
                        }
                        for (uint32_t object :
                             effect.selectedUnitIds)
                            if (object > 0 &&
                                !objectIds.count(
                                    object))
                                ++deferredReferences;
                        const auto field =
                            [&](size_t index) {
                                return index <
                                               effect.fields
                                                   .size()
                                           ? effect.fields[
                                                 index]
                                           : -1;
                            };
                        const int locationObject =
                            field(5);
                        if (locationObject > 0 &&
                            !objectIds.count(
                                (uint32_t)
                                    locationObject))
                            ++deferredReferences;
                        if (field(7) > 16 ||
                            field(8) > 16) {
                            conformance = false;
                            conformanceError =
                                "effect player is out of range";
                            break;
                        }
                    }
                    if (!conformance) break;
                }
                if (!conformance) {
                    conformanceError =
                        campaign.archiveName +
                        "/" +
                        campaign.missions[
                            missionIndex]
                            .title +
                        ": " +
                        conformanceError;
                    break;
                }
                const int firstDifficulty =
                    difficultySensitive ? 0 : 2;
                const int lastDifficulty =
                    difficultySensitive ? 4 : 2;
                for (int difficulty =
                         firstDifficulty;
                     difficulty <=
                         lastDifficulty &&
                     conformance;
                     ++difficulty) {
                    Game game(assets);
                    game.setLogger(
                        [&](const std::string
                                &message) {
                            if (message.rfind(
                                    "AI fact not implemented:",
                                    0) == 0 ||
                                message.rfind(
                                    "AI action not implemented:",
                                    0) == 0)
                                unsupportedAiMessages
                                    .insert(
                                        message);
                        });
                    const CampaignMission &mission =
                        campaign.missions[
                            missionIndex];
                    if (!game.initScenario(
                            scenario,
                            &conformanceError,
                            mission.archiveName,
                            mission.entry,
                            difficulty,
                            aiDirectory.string())) {
                        conformance = false;
                        break;
                    }
                    for (size_t player = 0;
                         player <
                             scenario.players.size();
                         ++player) {
                        const ScenarioPlayer
                            &scenarioPlayer =
                                scenario.players[
                                    player];
                        if (scenarioPlayer.active &&
                            !scenarioPlayer.human &&
                            !scenarioPlayer
                                 .personality
                                 .empty() &&
                            !game.aiLoadedForTesting(
                                (int)player + 1)) {
                            conformance = false;
                            conformanceError =
                                "embedded AI did not initialize for player " +
                                std::to_string(
                                    player + 1);
                            break;
                        }
                        if (difficulty ==
                            firstDifficulty) {
                            missingAiIncludes +=
                                game.aiMissingIncludeCountForTesting(
                                    (int)player +
                                    1);
                            for (const std::string
                                     &include :
                                 game.aiMissingIncludesForTesting(
                                     (int)player +
                                     1))
                                missingAiIncludeNames
                                    .insert(
                                        std::filesystem::path(
                                            include)
                                            .filename()
                                            .string());
                        }
                    }
                    InputState noInput;
                    for (int step = 0;
                         step < 60 &&
                         conformance;
                         ++step)
                        game.update(
                            0.1f, noInput);
                    ++conformanceRuns;
                }
                if (!conformance) {
                    conformanceError =
                        campaign.archiveName +
                        "/" +
                        campaign.missions[
                            missionIndex]
                            .title +
                        ": " +
                        conformanceError;
                    break;
                }
            }
        }
    }
    const bool runtimeConformance =
        conformance &&
        unsupportedAiMessages.empty();
    if (conformance &&
        !runtimeConformance)
        conformanceError =
            "campaign simulation reached unsupported AI forms";
    report(
        "stock-campaign-runtime-conformance",
        runtimeConformance,
        runtimeConformance
            ? std::to_string(
                  conformanceRuns) +
                  " runs, " +
                  std::to_string(
                      conformanceTriggers) +
                  " triggers, " +
                  std::to_string(
                      conformanceConditions) +
                  " conditions, " +
                  std::to_string(
                      conformanceEffects) +
                  " effects, " +
                  std::to_string(
                      deferredReferences) +
                  " safe deferred references, " +
                  std::to_string(
                      missingAiIncludes) +
                  " explicitly reported optional AI includes, " +
                  std::to_string(
                      unsupportedAiMessages
                          .size()) +
                  " runtime unsupported AI forms"
            : conformanceError);
    if (runtimeConformance &&
        !missingAiIncludeNames.empty()) {
        std::string names;
        for (const std::string &name :
             missingAiIncludeNames)
            names +=
                (names.empty() ? "" : ",") +
                name;
        printf(
            "    optional AI include names: %s\n",
            names.c_str());
    }
    if (!unsupportedAiMessages.empty())
        for (const std::string &message :
             unsupportedAiMessages)
            printf("    %s\n", message.c_str());

    CampaignProfile profile;
    profile.developmentAccess = false;
    bool progression = discovered;
    if (discovered) {
        const CampaignInfo &campaign =
            catalog.campaigns().front();
        progression =
            profile.isUnlocked(campaign, 0) &&
            !profile.isUnlocked(campaign, 1);
        profile.complete(campaign.missions[0].key);
        progression =
            progression &&
            profile.isUnlocked(campaign, 1);
        profile.developmentAccess = true;
        progression =
            progression &&
            profile.isUnlocked(
                campaign, campaign.missions.size() - 1);
    }
    report("campaign-profile-progression", progression);

    const char *profilePath = "test-campaign.profile";
    profile.difficulty = 4;
    bool profileRoundTrip =
        saveCampaignProfile(profilePath, profile, &err);
    CampaignProfile loadedProfile;
    profileRoundTrip =
        profileRoundTrip &&
        loadCampaignProfile(
            profilePath, loadedProfile, &err) &&
        loadedProfile.difficulty == 4 &&
        loadedProfile.developmentAccess &&
        loadedProfile.progress.size() ==
            profile.progress.size();
    report(
        "campaign-profile-atomic-roundtrip",
        profileRoundTrip, err);
    FILE *profileFile =
        std::fopen(profilePath, "r+b");
    if (profileFile) {
        std::fseek(profileFile, 12, SEEK_SET);
        const int value = std::fgetc(profileFile);
        std::fseek(profileFile, 12, SEEK_SET);
        std::fputc(value ^ 0x40, profileFile);
        std::fclose(profileFile);
    }
    CampaignProfile corruptProfile;
    err.clear();
    const bool corruptRejected =
        !loadCampaignProfile(
            profilePath, corruptProfile, &err) &&
        err.find("checksum") != std::string::npos;
    std::remove(profilePath);
    report(
        "campaign-profile-corrupt-rejection",
        corruptRejected, err);

    bool campaignSave = discovered;
    if (discovered) {
        Scenario scenario;
        err.clear();
        campaignSave = catalog.loadScenario(
            0, 0, scenario, &err);
        const CampaignMission &mission =
            catalog.campaigns()[0].missions[0];
        Game game(assets);
        campaignSave =
            campaignSave &&
            game.initScenario(
                scenario, &err, mission.archiveName,
                mission.entry, 2,
                aiDirectory.string());
        const char *savePath = "test-campaign.save";
        campaignSave =
            campaignSave && game.saveMatch(savePath, &err);
        MatchSaveMetadata saveMetadata;
        campaignSave =
            campaignSave &&
            Game::readSaveMetadata(
                savePath, saveMetadata, &err) &&
            saveMetadata.kind == MatchSaveKind::Campaign &&
            saveMetadata.campaignArchive ==
                mission.archiveName &&
            saveMetadata.campaignEntry == mission.entry;
        Game restored(assets);
        campaignSave =
            campaignSave &&
            restored.initScenario(
                scenario, &err, mission.archiveName,
                mission.entry, 2,
                aiDirectory.string()) &&
            restored.loadMatch(savePath, &err);
        FILE *saveFile = std::fopen(savePath, "r+b");
        if (saveFile) {
            std::fseek(saveFile, 24, SEEK_SET);
            const int value = std::fgetc(saveFile);
            std::fseek(saveFile, 24, SEEK_SET);
            std::fputc(value ^ 0x20, saveFile);
            std::fclose(saveFile);
        }
        Game corrupt(assets);
        std::string corruptError;
        const bool corruptSaveRejected =
            corrupt.initScenario(
                scenario, &corruptError,
                mission.archiveName, mission.entry,
                2, aiDirectory.string()) &&
            !corrupt.loadMatch(
                savePath, &corruptError) &&
            corruptError.find("checksum") !=
                std::string::npos;
        campaignSave =
            campaignSave && corruptSaveRejected;
        std::remove(savePath);
    }
    report("campaign-save-load-and-corruption", campaignSave, err);

    Frontend frontend;
    frontend.setCampaignData(&catalog, &loadedProfile);
    InputState input;
    input.menuActivate = true;
    frontend.update(input, -1); // title -> main
    input = {};
    input.menuActivate = true;
    frontend.update(input, -1); // main -> single player
    input = {};
    input.menuActivate = true;
    frontend.update(input, -1); // campaign browser
    input = {};
    input.menuActivate = true;
    frontend.update(input, -1); // mission list
    input = {};
    input.pointerTap = true;
    input.pointerY = 121;
    frontend.update(input, -1); // briefing
    input = {};
    input.menuActivate = true;
    const FrontendAction start =
        frontend.update(input, -1);
    const bool navigation =
        start == FrontendAction::StartCampaign &&
        frontend.selectedCampaign() == 0 &&
        frontend.selectedMission() == 0;
    report("campaign-menu-controller-touch", navigation);
    frontend.loadingFinished(true);
    frontend.update({}, 1);
    input = {};
    input.menuActivate = true;
    const FrontendAction continuation =
        frontend.update(input, 1);
    report(
        "campaign-victory-continuation",
        continuation ==
                FrontendAction::ReturnToCampaignBrowser &&
            frontend.screen() ==
                FrontendScreen::CampaignBriefing &&
            frontend.selectedMission() == 1);
    frontend.render(renderer, 960, 544);
    report(
        "vita-layout-bounds",
        renderer.pixels().size() ==
            960u * 544u * 4u,
        std::to_string(renderer.drawCalls()) +
            " bounded draws");

    bool transitions = discovered;
    if (discovered) {
        Scenario scenario;
        transitions =
            catalog.loadScenario(0, 0, scenario, &err);
        Game game(assets);
        for (int i = 0; i < 3 && transitions; ++i) {
            transitions =
                game.initScenario(
                    scenario, &err, "XCAM1", 0,
                    2, aiDirectory.string());
            game.clearMatch();
            SkirmishSettings settings;
            settings.seed = (uint32_t)i + 1;
            settings.mapSize = 48;
            transitions =
                transitions &&
                game.initSkirmish(settings, &err);
            game.clearMatch();
        }
    }
    report(
        "repeated-campaign-skirmish-transitions",
        transitions, err);
    return failures ? 1 : 0;
}

static int cmdTestFidelity(
    const char *dataDir) {
    std::string err;
    SoftRenderer renderer;
    Assets assets(&renderer);
    if (!assets.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n",
                err.c_str());
        return 1;
    }
    int failures = 0;
    auto report =
        [&](const char *name, bool ok,
            const std::string &detail) {
            printf(
                "%s %s %s\n",
                ok ? "PASS" : "FAIL",
                name, detail.c_str());
            if (!ok)
                failures++;
        };
    auto step = [](Game &game,
                   float seconds) {
        for (float elapsed = 0.0f;
             elapsed < seconds;
             elapsed += 1.0f / 30.0f)
            game.update(
                1.0f / 30.0f, {});
    };

    {
        const std::array<float, 11>
            shieldPoints{{
                0.0f, 99.99f, 100.0f,
                999.99f, 1000.0f,
                1999.99f, 2000.0f,
                2999.99f, 3000.0f,
                3999.99f, 4000.0f}};
        const std::array<float, 11>
            expected{{
                2.0f, 2.0f, 4.0f,
                4.0f, 8.0f, 8.0f,
                12.0f, 12.0f, 16.0f,
                16.0f, 20.0f}};
        bool tiers = true;
        for (size_t index = 0;
             index < shieldPoints.size();
             ++index)
            tiers =
                tiers &&
                Game::shieldRegenerationForTesting(
                    shieldPoints[index]) ==
                    expected[index];
        report(
            "shield-regeneration-tiers",
            tiers,
            "2/4/8/12/16/20 at "
            "100/1000/2000/3000/4000");

        Game game(assets);
        const bool initialized =
            game.initCompactTestMap(
                0x53484945u, 96, &err);
        const uint32_t inactiveGenerator =
            game.spawnObjectForTesting(
                3, 335, 1,
                42.0f, 6.0f);
        const uint32_t poweredGenerator =
            game.spawnObjectForTesting(
                3, 335, 1,
                54.0f, 6.0f);
        const uint32_t powerCore =
            game.spawnObjectForTesting(
                3, 12, 1,
                60.0f, 6.0f);
        const uint32_t worker =
            game.spawnObjectForTesting(
                3, 83, 1,
                45.5f, 6.0f);
        step(game, 12.0f);
        const float charged =
            game.objectShieldPoints(worker);
        const bool nearUnpowered =
            !game.objectPoweredForTesting(
                inactiveGenerator);
        const bool farPowered =
            game.objectPoweredForTesting(
                poweredGenerator);
        const bool poweredOverlap =
            initialized &&
            inactiveGenerator &&
            poweredGenerator &&
            powerCore && worker &&
            nearUnpowered &&
            farPowered &&
            charged > 20.0f &&
            game.objectMaxShieldPoints(
                worker) ==
                game.objectMaxHitPoints(
                    worker);
        game.damageObjectForTesting(
            powerCore, 100000);
        step(game, 1.0f);
        const float drained =
            game.objectShieldPoints(worker);
        report(
            "overlapping-shield-sources",
            poweredOverlap &&
                drained <= charged - 19.9f &&
                game.invariantsForTesting(),
            "sources=" +
                std::to_string(
                    nearUnpowered) +
                "/" +
                std::to_string(
                    farPowered) +
                " shield=" +
                std::to_string(charged) +
                "->" +
                std::to_string(drained));

        auto shieldRates =
            [&](bool superconducting) {
                Game probe(assets);
                if (!probe.initCompactTestMap(
                        0x53555045u, 96,
                        &err))
                    return std::array<float, 2>{
                        -1.0f, -1.0f};
                const uint32_t generator =
                    probe.spawnObjectForTesting(
                        3, 335, 1,
                        54.0f, 6.0f);
                const uint32_t core =
                    probe.spawnObjectForTesting(
                        3, 12, 1,
                        60.0f, 6.0f);
                const uint32_t building =
                    probe.spawnObjectForTesting(
                        3, 70, 1,
                        50.0f, 6.0f);
                if (!generator || !core ||
                    !building)
                    return std::array<float, 2>{
                        -1.0f, -1.0f};
                if (superconducting)
                    probe
                        .researchTechnologyForTesting(
                            1, 570);
                step(probe, 15.0f);
                const float beforeDrain =
                    probe.objectShieldPoints(
                        building);
                probe.damageObjectForTesting(
                    core, 100000);
                step(probe, 1.0f);
                return std::array<float, 2>{
                    beforeDrain,
                    probe.objectShieldPoints(
                        building)};
            };
        const auto baseRates =
            shieldRates(false);
        const auto upgradedRates =
            shieldRates(true);
        report(
            "superconducting-shield-drain",
            baseRates[0] > 29.5f &&
                baseRates[0] < 30.5f &&
                baseRates[1] < 0.1f &&
                upgradedRates[0] > 29.5f &&
                upgradedRates[0] < 30.5f &&
                upgradedRates[1] > 9.5f &&
                upgradedRates[1] < 10.5f,
            "base=" +
                std::to_string(
                    baseRates[0]) +
                "->" +
                std::to_string(
                    baseRates[1]) +
                " upgraded=" +
                std::to_string(
                    upgradedRates[0]) +
                "->" +
                std::to_string(
                    upgradedRates[1]));

        Game timer(assets);
        const bool timerInit =
            timer.initCompactTestMap(
                0x54494D45u, 96, &err);
        const uint32_t timerGenerator =
            timer.spawnObjectForTesting(
                3, 335, 1,
                54.0f, 6.0f);
        const uint32_t timerCore =
            timer.spawnObjectForTesting(
                3, 12, 1,
                60.0f, 6.0f);
        const uint32_t timerBuilding =
            timer.spawnObjectForTesting(
                3, 70, 1,
                50.0f, 6.0f);
        timer.update(0.5f, {});
        const char *timerSave =
            "test-fidelity-shield.sav";
        std::remove(timerSave);
        const bool timerSaved =
            timer.saveMatch(
                timerSave, &err);
        Game timerLoaded(assets);
        const bool timerLoadedInit =
            timerLoaded.initCompactTestMap(
                0x54494D45u, 96, &err);
        const bool timerRestored =
            timerLoadedInit &&
            timerLoaded.loadMatch(
                timerSave, &err);
        std::remove(timerSave);
        timerLoaded.update(0.49f, {});
        const float beforeTick =
            timerLoaded.objectShieldPoints(
                timerBuilding);
        timerLoaded.update(0.02f, {});
        const float afterTick =
            timerLoaded.objectShieldPoints(
                timerBuilding);
        report(
            "shield-timer-save-continuity",
            timerInit && timerGenerator &&
                timerCore && timerBuilding &&
                timerSaved && timerRestored &&
                beforeTick < 0.01f &&
                afterTick > 1.99f &&
                afterTick < 2.01f,
            "saved/loaded=" +
                std::to_string(
                    timerSaved) +
                "/" +
                std::to_string(
                    timerRestored) +
                " shield=" +
                std::to_string(
                    beforeTick) +
                "->" +
                std::to_string(
                    afterTick));
    }

    {
        constexpr int trials = 96;
        int hits = 0;
        int misses = 0;
        bool missGeometry = true;
        for (uint32_t seed = 1;
             seed <= trials; ++seed) {
            Game game(assets);
            if (!game.init(seed, 48,
                           &err)) {
                fprintf(
                    stderr, "error: %s\n",
                    err.c_str());
                return 1;
            }
            game.setDiplomacyForTesting(
                1, 2, 3);
            game.setDiplomacyForTesting(
                2, 1, 3);
            const uint32_t source =
                game.spawnObjectForTesting(
                    7, 6, 1,
                    20.0f, 20.0f);
            const uint32_t target =
                game.spawnObjectForTesting(
                    7, 460, 2,
                    23.5f, 20.0f);
            game.setAttackModeForTesting(
                source, 3);
            game.setAttackModeForTesting(
                target, 3);
            if (!game.issueAttackForTesting(
                    source, target))
                continue;
            for (int frame = 0;
                 frame < 300 &&
                 game.projectileCountForTesting() ==
                     0;
                 ++frame)
                game.update(
                    1.0f / 30.0f, {});
            if (!game
                     .projectileCountForTesting())
                continue;
            const auto aim =
                game.projectileAimForTesting(0);
            const float dx =
                aim[0] - 23.5f;
            const float dy =
                aim[1] - 20.0f;
            const float missDistance =
                std::sqrt(dx * dx +
                          dy * dy);
            if (missDistance > 0.35f) {
                misses++;
                missGeometry =
                    missGeometry &&
                    missDistance >= 0.49f &&
                    missDistance <= 0.96f;
            } else {
                hits++;
            }
        }
        const float hitRate =
            (float)hits /
            (float)std::max(
                1, hits + misses);
        report(
            "accuracy-statistical-boundary",
            hits + misses == trials &&
                hitRate >= 0.25f &&
                hitRate <= 0.65f &&
                missGeometry,
            "DAT=45% observed=" +
                std::to_string(hitRate) +
                " hits/misses=" +
                std::to_string(hits) +
                "/" +
                std::to_string(misses));
    }

    {
        const int civilization = 7;
        int rangedUnit = -1;
        int meleeUnit = -1;
        int lowDefenseUnit = -1;
        int highDefenseUnit = -1;
        for (const dat::Unit &unit :
             assets.dat()
                 .civs[(size_t)civilization]
                 .units) {
            if (!unit.exists ||
                unit.type <
                    dat::UT_Combatant ||
                unit.flyMode != 0 ||
                unit.hitPoints <= 0)
                continue;
            if (lowDefenseUnit < 0 &&
                unit.blastDefenseLevel < 3)
                lowDefenseUnit = unit.id;
            if (highDefenseUnit < 0 &&
                unit.blastDefenseLevel >= 3)
                highDefenseUnit = unit.id;
            if (unit.accuracyPercent < 100 ||
                unit.attacks.empty())
                continue;
            if (rangedUnit < 0 &&
                unit.maxRange > 1.0f &&
                unit.cls != 35)
                rangedUnit = unit.id;
            if (meleeUnit < 0 &&
                unit.maxRange <= 1.0f)
                meleeUnit = unit.id;
        }

        Game ranged(assets);
        const bool rangedInit =
            ranged.init(
                0x424C4153u, 48, &err);
        ranged.setDiplomacyForTesting(
            1, 2, 3);
        ranged.setDiplomacyForTesting(
            2, 1, 3);
        const uint32_t source =
            ranged.spawnObjectForTesting(
                civilization, rangedUnit, 1,
                20.0f, 20.0f);
        const uint32_t low =
            ranged.spawnObjectForTesting(
                civilization,
                lowDefenseUnit, 2,
                24.0f, 20.0f);
        const uint32_t high =
            ranged.spawnObjectForTesting(
                civilization,
                highDefenseUnit, 2,
                24.0f, 21.0f);
        const uint32_t ally =
            ranged.spawnObjectForTesting(
                civilization,
                highDefenseUnit, 1,
                24.0f, 22.0f);
        const uint32_t neutral =
            ranged.spawnObjectForTesting(
                civilization,
                highDefenseUnit, 0,
                24.0f, 23.0f);
        const float lowBefore =
            ranged.objectHitPoints(low);
        const float highBefore =
            ranged.objectHitPoints(high);
        const float allyBefore =
            ranged.objectHitPoints(ally);
        const float neutralBefore =
            ranged.objectHitPoints(neutral);
        ranged.applyBlastForTesting(
            source, 24.0f, 20.0f,
            0, 4.0f, 3, 10);
        const bool defenseGate =
            ranged.objectHitPoints(low) ==
                lowBefore &&
            ranged.objectHitPoints(high) <
                highBefore;
        const bool rangedDiplomacy =
            ranged.objectHitPoints(ally) <
                allyBefore &&
            ranged.objectHitPoints(neutral) <
                neutralBefore;

        Game melee(assets);
        const bool meleeInit =
            melee.init(
                0x4D454C45u, 48, &err);
        melee.setDiplomacyForTesting(
            1, 2, 3);
        melee.setDiplomacyForTesting(
            2, 1, 3);
        const uint32_t meleeSource =
            melee.spawnObjectForTesting(
                civilization, meleeUnit, 1,
                20.0f, 20.0f);
        const uint32_t meleeAlly =
            melee.spawnObjectForTesting(
                civilization,
                highDefenseUnit, 1,
                21.0f, 20.0f);
        const uint32_t meleeEnemy =
            melee.spawnObjectForTesting(
                civilization,
                highDefenseUnit, 2,
                21.0f, 21.0f);
        const float meleeAllyBefore =
            melee.objectHitPoints(
                meleeAlly);
        const float meleeEnemyBefore =
            melee.objectHitPoints(
                meleeEnemy);
        melee.applyBlastForTesting(
            meleeSource, 21.0f, 20.0f,
            0, 3.0f, 3, 10);
        const bool meleeDiplomacy =
            melee.objectHitPoints(
                meleeAlly) ==
                meleeAllyBefore &&
            melee.objectHitPoints(
                meleeEnemy) <
                meleeEnemyBefore;
        report(
            "blast-defense-diplomacy",
            rangedInit && meleeInit &&
                source && low && high &&
                ally && neutral &&
                meleeSource && meleeAlly &&
                meleeEnemy &&
                defenseGate &&
                rangedDiplomacy &&
                meleeDiplomacy,
            "units=" +
                std::to_string(
                    rangedUnit) +
                "/" +
                std::to_string(
                    meleeUnit) +
                " defense=" +
                std::to_string(
                    lowDefenseUnit) +
                "/" +
                std::to_string(
                    highDefenseUnit) +
                " gate/ranged/melee=" +
                std::to_string(
                    defenseGate) +
                "/" +
                std::to_string(
                    rangedDiplomacy) +
                "/" +
                std::to_string(
                    meleeDiplomacy));
    }

    {
        Game game(assets);
        const bool initialized =
            game.initCompactTestMap(
                0x44455445u, 96, &err);
        const int civilization =
            game.civilizationForPlayerForTesting(
                1);
        int forceDetectorUnit = -1;
        int vehicleDetectorUnit = -1;
        for (const dat::Unit &unit :
             assets.dat()
                 .civs[(size_t)civilization]
                 .units) {
            if (!unit.exists ||
                (unit.trait & 8u) != 0 ||
                unit.type <
                    dat::UT_Combatant ||
                unit.lineOfSight <= 0.0f)
                continue;
            if (forceDetectorUnit < 0 &&
                (unit.cls == 50 ||
                 unit.cls == 51))
                forceDetectorUnit = unit.id;
            if (vehicleDetectorUnit < 0 &&
                (unit.cls == 11 ||
                 unit.cls == 13 ||
                 unit.cls == 15 ||
                 unit.cls == 16))
                vehicleDetectorUnit = unit.id;
        }
        const uint32_t stealth =
            game.spawnConverterForTesting(
                2, 30.0f, 30.0f);
        const uint32_t forceDetector =
            forceDetectorUnit >= 0
                ? game.spawnObjectForTesting(
                      civilization,
                      forceDetectorUnit, 1,
                      30.3f, 30.0f)
                : 0;
        const uint32_t vehicleDetector =
            vehicleDetectorUnit >= 0
                ? game.spawnObjectForTesting(
                      civilization,
                      vehicleDetectorUnit, 1,
                      30.0f, 30.3f)
                : 0;
        const bool stealthEnabled =
            game.setStealthedForTesting(
                2, true);
        const bool hiddenWithoutResources =
            !game.objectDetectedForTesting(
                1, stealth);
        const bool forceResearch =
            game.researchTechnologyForTesting(
                1, 158);
        const bool forcePerception =
            game.objectDetectedForTesting(
                1, stealth);
        game.moveObjectForTesting(
            forceDetector, 80.0f, 80.0f);
        const bool vehicleResearch =
            game.researchTechnologyForTesting(
                1, 186);
        const bool vehiclePerception =
            game.objectDetectedForTesting(
                1, stealth);
        report(
            "detector-resource-classes",
            initialized && stealth &&
                forceDetector &&
                vehicleDetector &&
                stealthEnabled &&
                hiddenWithoutResources &&
                forceResearch &&
                forcePerception &&
                vehicleResearch &&
                vehiclePerception,
            "units=" +
                std::to_string(
                    forceDetectorUnit) +
                "/" +
                std::to_string(
                    vehicleDetectorUnit) +
                " hidden/force/vehicle=" +
                std::to_string(
                    hiddenWithoutResources) +
                "/" +
                std::to_string(
                    forcePerception) +
                "/" +
                std::to_string(
                    vehiclePerception));
    }

    {
        Game game(assets);
        const bool initialized =
            game.initCompactTestMap(
                0x464F524Du, 96, &err);
        const size_t baselineTerrainViolations =
            game.movementStats()
                .terrainViolations;
        const int civilization =
            game.civilizationForPlayerForTesting(
                1);
        std::vector<uint32_t> units;
        for (int index = 0;
             index < 12; ++index)
            units.push_back(
                game.spawnObjectForTesting(
                    civilization, 460, 1,
                    32.0f +
                        (index % 4) * 0.8f,
                    30.0f +
                        (index / 4) * 0.8f));
        bool formations = initialized;
        for (int formation = 0;
             formation < 4; ++formation) {
            game.groupMoveForTesting(
                units,
                38.0f + formation * 2.0f,
                35.0f + formation * 1.5f,
                formation);
            formations =
                formations &&
                game.formationMemberCountForTesting() ==
                    units.size() &&
                game.invariantsForTesting();
            step(game, 0.5f);
        }
        const char *savePath =
            "test-fidelity.sav";
        std::remove(savePath);
        const bool saved =
            game.saveMatch(savePath, &err);
        Game restored(assets);
        const bool restoredInit =
            restored.initCompactTestMap(
                0x464F524Du, 96, &err);
        const bool loaded =
            restoredInit &&
            restored.loadMatch(
                savePath, &err);
        std::remove(savePath);
        step(restored, 2.0f);
        const MovementStats movement =
            restored.movementStats();
        const bool valid =
            restored.invariantsForTesting();
        report(
            "formation-save-continuity",
            formations && saved && loaded &&
                valid &&
                movement.terrainViolations <=
                    baselineTerrainViolations &&
                movement.staticObstructionViolations ==
                    0,
            "members=" +
                std::to_string(
                    restored
                        .formationMemberCountForTesting()) +
                " saved/loaded=" +
                std::to_string(saved) +
                "/" +
                std::to_string(loaded) +
                " forms/valid=" +
                std::to_string(formations) +
                "/" +
                std::to_string(valid) +
                " violations=" +
                std::to_string(
                    movement.terrainViolations) +
                "(baseline " +
                std::to_string(
                    baselineTerrainViolations) +
                ")" +
                "/" +
                std::to_string(
                    movement
                        .staticObstructionViolations));
    }

    printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc < 3) return usage();
    const char *cmd = argv[1];
    if (!strcmp(cmd, "bench-ui")) return cmdBenchUi(argv[2]);
    if (!strcmp(cmd, "info")) return cmdInfo(argv[2]);
    if (!strcmp(cmd, "terrain") && argc >= 4) return cmdTerrain(argv[2], atoi(argv[3]));
    if (!strcmp(cmd, "restriction") && argc >= 4) return cmdRestriction(argv[2], atoi(argv[3]));
    if (!strcmp(cmd, "unit") && argc >= 4) return cmdUnit(argv[2], atoi(argv[3]));
    if (!strcmp(cmd, "string") && argc >= 4)
        return cmdString(
            argv[2], atoi(argv[3]),
            argc >= 5 ? atoi(argv[4]) : atoi(argv[3]));
    if (!strcmp(cmd, "units") && argc >= 4) return cmdUnits(argv[2], argv[3]);
    if (!strcmp(cmd, "graphics") && argc >= 4)
        return cmdGraphics(argv[2], argv[3]);
    if (!strcmp(cmd, "tech") && argc >= 4) return cmdTech(argv[2], atoi(argv[3]));
    if (!strcmp(cmd, "techs") && argc >= 4) return cmdTechs(argv[2], argv[3]);
    if (!strcmp(cmd, "effect-refs") && argc >= 4)
        return cmdEffectRefs(
            argv[2], atoi(argv[3]));
    if (!strcmp(cmd, "effect") && argc >= 4)
        return cmdEffect(
            argv[2], atoi(argv[3]));
    if (!strcmp(cmd, "attack-ground-candidates"))
        return cmdAttackGroundCandidates(argv[2]);
    if (!strcmp(cmd, "options") && argc >= 5)
        return cmdOptions(argv[2], atoi(argv[3]), atoi(argv[4]));
    if (!strcmp(cmd, "sound") && argc >= 4) return cmdSound(argv[2], atoi(argv[3]));
    if (!strcmp(cmd, "drs")) return cmdDrs(argv[2]);
    if (!strcmp(cmd, "drs-slps")) return cmdDrsSlps(argv[2]);
    if (!strcmp(cmd, "campaign")) return cmdCampaign(argv[2]);
    if (!strcmp(cmd, "scenario") && argc >= 4) return cmdScenario(argv[2], atoi(argv[3]));
    if (!strcmp(cmd, "scenario-units") && argc >= 5)
        return cmdScenarioUnits(argv[2], argv[3], atoi(argv[4]),
                                argc > 5 ? atoi(argv[5]) : -2);
    if (!strcmp(cmd, "angles") && argc >= 5) return cmdAngles(argv[2], atoi(argv[3]), argv[4]);
    if (!strcmp(cmd, "slp") && argc >= 5) return cmdSlp(argv[2], atoi(argv[3]), argv[4], argc > 5 ? atoi(argv[5]) : 16);
    if (!strcmp(cmd, "slopes") && argc >= 5)
        return cmdSlopes(argv[2], atoi(argv[3]), argv[4], argc > 5 ? (size_t)atoi(argv[5]) : 0);
    if (!strcmp(cmd, "render") && argc >= 4)
        return cmdRender(argv[2], argv[3], argc > 4 ? (uint32_t)atoi(argv[4]) : 1, argc > 5 ? (float)atof(argv[5]) : 0,
                         argc > 6 ? (float)atof(argv[6]) : 1.0f);
    if (!strcmp(cmd, "render-compact") && argc >= 4)
        return cmdRender(argv[2], argv[3],
                         argc > 4 ? (uint32_t)atoi(argv[4]) : 1,
                         argc > 5 ? (float)atof(argv[5]) : 0,
                         argc > 6 ? (float)atof(argv[6]) : 1.0f,
                         true, argc > 7 ? argv[7] : nullptr);
    if (!strcmp(cmd, "render-scenario") && argc >= 6)
        return cmdRenderScenario(argv[2], argv[3], atoi(argv[4]), argv[5],
                                 argc > 6 ? (float)atof(argv[6]) : -1.0f,
                                 argc > 7 ? (float)atof(argv[7]) : -1.0f,
                                 argc > 8 ? (float)atof(argv[8]) : 0.4f,
                                 argc > 9 && !strcmp(argv[9], "moving"));
    if (!strcmp(cmd, "stress-scenario") && argc >= 5)
        return cmdStressScenario(argv[2], argv[3], atoi(argv[4]),
                                 argc > 5 ? (float)atof(argv[5]) : 0.4f);
    if (!strcmp(cmd, "simulate-scenario") && argc >= 5)
        return cmdSimulateScenario(argv[2], argv[3], atoi(argv[4]),
                                   argc > 5 ? (float)atof(argv[5]) : 1.1f,
                                   argc > 6 ? argv[6] : nullptr);
    if (!strcmp(cmd, "test-controls") && argc >= 5)
        return cmdTestControls(argv[2], argv[3], atoi(argv[4]),
                               argc > 5 ? argv[5] : nullptr);
    if (!strcmp(cmd, "test-fixes"))
        return cmdTestFixes(argv[2], argc > 3 ? argv[3] : nullptr);
    if (!strcmp(cmd, "test-combat"))
        return cmdTestCombat(argv[2], argc > 3 ? argv[3] : nullptr);
    if (!strcmp(cmd, "ai-script"))
        return cmdAiScript(argv[2]);
    if (!strcmp(cmd, "test-ai") &&
        argc >= 4)
        return cmdTestAi(
            argv[2], argv[3]);
    if (!strcmp(cmd, "test-skirmish"))
        return cmdTestSkirmish(argv[2]);
    if (!strcmp(cmd, "test-maps-modes"))
        return cmdTestMapsModes(argv[2]);
    if (!strcmp(cmd, "test-interface"))
        return cmdTestInterface(argv[2]);
    if (!strcmp(cmd, "test-core-gameplay"))
        return cmdTestCoreGameplay(
            argv[2]);
    if (!strcmp(cmd, "test-major-mechanics"))
        return cmdTestMajorMechanics(
            argv[2]);
    if (!strcmp(cmd, "test-fidelity"))
        return cmdTestFidelity(argv[2]);
    if (!strcmp(cmd, "test-campaign") &&
        argc >= 4)
        return cmdTestCampaign(argv[2], argv[3]);
    if (!strcmp(cmd, "mp3")) return cmdMp3(argv[2]);
    return usage();
}
