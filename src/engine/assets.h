// SPDX-License-Identifier: GPL-3.0-or-later
// Asset manager: owns the DRS archives, palette and dat, and turns SLPs into
// GPU texture atlases on demand.
#pragma once
#include <array>
#include <algorithm>

#include "../core/blendomatic.h"
#include "../core/drs.h"
#include "../core/elevation.h"
#include "../core/genie_dat.h"
#include "../core/language_strings.h"
#include "../core/palette.h"
#include "../core/slp.h"
#include "../core/slope_lighting.h"
#include "../render/renderer.h"

#include <functional>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace swgb {

struct SpriteFrame {
    Texture *tex = nullptr;
    float u, v;       // top-left texel in the atlas page
    int w, h;
    int hotX, hotY;
    Texture *outlineTex = nullptr;
    // SWGB laser bolts: the projectile frame holds only the two end points
    // of the bolt; the bolt is drawn as a glowing line between them.
    bool laser = false;
    float laserX0 = 0, laserY0 = 0, laserX1 = 0, laserY1 = 0; // relative to hotspot
    uint8_t laserR = 0, laserG = 0, laserB = 0;
};

struct SpriteSheet {
    std::vector<SpriteFrame> frames;
    std::vector<Texture *> pages;
    std::vector<Texture *> outlinePages;
    size_t bytes = 0;
};

using LogFn = std::function<void(const std::string &)>;

class Assets {
public:
    explicit Assets(Renderer *r);
    ~Assets();

    // dataDir is the game's "Data" folder (contains genie_x1.dat, *.drs).
    bool init(const std::string &dataDir, std::string *err);
    void setLogger(LogFn fn) { log_ = std::move(fn); }

    const dat::DatFile &dat() const { return dat_; }
    const Palette &palette() const { return palette_; }
    const std::string &localizedString(int id) const {
        return languageStrings_.get(id);
    }

    // Returns the atlas for an SLP tinted for one player (playerColorBase
    // from the dat's player colour table). Null if the SLP doesn't exist.
    const SpriteSheet *sheet(int32_t slpId, int playerColorBase = 16);
    const SpriteSheet *interfaceSheet(int32_t slpId);
    const SpriteSheet *interfaceSheet(
        int32_t slpId, int32_t paletteId);
    const SpriteFrame *interfaceFrame(
        int32_t slpId, size_t frame,
        int32_t paletteId);
    void releaseInterfaceFrame(
        int32_t slpId, size_t frame,
        int32_t paletteId);
    const SpriteSheet *terrainSheet(int32_t slpId);
    // A frame of a loose SLP file (campaign media) with a JASC palette file.
    // Cached by path until releaseFileFrames().
    const SpriteFrame *fileFrame(const std::string &slpPath, size_t frame,
                                 const std::string &palettePath);
    void releaseFileFrames();
    // Builds a terrain sheet now, outside the per-frame build budget (used
    // while a match loads so no visible tile waits for its base art).
    const SpriteSheet *preloadTerrainSheet(int32_t slpId);
    const SpriteFrame *terrainSlopeFrame(int32_t slpId, int slope, size_t frame,
                                         const std::array<int8_t, 8> &neighbors);
    const SpriteFrame *blendMask(int mode, int mask, int slope = 0);
    bool readSound(int soundId, int civilization, uint32_t choice,
                   std::vector<uint8_t> &data, int *resourceId = nullptr,
                   std::string *fileName = nullptr);
    // Same choice as readSound/readSoundShared without reading the bytes: a
    // candidate counts as readable when it is cached or present in the DRS.
    bool selectSound(int soundId, int civilization, uint32_t choice,
                     int *resourceId = nullptr, std::string *fileName = nullptr);
    // Archive paths, in lookup order, so a worker thread can open its own
    // handles (sound effects: sounds_; interface sounds: interfac_ then sounds_).
    std::vector<std::string> soundArchivePaths() const;
    std::vector<std::string> interfaceArchivePaths() const;
    // Same selection as readSound, sharing the cached bytes instead of
    // copying them.
    bool readSoundShared(int soundId, int civilization, uint32_t choice,
                         std::shared_ptr<const std::vector<uint8_t>> &data,
                         int *resourceId = nullptr, std::string *fileName = nullptr);
    // Raw WAV from the sounds DRS by resource id (interface sounds such as
    // gatel.wav 50362 / gateu.wav 50363 are loaded by id, not via the dat).
    // They live in interfac.drs (button1.wav 50300 ... gateu.wav 50363).
    bool readSoundResource(int resourceId, std::vector<uint8_t> &data) {
        return interfac_.read(resourceId, data) || sounds_.read(resourceId, data);
    }
    Texture *selectionRing();
    bool hasBlendMasks() const { return blendMaskTexture_ != nullptr; }
    bool hasElevationMaps() const { return elevationMaps_ != nullptr; }
    // Starts a terrain preparation pass. Generated slope textures not touched
    // by the current pass may be evicted before a cache miss exceeds budget.
    void beginTerrainFrame(size_t textureBudget);
    void setBuildsPerFrame(size_t maximum) {
        maximumBuildsPerFrame_ = maximum;
    }
    // Unit/graphics sheets (sheet()) decode and pack on a worker thread; the
    // atlas is uploaded by the next beginTerrainFrame(). Until then sheet()
    // returns null, exactly as when the per-frame build budget is spent.
    // Only rendering reads these sheets.
    void setAsyncSheetBuilds(bool enabled);
    size_t pendingSheetBuilds() const;

