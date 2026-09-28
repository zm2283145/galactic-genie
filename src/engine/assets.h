// SPDX-License-Identifier: GPL-3.0-or-later
// Asset manager: owns the DRS archives, palette and dat, and turns SLPs into
// GPU texture atlases on demand.
#pragma once

#include "../core/drs.h"
#include "../core/genie_dat.h"
#include "../core/palette.h"
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
};

struct SpriteSheet {
    std::vector<SpriteFrame> frames;
    std::vector<Texture *> pages;
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

    // Returns the atlas for an SLP tinted for one player (playerColorBase
    // from the dat's player colour table). Null if the SLP doesn't exist.
    const SpriteSheet *sheet(int32_t slpId, int playerColorBase = 16);
    const SpriteSheet *terrainSheet(int32_t slpId);

    size_t textureBytes() const { return textureBytes_; }
    size_t sheetCount() const { return sheets_.size(); }

private:
    const SpriteSheet *build(ResourceSet &set, int32_t slpId, int playerColorBase, uint64_t key);
    void log(const std::string &s) const { if (log_) log_(s); }

    Renderer *renderer_;
    LogFn log_;
    ResourceSet graphics_, terrain_, interfac_;
    Palette palette_;
    dat::DatFile dat_;
    std::map<uint64_t, std::unique_ptr<SpriteSheet>> sheets_;
    size_t textureBytes_ = 0;
};

// Finds a file in dir, tolerating case differences (the GOG/CD installs mix
// upper and lower case names; the Vita filesystem doesn't care but Linux does).
std::string findFileNoCase(const std::string &dir, const std::string &name);

} // namespace swgb
