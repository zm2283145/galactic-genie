// SPDX-License-Identifier: GPL-3.0-or-later
#include "language_strings.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace swgb {

namespace {

uint16_t read16(const std::vector<uint8_t> &data, size_t offset) {
    if (offset + 2 > data.size()) return 0;
    return (uint16_t)data[offset] | (uint16_t)data[offset + 1] << 8;
}

uint32_t read32(const std::vector<uint8_t> &data, size_t offset) {
    if (offset + 4 > data.size()) return 0;
    return (uint32_t)data[offset] | (uint32_t)data[offset + 1] << 8 |
           (uint32_t)data[offset + 2] << 16 |
           (uint32_t)data[offset + 3] << 24;
}

void appendUtf8(std::string &out, uint32_t value) {
    if (value <= 0x7F) {
        out.push_back((char)value);
    } else if (value <= 0x7FF) {
        out.push_back((char)(0xC0 | value >> 6));
        out.push_back((char)(0x80 | (value & 0x3F)));
    } else if (value <= 0xFFFF) {
        out.push_back((char)(0xE0 | value >> 12));
        out.push_back((char)(0x80 | ((value >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (value & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | value >> 18));
        out.push_back((char)(0x80 | ((value >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((value >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (value & 0x3F)));
    }
}

std::string readUtf16(const std::vector<uint8_t> &data, size_t offset,
                      size_t length) {
    std::string result;
    for (size_t i = 0; i < length && offset + i * 2 + 2 <= data.size(); i++) {
        uint32_t value = read16(data, offset + i * 2);
        if (value >= 0xD800 && value <= 0xDBFF && i + 1 < length) {
            const uint32_t low = read16(data, offset + (i + 1) * 2);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                value =
                    0x10000 + ((value - 0xD800) << 10) + (low - 0xDC00);
                i++;
            }
        }
        appendUtf8(result, value);
    }
    return result;
}

} // namespace

bool LanguageStrings::load(const std::string &path, std::string *err,
                           bool overlay) {
    if (!overlay) strings_.clear();
    FILE *file = fopen(path.c_str(), "rb");
    if (!file) {
        if (err) *err = "could not open language resource " + path;
        return false;
    }
    fseek(file, 0, SEEK_END);
    const long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (length <= 0) {
        fclose(file);
        if (err) *err = "empty language resource " + path;
        return false;
    }
    std::vector<uint8_t> data((size_t)length);
    const bool read = fread(data.data(), 1, data.size(), file) == data.size();
    fclose(file);
    if (!read || data.size() < 0x40 || read16(data, 0) != 0x5A4D) {
        if (err) *err = "invalid language PE file " + path;
        return false;
    }

    const size_t pe = read32(data, 0x3C);
    if (pe + 24 > data.size() || read32(data, pe) != 0x00004550) {
        if (err) *err = "language resource has no PE header";
        return false;
    }
    const uint16_t sectionCount = read16(data, pe + 6);
    const uint16_t optionalSize = read16(data, pe + 20);
    const size_t optional = pe + 24;
    const uint16_t magic = read16(data, optional);
    const size_t dataDirectories =
        optional + (magic == 0x20B ? 112 : magic == 0x10B ? 96 : 0);
    if (dataDirectories == optional || dataDirectories + 24 > data.size()) {
        if (err) *err = "unsupported language PE format";
        return false;
    }
    const uint32_t resourceRva = read32(data, dataDirectories + 16);
    if (!resourceRva) {
        if (err) *err = "language PE has no resources";
        return false;
    }

    const size_t sections = optional + optionalSize;
    auto rvaOffset = [&](uint32_t rva) -> size_t {
        for (uint16_t i = 0; i < sectionCount; i++) {
            const size_t section = sections + (size_t)i * 40;
            if (section + 40 > data.size()) break;
            const uint32_t virtualSize = read32(data, section + 8);
            const uint32_t virtualAddress = read32(data, section + 12);
            const uint32_t rawSize = read32(data, section + 16);
            const uint32_t rawOffset = read32(data, section + 20);
            const uint32_t size = std::max(virtualSize, rawSize);
            if (rva >= virtualAddress && rva - virtualAddress < size)
                return (size_t)rawOffset + (rva - virtualAddress);
        }
        return data.size();
    };
    const size_t resourceRoot = rvaOffset(resourceRva);
    if (resourceRoot >= data.size()) {
        if (err) *err = "invalid language resource directory";
        return false;
    }

    auto directoryEntry = [&](size_t directory, uint32_t id,
                              uint32_t &offset) {
        if (directory + 16 > data.size()) return false;
        const size_t count =
            (size_t)read16(data, directory + 12) +
            read16(data, directory + 14);
        for (size_t i = 0; i < count; i++) {
            const size_t entry = directory + 16 + i * 8;
            if (entry + 8 > data.size()) return false;
            const uint32_t name = read32(data, entry);
            if (!(name & 0x80000000u) && name == id) {
                offset = read32(data, entry + 4);
                return true;
            }
        }
        return false;
    };

    uint32_t stringTypeOffset = 0;
    if (!directoryEntry(resourceRoot, 6, stringTypeOffset) ||
        !(stringTypeOffset & 0x80000000u)) {
        if (err) *err = "language PE has no string table";
        return false;
    }
    const size_t stringDirectory =
        resourceRoot + (stringTypeOffset & 0x7FFFFFFFu);
    if (stringDirectory + 16 > data.size()) {
        if (err) *err = "invalid language string directory";
        return false;
    }
    const size_t blockCount =
        (size_t)read16(data, stringDirectory + 12) +
        read16(data, stringDirectory + 14);
    for (size_t block = 0; block < blockCount; block++) {
        const size_t blockEntry = stringDirectory + 16 + block * 8;
        if (blockEntry + 8 > data.size()) break;
        const uint32_t blockId = read32(data, blockEntry);
        const uint32_t blockOffset = read32(data, blockEntry + 4);
        if ((blockId & 0x80000000u) || !(blockOffset & 0x80000000u) ||
            blockId == 0)
            continue;
        const size_t languages =
            resourceRoot + (blockOffset & 0x7FFFFFFFu);
        if (languages + 24 > data.size()) continue;
        const size_t languageCount =
            (size_t)read16(data, languages + 12) +
            read16(data, languages + 14);
        if (!languageCount) continue;
        const uint32_t languageOffset = read32(data, languages + 20);
        if (languageOffset & 0x80000000u) continue;
        const size_t dataEntry = resourceRoot + languageOffset;
        if (dataEntry + 16 > data.size()) continue;
        const size_t stringsOffset = rvaOffset(read32(data, dataEntry));
        const size_t stringsSize = read32(data, dataEntry + 4);
        if (stringsOffset >= data.size() ||
            stringsSize > data.size() - stringsOffset)
            continue;
        size_t cursor = stringsOffset;
        for (int index = 0; index < 16 && cursor + 2 <= data.size(); index++) {
            const size_t stringLength = read16(data, cursor);
            cursor += 2;
            if (stringLength && stringLength <= (data.size() - cursor) / 2)
                strings_[(int)((blockId - 1) * 16 + index)] =
                    readUtf16(data, cursor, stringLength);
            if (stringLength > (data.size() - cursor) / 2) break;
            cursor += stringLength * 2;
        }
    }
    if (strings_.empty()) {
        if (err) *err = "language string table is empty";
        return false;
    }
    return true;
}

const std::string &LanguageStrings::get(int id) const {
    static const std::string empty;
    auto it = strings_.find(id);
    return it == strings_.end() ? empty : it->second;
}

} // namespace swgb
