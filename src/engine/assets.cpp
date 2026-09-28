// SPDX-License-Identifier: GPL-3.0-or-later
#include "assets.h"

#include "../core/slp.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace swgb {

static bool fileExists(const std::string &p) {
    FILE *f = fopen(p.c_str(), "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

std::string findFileNoCase(const std::string &dir, const std::string &name) {
    std::string lower = name, upper = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    for (const std::string &n : {name, lower, upper}) {
        std::string p = dir + "/" + n;
        if (fileExists(p)) return p;
    }
    return std::string();
}

Assets::~Assets() {
    for (auto &kv : sheets_)
        for (Texture *t : kv.second->pages) renderer_->destroyTexture(t);
}

bool Assets::init(const std::string &dataDir, std::string *err) {
    struct Want {
        ResourceSet *set;
        const char *name;
        bool required;
    };
    // Expansion archives first so Clone Campaigns overrides win.
    const Want wants[] = {
        {&graphics_, "graphics_x1.drs", true}, {&graphics_, "graphics.drs", true},
        {&terrain_, "terrain_x1.drs", false},  {&terrain_, "terrain.drs", true},
        {&interfac_, "interfac_x1.drs", false}, {&interfac_, "interfac.drs", true},
    };
    for (const Want &w : wants) {
        std::string p = findFileNoCase(dataDir, w.name);
        std::string e;
        if (p.empty() || !w.set->add(p, &e)) {
            if (w.required) {
                if (err) *err = std::string("missing ") + w.name + " in " + dataDir + (e.empty() ? "" : (": " + e));
                return false;
            }
            continue;
        }
        log(std::string("opened ") + p);
    }

    std::vector<uint8_t> buf;
    if (!interfac_.read(50500, buf)) {
        if (err) *err = "palette 50500 not found in interfac.drs";
        return false;
    }
    if (!palette_.parse(buf, err)) return false;

    std::string datPath = findFileNoCase(dataDir, "genie_x1.dat");
    if (datPath.empty()) {
        if (err) *err = "missing genie_x1.dat in " + dataDir;
        return false;
    }
    if (!dat_.load(datPath, err)) return false;
    log("loaded " + datPath + ": " + std::to_string(dat_.graphics.size()) + " graphics, " +
        std::to_string(dat_.civs.size()) + " civs");
    return true;
}

const SpriteSheet *Assets::sheet(int32_t slpId, int playerColorBase) {
    uint64_t key = ((uint64_t)(uint32_t)slpId << 16) | (uint16_t)playerColorBase;
    auto it = sheets_.find(key);
    if (it != sheets_.end()) return it->second.get();
    return build(graphics_, slpId, playerColorBase, key);
}

const SpriteSheet *Assets::terrainSheet(int32_t slpId) {
    uint64_t key = ((uint64_t)(uint32_t)slpId << 16) | 0xFFFF;
    auto it = sheets_.find(key);
    if (it != sheets_.end()) return it->second.get();
    return build(terrain_, slpId, 16, key);
}

const SpriteSheet *Assets::build(ResourceSet &set, int32_t slpId, int playerColorBase, uint64_t key) {
    std::vector<uint8_t> data;
    if (slpId < 0 || !set.read(slpId, data)) {
        sheets_[key] = nullptr;
        return nullptr;
    }
    Slp slp;
    std::string err;
    if (!slp.parse(std::move(data), &err)) {
        log("slp " + std::to_string(slpId) + ": " + err);
        sheets_[key] = nullptr;
        return nullptr;
    }

    const size_t n = slp.frameCount();
    std::vector<SlpImage> imgs(n);
    for (size_t i = 0; i < n; i++) {
        if (!slp.decode(i, imgs[i], &err)) {
            log("slp " + std::to_string(slpId) + " frame " + std::to_string(i) + ": " + err);
            imgs[i] = SlpImage{};
        }
    }

    // Shelf-pack frames into pages. Pages are 1024 wide (wider if a single
    // frame needs it) and at most 2048 tall. 1px padding avoids bleeding.
    struct Place { int page, x, y; };
    std::vector<Place> place(n);
    std::vector<std::pair<int, int>> pageSize; // w, h
    int maxW = 1024;
    for (auto &im : imgs) maxW = std::max(maxW, im.width + 2);
    const int maxH = 2048;
    int page = 0, cx = 0, cy = 0, rowH = 0;
    pageSize.push_back({maxW, 0});
    for (size_t i = 0; i < n; i++) {
        int w = imgs[i].width + 1, h = imgs[i].height + 1;
        if (cx + w > maxW) { cx = 0; cy += rowH; rowH = 0; }
        if (cy + h > maxH && cy > 0) {
            page++;
            pageSize.push_back({maxW, 0});
            cx = cy = rowH = 0;
        }
        place[i] = {page, cx, cy};
        cx += w;
        rowH = std::max(rowH, h);
        pageSize[page].second = std::max(pageSize[page].second, cy + rowH);
    }

    auto sheet = std::make_unique<SpriteSheet>();
    std::vector<std::vector<uint8_t>> pixels(pageSize.size());
    for (size_t p = 0; p < pageSize.size(); p++) {
        pageSize[p].second = std::max(1, pageSize[p].second);
        pixels[p].assign((size_t)pageSize[p].first * pageSize[p].second * 4, 0);
    }
    ColorizeOptions opt;
    opt.playerColorBase = playerColorBase;
    for (size_t i = 0; i < n; i++) {
        const Place &pl = place[i];
        const int pw = pageSize[pl.page].first;
        if (imgs[i].width > 0 && imgs[i].height > 0)
            colorize(imgs[i], palette_, opt, &pixels[pl.page][((size_t)pl.y * pw + pl.x) * 4], pw);
    }
    for (size_t p = 0; p < pageSize.size(); p++) {
        Texture *t = renderer_->createTexture(pageSize[p].first, pageSize[p].second, pixels[p].data());
        sheet->pages.push_back(t);
        sheet->bytes += pixels[p].size();
    }
    sheet->frames.resize(n);
    for (size_t i = 0; i < n; i++) {
        SpriteFrame &f = sheet->frames[i];
        f.tex = sheet->pages[place[i].page];
        f.u = (float)place[i].x;
        f.v = (float)place[i].y;
        f.w = imgs[i].width;
        f.h = imgs[i].height;
        f.hotX = imgs[i].hotspotX;
        f.hotY = imgs[i].hotspotY;
    }
    textureBytes_ += sheet->bytes;
    const SpriteSheet *res = sheet.get();
    sheets_[key] = std::move(sheet);
    return res;
}

} // namespace swgb
