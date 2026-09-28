// SPDX-License-Identifier: GPL-3.0-or-later
#include "soft_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../third_party/stb_image_write.h"

namespace swgb {

namespace {
struct SoftTexture : Texture {
    std::vector<uint8_t> px;
};
} // namespace

bool writePng(const std::string &path, int w, int h, const uint8_t *rgba) {
    return stbi_write_png(path.c_str(), w, h, 4, rgba, w * 4) != 0;
}

Texture *SoftRenderer::createTexture(int width, int height, const uint8_t *rgba) {
    auto *t = new SoftTexture();
    t->width = width;
    t->height = height;
    t->px.assign(rgba, rgba + (size_t)width * height * 4);
    return t;
}

void SoftRenderer::destroyTexture(Texture *t) { delete static_cast<SoftTexture *>(t); }

void SoftRenderer::beginFrame(int screenW, int screenH, float scale, uint8_t r, uint8_t g, uint8_t b) {
    w_ = screenW;
    h_ = screenH;
    scale_ = scale;
    drawCalls_ = 0;
    fb_.resize((size_t)w_ * h_ * 4);
    for (size_t i = 0; i < fb_.size(); i += 4) {
        fb_[i] = r; fb_[i + 1] = g; fb_[i + 2] = b; fb_[i + 3] = 255;
    }
}

static inline void blend(uint8_t *d, const uint8_t *s) {
    unsigned a = s[3];
    if (a == 0) return;
    if (a == 255) {
        d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
        return;
    }
    unsigned ia = 255 - a;
    d[0] = (uint8_t)((s[0] * a + d[0] * ia) / 255);
    d[1] = (uint8_t)((s[1] * a + d[1] * ia) / 255);
    d[2] = (uint8_t)((s[2] * a + d[2] * ia) / 255);
}

void SoftRenderer::draw(Texture *tex, const Quad &q) {
    drawCalls_++;
    auto *t = static_cast<SoftTexture *>(tex);
    float x0 = q.x * scale_, y0 = q.y * scale_, x1 = (q.x + q.w) * scale_, y1 = (q.y + q.h) * scale_;
    int ix0 = std::max(0, (int)std::floor(x0)), iy0 = std::max(0, (int)std::floor(y0));
    int ix1 = std::min(w_, (int)std::ceil(x1)), iy1 = std::min(h_, (int)std::ceil(y1));
    if (ix0 >= ix1 || iy0 >= iy1) return;
    float du = (q.u1 - q.u0) / (x1 - x0), dv = (q.v1 - q.v0) / (y1 - y0);
    for (int y = iy0; y < iy1; y++) {
        float v = q.v0 + (y + 0.5f - y0) * dv;
        int tv = (int)std::floor(v);
        if (tv < 0 || tv >= t->height) continue;
        uint8_t *drow = &fb_[((size_t)y * w_) * 4];
        const uint8_t *srow = &t->px[(size_t)tv * t->width * 4];
        for (int x = ix0; x < ix1; x++) {
            float u = q.u0 + (x + 0.5f - x0) * du;
            int tu = (int)std::floor(u);
            if (tu < 0 || tu >= t->width) continue;
            blend(drow + x * 4, srow + tu * 4);
        }
    }
}

void SoftRenderer::fillRect(float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint8_t s[4] = {r, g, b, a};
    int ix0 = std::max(0, (int)(x * scale_)), iy0 = std::max(0, (int)(y * scale_));
    int ix1 = std::min(w_, (int)((x + w) * scale_)), iy1 = std::min(h_, (int)((y + h) * scale_));
    for (int yy = iy0; yy < iy1; yy++)
        for (int xx = ix0; xx < ix1; xx++) blend(&fb_[((size_t)yy * w_ + xx) * 4], s);
}

bool SoftRenderer::savePng(const std::string &path) const { return writePng(path, w_, h_, fb_.data()); }

} // namespace swgb
