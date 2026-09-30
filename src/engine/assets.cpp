// SPDX-License-Identifier: GPL-3.0-or-later
#include "assets.h"

#include "../core/slp.h"

#include <algorithm>
#include <cctype>
#include <cmath>
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
    for (auto &kv : slopeBlendMasks_)
        if (kv.second)
            for (Texture *t : kv.second->pages) renderer_->destroyTexture(t);
    renderer_->destroyTexture(blendMaskTexture_);
    renderer_->destroyTexture(selectionRingTexture_);
}

Texture *Assets::selectionRing() {
    if (selectionRingAttempted_) return selectionRingTexture_;
    selectionRingAttempted_ = true;
    constexpr int width = 128, height = 64;
    std::vector<uint8_t> pixels((size_t)width * height * 4, 0);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++) {
            const float nx = (x - 63.5f) / 59.0f;
            const float ny = (y - 31.5f) / 26.0f;
            const float edge = std::abs(std::sqrt(nx * nx + ny * ny) - 1.0f);
            uint8_t *pixel = &pixels[((size_t)y * width + x) * 4];
            if (edge <= 0.075f) {
                pixel[3] = 255;
                if (edge <= 0.035f) pixel[0] = pixel[1] = pixel[2] = 235;
            }
        }
    selectionRingTexture_ = renderer_->createTexture(width, height, pixels.data());
    if (selectionRingTexture_) textureBytes_ += pixels.size();
    return selectionRingTexture_;
}

void Assets::destroySheet(std::unique_ptr<SpriteSheet> &sheet) {
    if (!sheet) return;
    for (Texture *texture : sheet->pages) renderer_->destroyTexture(texture);
    for (Texture *texture : sheet->outlinePages)
        renderer_->destroyTexture(texture);
    textureBytes_ = sheet->bytes <= textureBytes_ ? textureBytes_ - sheet->bytes : 0;
    sheet.reset();
}

void Assets::beginTerrainFrame(size_t textureBudget) {
    terrainTextureBudget_ = textureBudget;
    terrainGeneration_++;
    if (terrainGeneration_ == 0) {
        terrainGeneration_ = 1;
        for (auto &entry : sheetUse_) entry.second = 0;
        for (auto &entry : slopeFrameUse_) entry.second = 0;
        for (auto &entry : slopeBlendMaskUse_) entry.second = 0;
    }
}

