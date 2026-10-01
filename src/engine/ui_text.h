// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../render/renderer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace swgb {

float uiTextWidth(const std::string &text, float scale);
void drawUiText(
    Renderer &renderer,
    const std::vector<std::string> &lines,
    float x, float y, float scale,
    uint8_t red = 255,
    uint8_t green = 255,
    uint8_t blue = 255);

} // namespace swgb