    size_t textureBytes() const { return textureBytes_; }
    // Sheets built so far (a steadily rising count means cache thrash).
    size_t buildCount() const { return buildCount_; }
    size_t slopeBuildCount() const { return slopeBuildCount_; }
    // Time spent building sprite sheets (decode + pack + upload), for the
    // Vita frame log; takeBuildTime() returns and resets {total, max} in us.
    std::pair<uint64_t, uint64_t> takeBuildTime() {
        const auto result = std::make_pair(buildUs_, buildMaxUs_);
        buildUs_ = buildMaxUs_ = 0;
        return result;
    }
    // Largest cached sprite sheets: (bytes, slp id).
    std::vector<std::pair<size_t, int>> largestSheets(size_t n) const {
        std::vector<std::pair<size_t, int>> list;
        for (const auto &entry : sheets_)
            if (entry.second) list.push_back({entry.second->bytes, (int)(uint32_t)(entry.first >> 16)});
        std::sort(list.rbegin(), list.rend());
        if (list.size() > n) list.resize(n);
        return list;
    }
    size_t sheetCount() const { return sheets_.size() + slopeFrames_.size() + slopeBlendMasks_.size(); }

private:
    const SpriteSheet *build(
        ResourceSet &set, int32_t slpId,
        int playerColorBase, uint64_t key,
        const Palette *palette = nullptr);
    struct SlopeFrameKey {
        int32_t slpId;
        uint32_t frame;
        uint8_t slope;
        SlopeLighting lighting;

        bool operator<(const SlopeFrameKey &other) const;
    };

