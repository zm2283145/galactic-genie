// SPDX-License-Identifier: GPL-3.0-or-later
// JASC-PAL palette loader (interfac.drs bina 50500+).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace swgb {

struct Rgba {
    uint8_t r, g, b, a;
};

struct Palette {
    std::array<Rgba, 256> colors{};
    bool parse(const std::vector<uint8_t> &data, std::string *err = nullptr);
    const Rgba &operator[](uint8_t i) const { return colors[i]; }
};

} // namespace swgb
