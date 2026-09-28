// SPDX-License-Identifier: GPL-3.0-or-later
#include "scenario.h"

#include "bytes.h"
#include "genie_dat.h"

#include <cmath>
#include <limits>

namespace swgb {

namespace {

constexpr uint32_t kSeparator = 0xFFFFFF9D;
constexpr size_t kPlayers = 16;

bool versionAbove(float version, float threshold) { return version > threshold + 0.0001f; }

std::string sizedString16(ByteReader &reader) {
    const uint16_t size = reader.u16();
    if (size > reader.remaining()) throw FormatError("scenario string exceeds remaining data");
    const char *data = reinterpret_cast<const char *>(reader.ptr(size));
    size_t length = size;
    if (length && data[length - 1] == '\0') length--;
    return std::string(data, length);
}

std::string sizedString32(ByteReader &reader) {
    const uint32_t size = reader.u32();
    if (size > reader.remaining()) throw FormatError("scenario header string exceeds remaining data");
    const char *data = reinterpret_cast<const char *>(reader.ptr(size));
    size_t length = size;
    if (length && data[length - 1] == '\0') length--;
    return std::string(data, length);
}

void expectSeparator(ByteReader &reader, const char *section) {
    if (reader.u32() != kSeparator) throw FormatError(std::string("invalid separator before ") + section);
}

void skipTimeline(ByteReader &reader) {
    const uint16_t count = reader.u16();
    reader.u16();
    reader.f32();
    reader.skip((size_t)count * 28);
}

void skipBitmap(ByteReader &reader) {
    const uint32_t included = reader.u32();
    const uint32_t width = reader.u32();
    const uint32_t height = reader.u32();
    reader.i16();
    if (!included) return;

    const uint32_t headerSize = reader.u32();
    const uint32_t bitmapWidth = reader.u32();
    const uint32_t bitmapHeight = reader.u32();
    const uint16_t planes = reader.u16();
    const uint16_t bitsPerPixel = reader.u16();
    const uint32_t compression = reader.u32();
    const uint32_t rawSize = reader.u32();
    reader.i32();
    reader.i32();
    const uint32_t paletteSize = reader.u32();
    reader.u32();
    if (headerSize != 40 || bitmapWidth != width || bitmapHeight != height || planes != 1 || compression != 0)
        throw FormatError("unsupported scenario bitmap");
    if (bitsPerPixel > 8 || paletteSize > 256) throw FormatError("invalid scenario bitmap palette");
    if ((uint64_t)paletteSize * 4 + rawSize > reader.remaining())
        throw FormatError("scenario bitmap exceeds remaining data");
    reader.skip((size_t)paletteSize * 4 + rawSize);
}

void skipAiFiles(ByteReader &reader, float version) {
    for (size_t i = 0; i < kPlayers; i++) {
        const uint32_t aiNameSize = reader.u32();
        const uint32_t citySize = reader.u32();
        const uint32_t personalitySize = versionAbove(version, 1.07f) ? reader.u32() : 0;
        const uint64_t total = (uint64_t)aiNameSize + citySize + personalitySize;
        if (total > reader.remaining()) throw FormatError("scenario AI file exceeds remaining data");
        reader.skip((size_t)total);
    }
}

void skipPlayerData(ByteReader &reader, Scenario &scenario) {
    const float version = reader.f32();
    scenario.playerDataVersion = version;
    if (!std::isfinite(version) || version < 1.0f || version > 2.0f)
        throw FormatError("unsupported scenario player-data version");

    if (versionAbove(version, 1.13f)) {
        reader.skip(kPlayers * 256);
        if (versionAbove(version, 1.15f)) reader.skip(kPlayers * 4);
        reader.skip(kPlayers * 4 * 4);
    }
    if (versionAbove(version, 1.06f)) reader.u8();
    skipTimeline(reader);
    scenario.originalFilename = sizedString16(reader);

    if (versionAbove(version, 1.15f)) reader.skip(5 * 4);
    if (versionAbove(version, 1.21f)) reader.skip(4);
    scenario.instructions = sizedString16(reader);
    if (versionAbove(version, 1.1f)) {
        scenario.hints = sizedString16(reader);
        sizedString16(reader);
        sizedString16(reader);
        sizedString16(reader);
    }
    if (versionAbove(version, 1.21f)) scenario.scouts = sizedString16(reader);
    if (version < 1.03f) {
        sizedString16(reader);
        sizedString16(reader);
        sizedString16(reader);
    }
    sizedString16(reader);
    sizedString16(reader);
    sizedString16(reader);
    if (versionAbove(version, 1.08f)) sizedString16(reader);
    if (versionAbove(version, 1.07f)) skipBitmap(reader);

    for (size_t i = 0; i < kPlayers * 2; i++) sizedString16(reader);
    if (versionAbove(version, 1.07f))
        for (size_t i = 0; i < kPlayers; i++) sizedString16(reader);
    skipAiFiles(reader, version);
    if (versionAbove(version, 1.1f) && std::fabs(version - 1.14f) > 0.0001f) reader.skip(kPlayers);
    if (versionAbove(version, 1.01f)) expectSeparator(reader, "player resources");

    if (version < 1.14f) {
        reader.skip(kPlayers * (256 + 8 * 4));
    } else {
        size_t resourceFields = versionAbove(version, 1.16f) ? 6 : 4;
        if (versionAbove(version, 1.23f)) resourceFields++;
        reader.skip(kPlayers * resourceFields * 4);
    }
    if (versionAbove(version, 1.01f)) expectSeparator(reader, "victory conditions");

    reader.skip((versionAbove(version, 1.12f) ? 10 : 7) * 4);
    reader.skip(kPlayers * kPlayers * 4);
    reader.skip(kPlayers * 180 * 4);
    if (versionAbove(version, 1.01f)) expectSeparator(reader, "allied victories");
    reader.skip((version < 1.02f ? kPlayers * kPlayers : kPlayers) * 4);
    if (versionAbove(version, 1.22f)) reader.u32();

    if (versionAbove(version, 1.03f)) {
        if (versionAbove(version, 1.17f)) reader.skip(kPlayers * 4);
        const size_t techSlots = version < 1.04f || version <= 1.14f ? 20 : version < 1.3f ? 30 : 60;
        reader.skip(kPlayers * techSlots * 4);
        if (versionAbove(version, 1.17f)) {
            const size_t unitSlots = version < 1.3f ? 30 : 60;
            const size_t buildingSlots = version < 1.3f ? 20 : 60;
            reader.skip(kPlayers * 4 + kPlayers * unitSlots * 4);
            reader.skip(kPlayers * 4 + kPlayers * buildingSlots * 4);
        }
    }
    if (versionAbove(version, 1.04f)) reader.u32();
    if (versionAbove(version, 1.11f)) reader.skip(8);
    if (versionAbove(version, 1.05f)) reader.skip(kPlayers * 4);
    if (versionAbove(version, 1.01f)) expectSeparator(reader, "camera");
    if (versionAbove(version, 1.18f)) {
        scenario.cameraX = reader.i32();
        scenario.cameraY = reader.i32();
    }
    if (versionAbove(version, 1.2f)) reader.i32();
    if (versionAbove(version, 1.23f)) reader.skip(kPlayers);
}

} // namespace

bool Scenario::load(const std::vector<uint8_t> &scx, std::string *err) {
    *this = Scenario{};
    try {
        ByteReader file(scx);
        version = file.fixedStr(4);
        if (version != "1.21") throw FormatError("unsupported SCX version '" + version + "'");
        const uint32_t headerLength = file.u32();
        if (headerLength > file.remaining()) throw FormatError("SCX header exceeds file size");
        const size_t compressedOffset = file.pos() + headerLength;

        ByteReader header(file.ptr(headerLength), headerLength);
        saveType = header.i32();
        lastSaveTime = header.u32();
        instructions = sizedString32(header);
        victoryType = header.i32();
        enabledPlayerCount = header.u32();
        if (enabledPlayerCount > 16) throw FormatError("invalid enabled-player count");

        std::vector<uint8_t> compressed(scx.begin() + (ptrdiff_t)compressedOffset, scx.end());
        std::vector<uint8_t> body;
        std::string inflateError;
        if (!dat::inflateRaw(compressed, body, &inflateError)) throw FormatError(inflateError);

        ByteReader reader(body);
        nextUnitId = reader.u32();
        skipPlayerData(reader, *this);
        map.width = reader.u32();
        map.height = reader.u32();
        if (!map.width || !map.height || map.width > 1024 || map.height > 1024)
            throw FormatError("invalid scenario map dimensions");
        const uint64_t tileCount = (uint64_t)map.width * map.height;
        if (tileCount > std::numeric_limits<size_t>::max() || tileCount * 3 > reader.remaining())
            throw FormatError("scenario map exceeds remaining data");
        map.tiles.resize((size_t)tileCount);
        for (ScenarioTile &tile : map.tiles) {
            tile.terrain = reader.u8();
            tile.elevation = reader.u8();
            const uint8_t unused = reader.u8();
            if (unused != 0) throw FormatError("nonzero scenario map tile padding");
        }
    } catch (const std::exception &e) {
        if (err) *err = std::string("invalid SCX: ") + e.what();
        *this = Scenario{};
        return false;
    }
    return true;
}

} // namespace swgb