void Assets::ensureTerrainCacheSpace(size_t additionalBytes) {
    if (additionalBytes <= terrainTextureBudget_ &&
        textureBytes_ <= terrainTextureBudget_ - additionalBytes)
        return;

    enum class CacheKind { SlopeBlend, SlopeFrame, Sheet };
    struct Candidate {
        CacheKind kind;
        uint64_t lastUse;
        uint64_t sheetKey = 0;
        SlopeFrameKey slopeKey{};
        uint32_t blendKey = 0;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(slopeBlendMasks_.size() + slopeFrames_.size() + sheets_.size());
    for (const auto &entry : slopeBlendMasks_) {
        if (!entry.second || !entry.second->bytes) continue;
        auto used = slopeBlendMaskUse_.find(entry.first);
        const uint64_t generation = used == slopeBlendMaskUse_.end() ? 0 : used->second;
        if (generation != terrainGeneration_)
            candidates.push_back({CacheKind::SlopeBlend, generation, 0, {}, entry.first});
    }
    for (const auto &entry : slopeFrames_) {
        if (!entry.second || !entry.second->bytes) continue;
        auto used = slopeFrameUse_.find(entry.first);
        const uint64_t generation = used == slopeFrameUse_.end() ? 0 : used->second;
        if (generation != terrainGeneration_)
            candidates.push_back({CacheKind::SlopeFrame, generation, 0, entry.first, 0});
    }
    for (const auto &entry : sheets_) {
        if (!entry.second || !entry.second->bytes) continue;
        auto used = sheetUse_.find(entry.first);
        const uint64_t generation = used == sheetUse_.end() ? 0 : used->second;
        if (generation != terrainGeneration_)
            candidates.push_back({CacheKind::Sheet, generation, entry.first, {}, 0});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
        if (a.lastUse != b.lastUse) return a.lastUse < b.lastUse;
        return a.kind < b.kind;
    });

    for (const Candidate &candidate : candidates) {
        if (additionalBytes <= terrainTextureBudget_ &&
            textureBytes_ <= terrainTextureBudget_ - additionalBytes)
            return;
        if (candidate.kind == CacheKind::SlopeBlend) {
            auto it = slopeBlendMasks_.find(candidate.blendKey);
            destroySheet(it->second);
            slopeBlendMasks_.erase(it);
            slopeBlendMaskUse_.erase(candidate.blendKey);
        } else if (candidate.kind == CacheKind::SlopeFrame) {
            auto it = slopeFrames_.find(candidate.slopeKey);
            destroySheet(it->second);
            slopeFrames_.erase(it);
            slopeFrameUse_.erase(candidate.slopeKey);
        } else {
            auto it = sheets_.find(candidate.sheetKey);
            destroySheet(it->second);
            sheets_.erase(it);
            sheetUse_.erase(candidate.sheetKey);
        }
    }
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
        {&sounds_, "sounds_x1.drs", false}, {&sounds_, "sounds.drs", false},
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

    bool loadedLanguage = false;
    for (const char *name :
         {"language.dll", "language_x0.dll", "language_x1.dll",
          "language_x2.dll"}) {
        std::string languagePath = findFileNoCase(dataDir, name);
        if (languagePath.empty())
            languagePath = findFileNoCase(dataDir + "/..", name);
        if (languagePath.empty()) continue;
        std::string languageError;
        if (languageStrings_.load(languagePath, &languageError,
                                  loadedLanguage)) {
            loadedLanguage = true;
            log("loaded " + languagePath + ": " +
                std::to_string(languageStrings_.size()) +
                " localized strings");
        } else {
            log(languageError);
        }
    }
    if (!loadedLanguage)
        log("language DLLs not found; using internal object names");

    std::string blendPath = findFileNoCase(dataDir, "blendomatic.dat");
    if (blendPath.empty()) {
        log("blendomatic.dat not found; terrain blending disabled");
    } else {
        auto blendomatic = std::make_unique<Blendomatic>();
        if (!blendomatic->load(blendPath, err) || !buildBlendMasks(*blendomatic, err)) return false;
        log("loaded " + blendPath + ": " + std::to_string(blendomatic->modeCount()) + " modes, " +
            std::to_string(blendomatic->maskCount()) + " masks each");
        blendomatic_ = std::move(blendomatic);
    }

    const std::string templatePath = findFileNoCase(dataDir, "STemplet.dat");
    const std::string filterPath = findFileNoCase(dataDir, "FilterMaps.dat");
    const std::string icmPath = findFileNoCase(dataDir, "VIEW_ICM.DAT");
    const std::string lightPath = findFileNoCase(dataDir, "lightMaps.dat");
    const std::string patternPath = findFileNoCase(dataDir, "PatternMasks.dat");
    if (templatePath.empty() || filterPath.empty() || icmPath.empty() || lightPath.empty() ||
        patternPath.empty()) {
        log("terrain elevation resources not found; sloped terrain disabled");
    } else {
        auto maps = std::make_unique<ElevationMaps>();
        if (!maps->load(templatePath, filterPath, icmPath, lightPath, patternPath, err)) return false;
        elevationMaps_ = std::move(maps);
        log("loaded terrain elevation maps: " + std::to_string(kSlopeCount) + " slope types");
    }
    return true;
}

