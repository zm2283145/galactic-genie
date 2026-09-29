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
#include "../src/engine/game.h"
#include "../src/render/soft_renderer.h"

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
            "  swgbtool drs    <file.drs>\n"
            "  swgbtool slp    <DataDir> <slpId> <out.png> [playerBase]\n"
            "  swgbtool slopes <DataDir> <slpId> <out.png> [frame]\n"
            "  swgbtool render <DataDir> <out.png> [seed] [seconds] [zoom]\n"
            "  swgbtool campaign <file.cpx>\n"
            "  swgbtool scenario <file.cpx> <entry>\n"
            "  swgbtool scenario-units <DataDir> <file.cpx> <entry> [unitId]\n"
            "  swgbtool render-scenario <DataDir> <file.cpx> <entry> <out.png> [x] [y] [zoom]\n"
            "  swgbtool stress-scenario <DataDir> <file.cpx> <entry> [zoom]\n"
            "  swgbtool simulate-scenario <DataDir> <file.cpx> <entry> [seconds] [out.png]\n"
            "  swgbtool test-controls <DataDir> <file.cpx> <entry> [out.png]\n"
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
    for (size_t i = 0; i < d.terrainBlock.terrains.size(); i++) {
        const auto &terrain = d.terrainBlock.terrains[i];
        if (terrain.blendType >= 8)
            printf("  terrain %zu %-16s blend type %d, priority %d\n", i, terrain.name2.c_str(),
                   terrain.blendType, terrain.blendPriority);
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
                   "outline %.2f,%.2f,%.2f\n", civ,
                   assets.dat().civs[civ].name.c_str(), unit.name.c_str(), unit.type, unit.cls,
                   unit.hideInEditor, unit.heroMode, graphicId,
                   graphic ? graphic->slp : -1, graphic ? graphic->frameCount : 0,
                   graphic ? graphic->angleCount : 0, graphic ? graphic->frameDuration : 0,
                   graphic ? graphic->sequenceType : 0, graphic ? graphic->mirroringMode : 0,
                   graphic ? graphic->deltas.size() : 0, unit.specialGraphic, unit.specialAbility,
                   unit.adjacentMode, unit.graphicsAngle, unit.speed, unit.terrainRestriction,
                   unit.flyMode, unit.obstructionType, unit.obstructionClass,
                   unit.collisionSize[0], unit.collisionSize[1],
                   unit.outlineSize[0], unit.outlineSize[1], unit.outlineSize[2]);
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
    const SpriteSheet *sh = a.sheet(id, base);
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

static int cmdRender(const char *dataDir, const char *out, uint32_t seed, float seconds, float zoom) {
    SoftRenderer r;
    Assets a(&r);
    std::string err;
    auto t0 = std::chrono::steady_clock::now();
    if (!a.init(dataDir, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    Game g(a);
    if (!g.init(seed, 64, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    g.setZoom(zoom);
    InputState in;
    for (float t = 0; t < seconds; t += 1.0f / 30) g.update(1.0f / 30, in);
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
    game.setLogger([](const std::string &message) { printf("runtime: %s\n", message.c_str()); });
    game.setSoundPlayer([](const std::string &name) {
        printf("sound: %s\n", name.c_str());
        return 2.0f;
    });
    if (!game.initScenario(scenario, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    InputState input;
    const float step = 1.0f / 30.0f;
    for (float elapsed = 0; elapsed < seconds; elapsed += step)
        game.update(std::min(step, seconds - elapsed), input);
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
    if (!game.initScenario(scenario, &err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }

    InputState input;
    input.pointerX = 480;
    input.pointerY = 272;
    input.selectPressed = true;
    game.update(0.001f, input);
    const size_t singleSelected = game.selectedObjectCount();

    input = {};
    input.pointerX = 340;
    input.pointerY = 195;
    input.selectPressed = true;
    game.update(0.1f, input);
    input = {};
    input.pointerX = 340;
    input.pointerY = 195;
    input.selectPressed = true;
    game.update(0.1f, input);
    const size_t doubleSelected = game.selectedObjectCount();

    input = {};
    input.boxSelectCommit = true;
    input.boxStartX = 0;
    input.boxStartY = 0;
    input.boxEndX = 959;
    input.boxEndY = 543;
    game.update(0.001f, input);
    const size_t boxSelected = game.selectedObjectCount();

    input = {};
    input.pointerX = 620;
    input.pointerY = 360;
    input.commandPressed = true;
    game.update(0.001f, input);
    const size_t commanded = game.selectedMovingObjectCount();
    input = {};
    for (int i = 0; i < 30; i++) game.update(1.0f / 30.0f, input);

    if (out) {
        game.render(renderer, 960, 544);
        if (!renderer.savePng(out)) {
            fprintf(stderr, "error: could not write %s\n", out);
            return 1;
        }
    }
    const MovementStats movement = game.movementStats();
    printf("controls: single %zu, double %zu, box %zu, commanded %zu, overlaps %zu, "
           "terrain violations %zu\n",
           singleSelected, doubleSelected, boxSelected, commanded,
           movement.overlappingPairs, movement.terrainViolations);
    if (singleSelected != 1 || doubleSelected <= 1 || boxSelected < doubleSelected ||
        commanded == 0 || movement.overlappingPairs != 0 ||
        movement.terrainViolations != 0) {
        fprintf(stderr, "error: control validation failed\n");
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

int main(int argc, char **argv) {
    if (argc < 3) return usage();
    const char *cmd = argv[1];
    if (!strcmp(cmd, "info")) return cmdInfo(argv[2]);
    if (!strcmp(cmd, "terrain") && argc >= 4) return cmdTerrain(argv[2], atoi(argv[3]));
    if (!strcmp(cmd, "restriction") && argc >= 4) return cmdRestriction(argv[2], atoi(argv[3]));
    if (!strcmp(cmd, "unit") && argc >= 4) return cmdUnit(argv[2], atoi(argv[3]));
    if (!strcmp(cmd, "drs")) return cmdDrs(argv[2]);
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
    if (!strcmp(cmd, "mp3")) return cmdMp3(argv[2]);
    return usage();
}
