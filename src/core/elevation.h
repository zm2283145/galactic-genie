// SPDX-License-Identifier: GPL-3.0-or-later
// Readers for the terrain slope templates and high-quality texture maps.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace swgb {

constexpr size_t kSlopeCount = 17;

struct SlopeTemplate {
    int width = 0, height = 0;
    int hotspotX = 0, hotspotY = 0;
    std::vector<uint16_t> leftEdges;
    std::vector<uint16_t> rightEdges;
};

struct FilterSource {
    uint16_t alpha = 0;
    uint32_t sourceOffset = 0;
};

struct FilterPixel {
    uint16_t lightIndex = 0;
    std::vector<FilterSource> sources;
};

struct FilterLine {
    std::vector<FilterPixel> pixels;
};

struct FilterMap {
    std::vector<FilterLine> lines;
};

class ElevationMaps {
public:
    bool load(const std::string &templatePath, const std::string &filterPath,
              const std::string &icmPath, const std::string &lightPath,
              const std::string &patternPath, std::string *err);

    const SlopeTemplate &slopeTemplate(size_t slope) const { return templates_[slope]; }
    const FilterMap &filterMap(size_t slope) const { return filters_[slope]; }
    uint8_t colorIndex(size_t map, uint8_t r, uint8_t g, uint8_t b) const;
    uint8_t lightIndex(uint16_t textureIndex, const uint8_t *patterns, size_t patternCount) const;

private:
    bool loadTemplates(const std::string &path, std::string *err);
    bool loadFilters(const std::string &path, std::string *err);
    bool loadIcm(const std::string &path, std::string *err);
    bool loadTextureMaps(const std::string &path, size_t count,
                         std::vector<std::array<uint8_t, 4096>> &maps, std::string *err);

    std::array<SlopeTemplate, kSlopeCount> templates_;
    std::array<FilterMap, kSlopeCount> filters_;
    std::vector<uint8_t> icm_;
    std::vector<std::array<uint8_t, 4096>> lightMaps_;
    std::vector<std::array<uint8_t, 4096>> patternMasks_;
};

} // namespace swgb
