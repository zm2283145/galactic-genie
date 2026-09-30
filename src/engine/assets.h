// SPDX-License-Identifier: GPL-3.0-or-later
// Asset manager: owns the DRS archives, palette and dat, and turns SLPs into
// GPU texture atlases on demand.
#pragma once

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
    explicit Assets(Renderer *r) : renderer_(r) {}
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
    const SpriteSheet *terrainSheet(int32_t slpId);
    const SpriteFrame *terrainSlopeFrame(int32_t slpId, int slope, size_t frame,
                                         const std::array<int8_t, 8> &neighbors);
    const SpriteFrame *blendMask(int mode, int mask, int slope = 0);
    bool readSound(int soundId, int civilization, uint32_t choice,
                   std::vector<uint8_t> &data, int *resourceId = nullptr,
                   std::string *fileName = nullptr);
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

    size_t textureBytes() const { return textureBytes_; }
    size_t sheetCount() const { return sheets_.size() + slopeFrames_.size() + slopeBlendMasks_.size(); }

private:
    const SpriteSheet *build(ResourceSet &set, int32_t slpId, int playerColorBase, uint64_t key);
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
    std::unique_ptr<SpriteSheet> pack(const std::vector<SlpImage> &imgs, int playerColorBase);
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
    std::map<int32_t, std::vector<uint8_t>> soundDataCache_;
    Texture *blendMaskTexture_ = nullptr;
    Texture *selectionRingTexture_ = nullptr;
    bool selectionRingAttempted_ = false;
    std::vector<std::vector<SpriteFrame>> blendMasks_;
    size_t textureBytes_ = 0;
    size_t terrainTextureBudget_ = SIZE_MAX;
    uint64_t terrainGeneration_ = 0;
};

// Finds a file in dir, tolerating case differences (the GOG/CD installs mix
// upper and lower case names; the Vita filesystem doesn't care but Linux does).
std::string findFileNoCase(const std::string &dir, const std::string &name);

} // namespace swgb
