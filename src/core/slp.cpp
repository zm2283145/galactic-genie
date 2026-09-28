// SPDX-License-Identifier: GPL-3.0-or-later
#include "slp.h"

#include "bytes.h"

#include <cstring>

namespace swgb {

bool Slp::parse(std::vector<uint8_t> data, std::string *err) {
    data_ = std::move(data);
    frames_.clear();
    try {
        ByteReader r(data_);
        char ver[4];
        memcpy(ver, r.ptr(4), 4);
        if (ver[0] != '2' && ver[0] != '3') {
            if (err) *err = "unsupported SLP version";
            return false;
        }
        int32_t n = r.i32();
        r.skip(24); // comment
        if (n < 0 || n > 10000) {
            if (err) *err = "bad SLP frame count";
            return false;
        }
        frames_.resize(n);
        for (int32_t i = 0; i < n; i++) {
            SlpFrameInfo &f = frames_[i];
            f.cmdTableOffset = r.u32();
            f.outlineTableOffset = r.u32();
            r.u32(); // palette offset (unused)
            r.u32(); // properties (unused in SWGB)
            f.width = r.i32();
            f.height = r.i32();
            f.hotspotX = r.i32();
            f.hotspotY = r.i32();
            if (f.width < 0 || f.height < 0 || f.width > 4096 || f.height > 4096) {
                if (err) *err = "bad SLP frame size";
                return false;
            }
        }
    } catch (const FormatError &e) {
        if (err) *err = e.what();
        return false;
    }
    return true;
}

bool Slp::decode(size_t fi, SlpImage &out, std::string *err) const {
    if (fi >= frames_.size()) {
        if (err) *err = "frame out of range";
        return false;
    }
    const SlpFrameInfo &f = frames_[fi];
    out.width = f.width;
    out.height = f.height;
    out.hotspotX = f.hotspotX;
    out.hotspotY = f.hotspotY;
    const size_t npx = (size_t)f.width * f.height;
    out.kind.assign(npx, PX_TRANSPARENT);
    out.index.assign(npx, 0);

    try {
        ByteReader edges(data_);
        edges.seek(f.outlineTableOffset);
        ByteReader cmds(data_);
        ByteReader rowOffsets(data_);
        rowOffsets.seek(f.cmdTableOffset);

        for (int32_t y = 0; y < f.height; y++) {
            uint16_t left = edges.u16();
            uint16_t right = edges.u16();
            uint32_t rowOff = rowOffsets.u32();
            if (left == 0x8000 || right == 0x8000) continue; // fully transparent row
            cmds.seek(rowOff);

            uint8_t *kind = &out.kind[(size_t)y * f.width];
            uint8_t *idx = &out.index[(size_t)y * f.width];
            int32_t x = left;
            auto put = [&](uint8_t k, uint8_t v) {
                if (x >= 0 && x < f.width) {
                    kind[x] = k;
                    idx[x] = v;
                }
                x++;
            };
            auto countOrNext = [&](uint8_t cmd, int shift) -> uint32_t {
                uint32_t c = cmd >> shift;
                return c ? c : cmds.u8();
            };

            for (;;) {
                uint8_t cmd = cmds.u8();
                if (cmd == 0x0F) break; // end of row
                switch (cmd & 0x03) {
                case 0x00: { // lesser draw
                    uint32_t c = cmd >> 2;
                    for (uint32_t i = 0; i < c; i++) put(PX_COLOR, cmds.u8());
                    continue;
                }
                case 0x01: { // lesser skip
                    x += (int32_t)countOrNext(cmd, 2);
                    continue;
                }
                default: break;
                }
                switch (cmd & 0x0F) {
                case 0x02: { // greater draw
                    uint32_t c = ((cmd & 0xF0u) << 4) + cmds.u8();
                    for (uint32_t i = 0; i < c; i++) put(PX_COLOR, cmds.u8());
                    break;
                }
                case 0x03: { // greater skip
                    x += (int32_t)(((cmd & 0xF0u) << 4) + cmds.u8());
                    break;
                }
                case 0x06: { // player color draw
                    uint32_t c = countOrNext(cmd, 4);
                    for (uint32_t i = 0; i < c; i++) put(PX_PLAYER, cmds.u8());
                    break;
                }
                case 0x07: { // fill
                    uint32_t c = countOrNext(cmd, 4);
                    uint8_t v = cmds.u8();
                    for (uint32_t i = 0; i < c; i++) put(PX_COLOR, v);
                    break;
                }
                case 0x0A: { // fill player color
                    uint32_t c = countOrNext(cmd, 4);
                    uint8_t v = cmds.u8();
                    for (uint32_t i = 0; i < c; i++) put(PX_PLAYER, v);
                    break;
                }
                case 0x0B: { // shadow
                    uint32_t c = countOrNext(cmd, 4);
                    for (uint32_t i = 0; i < c; i++) put(PX_SHADOW, 0);
                    break;
                }
                case 0x0E: { // extended
                    switch (cmd) {
                    case 0x0E: case 0x1E: case 0x2E: case 0x3E: break; // obsolete transform/flip hints
                    case 0x4E: put(PX_OUTLINE, 0); break;
                    case 0x5E: { uint32_t c = cmds.u8(); for (uint32_t i = 0; i < c; i++) put(PX_OUTLINE, 0); break; }
                    case 0x6E: put(PX_SHIELD, 243); break;
                    case 0x7E: { uint32_t c = cmds.u8(); for (uint32_t i = 0; i < c; i++) put(PX_SHIELD, 243); break; }
                    default:
                        if (err) *err = "unknown extended SLP command";
                        return false;
                    }
                    break;
                }
                default:
                    if (err) *err = "unknown SLP command";
                    return false;
                }
            }
        }
    } catch (const FormatError &e) {
        if (err) *err = e.what();
        return false;
    }
    return true;
}

void colorize(const SlpImage &img, const Palette &pal, const ColorizeOptions &opt, uint8_t *out, int pitch) {
    for (int32_t y = 0; y < img.height; y++) {
        const uint8_t *k = &img.kind[(size_t)y * img.width];
        const uint8_t *ix = &img.index[(size_t)y * img.width];
        uint8_t *row = out + (size_t)y * pitch * 4;
        for (int32_t x = 0; x < img.width; x++) {
            int32_t dx = opt.flipX ? (img.width - 1 - x) : x;
            uint8_t *d = row + dx * 4;
            switch (k[x]) {
            case PX_COLOR: {
                const Rgba &c = pal[ix[x]];
                d[0] = c.r; d[1] = c.g; d[2] = c.b; d[3] = 255;
                break;
            }
            case PX_PLAYER: {
                const Rgba &c = pal[(uint8_t)(opt.playerColorBase + (ix[x] & 7))];
                d[0] = c.r; d[1] = c.g; d[2] = c.b; d[3] = 255;
                break;
            }
            case PX_SHADOW:
                d[0] = d[1] = d[2] = 0; d[3] = opt.shadowAlpha;
                break;
            default: // transparent, outlines (occlusion-only), shield flash
                d[0] = d[1] = d[2] = d[3] = 0;
                break;
            }
        }
    }
}

} // namespace swgb
