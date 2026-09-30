// SPDX-License-Identifier: GPL-3.0-or-later
#include "gl_renderer.h"

#if defined(__vita__)
#include <vitaGL.h>
#else
#include <GL/gl.h>
#endif

namespace swgb {

namespace {
struct GlTexture : Texture {
    GLuint id = 0;
    float invW = 1, invH = 1;
};
} // namespace

GlRenderer::GlRenderer() {
    verts_.reserve(6 * 4096);
    const uint8_t white[4] = {255, 255, 255, 255};
    white_ = createTexture(1, 1, white);
}

GlRenderer::~GlRenderer() { destroyTexture(white_); }

Texture *GlRenderer::createTexture(int width, int height, const uint8_t *rgba) {
    while (glGetError() != GL_NO_ERROR) {}
    auto *t = new GlTexture();
    t->width = width;
    t->height = height;
    t->invW = 1.0f / width;
    t->invH = 1.0f / height;
    glGenTextures(1, &t->id);
    glBindTexture(GL_TEXTURE_2D, t->id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (!t->id || glGetError() != GL_NO_ERROR) {
        if (t->id) glDeleteTextures(1, &t->id);
        delete t;
        return nullptr;
    }
    current_ = nullptr; // binding changed
    return t;
}

Texture *GlRenderer::createMaskTexture(int width, int height, const uint8_t *alpha) {
    while (glGetError() != GL_NO_ERROR) {}
    auto *t = new GlTexture();
    t->width = width;
    t->height = height;
    t->alphaOnly = true;
    t->invW = 1.0f / width;
    t->invH = 1.0f / height;
    glGenTextures(1, &t->id);
    glBindTexture(GL_TEXTURE_2D, t->id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, width, height, 0, GL_ALPHA, GL_UNSIGNED_BYTE, alpha);
    if (!t->id || glGetError() != GL_NO_ERROR) {
        if (t->id) glDeleteTextures(1, &t->id);
        delete t;
        return nullptr;
    }
    current_ = currentMask_ = nullptr;
    return t;
}

void GlRenderer::destroyTexture(Texture *t) {
    if (!t) return;
    auto *gt = static_cast<GlTexture *>(t);
    glDeleteTextures(1, &gt->id);
    delete gt;
}

void GlRenderer::beginFrame(int screenW, int screenH, float scale, uint8_t r, uint8_t g, uint8_t b) {
    drawCalls_ = quads_ = 0;
    glViewport(0, 0, screenW, screenH);
    glClearColor(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, screenW / scale, screenH / scale, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glActiveTexture(GL_TEXTURE1);
    glDisable(GL_TEXTURE_2D);
    glClientActiveTexture(GL_TEXTURE1);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glActiveTexture(GL_TEXTURE0);
    glClientActiveTexture(GL_TEXTURE0);
    current_ = currentMask_ = nullptr;
    verts_.clear();
}

void GlRenderer::push(Texture *tex, Texture *mask, float x0, float y0, float x1, float y1, float u0, float v0,
                      float u1, float v1, float mu0, float mv0, float mu1, float mv1, uint8_t r, uint8_t g,
                      uint8_t b, uint8_t a) {
    if (tex != current_ || mask != currentMask_) {
        flush();
        current_ = tex;
        currentMask_ = mask;
    }
    const Vertex tl{x0, y0, u0, v0, mu0, mv0, r, g, b, a};
    const Vertex tr{x1, y0, u1, v0, mu1, mv0, r, g, b, a};
    const Vertex bl{x0, y1, u0, v1, mu0, mv1, r, g, b, a};
    const Vertex br{x1, y1, u1, v1, mu1, mv1, r, g, b, a};
    verts_.push_back(tl); verts_.push_back(tr); verts_.push_back(bl);
    verts_.push_back(tr); verts_.push_back(br); verts_.push_back(bl);
    quads_++;
}

void GlRenderer::draw(Texture *tex, const Quad &q) {
    auto *gt = static_cast<GlTexture *>(tex);
    push(tex, nullptr, q.x, q.y, q.x + q.w, q.y + q.h, q.u0 * gt->invW, q.v0 * gt->invH, q.u1 * gt->invW,
         q.v1 * gt->invH, 0, 0, 0, 0, 255, 255, 255, 255);
}

void GlRenderer::drawMasked(Texture *tex, const Quad &q, Texture *mask, const Quad &maskQ) {
    auto *gt = static_cast<GlTexture *>(tex);
    auto *gm = static_cast<GlTexture *>(mask);
    push(tex, mask, q.x, q.y, q.x + q.w, q.y + q.h, q.u0 * gt->invW, q.v0 * gt->invH, q.u1 * gt->invW,
         q.v1 * gt->invH, maskQ.u0 * gm->invW, maskQ.v0 * gm->invH, maskQ.u1 * gm->invW,
         maskQ.v1 * gm->invH, 255, 255, 255, 255);
}

void GlRenderer::drawMaskedTinted(Texture *tex, const Quad &q, Texture *mask,
                                  const Quad &maskQ, uint8_t r, uint8_t g,
                                  uint8_t b, uint8_t a) {
    auto *gt = static_cast<GlTexture *>(tex);
    auto *gm = static_cast<GlTexture *>(mask);
    push(tex, mask, q.x, q.y, q.x + q.w, q.y + q.h,
         q.u0 * gt->invW, q.v0 * gt->invH,
         q.u1 * gt->invW, q.v1 * gt->invH,
         maskQ.u0 * gm->invW, maskQ.v0 * gm->invH,
         maskQ.u1 * gm->invW, maskQ.v1 * gm->invH,
         r, g, b, a);
}

void GlRenderer::fillRect(float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    push(white_, nullptr, x, y, x + w, y + h, 0, 0, 1, 1, 0, 0, 0, 0, r, g, b, a);
}

void GlRenderer::flush() {
    if (verts_.empty() || !current_) {
        verts_.clear();
        return;
    }
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, static_cast<GlTexture *>(current_)->id);
    glClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), &verts_[0].u);
    glActiveTexture(GL_TEXTURE1);
    glClientActiveTexture(GL_TEXTURE1);
    if (currentMask_) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, static_cast<GlTexture *>(currentMask_)->id);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), &verts_[0].mu);
    } else {
        glDisable(GL_TEXTURE_2D);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }
    glActiveTexture(GL_TEXTURE0);
    glClientActiveTexture(GL_TEXTURE0);
    glVertexPointer(2, GL_FLOAT, sizeof(Vertex), &verts_[0].x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), &verts_[0].r);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts_.size());
    drawCalls_++;
    verts_.clear();
}

void GlRenderer::endFrame() {
    flush();
    current_ = currentMask_ = nullptr;
}

} // namespace swgb
