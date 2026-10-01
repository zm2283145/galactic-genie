// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_text.h"
#include "font_atlas.h"

#include <algorithm>
#include <cmath>

namespace swgb {
namespace {

struct FontTextures {
    Renderer *owner = nullptr;
    Texture *faces[font::faceCount] = {};
};

FontTextures g_fonts;

const font::Face &pickFace(float pixels, int &index) {
    pixels = std::max(11.0f, pixels * 1.12f);
    index = 0;
    float best = 1e9f;
    for (int i = 0; i < font::faceCount; ++i) {
        const float distance =
            std::abs(font::faces[i].lineHeight - pixels);
        if (distance < best) {
            best = distance;
            index = i;
        }
    }
    return font::faces[index];
}

Texture *fontTexture(Renderer &renderer, int index) {
    if (g_fonts.owner != &renderer) {
        g_fonts = FontTextures{};
        g_fonts.owner = &renderer;
    }
    if (g_fonts.faces[index])
        return g_fonts.faces[index];
    const font::Face &face = font::faces[index];
    std::vector<uint8_t> rgba(
        (size_t)face.atlasW * face.atlasH * 4, 255);
    for (size_t i = 0;
         i < (size_t)face.atlasW * face.atlasH; ++i)
        rgba[i * 4 + 3] = face.alpha[i];
    g_fonts.faces[index] = renderer.createTexture(
        face.atlasW, face.atlasH, rgba.data());
    return g_fonts.faces[index];
}

} // namespace

float uiTextWidth(const std::string &text, float scale) {
    int index = 0;
    const font::Face &face =
        pickFace(9.0f * scale, index);
    float width = 0.0f;
    for (char character : text) {
        const int code = (unsigned char)character;
        if (code >= 32 && code <= 126)
            width += face.glyphs[code - 32].advance;
    }
    return width;
}

void drawUiText(
    Renderer &renderer,
    const std::vector<std::string> &lines,
    float x, float y, float scale,
    uint8_t red, uint8_t green, uint8_t blue) {
    int index = 0;
    const font::Face &face =
        pickFace(9.0f * scale, index);
    Texture *atlas = fontTexture(renderer, index);
    if (!atlas) return;
    const float lineStep = std::max(
        9.0f * scale, (float)face.lineHeight);
    const float top =
        y + 7.0f * scale - face.ascent * 0.72f;
    auto drawPass = [&](float offset,
                        uint8_t r, uint8_t g, uint8_t b) {
        for (size_t line = 0; line < lines.size();
             ++line) {
            float penX = std::round(x + offset);
            const float baseY = std::round(
                top + offset + line * lineStep);
            for (char character : lines[line]) {
                const int code = (unsigned char)character;
                if (code < 32 || code > 126) continue;
                const font::Glyph &glyph =
                    face.glyphs[code - 32];
                if (glyph.w && glyph.h) {
                    renderer.drawTinted(
                        atlas,
                        {penX + glyph.xoff,
                         baseY + glyph.yoff,
                         (float)glyph.w,
                         (float)glyph.h,
                         (float)glyph.x,
                         (float)glyph.y,
                         (float)(glyph.x + glyph.w),
                         (float)(glyph.y + glyph.h)},
                        r, g, b, 255);
                }
                penX += glyph.advance;
            }
        }
    };
    drawPass(1.0f, 0, 0, 0);
    drawPass(0.0f, red, green, blue);
}

} // namespace swgb
