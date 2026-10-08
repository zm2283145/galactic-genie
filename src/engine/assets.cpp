// SPDX-License-Identifier: GPL-3.0-or-later
#include "assets.h"

#include "../core/slp.h"

#include <algorithm>
#include <cstdio>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#if defined(__vita__)
#include <psp2/kernel/threadmgr.h>
#else
#include <thread>
#endif
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

struct Assets::SheetWorker {
    struct Job {
        uint64_t key;
        int32_t slpId;
        int playerColorBase;
    };
    struct Done {
        uint64_t key;
        PreparedSheet prepared;
    };
    const Assets *assets = nullptr;
    ResourceSet graphics; // own file handles: the main thread keeps using graphics_
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job> jobs;
    std::deque<Done> done;
    std::set<uint64_t> pending;
    bool stop = false;
#if defined(__vita__)
    SceUID thread = -1;
    static int entry(SceSize args, void *argp) {
        if (args != sizeof(SheetWorker *) || !argp) return -1;
        (*static_cast<SheetWorker **>(argp))->run();
        return 0;
    }
#else
    std::thread thread;
#endif
    void run() {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [&] { return stop || !jobs.empty(); });
                if (stop) return;
                job = jobs.front();
                jobs.pop_front();
            }
            Done result{job.key, {}};
            assets->prepareSheet(graphics, job.slpId, job.playerColorBase, nullptr, result.prepared);
            std::lock_guard<std::mutex> lock(mutex);
            done.push_back(std::move(result));
        }
    }
    bool start() {
#if defined(__vita__)
        thread = sceKernelCreateThread("swgb_sheets", entry, 0x10000100 + 10, 128 * 1024, 0,
                                       SCE_KERNEL_CPU_MASK_USER_2, nullptr);
        if (thread < 0) return false;
        SheetWorker *self = this;
        if (sceKernelStartThread(thread, sizeof(self), &self) < 0) {
            sceKernelDeleteThread(thread);
            thread = -1;
            return false;
        }
        return true;
#else
        thread = std::thread([this] { run(); });
        return true;
#endif
    }
    ~SheetWorker() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stop = true;
        }
        wake.notify_all();
#if defined(__vita__)
        if (thread >= 0) {
            sceKernelWaitThreadEnd(thread, nullptr, nullptr);
            sceKernelDeleteThread(thread);
        }
#else
        if (thread.joinable()) thread.join();
#endif
    }
};

Assets::Assets(Renderer *r) : renderer_(r) {}

void Assets::setAsyncSheetBuilds(bool enabled) {
    if (!enabled) {
        sheetWorker_.reset();
        return;
    }
    if (sheetWorker_) return;
    auto worker = std::make_unique<SheetWorker>();
    worker->assets = this;
    for (const auto &archive : graphics_.archives())
        if (!worker->graphics.add(archive->path())) return;
    if (!worker->start()) return;
    sheetWorker_ = std::move(worker);
}

size_t Assets::pendingSheetBuilds() const {
    if (!sheetWorker_) return 0;
    std::lock_guard<std::mutex> lock(sheetWorker_->mutex);
    return sheetWorker_->pending.size();
}

void Assets::pumpAsyncSheets() {
    if (!sheetWorker_) return;
    // Upload at most a couple of finished atlases per frame (texture creation
    // stays on the render thread and before beginFrame).
    for (int uploads = 0; uploads < 2; ++uploads) {
        SheetWorker::Done result;
        {
            std::lock_guard<std::mutex> lock(sheetWorker_->mutex);
            if (sheetWorker_->done.empty()) return;
            result = std::move(sheetWorker_->done.front());
            sheetWorker_->done.pop_front();
            sheetWorker_->pending.erase(result.key);
        }
        auto existing = sheets_.find(result.key);
        if (existing != sheets_.end() && existing->second) continue;
        const auto start = std::chrono::steady_clock::now();
        buildCount_++;
        finishSheet(result.key, result.prepared);
        const uint64_t us = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - start)
                                .count();
        buildUs_ += us;
        buildMaxUs_ = std::max(buildMaxUs_, us);
    }
}

