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
        if (kv.second)
            for (Texture *t : kv.second->pages) renderer_->destroyTexture(t);
    for (auto &kv : slopeFrames_)
        if (kv.second)
            for (Texture *t : kv.second->pages) renderer_->destroyTexture(t);
    renderer_->destroyTexture(blendMaskTexture_);
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

    std::string blendPath = findFileNoCase(dataDir, "blendomatic.dat");
    if (blendPath.empty()) {
        log("blendomatic.dat not found; terrain blending disabled");
    } else {
        Blendomatic blendomatic;
        if (!blendomatic.load(blendPath, err) || !buildBlendMasks(blendomatic, err)) return false;
        log("loaded " + blendPath + ": " + std::to_string(blendomatic.modeCount()) + " modes, " +
            std::to_string(blendomatic.maskCount()) + " masks each");
    }

    const std::string templatePath = findFileNoCase(dataDir, "STemplet.dat");
    const std::string filterPath = findFileNoCase(dataDir, "FilterMaps.dat");
    const std::string icmPath = findFileNoCase(dataDir, "VIEW_ICM.DAT");
    if (templatePath.empty() || filterPath.empty() || icmPath.empty()) {
        log("terrain elevation resources not found; sloped terrain disabled");
    } else {
        auto maps = std::make_unique<ElevationMaps>();
        if (!maps->load(templatePath, filterPath, icmPath, err)) return false;
        elevationMaps_ = std::move(maps);
        log("loaded terrain elevation maps: " + std::to_string(kSlopeCount) + " slope types");
    }
    return true;
}

bool Assets::buildBlendMasks(const Blendomatic &blendomatic, std::string *err) {
    if (blendomatic.modes().empty() || blendomatic.modes()[0].masks.empty()) {
        if (err) *err = "blendomatic contains no masks";
        return false;
    }
    const int maskW = blendomatic.modes()[0].masks[0].width;
    const int maskH = blendomatic.modes()[0].masks[0].height;
    const int strideW = maskW + 2, strideH = maskH + 2;
    const int count = (int)blendomatic.maskCount();
    const int modes = (int)blendomatic.modeCount();
    const int columns = std::min(count, 2048 / strideW);
    if (columns <= 0) {
        if (err) *err = "blendomatic masks are too wide for an atlas";
        return false;
    }
    const int rowsPerMode = (count + columns - 1) / columns;
    const int atlasW = columns * strideW;
    const int atlasH = modes * rowsPerMode * strideH;
    if (atlasH > 2048) {
        if (err) *err = "blendomatic masks are too large for an atlas";
        return false;
    }

    std::vector<uint8_t> alpha((size_t)atlasW * atlasH, 0);
    blendMasks_.assign(modes, std::vector<SpriteFrame>(count));
    for (int mode = 0; mode < modes; mode++) {
        if ((int)blendomatic.modes()[mode].masks.size() != count) {
            if (err) *err = "blendomatic modes have inconsistent mask counts";
            return false;
        }
        for (int maskIndex = 0; maskIndex < count; maskIndex++) {
            const BlendMask &mask = blendomatic.modes()[mode].masks[maskIndex];
            if (mask.width != maskW || mask.height != maskH) {
                if (err) *err = "blendomatic masks have inconsistent dimensions";
                return false;
            }
            const int cellX = maskIndex % columns;
            const int cellY = mode * rowsPerMode + maskIndex / columns;
            const int x = cellX * strideW + 1;
            const int y = cellY * strideH + 1;
            for (int row = 0; row < maskH; row++)
                std::memcpy(&alpha[(size_t)(y + row) * atlasW + x],
                            &mask.alpha[(size_t)row * maskW], (size_t)maskW);
            SpriteFrame &frame = blendMasks_[mode][maskIndex];
            frame.u = (float)x;
            frame.v = (float)y;
            frame.w = maskW;
            frame.h = maskH;
            frame.hotX = frame.hotY = 0;
        }
    }

    blendMaskTexture_ = renderer_->createMaskTexture(atlasW, atlasH, alpha.data());
    if (!blendMaskTexture_) {
        blendMasks_.clear();
        if (err) *err = "failed to create blend mask texture";
        return false;
    }
    for (auto &mode : blendMasks_)
        for (SpriteFrame &frame : mode) frame.tex = blendMaskTexture_;
    textureBytes_ += alpha.size();
    return true;
}

