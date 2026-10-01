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
#include "../src/engine/game.h"
#include "../src/render/soft_renderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

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
                   "sounds select/move/attack %d/%d/%d\n", civ,
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
                   unit.selectionSound, unit.moveSound, unit.attackSound);
            printf("  combat hp %d base armor %d level %u range %.2f..%.2f reload %.2f "
                   "garrison capacity/type/heal %.0f/%u/%.2f "
                   "attack graphic %d projectile %d/%d frame delay %d displacement %.2f,%.2f,%.2f "
                   "displayed attack/armor %d/%d\n",
                   unit.hitPoints, unit.baseArmor, unit.combatLevel,
                   unit.minRange, unit.maxRange,
                   unit.reloadTime, (float)unit.garrisonCapacity,
                   unit.garrisonType, unit.garrisonHealRate,
                   unit.attackGraphic, unit.projectileUnitId,
                   unit.secondaryProjectileUnit,
                   unit.frameDelay, unit.graphicDisplacement[0], unit.graphicDisplacement[1],
                   unit.graphicDisplacement[2],
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
    printf("units at building %d for civ %d '%s'\n",
           buildingId, civId, civ.name.c_str());
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
               "%.0f/%.0f/%.0f/%.0f/%.0f, population %.0f, allied victory %d, name '%s', diplomacy",
               i + 1, player.active, player.human, player.civilization, player.color,
               player.cameraX, player.cameraY,
               player.resources[0], player.resources[1], player.resources[2],
               player.resources[3], player.resources[4], player.populationLimit,
               player.alliedVictory, player.name.c_str());
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
    const bool mobileShieldBleedThrough =
        chargedWorkerShield >= 39.9f &&
        std::abs(
            systems.objectHitPoints(
                shieldWorkerId) -
            (workerHealthBeforeShieldHit - 1.0f)) <
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
        workerShieldBeforeOverflow + 1.0f;
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
    systems.update(0.1f, {});
    const bool shieldRetainedOutsideRadius =
        systems.objectShieldPoints(
            shieldBuildingId) <
            shieldBeforeLeaving &&
        systems.objectShieldPoints(
            shieldBuildingId) >
            shieldBeforeLeaving - 4.1f &&
        systems.objectMaxShieldPoints(
            shieldBuildingId) > 0;
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
    const bool unpoweredShieldDrained =
        systems.objectShieldPoints(
            shieldBuildingId) <=
            poweredShield - 19.9f;

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
           mobileShieldBleedThrough ? 1 : 0,
           shieldOverflowDamagedHealth ? 1 : 0,
           shieldRetainedOutsideRadius ? 1 : 0,
           shieldChargedFully ? 1 : 0,
           shieldFixturesCreated &&
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
        !mobileShieldBleedThrough ||
        !shieldOverflowDamagedHealth ||
        !shieldRetainedOutsideRadius ||
        !shieldChargedFully ||
        !shieldFixturesCreated ||
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
        const size_t laterAlerts =
            std::count(
                interfaceSounds.begin(),
                interfaceSounds.end(),
                50315);
        report(
            "under-attack-alert",
            firstAlerts == 1 &&
                laterAlerts == 2,
            "first=" +
                std::to_string(firstAlerts) +
                " later=" +
                std::to_string(laterAlerts));
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
    // 14) Formations keep their shape while marching.
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
        // Shelling a building also hits troops standing beside it.
        const uint32_t hut = g.spawnObjectForTesting(3, 70, 2, 40.0f, 40.0f);
        const uint32_t guard = g.spawnObjectForTesting(3, 460, 2, 41.9f, 40.0f);
        g.setAttackModeForTesting(guard, 3);
        g.moveObjectForTesting(mech, 40.0f, 34.0f);
        const float hpG = g.objectHitPoints(guard);
        g.issueAttackForTesting(mech, hut);
        for (int f = 0; f < 30 * 20 && g.objectActive(guard) && g.objectHitPoints(guard) >= hpG; f++)
            g.update(1.0f / 30.0f, {});
        const bool besideHit = !g.objectActive(guard) || g.objectHitPoints(guard) < hpG;
        report("blast-minrange", splashed && firedClose && besideHit,
               "splash " + std::to_string(splashed) + " closeHit " + std::to_string(firedClose) +
                   " besideBuilding " + std::to_string(besideHit));
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
    const uint32_t worker =
        game.spawnObjectForTesting(
            3, 83, 2, 69.0f, 69.0f);
    const uint32_t carbon =
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
        game.objectGatheringTarget(
            worker, carbon);
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
            0x5A17u, 64, &err)) {
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
    if (!openingGame.loadAiScript(
            2, entry,
            {"DIFFICULTY-MODERATE"},
            &err)) {
        fprintf(
            stderr, "error: %s\n",
            err.c_str());
        return 1;
    }
    for (int frame = 0;
         frame < 30 * 6; ++frame)
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
    printf(
        "AI parser files/constants/rules "
        "%zu/%zu/%zu, init %d, gather %d, "
        "carbon/food buildings %d/%d, "
        "queued workers %zu, resources %d, "
        "original opening %d "
        "(%d%% assigned, %d%% carbon, "
        "%zu workers)\n",
        original.files.size(),
        original.constants.size(),
        original.rules.size(),
        initialized ? 1 : 0,
        gathered ? 1 : 0,
        carbonBuildings,
        foodBuildings,
        queuedWorkers,
        startingResources ? 1 : 0,
        originalOpening ? 1 : 0,
        openingGatherPercentage,
        openingCarbonPercentage,
        openingCarbonGatherers);
    if (!originalParsed ||
        !initialized || !gathered ||
        carbonBuildings != 1 ||
        foodBuildings != 1 ||
        queuedWorkers != 1 ||
        !startingResources ||
        !originalOpening) {
        fprintf(
            stderr,
            "error: AI validation failed\n");
        return 1;
    }
    return 0;
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
    if (!strcmp(cmd, "mp3")) return cmdMp3(argv[2]);
    return usage();
}
