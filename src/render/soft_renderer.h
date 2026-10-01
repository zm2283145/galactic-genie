// SPDX-License-Identifier: GPL-3.0-or-later
// CPU renderer: used by the PC tools to render scenes to PNG without a GPU.
#pragma once

#include "renderer.h"

#include <string>
#include <vector>

namespace swgb {

class SoftRenderer : public Renderer {
public:
    Texture *createTexture(int width, int height, const uint8_t *rgba) override;
    Texture *createMaskTexture(int width, int height, const uint8_t *alpha) override;
    bool updateTexture(Texture *t, const uint8_t *rgba) override;
    void destroyTexture(Texture *t) override;
    void beginFrame(int screenW, int screenH, float scale, uint8_t r, uint8_t g, uint8_t b) override;
    void draw(Texture *tex, const Quad &q) override;
    void drawMasked(Texture *tex, const Quad &q, Texture *mask, const Quad &maskQ) override;
    void drawMaskedTinted(Texture *tex, const Quad &q, Texture *mask,
                          const Quad &maskQ, uint8_t r, uint8_t g,
                          uint8_t b, uint8_t a) override;
    void drawTinted(Texture *tex, const Quad &q, uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
    void drawLine(float x0, float y0, float x1, float y1, float thickness,
                  uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
    void fillRect(float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
    void endFrame() override {}

    bool savePng(const std::string &path) const;
    const std::vector<uint8_t> &pixels() const { return fb_; }
    int drawCalls() const { return drawCalls_; }

private:
    int w_ = 0, h_ = 0;
    float scale_ = 1.0f;
    std::vector<uint8_t> fb_;
    int drawCalls_ = 0;
};

bool writePng(const std::string &path, int w, int h, const uint8_t *rgba);

} // namespace swgb
