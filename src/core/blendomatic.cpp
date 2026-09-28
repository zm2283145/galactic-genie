// SPDX-License-Identifier: GPL-3.0-or-later
#include "blendomatic.h"

#include "bytes.h"

#include <algorithm>
#include <cstdio>

namespace swgb {

bool Blendomatic::load(const std::string &path, std::string *err) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) {
        if (err) *err = "cannot open " + path;
        return false;
    }
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (length < 0) {
        fclose(f);
        if (err) *err = "cannot determine size of " + path;
        return false;
    }
    std::vector<uint8_t> data((size_t)length);
    if (!data.empty() && fread(data.data(), 1, data.size(), f) != data.size()) {
        fclose(f);
        if (err) *err = "cannot read " + path;
        return false;
    }
    fclose(f);
    return parse(std::move(data), err);
}

bool Blendomatic::parse(std::vector<uint8_t> data, std::string *err) {
    modes_.clear();
    try {
        ByteReader r(data);
        const uint32_t modeCount = r.u32();
        const uint32_t maskCount = r.u32();
        if (modeCount == 0 || modeCount > 64 || maskCount == 0 || maskCount > 64)
            throw FormatError("invalid mode or mask count");

        modes_.resize(modeCount);
        for (uint32_t mode = 0; mode < modeCount; mode++) {
            const uint32_t tileSize = r.u32();
            if (tileSize == 0 || tileSize > 65536) throw FormatError("invalid mask tile size");
            r.skip(maskCount); // Per-mask flags are unused by the original renderer.
            if (tileSize > r.remaining() / 4) throw FormatError("truncated alpha bitmasks");
            const uint8_t *bitmaps = r.ptr((size_t)tileSize * 4); // One uint32 mask word per diamond pixel.

            uint32_t rows = 1;
            while ((uint64_t)rows * rows - rows + 1 < tileSize) rows++;
            if ((uint64_t)rows * rows - rows + 1 != tileSize || (rows & 1) == 0)
                throw FormatError("mask tile is not an odd-row diamond");

            BlendMode &outMode = modes_[mode];
            outMode.masks.resize(maskCount);
            const size_t byteMaskSize = (size_t)maskCount * tileSize;
            const bool hasByteMasks = r.remaining() >= byteMaskSize;
            if (!hasByteMasks && (mode + 1 != modeCount || r.remaining() != 0))
                throw FormatError("truncated alpha bytemasks");
            for (uint32_t maskIndex = 0; maskIndex < maskCount; maskIndex++) {
                const uint8_t *packed = hasByteMasks ? r.ptr(tileSize) : nullptr;
                BlendMask &mask = outMode.masks[maskIndex];
                mask.width = (int)(rows * 2 - 1);
                mask.height = (int)rows;
                mask.alpha.assign((size_t)mask.width * mask.height, 0);

                size_t source = 0;
                const uint32_t half = rows / 2;
                for (uint32_t y = 0; y < rows; y++) {
                    const uint32_t count = y < half ? 1 + y * 4 : rows * 2 - 1 - (y - half) * 4;
                    const uint32_t left = rows - 1 - count / 2;
                    for (uint32_t x = 0; x < count; x++) {
                        uint8_t alpha;
                        if (packed) {
                            const uint8_t value = packed[source];
                            alpha = value == 128 ? 0 : (uint8_t)((127 - (value & 0x7F)) * 2);
                        } else {
                            const uint8_t *word = bitmaps + source * 4;
                            const uint32_t bits = (uint32_t)word[0] | (uint32_t)word[1] << 8 |
                                                  (uint32_t)word[2] << 16 | (uint32_t)word[3] << 24;
                            alpha = bits & (1u << maskIndex) ? 255 : 0;
                        }
                        mask.alpha[(size_t)y * mask.width + left + x] = alpha;
                        source++;
                    }
                }
                if (source != tileSize) throw FormatError("mask tile has leftover pixels");
            }
        }
        if (r.remaining() != 0) throw FormatError("unexpected trailing data");
        return true;
    } catch (const FormatError &e) {
        modes_.clear();
        if (err) *err = std::string("blendomatic: ") + e.what();
        return false;
    }
}

} // namespace swgb