bool Assets::readSound(int soundId, int civilization, uint32_t choice,
                       std::vector<uint8_t> &data, int *resourceId,
                       std::string *fileName) {
    const dat::Sound *sound = nullptr;
    if (soundId >= 0 && (size_t)soundId < dat_.sounds.size() &&
        dat_.sounds[(size_t)soundId].id == soundId)
        sound = &dat_.sounds[(size_t)soundId];
    if (!sound)
        for (const dat::Sound &candidate : dat_.sounds)
            if (candidate.id == soundId) {
                sound = &candidate;
                break;
            }
    if (!sound || sound->items.empty()) return false;

    std::vector<const dat::SoundItem *> eligible;
    int totalWeight = 0;
    for (const dat::SoundItem &item : sound->items)
        if (item.civilization < 0 || item.civilization == civilization) {
            eligible.push_back(&item);
            totalWeight += std::max<int>(1, item.probability);
        }
    if (totalWeight <= 0) return false;
    int selectedWeight = (int)(choice % (uint32_t)totalWeight);
    size_t selectedIndex = 0;
    for (; selectedIndex < eligible.size(); selectedIndex++) {
        selectedWeight -= std::max<int>(1, eligible[selectedIndex]->probability);
        if (selectedWeight < 0) {
            break;
        }
    }
    for (size_t offset = 0; offset < eligible.size(); offset++) {
        const dat::SoundItem &selected =
            *eligible[(selectedIndex + offset) % eligible.size()];
        const auto cached = soundDataCache_.find(selected.resourceId);
        if (cached != soundDataCache_.end()) {
            data = cached->second;
        } else {
            if (!sounds_.read(selected.resourceId, data)) continue;
            if (soundDataCache_.size() >= 32) soundDataCache_.clear();
            soundDataCache_[selected.resourceId] = data;
        }
        if (resourceId) *resourceId = selected.resourceId;
        if (fileName) *fileName = selected.fileName;
        return true;
    }
    return false;
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

const SpriteFrame *Assets::blendMask(int mode, int mask, int slope) {
    if (mode < 0 || (size_t)mode >= blendMasks_.size()) return nullptr;
    if (mask < 0 || (size_t)mask >= blendMasks_[mode].size()) return nullptr;
    if (slope > 0 && elevationMaps_) {
        if ((size_t)slope >= kSlopeCount) return nullptr;
        uint32_t key = (uint32_t)(uint8_t)mode << 16 | (uint32_t)(uint8_t)mask << 8 |
                       (uint32_t)(uint8_t)slope;
        auto it = slopeBlendMasks_.find(key);
        if (it != slopeBlendMasks_.end()) {
            slopeBlendMaskUse_[key] = terrainGeneration_;
            return it->second && !it->second->frames.empty() ? &it->second->frames[0] : nullptr;
        }
        return buildSlopeBlendMask(mode, mask, slope, key);
    }
    return &blendMasks_[mode][mask];
}

const SpriteFrame *Assets::buildSlopeBlendMask(int mode, int mask, int slope, uint32_t key) {
    if (!blendomatic_ || (size_t)mode >= blendomatic_->modes().size() ||
        (size_t)mask >= blendomatic_->modes()[(size_t)mode].masks.size()) {
        slopeBlendMasks_[key] = nullptr;
        slopeBlendMaskUse_[key] = terrainGeneration_;
        return nullptr;
    }
    const BlendMask &source = blendomatic_->modes()[(size_t)mode].masks[(size_t)mask];
    if (source.width != 97 || source.height != 49) {
        slopeBlendMasks_[key] = nullptr;
        slopeBlendMaskUse_[key] = terrainGeneration_;
        return nullptr;
    }

    std::vector<uint16_t> commandPixels;
    size_t commandOffset = 0;
    for (int y = 0; y < source.height; y++) {
        const int width = 1 + 4 * std::min(y, source.height - 1 - y);
        const int left = (source.width - width) / 2;
        const int commandBytes = width <= 63 ? 1 : 2;
        if (commandPixels.size() < commandOffset + commandBytes + (size_t)width + 1)
            commandPixels.resize(commandOffset + commandBytes + (size_t)width + 1, UINT16_MAX);
        for (int x = 0; x < width; x++)
            commandPixels[commandOffset + commandBytes + x] =
                (uint16_t)((size_t)y * source.width + left + x);
        commandOffset += commandBytes + width + 1;
    }

    const SlopeTemplate &shape = elevationMaps_->slopeTemplate((size_t)slope);
    const FilterMap &filter = elevationMaps_->filterMap((size_t)slope);
    std::vector<uint8_t> alpha((size_t)shape.width * shape.height, 0);
    bool valid = true;
    for (int y = 0; y < shape.height && valid; y++) {
        int x = shape.leftEdges[(size_t)y];
        for (const FilterPixel &pixel : filter.lines[(size_t)y].pixels) {
            uint32_t value = 0;
            for (const FilterSource &sample : pixel.sources) {
                if (sample.sourceOffset >= commandPixels.size() ||
                    commandPixels[sample.sourceOffset] == UINT16_MAX) {
                    valid = false;
                    break;
                }
                value += source.alpha[commandPixels[sample.sourceOffset]] * sample.alpha;
            }
            if (!valid) break;
            alpha[(size_t)y * shape.width + x++] = (uint8_t)std::min(255u, (value + 128) >> 8);
        }
    }
    if (!valid) {
        log("slope blend mask references invalid source data");
        slopeBlendMasks_[key] = nullptr;
        slopeBlendMaskUse_[key] = terrainGeneration_;
        return nullptr;
    }

    ensureTerrainCacheSpace(alpha.size());
    Texture *texture = renderer_->createMaskTexture(shape.width, shape.height, alpha.data());
    if (!texture) {
        log("slope blend mask texture allocation failed");
        slopeBlendMasks_[key] = nullptr;
        slopeBlendMaskUse_[key] = terrainGeneration_;
        return nullptr;
    }
    auto sheet = std::make_unique<SpriteSheet>();
    sheet->pages.push_back(texture);
    sheet->bytes = alpha.size();
    sheet->frames.push_back({texture, 0, 0, shape.width, shape.height, 0, 0});
    textureBytes_ += sheet->bytes;
    const SpriteFrame *result = &sheet->frames[0];
    slopeBlendMasks_[key] = std::move(sheet);
    slopeBlendMaskUse_[key] = terrainGeneration_;
    return result;
}

const SpriteSheet *Assets::sheet(int32_t slpId, int playerColorBase) {
    uint64_t key = ((uint64_t)(uint32_t)slpId << 16) | (uint16_t)playerColorBase;
    auto it = sheets_.find(key);
    if (it != sheets_.end()) {
        sheetUse_[key] = terrainGeneration_;
        return it->second.get();
    }
    return build(graphics_, slpId, playerColorBase, key);
}

const SpriteSheet *Assets::interfaceSheet(int32_t slpId) {
    const uint64_t key = (1ull << 63) | ((uint64_t)(uint32_t)slpId << 16);
    auto it = sheets_.find(key);
    if (it != sheets_.end()) {
        sheetUse_[key] = terrainGeneration_;
        return it->second.get();
    }
    return build(interfac_, slpId, 16, key);
}

const SpriteSheet *Assets::terrainSheet(int32_t slpId) {
    uint64_t key = ((uint64_t)(uint32_t)slpId << 16) | 0xFFFF;
    auto it = sheets_.find(key);
    if (it != sheets_.end()) {
        sheetUse_[key] = terrainGeneration_;
        return it->second.get();
    }
    return build(terrain_, slpId, 16, key);
}

bool Assets::SlopeFrameKey::operator<(const SlopeFrameKey &other) const {
    if (slpId != other.slpId) return slpId < other.slpId;
    if (frame != other.frame) return frame < other.frame;
    if (slope != other.slope) return slope < other.slope;
    if (lighting.count != other.lighting.count) return lighting.count < other.lighting.count;
    return lighting.patterns < other.lighting.patterns;
}

const SpriteFrame *Assets::terrainSlopeFrame(int32_t slpId, int slope, size_t frame,
                                             const std::array<int8_t, 8> &neighbors) {
    if (slope == 0 || !elevationMaps_) {
        const SpriteSheet *sheet = terrainSheet(slpId);
        return sheet && frame < sheet->frames.size() ? &sheet->frames[frame] : nullptr;
    }
    if (slope < 0 || (size_t)slope >= kSlopeCount || frame > 0xFFFFFF) return nullptr;
    SlopeFrameKey key{
        slpId, (uint32_t)frame, (uint8_t)slope, selectSlopeLighting((uint8_t)slope, neighbors)};
    auto it = slopeFrames_.find(key);
    if (it != slopeFrames_.end()) {
        slopeFrameUse_[key] = terrainGeneration_;
        return it->second && !it->second->frames.empty() ? &it->second->frames[0] : nullptr;
    }
    return buildTerrainSlopeFrame(key);
}

const SpriteSheet *Assets::build(ResourceSet &set, int32_t slpId, int playerColorBase, uint64_t key) {
    std::vector<uint8_t> data;
    if (slpId < 0 || !set.read(slpId, data)) {
        sheets_[key] = nullptr;
        sheetUse_[key] = terrainGeneration_;
        return nullptr;
    }
    Slp slp;
    std::string err;
    if (!slp.parse(std::move(data), &err)) {
        log("slp " + std::to_string(slpId) + ": " + err);
        sheets_[key] = nullptr;
        sheetUse_[key] = terrainGeneration_;
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
        sheetUse_[key] = terrainGeneration_;
        return nullptr;
    }
    textureBytes_ += sheet->bytes;
    const SpriteSheet *res = sheet.get();
    sheets_[key] = std::move(sheet);
    sheetUse_[key] = terrainGeneration_;
    return res;
}

const SpriteFrame *Assets::buildTerrainSlopeFrame(const SlopeFrameKey &key) {
    const int32_t slpId = key.slpId;
    const int slope = key.slope;
    const size_t frame = key.frame;
    std::string err;
    auto slpIt = terrainSlps_.find(slpId);
    if (slpIt == terrainSlps_.end()) {
        std::vector<uint8_t> data;
        if (slpId < 0 || !terrain_.read(slpId, data)) {
            slopeFrames_[key] = nullptr;
            slopeFrameUse_[key] = terrainGeneration_;
            return nullptr;
        }
        auto parsed = std::make_unique<Slp>();
        if (!parsed->parse(std::move(data), &err)) {
            log("terrain slp " + std::to_string(slpId) + ": " + err);
            slopeFrames_[key] = nullptr;
            slopeFrameUse_[key] = terrainGeneration_;
            return nullptr;
        }
        slpIt = terrainSlps_.emplace(slpId, std::move(parsed)).first;
    }
    Slp &slp = *slpIt->second;
    if (frame >= slp.frameCount()) {
        slopeFrames_[key] = nullptr;
        slopeFrameUse_[key] = terrainGeneration_;
        return nullptr;
    }

    const SlopeTemplate &shape = elevationMaps_->slopeTemplate((size_t)slope);
    const FilterMap &filter = elevationMaps_->filterMap((size_t)slope);
    SlpImage source;
    std::vector<uint8_t> commandPalette;
    if (!slp.decode(frame, source, &err, &commandPalette)) {
        log("terrain slp " + std::to_string(slpId) + " frame " + std::to_string(frame) + ": " + err);
        slopeFrames_[key] = nullptr;
        slopeFrameUse_[key] = terrainGeneration_;
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
            uint8_t light = elevationMaps_->lightIndex(
                pixel.lightIndex, key.lighting.patterns.data(), key.lighting.count);
            out.index[dst] = elevationMaps_->colorIndex(
                light, (uint8_t)std::min(31u, r >> 11), (uint8_t)std::min(31u, g >> 11),
                (uint8_t)std::min(31u, b >> 11));
        }
    }
    if (!valid) {
        log("terrain slp " + std::to_string(slpId) + " frame " + std::to_string(frame) +
            ": filter map references invalid source data");
        slopeFrames_[key] = nullptr;
        slopeFrameUse_[key] = terrainGeneration_;
        return nullptr;
    }

    std::vector<uint8_t> pixels((size_t)out.width * out.height * 4, 0);
    ColorizeOptions options;
    colorize(out, palette_, options, pixels.data(), out.width);
    ensureTerrainCacheSpace(pixels.size());
    Texture *texture = renderer_->createTexture(out.width, out.height, pixels.data());
    if (!texture) {
        log("terrain slope texture allocation failed");
        slopeFrames_[key] = nullptr;
        slopeFrameUse_[key] = terrainGeneration_;
        return nullptr;
    }
    auto sheet = std::make_unique<SpriteSheet>();
    sheet->pages.push_back(texture);
    sheet->bytes = pixels.size();
    sheet->frames.push_back({texture, 0, 0, out.width, out.height, out.hotspotX, out.hotspotY});
    textureBytes_ += sheet->bytes;
    const SpriteFrame *result = &sheet->frames[0];
    slopeFrames_[key] = std::move(sheet);
    slopeFrameUse_[key] = terrainGeneration_;
    return result;
}

