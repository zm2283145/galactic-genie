// SPDX-License-Identifier: GPL-3.0-or-later
// Minimal 2D renderer interface. The engine only ever draws textured,
// alpha-blended quads in screen space, which maps well to both vitaGL and a
// CPU rasterizer (used for headless tests on PC).
#pragma once

#include <cstdint>

namespace swgb {

struct Texture {
    int width = 0, height = 0;
    virtual ~Texture() = default;
};

struct Quad {
    float x, y, w, h;      // destination rectangle in screen pixels
    float u0, v0, u1, v1;  // source rectangle in texels (u1 < u0 means flipped)
};

class Renderer {
public:
    virtual ~Renderer() = default;

    // rgba: width*height*4 bytes, tightly packed, straight alpha.
    virtual Texture *createTexture(int width, int height, const uint8_t *rgba) = 0;
    // alpha: width*height bytes. Sampling yields white RGB and the stored alpha.
    virtual Texture *createMaskTexture(int width, int height, const uint8_t *alpha) = 0;
    virtual void destroyTexture(Texture *t) = 0;

    virtual void beginFrame(int screenW, int screenH, float scale, uint8_t r, uint8_t g, uint8_t b) = 0;
    virtual void draw(Texture *tex, const Quad &q) = 0;
    virtual void drawMasked(Texture *tex, const Quad &q, Texture *mask, const Quad &maskQ) = 0;
    virtual void fillRect(float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) = 0;
    virtual void endFrame() = 0;
};

} // namespace swgb
