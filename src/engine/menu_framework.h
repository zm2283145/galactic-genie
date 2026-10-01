// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../render/renderer.h"

#include <cstddef>
#include <string>
#include <vector>

namespace swgb {

struct MenuPanelStyle {
    uint8_t backgroundR = 7;
    uint8_t backgroundG = 17;
    uint8_t backgroundB = 31;
    uint8_t accentR = 202;
    uint8_t accentG = 168;
    uint8_t accentB = 74;
};

void drawModernPanel(
    Renderer &renderer, float x, float y,
    float width, float height,
    const MenuPanelStyle &style = {});
void drawModernMenuRow(
    Renderer &renderer, const std::string &label,
    const std::string &value, float x, float y,
    float width, bool selected, bool disabled = false);
void drawModernTabs(
    Renderer &renderer,
    const std::vector<std::string> &labels,
    size_t selected, float x, float y, float width);
void drawModernTooltip(
    Renderer &renderer, const std::string &text,
    float x, float y, float width);
void drawModernStatusPill(
    Renderer &renderer, const std::string &text,
    float x, float y, bool warning = false);

} // namespace swgb
