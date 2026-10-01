// SPDX-License-Identifier: GPL-3.0-or-later
#include "menu_framework.h"

#include "ui_text.h"

#include <algorithm>

namespace swgb {

void drawModernPanel(
    Renderer &renderer, float x, float y,
    float width, float height,
    const MenuPanelStyle &style) {
    renderer.fillRect(
        x + 5, y + 7, width, height, 0, 0, 0, 120);
    renderer.fillRect(
        x, y, width, height, style.backgroundR,
        style.backgroundG, style.backgroundB, 246);
    renderer.fillRect(
        x, y, width, 3, style.accentR,
        style.accentG, style.accentB, 255);
    renderer.fillRect(
        x, y + height - 2, width, 2, 72, 102, 132, 255);
    renderer.fillRect(
        x, y, 2, height, 72, 102, 132, 255);
    renderer.fillRect(
        x + width - 2, y, 2, height, 72, 102, 132, 255);
}

void drawModernMenuRow(
    Renderer &renderer, const std::string &label,
    const std::string &value, float x, float y,
    float width, bool selected, bool disabled) {
    if (selected) {
        renderer.fillRect(
            x, y - 6, width, 29, 30, 72, 102, 255);
        renderer.fillRect(
            x, y - 6, 4, 29, 236, 190, 79, 255);
    }
    const uint8_t base =
        disabled ? 105 : selected ? 255 : 196;
    drawUiText(
        renderer, {label}, x + 12, y, 0.88f,
        base, disabled ? 116 : selected ? 231 : 211,
        disabled ? 128 : selected ? 159 : 225);
    if (!value.empty()) {
        const float valueWidth =
            uiTextWidth(value, 0.82f);
        drawUiText(
            renderer, {value},
            std::max(x + width * 0.45f,
                     x + width - valueWidth - 10),
            y, 0.82f,
            disabled ? 105 : 213,
            disabled ? 116 : 222,
            disabled ? 128 : 229);
    }
}

void drawModernTabs(
    Renderer &renderer,
    const std::vector<std::string> &labels,
    size_t selected, float x, float y, float width) {
    if (labels.empty()) return;
    const float tabWidth = width / labels.size();
    for (size_t i = 0; i < labels.size(); ++i) {
        const bool active = i == selected;
        renderer.fillRect(
            x + tabWidth * i, y, tabWidth - 2, 30,
            active ? 30 : 12, active ? 72 : 30,
            active ? 102 : 48, 255);
        if (active)
            renderer.fillRect(
                x + tabWidth * i, y + 28,
                tabWidth - 2, 2, 236, 190, 79, 255);
        drawUiText(
            renderer, {labels[i]},
            x + tabWidth * i + 8, y + 8, 0.65f,
            active ? 255 : 163,
            active ? 231 : 190,
            active ? 159 : 208);
    }
}

void drawModernTooltip(
    Renderer &renderer, const std::string &text,
    float x, float y, float width) {
    renderer.fillRect(x, y, width, 24, 4, 12, 22, 244);
    renderer.fillRect(x, y, width, 1, 72, 102, 132, 255);
    drawUiText(
        renderer, {text}, x + 8, y + 7, 0.62f,
        151, 177, 199);
}

void drawModernStatusPill(
    Renderer &renderer, const std::string &text,
    float x, float y, bool warning) {
    const float width =
        uiTextWidth(text, 0.62f) + 18.0f;
    renderer.fillRect(
        x, y, width, 21,
        warning ? 91 : 21,
        warning ? 52 : 64,
        warning ? 38 : 72, 255);
    drawUiText(
        renderer, {text}, x + 9, y + 6, 0.62f,
        warning ? 255 : 190,
        warning ? 205 : 225,
        warning ? 146 : 203);
}

} // namespace swgb
