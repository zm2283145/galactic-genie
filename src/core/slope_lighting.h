// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

namespace swgb {

enum SlopeNeighbor : uint8_t {
    SLOPE_NEIGHBOR_W,
    SLOPE_NEIGHBOR_N,
    SLOPE_NEIGHBOR_E,
    SLOPE_NEIGHBOR_S,
    SLOPE_NEIGHBOR_NW,
    SLOPE_NEIGHBOR_NE,
    SLOPE_NEIGHBOR_SE,
    SLOPE_NEIGHBOR_SW,
};

struct SlopeLighting {
    std::array<uint8_t, 16> patterns{};
    uint8_t count = 0;
};

SlopeLighting selectSlopeLighting(uint8_t slope, const std::array<int8_t, 8> &neighbors);

} // namespace swgb
