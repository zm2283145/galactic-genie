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
            "  swgbtool unit <DataDir> <unitId>\n"
            "  swgbtool drs    <file.drs>\n"
            "  swgbtool slp    <DataDir> <slpId> <out.png> [playerBase]\n"
            "  swgbtool slopes <DataDir> <slpId> <out.png> [frame]\n"
            "  swgbtool render <DataDir> <out.png> [seed] [seconds] [zoom]\n"
            "  swgbtool campaign <file.cpx>\n"
            "  swgbtool scenario <file.cpx> <entry>\n"
            "  swgbtool scenario-units <DataDir> <file.cpx> <entry> [unitId]\n"
            "  swgbtool render-scenario <DataDir> <file.cpx> <entry> <out.png> [x] [y] [zoom]\n"
            "  swgbtool stress-scenario <DataDir> <file.cpx> <entry>\n");
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
            printf("civ %zu %-24s unit '%s', type %u, graphic %d, slp %d, frames %d, angles %d, "
                   "duration %.3f, sequence 0x%02x, mirror %u, deltas %zu, special graphic %d, "
                   "special ability %u, adjacent mode %u, graphics angle %d\n", civ,
                   assets.dat().civs[civ].name.c_str(), unit.name.c_str(), unit.type, graphicId,
                   graphic ? graphic->slp : -1, graphic ? graphic->frameCount : 0,
                   graphic ? graphic->angleCount : 0, graphic ? graphic->frameDuration : 0,
                   graphic ? graphic->sequenceType : 0, graphic ? graphic->mirroringMode : 0,
                   graphic ? graphic->deltas.size() : 0, unit.specialGraphic, unit.specialAbility,
                   unit.adjacentMode, unit.graphicsAngle);
            if (graphic)
                for (const auto &delta : graphic->deltas)
                    if (const auto *child = assets.dat().graphic(delta.graphicId))
                        printf("  delta graphic %d slp %d frames %d angles %d offset %d,%d display angle %d\n",
                               delta.graphicId, child->slp, child->frameCount, child->angleCount,
                               delta.offsetX, delta.offsetY, delta.displayAngle);
        }
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
    printf("camera %.1f,%.1f\n", scenario.cameraX, scenario.cameraY);
    printf("instructions: %s\n", scenario.instructions.c_str());
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
    if (detailId >= 0) {
        for (const ScenarioUnit &unit : scenario.units) {
            if (unit.unitId != detailId) continue;
            printf("player %u spawn %u unit %u at %.2f,%.2f rotation %.6f frame %u\n",
                   unit.player, unit.spawnId, unit.unitId, unit.x, unit.y, unit.rotation,
                   unit.initialFrame);
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
        printf("player %u civ %zu unit %u count %zu '%s'\n", player, civ, id, count,
               unit ? unit->name.c_str() : "?");
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
                             float x, float y, float zoom) {
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
    game.render(renderer, 960, 544);
    renderer.savePng(out);
    auto end = std::chrono::steady_clock::now();
    printf("rendered %s (%ux%u) at %.1f,%.1f: %d tiles, %.1f MB textures, %.0f ms -> %s\n",
           scenario.originalFilename.c_str(), scenario.map.width, scenario.map.height,
           x >= 0 ? x : scenario.map.width * 0.5f, y >= 0 ? y : scenario.map.height * 0.5f,
           game.stats().tiles, assets.textureBytes() / 1048576.0,
           std::chrono::duration<double, std::milli>(end - start).count(), out);
    return 0;
}

static int cmdStressScenario(const char *dataDir, const char *campaignPath, int entryNumber) {
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
    game.setZoom(0.4f);
    constexpr int steps = 5;
    size_t peakBytes = 0, peakSheets = 0;
    for (int row = 0; row < steps; row++) {
        for (int column = 0; column < steps; column++) {
            const int orderedColumn = row & 1 ? steps - 1 - column : column;
            const float x = 12.0f + orderedColumn * (scenario.map.width - 24.0f) / (steps - 1);
            const float y = 12.0f + row * (scenario.map.height - 24.0f) / (steps - 1);
            game.lookAt(x, y);
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
    if (!strcmp(cmd, "unit") && argc >= 4) return cmdUnit(argv[2], atoi(argv[3]));
    if (!strcmp(cmd, "drs")) return cmdDrs(argv[2]);
    if (!strcmp(cmd, "campaign")) return cmdCampaign(argv[2]);
    if (!strcmp(cmd, "scenario") && argc >= 4) return cmdScenario(argv[2], atoi(argv[3]));
    if (!strcmp(cmd, "scenario-units") && argc >= 5)
        return cmdScenarioUnits(argv[2], argv[3], atoi(argv[4]), argc > 5 ? atoi(argv[5]) : -1);
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
                                 argc > 8 ? (float)atof(argv[8]) : 0.4f);
    if (!strcmp(cmd, "stress-scenario") && argc >= 5)
        return cmdStressScenario(argv[2], argv[3], atoi(argv[4]));
    return usage();
}