Assets::~Assets() {
    sheetWorker_.reset();
    for (auto &kv : sheets_)
        if (kv.second)
            for (Texture *t : kv.second->pages) renderer_->destroyTexture(t);
    for (auto &kv : slopeFrames_)
        if (kv.second)
            for (Texture *t : kv.second->pages) renderer_->destroyTexture(t);
    for (auto &kv : slopeBlendMasks_)
        if (kv.second)
            for (Texture *t : kv.second->pages) renderer_->destroyTexture(t);
    for (auto &kv : fileFrames_)
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
    sheetCache_.fill(SheetCacheEntry{});
    if (!sheet) return;
    for (Texture *texture : sheet->pages) renderer_->destroyTexture(texture);
    for (Texture *texture : sheet->outlinePages)
        renderer_->destroyTexture(texture);
    textureBytes_ = sheet->bytes <= textureBytes_ ? textureBytes_ - sheet->bytes : 0;
    sheet.reset();
}

void Assets::beginTerrainFrame(size_t textureBudget) {
    terrainTextureBudget_ = textureBudget;
    pumpAsyncSheets();
    buildsThisFrame_ = 0;
    slopeBuildsThisFrame_ = 0;
    terrainFrameStartUs_ = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count();
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
        if ((entry.first >> 62) == 3) continue;
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

bool Assets::readGamedataText(int32_t id, std::string &text) {
    std::vector<uint8_t> data;
    if (!gamedata_.read(id, data)) return false;
    text.assign(data.begin(), data.end());
    return true;
}

bool Assets::init(const std::string &dataDir, std::string *err) {
    struct Want {
        ResourceSet *set;
        const char *name;
        bool required;
    };
    // Later expansions and official patch archives must precede the base
    // archives. Duplicate ids are common in interface resources.
    const Want wants[] = {
        {&graphics_, "graphics_x1_p1.drs", false},
        {&graphics_, "graphics_x1.drs", true},
        {&graphics_, "graphics_p1.drs", false},
        {&graphics_, "graphics.drs", true},
        {&terrain_, "terrain_x1_p1.drs", false},
        {&terrain_, "terrain_x1.drs", false},
        {&terrain_, "terrain_p1.drs", false},
        {&terrain_, "terrain.drs", true},
        {&interfac_, "interfac_x1_p1.drs", false},
        {&interfac_, "interfac_x1.drs", false},
        {&interfac_, "interfac_p1.drs", false},
        {&interfac_, "interfac.drs", true},
        // Expanding Fronts uses unique frontend ids. Keep it after the
        // original archives so its incompatible duplicate 50500 palette
        // cannot replace the global gameplay palette.
        {&interfac_, "interfac_x2.drs", false},
        {&sounds_, "sounds_x1.drs", false},
        {&sounds_, "sounds.drs", false},
        // Standard AI modules (bina 60001-60056) and random map scripts.
        {&gamedata_, "gamedata_x1.drs", false},
        {&gamedata_, "gamedata.drs", false},
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
    // The engine's world axes are the original's swapped (X = original y,
    // Y = mapSize - original x, as in scenario loading and annex offsets),
    // so footprint half-sizes swap too. Square footprints are unaffected;
    // gates (1.0 x 0.5 along their posts) now lie along their posts.
    for (dat::Civ &civ : dat_.civs)
        for (dat::Unit &unit : civ.units) {
            std::swap(unit.collisionSize[0], unit.collisionSize[1]);
            std::swap(unit.outlineSize[0], unit.outlineSize[1]);
            std::swap(unit.clearanceSize[0], unit.clearanceSize[1]);
        }
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
    std::shared_ptr<const std::vector<uint8_t>> shared;
    if (!readSoundShared(soundId, civilization, choice, shared, resourceId, fileName))
        return false;
    data = *shared;
    return true;
}

std::vector<std::string> Assets::soundArchivePaths() const {
    std::vector<std::string> paths;
    for (const auto &archive : sounds_.archives()) paths.push_back(archive->path());
    return paths;
}

std::vector<std::string> Assets::interfaceArchivePaths() const {
    std::vector<std::string> paths;
    for (const auto &archive : interfac_.archives()) paths.push_back(archive->path());
    return paths;
}

bool Assets::selectSound(int soundId, int civilization, uint32_t choice,
                         int *resourceId, std::string *fileName) {
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
        if (!soundDataCache_.count(selected.resourceId) && !sounds_.has(selected.resourceId))
            continue;
        if (resourceId) *resourceId = selected.resourceId;
        if (fileName) *fileName = selected.fileName;
        return true;
    }
    return false;
}

bool Assets::readSoundShared(int soundId, int civilization, uint32_t choice,
                             std::shared_ptr<const std::vector<uint8_t>> &data,
                             int *resourceId, std::string *fileName) {
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
            auto bytes = std::make_shared<std::vector<uint8_t>>();
            if (!sounds_.read(selected.resourceId, *bytes)) continue;
            data = bytes;
            // Evict one entry, not the whole cache: battles cycle through
            // more than a few dozen sounds and each miss reads the DRS.
            if (soundDataCache_.size() >= 96) soundDataCache_.erase(soundDataCache_.begin());
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
    // Single-frame slope tiles and masks are small: they get their own budget
    // (twice the sheet budget) so scrolling fills in quickly.
    if (maximumBuildsPerFrame_ != SIZE_MAX &&
        slopeBuildsThisFrame_ >= maximumBuildsPerFrame_ * 2)
        return nullptr;
    ++slopeBuildsThisFrame_;
    ++slopeBuildCount_;
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

const SpriteSheet *Assets::cachedSheet(uint64_t key) {
    SheetCacheEntry &entry = sheetCache_[(size_t)((key ^ (key >> 16) ^ (key >> 40)) & 255)];
    if (entry.key != key) return nullptr;
    if (entry.stamped != terrainGeneration_) {
        entry.stamped = terrainGeneration_;
        sheetUse_[key] = terrainGeneration_;
    }
    return entry.sheet;
}

void Assets::rememberSheet(uint64_t key, const SpriteSheet *sheet) {
    if (!sheet) return;
    SheetCacheEntry &entry = sheetCache_[(size_t)((key ^ (key >> 16) ^ (key >> 40)) & 255)];
    entry.key = key;
    entry.sheet = sheet;
    entry.stamped = terrainGeneration_;
}

const SpriteSheet *Assets::sheet(int32_t slpId, int playerColorBase) {
    uint64_t key = ((uint64_t)(uint32_t)slpId << 16) | (uint16_t)playerColorBase;
    if (const SpriteSheet *hit = cachedSheet(key)) return hit;
    auto it = sheets_.find(key);
    if (it != sheets_.end()) {
        sheetUse_[key] = terrainGeneration_;
        rememberSheet(key, it->second.get());
        return it->second.get();
    }
    if (sheetWorker_) {
        std::lock_guard<std::mutex> lock(sheetWorker_->mutex);
        if (sheetWorker_->pending.insert(key).second) {
            sheetWorker_->jobs.push_back({key, slpId, playerColorBase});
            sheetWorker_->wake.notify_one();
        }
        return nullptr;
    }
    const SpriteSheet *built = build(graphics_, slpId, playerColorBase, key);
    rememberSheet(key, built);
    return built;
}

const SpriteSheet *Assets::interfaceSheet(int32_t slpId) {
    const uint64_t key = (1ull << 63) | ((uint64_t)(uint32_t)slpId << 16);
    if (const SpriteSheet *hit = cachedSheet(key)) return hit;
    auto it = sheets_.find(key);
    if (it != sheets_.end()) {
        sheetUse_[key] = terrainGeneration_;
        rememberSheet(key, it->second.get());
        return it->second.get();
    }
    const SpriteSheet *built = build(interfac_, slpId, 16, key);
    rememberSheet(key, built);
    return built;
}

const SpriteSheet *Assets::interfaceSheet(
    int32_t slpId, int32_t paletteId) {
    if (paletteId == 50500)
        return interfaceSheet(slpId);
    const uint64_t key =
        (1ull << 63) |
        ((uint64_t)(uint32_t)slpId << 16) |
        (uint16_t)paletteId;
    if (const SpriteSheet *hit = cachedSheet(key))
        return hit;
    auto it = sheets_.find(key);
    if (it != sheets_.end()) {
        sheetUse_[key] = terrainGeneration_;
        rememberSheet(key, it->second.get());
        return it->second.get();
    }
    std::vector<uint8_t> data;
    Palette palette;
    std::string err;
    if (!interfac_.read(paletteId, data) ||
        !palette.parse(data, &err)) {
        log(
            "interface palette " +
            std::to_string(paletteId) +
            (err.empty() ? " not found" : ": " + err));
        sheets_[key] = nullptr;
        sheetUse_[key] = terrainGeneration_;
        return nullptr;
    }
    const SpriteSheet *built =
        build(interfac_, slpId, 16, key, &palette);
    rememberSheet(key, built);
    return built;
}

const SpriteFrame *Assets::interfaceFrame(
    int32_t slpId, size_t frame,
    int32_t paletteId) {
    if (frame > 0xFFFF) return nullptr;
    const uint64_t key =
        (3ull << 62) |
        ((uint64_t)(uint16_t)slpId << 32) |
        ((uint64_t)(uint16_t)paletteId << 16) |
        (uint16_t)frame;
    auto cached = sheets_.find(key);
    if (cached != sheets_.end())
        return cached->second &&
                       !cached->second->frames.empty()
                   ? &cached->second->frames[0]
                   : nullptr;
    // Screens take many frames from one large SLP (the main menu, the
    // campaign maps); keep the last few parsed SLPs and palettes instead of
    // reading and parsing the whole file again for every frame.
    std::string err;
    const Slp *slp = nullptr;
    for (auto &entry : interfaceSlpCache_)
        if (entry.first == slpId) slp = entry.second.get();
    if (!slp) {
        std::vector<uint8_t> slpData;
        auto parsed = std::make_unique<Slp>();
        if (interfac_.read(slpId, slpData) && parsed->parse(std::move(slpData), &err)) {
            if (interfaceSlpCache_.size() >= 3) interfaceSlpCache_.pop_front();
            interfaceSlpCache_.emplace_back(slpId, std::move(parsed));
            slp = interfaceSlpCache_.back().second.get();
        }
    }
    const Palette *palettePointer = nullptr;
    for (auto &entry : interfacePaletteCache_)
        if (entry.first == paletteId) palettePointer = &entry.second;
    if (!palettePointer) {
        std::vector<uint8_t> paletteData;
        Palette parsedPalette;
        if (interfac_.read(paletteId, paletteData) && parsedPalette.parse(paletteData, &err)) {
            if (interfacePaletteCache_.size() >= 8) interfacePaletteCache_.pop_front();
            interfacePaletteCache_.emplace_back(paletteId, std::move(parsedPalette));
            palettePointer = &interfacePaletteCache_.back().second;
        }
    }
    SlpImage image;
    if (!slp || frame >= slp->frameCount() || !slp->decode(frame, image, &err) ||
        !palettePointer) {
        log(
            "interface frame " +
            std::to_string(slpId) + ":" +
            std::to_string(frame) + ": " +
            (err.empty() ? "resource unavailable" : err));
        sheets_[key] = nullptr;
        sheetUse_[key] = terrainGeneration_;
        return nullptr;
    }
    const Palette palette = *palettePointer;
    std::vector<SlpImage> images;
    images.push_back(std::move(image));
    auto sheet = pack(images, 16, &palette);
    if (!sheet) {
        sheets_[key] = nullptr;
        sheetUse_[key] = terrainGeneration_;
        return nullptr;
    }
    textureBytes_ += sheet->bytes;
    const SpriteFrame *result =
        &sheet->frames[0];
    sheets_[key] = std::move(sheet);
    sheetUse_[key] = terrainGeneration_;
    return result;
}

namespace {
bool readWholeFile(const std::string &path, std::vector<uint8_t> &data) {
    data.clear();
    FILE *file = fopen(path.c_str(), "rb");
    if (!file) return false;
    bool ok = fseek(file, 0, SEEK_END) == 0;
    const long length = ok ? ftell(file) : -1;
    ok = ok && length > 0 && fseek(file, 0, SEEK_SET) == 0;
    if (ok) {
        data.resize((size_t)length);
        ok = fread(data.data(), 1, data.size(), file) == data.size();
    }
    fclose(file);
    if (!ok) data.clear();
    return ok;
}
} // namespace

const SpriteFrame *Assets::fileFrame(const std::string &slpPath, size_t frame,
                                     const std::string &palettePath) {
    const std::string key = slpPath + "|" + std::to_string(frame) + "|" + palettePath;
    auto cached = fileFrames_.find(key);
    if (cached != fileFrames_.end())
        return cached->second && !cached->second->frames.empty() ? &cached->second->frames[0]
                                                                 : nullptr;
    std::vector<uint8_t> slpData, paletteData;
    Palette palette;
    Slp slp;
    SlpImage image;
    std::string err;
    if (!readWholeFile(slpPath, slpData) || !slp.parse(std::move(slpData), &err) ||
        frame >= slp.frameCount() || !slp.decode(frame, image, &err) ||
        !readWholeFile(palettePath, paletteData) || !palette.parse(paletteData, &err)) {
        log("media frame " + slpPath + ":" + std::to_string(frame) + ": " +
            (err.empty() ? "file unavailable" : err));
        fileFrames_[key] = nullptr;
        return nullptr;
    }
    std::vector<SlpImage> images;
    images.push_back(std::move(image));
    auto sheet = pack(images, 16, &palette);
    if (!sheet) {
        fileFrames_[key] = nullptr;
        return nullptr;
    }
    textureBytes_ += sheet->bytes;
    const SpriteFrame *result = &sheet->frames[0];
    fileFrames_[key] = std::move(sheet);
    return result;
}

void Assets::releaseFileFrames() {
    for (auto &entry : fileFrames_) destroySheet(entry.second);
    fileFrames_.clear();
}

void Assets::releaseInterfaceFrame(
    int32_t slpId, size_t frame,
    int32_t paletteId) {
    if (frame > 0xFFFF) return;
    const uint64_t key =
        (3ull << 62) |
        ((uint64_t)(uint16_t)slpId << 32) |
        ((uint64_t)(uint16_t)paletteId << 16) |
        (uint16_t)frame;
    auto sheet = sheets_.find(key);
    if (sheet == sheets_.end()) return;
    destroySheet(sheet->second);
    sheets_.erase(sheet);
    sheetUse_.erase(key);
}

const SpriteSheet *Assets::preloadTerrainSheet(int32_t slpId) {
    const size_t maximum = maximumBuildsPerFrame_;
    const size_t used = buildsThisFrame_;
    maximumBuildsPerFrame_ = SIZE_MAX;
    const SpriteSheet *sheet = terrainSheet(slpId);
    maximumBuildsPerFrame_ = maximum;
    buildsThisFrame_ = used;
    return sheet;
}

const SpriteSheet *Assets::terrainSheet(int32_t slpId) {
    uint64_t key = ((uint64_t)(uint32_t)slpId << 16) | 0xFFFF;
    if (const SpriteSheet *hit = cachedSheet(key)) return hit;
    auto it = sheets_.find(key);
    if (it != sheets_.end()) {
        sheetUse_[key] = terrainGeneration_;
        rememberSheet(key, it->second.get());
        return it->second.get();
    }
    const SpriteSheet *built = build(terrain_, slpId, 16, key);
    rememberSheet(key, built);
    return built;
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

void Assets::prepareSheet(ResourceSet &set, int32_t slpId, int playerColorBase,
                          const Palette *palette, PreparedSheet &out) const {
    out = PreparedSheet{};
    std::vector<uint8_t> data;
    if (slpId < 0 || !set.read(slpId, data)) return;
    Slp slp;
    std::string err;
    if (!slp.parse(std::move(data), &err)) {
        out.logs.push_back("slp " + std::to_string(slpId) + ": " + err);
        return;
    }

    const size_t n = slp.frameCount();
    std::vector<SlpImage> imgs(n);
    for (size_t i = 0; i < n; i++) {
        if (!slp.decode(i, imgs[i], &err)) {
            out.logs.push_back("slp " + std::to_string(slpId) + " frame " + std::to_string(i) + ": " + err);
            imgs[i] = SlpImage{};
        }
    }

    out.packed = packPixels(imgs, playerColorBase, palette);
    // Laser bolt frames: a large frame with only a few coloured pixels, the
    // bolt's ends. Record the two farthest ones and their colour.
    out.lasers.assign(n, SpriteFrame{});
    for (size_t i = 0; i < n; i++) {
        const SlpImage &image = imgs[i];
        if (image.width < 8 && image.height < 8) continue;
        std::vector<int> lit;
        for (size_t p = 0; p < image.kind.size() && lit.size() <= 48; p++)
            if (image.kind[p] == PX_COLOR || image.kind[p] == PX_PLAYER) lit.push_back((int)p);
        if (lit.size() < 2 || lit.size() > 48) continue;
        int best0 = lit[0], best1 = lit[1], bestD = -1;
        for (int a : lit)
            for (int b : lit) {
                const int dx = a % image.width - b % image.width, dy = a / image.width - b / image.width;
                if (dx * dx + dy * dy > bestD) { bestD = dx * dx + dy * dy; best0 = a; best1 = b; }
            }
        if (bestD < 16) continue;
        SpriteFrame &frame = out.lasers[i];
        frame.laser = true;
        frame.laserX0 = (float)(best0 % image.width - image.hotspotX);
        frame.laserY0 = (float)(best0 / image.width - image.hotspotY);
        frame.laserX1 = (float)(best1 % image.width - image.hotspotX);
        frame.laserY1 = (float)(best1 / image.width - image.hotspotY);
        const uint8_t index = image.kind[(size_t)best0] == PX_PLAYER
                                  ? (uint8_t)(image.index[(size_t)best0] + playerColorBase)
                                  : image.index[(size_t)best0];
        const Palette &colors =
            palette ? *palette : palette_;
        frame.laserR = colors.colors[index].r;
        frame.laserG = colors.colors[index].g;
        frame.laserB = colors.colors[index].b;
    }
    out.ok = true;
}

const SpriteSheet *Assets::finishSheet(uint64_t key, PreparedSheet &prepared) {
    for (const std::string &message : prepared.logs) log(message);
    std::unique_ptr<SpriteSheet> sheet;
    if (prepared.ok) sheet = uploadPacked(prepared.packed);
    if (!sheet) {
        sheets_[key] = nullptr;
        sheetUse_[key] = terrainGeneration_;
        return nullptr;
    }
    for (size_t i = 0; i < prepared.lasers.size() && i < sheet->frames.size(); i++) {
        const SpriteFrame &laser = prepared.lasers[i];
        if (!laser.laser) continue;
        SpriteFrame &frame = sheet->frames[i];
        frame.laser = true;
        frame.laserX0 = laser.laserX0;
        frame.laserY0 = laser.laserY0;
        frame.laserX1 = laser.laserX1;
        frame.laserY1 = laser.laserY1;
        frame.laserR = laser.laserR;
        frame.laserG = laser.laserG;
        frame.laserB = laser.laserB;
    }
    textureBytes_ += sheet->bytes;
    const SpriteSheet *res = sheet.get();
    sheets_[key] = std::move(sheet);
    sheetUse_[key] = terrainGeneration_;
    return res;
}

const SpriteSheet *Assets::build(
    ResourceSet &set, int32_t slpId,
    int playerColorBase, uint64_t key,
    const Palette *palette) {
    if (buildsThisFrame_ >=
        maximumBuildsPerFrame_)
        return nullptr;
    ++buildsThisFrame_;
    buildCount_++;
    const auto buildStart = std::chrono::steady_clock::now();
    struct BuildTimer {
        std::chrono::steady_clock::time_point start;
        uint64_t &total, &maximum;
        ~BuildTimer() {
            const uint64_t us = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
                                    std::chrono::steady_clock::now() - start)
                                    .count();
            total += us;
            maximum = std::max(maximum, us);
        }
    } buildTimer{buildStart, buildUs_, buildMaxUs_};
    PreparedSheet prepared;
    prepareSheet(set, slpId, playerColorBase, palette, prepared);
    return finishSheet(key, prepared);
}

const SpriteFrame *Assets::buildTerrainSlopeFrame(const SlopeFrameKey &key) {
    // Single-frame slope tiles and masks are small: they get their own budget
    // (twice the sheet budget) so scrolling fills in quickly.
    // Visible slope tiles have no usable stand-in (a flat tile leaves a hole
    // on a hill), so past the count budget keep building them until the
    // frame has spent kSlopeBuildWindowUs on terrain.
    if (maximumBuildsPerFrame_ != SIZE_MAX &&
        slopeBuildsThisFrame_ >= maximumBuildsPerFrame_ * 2) {
        constexpr uint64_t kSlopeBuildWindowUs = 12000;
        const uint64_t now = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch())
                                 .count();
        if (now - terrainFrameStartUs_ > kSlopeBuildWindowUs) return nullptr;
    }
    ++slopeBuildsThisFrame_;
    ++slopeBuildCount_;
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

Assets::PackedSheet Assets::packPixels(
    const std::vector<SlpImage> &imgs,
    int playerColorBase,
    const Palette *palette) const {
    PackedSheet packed;
    const size_t n = imgs.size();
    // Shelf-pack frames into pages. Pages are 1024 wide (wider if a single
    // frame needs it) and at most 2048 tall. 1px padding avoids bleeding.
    struct Place { int page, x, y; };
    std::vector<Place> place(n);
    std::vector<std::pair<int, int>> &pageSize = packed.pageSize; // w, h
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

    std::vector<std::vector<uint8_t>> &pixels = packed.pixels;
    pixels.resize(pageSize.size());
    bool hasOutlines = false;
    for (const SlpImage &image : imgs)
        if (std::find(image.kind.begin(), image.kind.end(),
                      PX_OUTLINE) != image.kind.end()) {
            hasOutlines = true;
            break;
        }
    packed.hasOutlines = hasOutlines;
    std::vector<std::vector<uint8_t>> &outlines = packed.outlines;
    outlines.resize(hasOutlines ? pageSize.size() : 0);
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
            colorize(
                imgs[i], palette ? *palette : palette_,
                opt,
                &pixels[pl.page][
                    ((size_t)pl.y * pw + pl.x) * 4],
                pw);
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
    packed.bytes = packedBytes;
    packed.frames.resize(n);
    for (size_t i = 0; i < n; i++) {
        PackedSheet::Frame &f = packed.frames[i];
        f.page = place[i].page;
        f.x = place[i].x;
        f.y = place[i].y;
        f.w = imgs[i].width;
        f.h = imgs[i].height;
        f.hotX = imgs[i].hotspotX;
        f.hotY = imgs[i].hotspotY;
    }
    return packed;
}

std::unique_ptr<SpriteSheet> Assets::uploadPacked(PackedSheet &packed) {
    auto sheet = std::make_unique<SpriteSheet>();
    const auto &pageSize = packed.pageSize;
    const bool hasOutlines = packed.hasOutlines;
    ensureTerrainCacheSpace(packed.bytes);
    for (size_t p = 0; p < pageSize.size(); p++) {
        Texture *t = renderer_->createTexture(pageSize[p].first, pageSize[p].second, packed.pixels[p].data());
        if (!t) {
            for (Texture *pageTexture : sheet->pages) renderer_->destroyTexture(pageTexture);
            log("sprite texture allocation failed");
            return nullptr;
        }
        sheet->pages.push_back(t);
        if (hasOutlines) {
            Texture *outline = renderer_->createMaskTexture(
                pageSize[p].first, pageSize[p].second,
                packed.outlines[p].data());
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
    sheet->bytes = packed.bytes;
    const size_t n = packed.frames.size();
    sheet->frames.resize(n);
    for (size_t i = 0; i < n; i++) {
        const PackedSheet::Frame &placed = packed.frames[i];
        SpriteFrame &f = sheet->frames[i];
        f.tex = sheet->pages[(size_t)placed.page];
        if (hasOutlines)
            f.outlineTex =
                sheet->outlinePages[(size_t)placed.page];
        f.u = (float)placed.x;
        f.v = (float)placed.y;
        f.w = placed.w;
        f.h = placed.h;
        f.hotX = placed.hotX;
        f.hotY = placed.hotY;
    }
    return sheet;
}

std::unique_ptr<SpriteSheet> Assets::pack(
    const std::vector<SlpImage> &imgs,
    int playerColorBase,
    const Palette *palette) {
    PackedSheet packed = packPixels(imgs, playerColorBase, palette);
    return uploadPacked(packed);
}
} // namespace swgb
