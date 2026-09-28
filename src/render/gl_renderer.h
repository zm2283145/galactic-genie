// SPDX-License-Identifier: GPL-3.0-or-later
// Fixed-function OpenGL renderer (vitaGL on Vita, legacy GL on desktop).
// Quads are batched per texture into one client-side vertex array.
#pragma once

#include "renderer.h"

#include <vector>

namespace swgb {

class GlRenderer : public Renderer {
public:
    GlRenderer();
    ~GlRenderer() override;

    Texture *createTexture(int width, int height, const uint8_t *rgba) override;
    void destroyTexture(Texture *t) override;
    void beginFrame(int screenW, int screenH, float scale, uint8_t r, uint8_t g, uint8_t b) override;
    void draw(Texture *tex, const Quad &q) override;
    void fillRect(float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
    void endFrame() override;

    int drawCalls() const { return drawCalls_; }
    int quads() const { return quads_; }

private:
    struct Vertex {
        float x, y;
        float u, v;
        uint8_t r, g, b, a;
    };
    void flush();
    void push(Texture *tex, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
              uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    std::vector<Vertex> verts_;
    Texture *current_ = nullptr;
    Texture *white_ = nullptr;
    int drawCalls_ = 0, quads_ = 0;
};

} // namespace swgb
