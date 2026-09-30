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

Texture *SoftRenderer::createMaskTexture(int width, int height, const uint8_t *alpha) {
    auto *t = new SoftTexture();
    t->width = width;
    t->height = height;
    t->alphaOnly = true;
    t->px.assign(alpha, alpha + (size_t)width * height);
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

void SoftRenderer::drawTinted(Texture *tex, const Quad &q, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    drawCalls_++;
    auto *t = static_cast<SoftTexture *>(tex);
    float x0 = q.x * scale_, y0 = q.y * scale_, x1 = (q.x + q.w) * scale_, y1 = (q.y + q.h) * scale_;
    int ix0 = std::max(0, (int)std::floor(x0)), iy0 = std::max(0, (int)std::floor(y0));
    int ix1 = std::min(w_, (int)std::ceil(x1)), iy1 = std::min(h_, (int)std::ceil(y1));
    if (ix0 >= ix1 || iy0 >= iy1) return;
    float du = (q.u1 - q.u0) / (x1 - x0), dv = (q.v1 - q.v0) / (y1 - y0);
    for (int y = iy0; y < iy1; y++) {
        int tv = (int)std::floor(q.v0 + (y + 0.5f - y0) * dv);
        if (tv < 0 || tv >= t->height) continue;
        uint8_t *drow = &fb_[((size_t)y * w_) * 4];
        const uint8_t *srow = &t->px[(size_t)tv * t->width * 4];
        for (int x = ix0; x < ix1; x++) {
            int tu = (int)std::floor(q.u0 + (x + 0.5f - x0) * du);
            if (tu < 0 || tu >= t->width) continue;
            const uint8_t *src = srow + tu * 4;
            const uint8_t tinted[4] = {(uint8_t)(src[0] * r / 255), (uint8_t)(src[1] * g / 255),
                                       (uint8_t)(src[2] * b / 255), (uint8_t)(src[3] * a / 255)};
            blend(drow + x * 4, tinted);
        }
    }
}

void SoftRenderer::drawMasked(Texture *tex, const Quad &q, Texture *mask, const Quad &maskQ) {
    drawCalls_++;
    auto *t = static_cast<SoftTexture *>(tex);
    auto *m = static_cast<SoftTexture *>(mask);
    float x0 = q.x * scale_, y0 = q.y * scale_, x1 = (q.x + q.w) * scale_, y1 = (q.y + q.h) * scale_;
    int ix0 = std::max(0, (int)std::floor(x0)), iy0 = std::max(0, (int)std::floor(y0));
    int ix1 = std::min(w_, (int)std::ceil(x1)), iy1 = std::min(h_, (int)std::ceil(y1));
    if (ix0 >= ix1 || iy0 >= iy1) return;
    float du = (q.u1 - q.u0) / (x1 - x0), dv = (q.v1 - q.v0) / (y1 - y0);
    float dmu = (maskQ.u1 - maskQ.u0) / (x1 - x0), dmv = (maskQ.v1 - maskQ.v0) / (y1 - y0);
    for (int y = iy0; y < iy1; y++) {
        int tv = (int)std::floor(q.v0 + (y + 0.5f - y0) * dv);
        int mv = (int)std::floor(maskQ.v0 + (y + 0.5f - y0) * dmv);
        if (tv < 0 || tv >= t->height || mv < 0 || mv >= m->height) continue;
        uint8_t *drow = &fb_[((size_t)y * w_) * 4];
        const uint8_t *srow = &t->px[(size_t)tv * t->width * 4];
        const uint8_t *mrow = &m->px[(size_t)mv * m->width *
                                     (m->alphaOnly ? 1 : 4)];
        for (int x = ix0; x < ix1; x++) {
            int tu = (int)std::floor(q.u0 + (x + 0.5f - x0) * du);
            int mu = (int)std::floor(maskQ.u0 + (x + 0.5f - x0) * dmu);
            if (tu < 0 || tu >= t->width || mu < 0 || mu >= m->width) continue;
            const uint8_t *source = srow + tu * 4;
            const uint8_t maskAlpha =
                mrow[mu * (m->alphaOnly ? 1 : 4) +
                     (m->alphaOnly ? 0 : 3)];
            uint8_t masked[4] = {
                source[0], source[1], source[2],
                (uint8_t)((unsigned)source[3] * maskAlpha / 255)};
            blend(drow + x * 4, masked);
        }
    }
}

void SoftRenderer::drawMaskedTinted(Texture *tex, const Quad &q,
                                    Texture *mask, const Quad &maskQ,
                                    uint8_t red, uint8_t green,
                                    uint8_t blue, uint8_t alpha) {
    drawCalls_++;
    auto *t = static_cast<SoftTexture *>(tex);
    auto *m = static_cast<SoftTexture *>(mask);
    const float x0 = q.x * scale_, y0 = q.y * scale_;
    const float x1 = (q.x + q.w) * scale_;
    const float y1 = (q.y + q.h) * scale_;
    const int ix0 = std::max(0, (int)std::floor(x0));
    const int iy0 = std::max(0, (int)std::floor(y0));
    const int ix1 = std::min(w_, (int)std::ceil(x1));
    const int iy1 = std::min(h_, (int)std::ceil(y1));
    if (ix0 >= ix1 || iy0 >= iy1) return;
    const float du = (q.u1 - q.u0) / (x1 - x0);
    const float dv = (q.v1 - q.v0) / (y1 - y0);
    const float dmu = (maskQ.u1 - maskQ.u0) / (x1 - x0);
    const float dmv = (maskQ.v1 - maskQ.v0) / (y1 - y0);
    for (int y = iy0; y < iy1; y++) {
        const int tv = (int)std::floor(
            q.v0 + (y + 0.5f - y0) * dv);
        const int mv = (int)std::floor(
            maskQ.v0 + (y + 0.5f - y0) * dmv);
        if (tv < 0 || tv >= t->height ||
            mv < 0 || mv >= m->height)
            continue;
        uint8_t *destination =
            &fb_[((size_t)y * w_) * 4];
        const uint8_t *source =
            &t->px[(size_t)tv * t->width *
                   (t->alphaOnly ? 1 : 4)];
        const uint8_t *maskRow =
            &m->px[(size_t)mv * m->width *
                   (m->alphaOnly ? 1 : 4)];
        for (int x = ix0; x < ix1; x++) {
            const int tu = (int)std::floor(
                q.u0 + (x + 0.5f - x0) * du);
            const int mu = (int)std::floor(
                maskQ.u0 + (x + 0.5f - x0) * dmu);
            if (tu < 0 || tu >= t->width ||
                mu < 0 || mu >= m->width)
                continue;
            const uint8_t sourceAlpha =
                source[tu * (t->alphaOnly ? 1 : 4) +
                       (t->alphaOnly ? 0 : 3)];
            const uint8_t maskAlpha =
                maskRow[mu * (m->alphaOnly ? 1 : 4) +
                        (m->alphaOnly ? 0 : 3)];
            uint8_t tinted[4] = {
                red, green, blue,
                (uint8_t)((unsigned)sourceAlpha *
                          maskAlpha * alpha / (255u * 255u))};
            blend(destination + x * 4, tinted);
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