const SpriteFrame *Assets::blendMask(int mode, int mask) const {
    if (mode < 0 || (size_t)mode >= blendMasks_.size()) return nullptr;
    if (mask < 0 || (size_t)mask >= blendMasks_[mode].size()) return nullptr;
    return &blendMasks_[mode][mask];
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

const SpriteFrame *Assets::terrainSlopeFrame(int32_t slpId, int slope, size_t frame) {
    if (slope == 0 || !elevationMaps_) {
        const SpriteSheet *sheet = terrainSheet(slpId);
        return sheet && frame < sheet->frames.size() ? &sheet->frames[frame] : nullptr;
    }
    if (slope < 0 || (size_t)slope >= kSlopeCount || frame > 0xFFFFFF) return nullptr;
    uint64_t key = ((uint64_t)(uint32_t)slpId << 32) | (uint64_t)(uint8_t)slope << 24 | frame;
    auto it = slopeFrames_.find(key);
    if (it != slopeFrames_.end())
        return it->second && !it->second->frames.empty() ? &it->second->frames[0] : nullptr;
    return buildTerrainSlopeFrame(slpId, slope, frame, key);
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

    auto sheet = pack(imgs, playerColorBase);
    if (!sheet) {
        sheets_[key] = nullptr;
        return nullptr;
    }
    textureBytes_ += sheet->bytes;
    const SpriteSheet *res = sheet.get();
    sheets_[key] = std::move(sheet);
    return res;
}

const SpriteFrame *Assets::buildTerrainSlopeFrame(int32_t slpId, int slope, size_t frame, uint64_t key) {
    std::string err;
    auto slpIt = terrainSlps_.find(slpId);
    if (slpIt == terrainSlps_.end()) {
        std::vector<uint8_t> data;
        if (slpId < 0 || !terrain_.read(slpId, data)) {
            slopeFrames_[key] = nullptr;
            return nullptr;
        }
        auto parsed = std::make_unique<Slp>();
        if (!parsed->parse(std::move(data), &err)) {
            log("terrain slp " + std::to_string(slpId) + ": " + err);
            slopeFrames_[key] = nullptr;
            return nullptr;
        }
        slpIt = terrainSlps_.emplace(slpId, std::move(parsed)).first;
    }
    Slp &slp = *slpIt->second;
    if (frame >= slp.frameCount()) {
        slopeFrames_[key] = nullptr;
        return nullptr;
    }

    const SlopeTemplate &shape = elevationMaps_->slopeTemplate((size_t)slope);
    const FilterMap &filter = elevationMaps_->filterMap((size_t)slope);
    SlpImage source;
    std::vector<uint8_t> commandPalette;
    if (!slp.decode(frame, source, &err, &commandPalette)) {
        log("terrain slp " + std::to_string(slpId) + " frame " + std::to_string(frame) + ": " + err);
        slopeFrames_[key] = nullptr;
        return nullptr;
    }

    SlpImage out;
    out.width = shape.width;
    out.height = shape.height;
    out.hotspotX = shape.hotspotX;
    out.hotspotY = shape.hotspotY;
    out.kind.assign((size_t)out.width * out.height, PX_TRANSPARENT);
    out.index.assign((size_t)out.width * out.height, 0);
    bool valid = true;
    for (int y = 0; y < out.height && valid; y++) {
        const FilterLine &line = filter.lines[(size_t)y];
        int x = shape.leftEdges[(size_t)y];
        if (x + (int)line.pixels.size() + shape.rightEdges[(size_t)y] != out.width) {
            valid = false;
            break;
        }
        for (const FilterPixel &pixel : line.pixels) {
            uint32_t r = 0, g = 0, b = 0;
            for (const FilterSource &sample : pixel.sources) {
                if (sample.sourceOffset >= commandPalette.size()) {
                    valid = false;
                    break;
                }
                const Rgba &color = palette_[commandPalette[sample.sourceOffset]];
                r += color.r * sample.alpha;
                g += color.g * sample.alpha;
                b += color.b * sample.alpha;
            }
            if (!valid) break;
            const size_t dst = (size_t)y * out.width + x++;
            out.kind[dst] = PX_COLOR;
            out.index[dst] = elevationMaps_->colorIndex(
                4, (uint8_t)std::min(31u, r >> 11), (uint8_t)std::min(31u, g >> 11),
                (uint8_t)std::min(31u, b >> 11));
        }
    }
    if (!valid) {
        log("terrain slp " + std::to_string(slpId) + " frame " + std::to_string(frame) +
            ": filter map references invalid source data");
        slopeFrames_[key] = nullptr;
        return nullptr;
    }

    std::vector<uint8_t> pixels((size_t)out.width * out.height * 4, 0);
    ColorizeOptions options;
    colorize(out, palette_, options, pixels.data(), out.width);
    Texture *texture = renderer_->createTexture(out.width, out.height, pixels.data());
    if (!texture) {
        slopeFrames_[key] = nullptr;
        return nullptr;
    }
    auto sheet = std::make_unique<SpriteSheet>();
    sheet->pages.push_back(texture);
    sheet->bytes = pixels.size();
    sheet->frames.push_back({texture, 0, 0, out.width, out.height, out.hotspotX, out.hotspotY});
    textureBytes_ += sheet->bytes;
    const SpriteFrame *result = &sheet->frames[0];
    slopeFrames_[key] = std::move(sheet);
    return result;
}

std::unique_ptr<SpriteSheet> Assets::pack(const std::vector<SlpImage> &imgs, int playerColorBase) {
    const size_t n = imgs.size();
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
    return sheet;
}

} // namespace swgb
