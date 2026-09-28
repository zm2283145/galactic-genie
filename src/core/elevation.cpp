// SPDX-License-Identifier: GPL-3.0-or-later
#include "elevation.h"

#include "bytes.h"

#include <cstdio>

namespace swgb {

namespace {

bool readFile(const std::string &path, std::vector<uint8_t> &data, std::string *err) {
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
    data.resize((size_t)length);
    if (!data.empty() && fread(data.data(), 1, data.size(), f) != data.size()) {
        fclose(f);
        if (err) *err = "cannot read " + path;
        return false;
    }
    fclose(f);
    return true;
}

} // namespace

bool ElevationMaps::load(const std::string &templatePath, const std::string &filterPath,
                         const std::string &icmPath, std::string *err) {
    templates_ = {};
    filters_ = {};
    icm_.clear();
    return loadTemplates(templatePath, err) && loadFilters(filterPath, err) && loadIcm(icmPath, err);
}

uint8_t ElevationMaps::colorIndex(size_t map, uint8_t r, uint8_t g, uint8_t b) const {
    const size_t index = map * 32768 + ((size_t)r * 32 + g) * 32 + b;
    return index < icm_.size() ? icm_[index] : 0;
}

bool ElevationMaps::loadTemplates(const std::string &path, std::string *err) {
    std::vector<uint8_t> data;
    if (!readFile(path, data, err)) return false;
    try {
        ByteReader file(data);
        for (SlopeTemplate &out : templates_) {
            uint32_t size = file.u32();
            if (size < 28 || size > file.remaining()) throw FormatError("invalid slope template size");
            ByteReader block(file.ptr(size), size);
            out.width = (int)block.u32();
            out.height = (int)block.u32();
            out.hotspotX = block.i32();
            out.hotspotY = block.i32();
            block.i32(); // Generated SLP data size.
            uint32_t outlineOffset = block.u32();
            block.u32(); // Generated SLP command-table offset.
            if (out.width <= 0 || out.width > 4096 || out.height <= 0 || out.height > 4096)
                throw FormatError("invalid slope template dimensions");
            if (outlineOffset > size || (size_t)out.height * 4 > size - outlineOffset)
                throw FormatError("invalid slope template outline table");
            block.seek(outlineOffset);
            out.leftEdges.resize(out.height);
            out.rightEdges.resize(out.height);
            for (int y = 0; y < out.height; y++) {
                out.leftEdges[y] = block.u16();
                out.rightEdges[y] = block.u16();
                if ((uint32_t)out.leftEdges[y] + out.rightEdges[y] > (uint32_t)out.width)
                    throw FormatError("invalid slope template row edges");
            }
        }
        if (file.remaining() != 0) throw FormatError("unexpected trailing slope template data");
        return true;
    } catch (const FormatError &e) {
        if (err) *err = std::string("STemplet.dat: ") + e.what();
        templates_ = {};
        return false;
    }
}

bool ElevationMaps::loadFilters(const std::string &path, std::string *err) {
    std::vector<uint8_t> data;
    if (!readFile(path, data, err)) return false;
    try {
        ByteReader file(data);
        for (size_t slope = 0; slope < filters_.size(); slope++) {
            uint32_t size = file.u32();
            if (size < 4 || size > file.remaining()) throw FormatError("invalid filter map size");
            ByteReader block(file.ptr(size), size);
            uint32_t height = block.u32();
            if (height != (uint32_t)templates_[slope].height)
                throw FormatError("filter map height does not match slope template");
            FilterMap &out = filters_[slope];
            out.lines.resize(height);
            for (FilterLine &line : out.lines) {
                uint8_t width = block.u8();
                line.pixels.resize(width);
                for (FilterPixel &pixel : line.pixels) {
                    uint16_t descriptor = block.u16();
                    pixel.lightIndex = descriptor >> 4;
                    uint16_t sourceCount = descriptor & 0xF;
                    if (sourceCount == 0) throw FormatError("filter pixel has no sources");
                    pixel.sources.resize(sourceCount);
                    for (FilterSource &source : pixel.sources) {
                        const uint8_t *packed = block.ptr(3);
                        uint32_t value = (uint32_t)packed[0] | (uint32_t)packed[1] << 8 |
                                         (uint32_t)packed[2] << 16;
                        source.alpha = value & 0x1FF;
                        source.sourceOffset = value >> 9;
                    }
                }
            }
            if (block.remaining() != 0) throw FormatError("unexpected trailing filter map data");
        }
        if (file.remaining() != 0) throw FormatError("unexpected trailing filter-map file data");
        return true;
    } catch (const FormatError &e) {
        if (err) *err = std::string("FilterMaps.dat: ") + e.what();
        filters_ = {};
        return false;
    }
}

bool ElevationMaps::loadIcm(const std::string &path, std::string *err) {
    if (!readFile(path, icm_, err)) return false;
    if (icm_.size() != 10 * 32768) {
        if (err) *err = "VIEW_ICM.DAT: expected ten 32x32x32 color maps";
        icm_.clear();
        return false;
    }
    return true;
}

} // namespace swgb