    const SpriteFrame *buildTerrainSlopeFrame(const SlopeFrameKey &key);
    void ensureTerrainCacheSpace(size_t additionalBytes);
    void destroySheet(std::unique_ptr<SpriteSheet> &sheet);
    // CPU half of a sheet build (DRS read, SLP decode, atlas packing); safe
    // on a worker thread given its own ResourceSet.
    struct PackedSheet {
        std::vector<std::pair<int, int>> pageSize; // w, h
        std::vector<std::vector<uint8_t>> pixels, outlines;
        bool hasOutlines = false;
        struct Frame {
            int page = 0, x = 0, y = 0, w = 0, h = 0, hotX = 0, hotY = 0;
        };
        std::vector<Frame> frames;
        size_t bytes = 0;
    };
    struct PreparedSheet {
        bool ok = false;
        PackedSheet packed;
        std::vector<SpriteFrame> lasers; // laser fields per frame (laser=false if none)
        std::vector<std::string> logs;
    };
    void prepareSheet(ResourceSet &set, int32_t slpId, int playerColorBase,
                      const Palette *palette, PreparedSheet &out) const;
    const SpriteSheet *finishSheet(uint64_t key, PreparedSheet &prepared);
    PackedSheet packPixels(const std::vector<SlpImage> &imgs, int playerColorBase,
                           const Palette *palette) const;
    std::unique_ptr<SpriteSheet> uploadPacked(PackedSheet &packed);
    std::unique_ptr<SpriteSheet> pack(
        const std::vector<SlpImage> &imgs,
        int playerColorBase,
        const Palette *palette = nullptr);
    struct SheetWorker;
    std::unique_ptr<SheetWorker> sheetWorker_;
    void pumpAsyncSheets();
    bool buildBlendMasks(const Blendomatic &blendomatic, std::string *err);
    const SpriteFrame *buildSlopeBlendMask(int mode, int mask, int slope, uint32_t key);
    void log(const std::string &s) const { if (log_) log_(s); }

    Renderer *renderer_;
    LogFn log_;
    ResourceSet graphics_, terrain_, interfac_, sounds_;
    Palette palette_;
    dat::DatFile dat_;
    LanguageStrings languageStrings_;
    std::unique_ptr<Blendomatic> blendomatic_;
    std::unique_ptr<ElevationMaps> elevationMaps_;
    std::map<uint64_t, std::unique_ptr<SpriteSheet>> sheets_;
    std::map<uint64_t, uint64_t> sheetUse_;
    std::map<SlopeFrameKey, std::unique_ptr<SpriteSheet>> slopeFrames_;
    std::map<SlopeFrameKey, uint64_t> slopeFrameUse_;
    std::map<uint32_t, std::unique_ptr<SpriteSheet>> slopeBlendMasks_;
    std::map<uint32_t, uint64_t> slopeBlendMaskUse_;
    std::map<int32_t, std::unique_ptr<Slp>> terrainSlps_;
    std::map<std::string, std::unique_ptr<SpriteSheet>> fileFrames_;
    std::deque<std::pair<int32_t, std::unique_ptr<Slp>>> interfaceSlpCache_;
    std::deque<std::pair<int32_t, Palette>> interfacePaletteCache_;
    std::map<int32_t, std::shared_ptr<const std::vector<uint8_t>>> soundDataCache_;
    Texture *blendMaskTexture_ = nullptr;
    Texture *selectionRingTexture_ = nullptr;
    bool selectionRingAttempted_ = false;
    std::vector<std::vector<SpriteFrame>> blendMasks_;
    size_t textureBytes_ = 0;
    // Direct-mapped front cache for sheet lookups (the renderer asks for the
    // same few sheets per tile/sprite every frame; std::map lookups plus the
    // use-stamp map write were ~10% of a frame).
    struct SheetCacheEntry {
        uint64_t key = ~0ull;
        const SpriteSheet *sheet = nullptr;
        uint64_t stamped = 0;
    };
    std::array<SheetCacheEntry, 256> sheetCache_{};
    const SpriteSheet *cachedSheet(uint64_t key);
    void rememberSheet(uint64_t key, const SpriteSheet *sheet);
    size_t buildCount_ = 0;
    uint64_t buildUs_ = 0, buildMaxUs_ = 0;
    size_t buildsThisFrame_ = 0;
    size_t maximumBuildsPerFrame_ = SIZE_MAX;
    size_t slopeBuildsThisFrame_ = 0;
    size_t slopeBuildCount_ = 0;
    uint64_t terrainFrameStartUs_ = 0;
    size_t terrainTextureBudget_ = SIZE_MAX;
    uint64_t terrainGeneration_ = 0;
};

// Finds a file in dir, tolerating case differences (the GOG/CD installs mix
// upper and lower case names; the Vita filesystem doesn't care but Linux does).
std::string findFileNoCase(const std::string &dir, const std::string &name);

} // namespace swgb
