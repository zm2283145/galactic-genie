// SPDX-License-Identifier: GPL-3.0-or-later
// PC-side tool for inspecting SWGB data and rendering engine scenes headless.
//
//   swgbtool info   <DataDir>
//   swgbtool drs    <file.drs>
//   swgbtool slp    <DataDir> <slpId> <out.png> [playerBase]
//   swgbtool slopes <DataDir> <slpId> <out.png> [frame]
//   swgbtool render <DataDir> <out.png> [seed] [seconds] [zoom]
#include "../src/core/drs.h"
#include "../src/core/genie_dat.h"
#include "../src/engine/assets.h"
#include "../src/engine/game.h"
#include "../src/render/soft_renderer.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace swgb;

static int usage() {
    fprintf(stderr,
            "usage:\n"
            "  swgbtool info   <DataDir>\n"
            "  swgbtool drs    <file.drs>\n"
            "  swgbtool slp    <DataDir> <slpId> <out.png> [playerBase]\n"
            "  swgbtool slopes <DataDir> <slpId> <out.png> [frame]\n"
            "  swgbtool render <DataDir> <out.png> [seed] [seconds] [zoom]\n");
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
    for (int slope = 0; slope < (int)kSlopeCount; slope++) {
        const SpriteFrame *frameData = a.terrainSlopeFrame(id, slope, frame);
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
    if (!strcmp(cmd, "drs")) return cmdDrs(argv[2]);
    if (!strcmp(cmd, "angles") && argc >= 5) return cmdAngles(argv[2], atoi(argv[3]), argv[4]);
    if (!strcmp(cmd, "slp") && argc >= 5) return cmdSlp(argv[2], atoi(argv[3]), argv[4], argc > 5 ? atoi(argv[5]) : 16);
    if (!strcmp(cmd, "slopes") && argc >= 5)
        return cmdSlopes(argv[2], atoi(argv[3]), argv[4], argc > 5 ? (size_t)atoi(argv[5]) : 0);
    if (!strcmp(cmd, "render") && argc >= 4)
        return cmdRender(argv[2], argv[3], argc > 4 ? (uint32_t)atoi(argv[4]) : 1, argc > 5 ? (float)atof(argv[5]) : 0,
                         argc > 6 ? (float)atof(argv[6]) : 1.0f);
    return usage();
}