std::unique_ptr<SpriteSheet> Assets::pack(const std::vector<SlpImage> &imgs, int playerColorBase) {
    const size_t n = imgs.size();
    // Shelf-pack frames into pages. Pages are 1024 wide (wider if a single
    // frame needs it) and at most 2048 tall. 1px padding avoids bleeding.
    struct Place { int page, x, y; };
    std::vector<Place> place(n);
    std::vector<std::pair<int, int>> pageSize; // w, h
    int maxW = 1;
    size_t totalArea = 0;
    for (const auto &image : imgs) {
        maxW = std::max(maxW, image.width + 2);
        totalArea += (size_t)std::max(1, image.width + 1) * std::max(1, image.height + 1);
    }
    int packedWidth = 1;
    while (packedWidth < maxW) packedWidth *= 2;
    while (packedWidth < 1024 && (size_t)packedWidth * packedWidth < totalArea) packedWidth *= 2;
    maxW = std::max(maxW, packedWidth);
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
    bool hasOutlines = false;
    for (const SlpImage &image : imgs)
        if (std::find(image.kind.begin(), image.kind.end(),
                      PX_OUTLINE) != image.kind.end()) {
            hasOutlines = true;
            break;
        }
    std::vector<std::vector<uint8_t>> outlines(
        hasOutlines ? pageSize.size() : 0);
    for (size_t p = 0; p < pageSize.size(); p++) {
        pageSize[p].second = std::max(1, pageSize[p].second);
        pixels[p].assign((size_t)pageSize[p].first * pageSize[p].second * 4, 0);
        if (hasOutlines)
            outlines[p].assign(
                (size_t)pageSize[p].first * pageSize[p].second, 0);
    }
    ColorizeOptions opt;
    opt.playerColorBase = playerColorBase;
    for (size_t i = 0; i < n; i++) {
        const Place &pl = place[i];
        const int pw = pageSize[pl.page].first;
        if (imgs[i].width > 0 && imgs[i].height > 0) {
            colorize(imgs[i], palette_, opt, &pixels[pl.page][((size_t)pl.y * pw + pl.x) * 4], pw);
            if (hasOutlines)
                for (int y = 0; y < imgs[i].height; y++)
                    for (int x = 0; x < imgs[i].width; x++)
                        if (imgs[i].kind[
                                (size_t)y * imgs[i].width + x] ==
                            PX_OUTLINE)
                            outlines[pl.page][
                                (size_t)(pl.y + y) * pw +
                                pl.x + x] = 255;
        }
    }
    size_t packedBytes = 0;
    for (const auto &pagePixels : pixels) packedBytes += pagePixels.size();
    for (const auto &pageOutlines : outlines)
        packedBytes += pageOutlines.size();
    ensureTerrainCacheSpace(packedBytes);
    for (size_t p = 0; p < pageSize.size(); p++) {
        Texture *t = renderer_->createTexture(pageSize[p].first, pageSize[p].second, pixels[p].data());
        if (!t) {
            for (Texture *pageTexture : sheet->pages) renderer_->destroyTexture(pageTexture);
            log("sprite texture allocation failed");
            return nullptr;
        }
        sheet->pages.push_back(t);
        if (hasOutlines) {
            Texture *outline = renderer_->createMaskTexture(
                pageSize[p].first, pageSize[p].second,
                outlines[p].data());
            if (!outline) {
                for (Texture *pageTexture : sheet->pages)
                    renderer_->destroyTexture(pageTexture);
                for (Texture *pageTexture :
                     sheet->outlinePages)
                    renderer_->destroyTexture(pageTexture);
                log("sprite outline texture allocation failed");
                return nullptr;
            }
            sheet->outlinePages.push_back(outline);
        }
    }
    sheet->bytes = packedBytes;
    sheet->frames.resize(n);
    for (size_t i = 0; i < n; i++) {
        SpriteFrame &f = sheet->frames[i];
        f.tex = sheet->pages[place[i].page];
        if (hasOutlines)
            f.outlineTex =
                sheet->outlinePages[place[i].page];
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
