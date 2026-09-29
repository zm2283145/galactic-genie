// SPDX-License-Identifier: GPL-3.0-or-later
// SLP 2.0N sprite decoder (AoE1/AoK/SWGB).
#pragma once

#include "palette.h"

#include <cstdint>
#include <string>
#include <vector>

namespace swgb {

// Pixel classes produced by the decoder. Colorization happens later so the
// same decoded frame can be tinted for any player.
enum SlpPixel : uint8_t {
    PX_TRANSPARENT = 0,
    PX_COLOR = 1,      // index = palette index
    PX_PLAYER = 2,     // index = player color offset (0..7), add player base
    PX_SHADOW = 3,     // darken what's underneath
    PX_OUTLINE = 4,    // player-color outline, drawn only when occluded
    PX_SHIELD = 5,     // SWGB shield-hit outline (palette 243)
};

struct SlpFrameInfo {
    uint32_t cmdTableOffset;
    uint32_t outlineTableOffset;
    int32_t width, height;
    int32_t hotspotX, hotspotY;
};

struct SlpImage {
    int32_t width = 0, height = 0;
    int32_t hotspotX = 0, hotspotY = 0;
    std::vector<uint8_t> kind;  // SlpPixel per pixel
    std::vector<uint8_t> index; // palette index / player offset
};

class Slp {
public:
    bool parse(std::vector<uint8_t> data, std::string *err = nullptr);
    size_t frameCount() const { return frames_.size(); }
    const SlpFrameInfo &frame(size_t i) const { return frames_[i]; }
    bool decode(size_t frame, SlpImage &out, std::string *err = nullptr,
                std::vector<uint8_t> *commandPalette = nullptr) const;

private:
    std::vector<uint8_t> data_;
    std::vector<SlpFrameInfo> frames_;
};

struct ColorizeOptions {
    int playerColorBase = 16; // first palette index of this player's 8 shades
    uint8_t shadowAlpha = 64;
    bool flipX = false;
};

// Converts a decoded frame to RGBA8888. out must hold width*height*4 bytes
// with the given row pitch (in pixels).
void colorize(const SlpImage &img, const Palette &pal, const ColorizeOptions &opt, uint8_t *out,
              int pitchPixels);

} // namespace swgb
