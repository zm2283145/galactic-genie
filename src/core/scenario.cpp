// SPDX-License-Identifier: GPL-3.0-or-later
#include "scenario.h"

#include "bytes.h"
#include "genie_dat.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <zlib.h>

namespace swgb {

// Debug aid (swgbtool scx-diff): body offsets of the sections as read.
std::vector<std::pair<std::string, size_t>> *g_scenarioTrace = nullptr;

namespace {

void trace(const char *what, const ByteReader &reader) {
    if (g_scenarioTrace) g_scenarioTrace->push_back({what, reader.pos()});
}

constexpr uint32_t kSeparator = 0xFFFFFF9D;
constexpr size_t kPlayers = 16;
constexpr size_t kPlayablePlayers = 8;
constexpr int32_t kMaxTriggerFields = 64;
constexpr int32_t kMaxTriggerRecords = 100000;

bool versionAbove(float version, float threshold) { return version > threshold + 0.0001f; }

// Star Wars Galactic Battlegrounds: Clone Campaigns writes player data 1.30
// (.sc1 in .cp1 campaigns). Its layout follows SWGB 1.22 except where noted.
bool isCloneCampaignsPlayerData(float version) { return std::fabs(version - 1.30f) < 0.0001f; }

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

void skipSizedBlock32(ByteReader &reader, const char *what) {
    const uint32_t size = reader.u32();
    if (size > reader.remaining()) throw FormatError(std::string(what) + " exceeds remaining data");
    reader.skip(size);
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

void readAiFiles(ByteReader &reader, float version, Scenario &scenario) {
    for (size_t i = 0; i < kPlayers; i++) {
        const uint32_t aiNameSize = reader.u32();
        const uint32_t citySize = reader.u32();
        const uint32_t personalitySize = versionAbove(version, 1.07f) ? reader.u32() : 0;
        const uint64_t total = (uint64_t)aiNameSize + citySize + personalitySize;
        if (total > reader.remaining()) throw FormatError("scenario AI file exceeds remaining data");
        auto readText = [&](uint32_t size) {
            const char *data = reinterpret_cast<const char *>(reader.ptr(size));
            size_t length = size;
            while (length && data[length - 1] == '\0') length--;
            return std::string(data, length);
        };
        scenario.players[i].aiFilename = readText(aiNameSize);
        scenario.players[i].cityFilename = readText(citySize);
        scenario.players[i].personality = readText(personalitySize);
    }
}

void readFixedIds(
    ByteReader &reader,
    const std::array<uint32_t, kPlayers> &counts,
    size_t slots,
    std::array<ScenarioPlayer, kPlayers> &players,
    std::vector<uint32_t> ScenarioPlayer::*member,
    const char *what) {
    for (size_t player = 0; player < kPlayers; ++player) {
        if (counts[player] > slots)
            throw FormatError(std::string("scenario ") + what + " count exceeds slots");
        std::vector<uint32_t> &values = players[player].*member;
        values.clear();
        values.reserve(counts[player]);
        for (size_t slot = 0; slot < slots; ++slot) {
            const uint32_t value = reader.u32();
            if (slot < counts[player]) values.push_back(value);
        }
    }
}

void skipPlayerData(ByteReader &reader, Scenario &scenario) {
    const float version = reader.f32();
    scenario.playerDataVersion = version;
    if (!std::isfinite(version) || version < 1.0f || version > 2.0f)
        throw FormatError("unsupported scenario player-data version");

    if (versionAbove(version, 1.13f)) {
        for (ScenarioPlayer &player : scenario.players) player.name = reader.fixedStr(256);
        if (versionAbove(version, 1.15f))
            for (size_t i = 0; i < kPlayers; i++) scenario.players[i].nameStringId = reader.i32();
        for (size_t i = 0; i < kPlayers; i++) {
            ScenarioPlayer &player = scenario.players[i];
            player.active = reader.u32() != 0;
            player.human = reader.u32() != 0;
            player.civilization = reader.u32();
            scenario.civilizations[i] = player.civilization;
            reader.u32();
        }
    }
    trace("timeline", reader);
    if (versionAbove(version, 1.06f)) scenario.playerDataByte = reader.u8();
    {
        const uint16_t count = reader.u16();
        reader.u16();
        scenario.timelineValue = reader.f32();
        reader.skip((size_t)count * 28);
    }
    scenario.originalFilename = sizedString16(reader);

    if (versionAbove(version, 1.15f))
        for (size_t i = 0; i < 5; ++i) scenario.messageStringIds[i] = reader.i32();
    if (versionAbove(version, 1.21f)) scenario.messageStringIds[5] = reader.i32();
    trace("messages", reader);
    // Records whether each message string was stored NUL-terminated.
    int messageIndex = 0;
    scenario.messageTerminatedMask = 0;
    auto message = [&](std::string &out) {
        const uint16_t size = reader.data()[reader.pos()] | (reader.data()[reader.pos() + 1] << 8);
        const bool terminated =
            size > 0 && reader.remaining() >= 2u + size && reader.data()[reader.pos() + 1 + size] == 0;
        if (terminated) scenario.messageTerminatedMask |= 1u << messageIndex;
        messageIndex++;
        out = sizedString16(reader);
    };
    message(scenario.instructions);
    if (versionAbove(version, 1.1f)) {
        message(scenario.hints);
        message(scenario.victoryMessage);
        message(scenario.lossMessage);
        message(scenario.history);
    }
    if (versionAbove(version, 1.21f)) message(scenario.scouts);
    if (version < 1.03f) {
        sizedString16(reader);
        sizedString16(reader);
        sizedString16(reader);
    }
    messageIndex = 6;
    message(scenario.pregameCinematic);
    message(scenario.victoryCinematic);
    message(scenario.lossCinematic);
    if (versionAbove(version, 1.08f)) message(scenario.background);
    if (versionAbove(version, 1.07f)) skipBitmap(reader);

    trace("ai names", reader);
    for (ScenarioPlayer &player : scenario.players) player.aiName = sizedString16(reader);
    for (ScenarioPlayer &player : scenario.players) player.cityName = sizedString16(reader);
    if (versionAbove(version, 1.07f))
        for (ScenarioPlayer &player : scenario.players) player.personalityName = sizedString16(reader);
    trace("ai files", reader);
    readAiFiles(reader, version, scenario);
    if (versionAbove(version, 1.1f) && std::fabs(version - 1.14f) > 0.0001f)
        for (ScenarioPlayer &player : scenario.players) player.aiType = reader.u8();
    trace("resources", reader);
    if (versionAbove(version, 1.01f)) expectSeparator(reader, "player resources");

    if (version < 1.14f) {
        reader.skip(kPlayers * (256 + 8 * 4));
    } else {
        size_t resourceFields = versionAbove(version, 1.16f) ? 6 : 4;
        // Clone Campaigns' player data 1.30 keeps SWGB's six resource fields
        // (food, carbon, nova, ore, the spare slot, population).
        if (versionAbove(version, 1.23f) && !isCloneCampaignsPlayerData(version)) resourceFields++;
        for (ScenarioPlayer &player : scenario.players) {
            for (size_t field = 0; field < resourceFields; field++) {
                const uint32_t value = reader.u32();
                if (field < player.resources.size()) player.resources[field] = (float)value;
                if (field < player.primaryResources.size()) player.primaryResources[field] = value;
            }
        }
    }
    if (versionAbove(version, 1.01f)) expectSeparator(reader, "victory conditions");

    trace("victory", reader);
    scenario.victory.conquestRequired = reader.u32() != 0;
    reader.u32();
    scenario.victory.requiredHolocrons = reader.u32();
    reader.u32();
    scenario.victory.requiredExploredPercent = reader.u32();
    reader.u32();
    scenario.victory.allConditionsRequired = reader.u32() != 0;
    if (versionAbove(version, 1.12f)) {
        scenario.victory.mode = reader.u32();
        scenario.victory.requiredScore = reader.u32();
        scenario.victory.timeLimit = reader.u32();
    }
    for (ScenarioPlayer &player : scenario.players)
        for (size_t i = 0; i < player.diplomacy.size(); ++i)
            player.diplomacy[i] = player.primaryDiplomacy[i] = reader.u32();
    trace("victory180", reader);
    reader.skip(kPlayers * 180 * 4);
    if (versionAbove(version, 1.01f)) expectSeparator(reader, "allied victories");
    const size_t alliedVictoryCount = version < 1.02f ? kPlayers * kPlayers : kPlayers;
    for (size_t index = 0; index < alliedVictoryCount; ++index) {
        const bool enabled = reader.u32() != 0;
        if (index < kPlayers) scenario.players[index].alliedVictory = enabled;
    }
    if (versionAbove(version, 1.22f) && !isCloneCampaignsPlayerData(version)) reader.u32();

    trace("disabled", reader);
    if (versionAbove(version, 1.03f)) {
        std::array<uint32_t, kPlayers> counts{};
        if (versionAbove(version, 1.17f))
            for (uint32_t &count : counts) count = reader.u32();
        else
            counts.fill(version < 1.04f ? 20u : 30u);
        const size_t techSlots = version < 1.04f || version <= 1.14f ? 20 : version < 1.3f ? 30 : 60;
        readFixedIds(reader, counts, techSlots, scenario.players,
                     &ScenarioPlayer::disabledTechnologies, "disabled technology");
        if (versionAbove(version, 1.17f)) {
            const size_t unitSlots = version < 1.3f ? 30 : 60;
            const size_t buildingSlots = version < 1.3f ? 20 : 60;
            for (uint32_t &count : counts) count = reader.u32();
            readFixedIds(reader, counts, unitSlots, scenario.players,
                         &ScenarioPlayer::disabledUnits, "disabled unit");
            for (uint32_t &count : counts) count = reader.u32();
            readFixedIds(reader, counts, buildingSlots, scenario.players,
                         &ScenarioPlayer::disabledBuildings, "disabled building");
        }
    }
    if (versionAbove(version, 1.04f)) reader.u32();
    if (versionAbove(version, 1.11f)) {
        reader.u32();
        scenario.allTechnologies = reader.u32() != 0;
    }
    if (versionAbove(version, 1.05f))
        for (ScenarioPlayer &player : scenario.players)
            player.startingAge = reader.i32();
    trace("camera", reader);
    if (versionAbove(version, 1.01f)) expectSeparator(reader, "camera");
    if (versionAbove(version, 1.18f)) {
        scenario.headerCameraX = reader.i32();
        scenario.headerCameraY = reader.i32();
        scenario.cameraX = (float)scenario.headerCameraX;
        scenario.cameraY = (float)scenario.headerCameraY;
        scenario.mapCameraX = scenario.cameraX;
        scenario.mapCameraY = scenario.cameraY;
    }
    if (versionAbove(version, 1.2f)) scenario.headerCameraExtra = reader.i32();
    if (versionAbove(version, 1.23f) && !isCloneCampaignsPlayerData(version))
        for (ScenarioPlayer &player : scenario.players) player.aiType = reader.u8();
}

ScenarioEffect readEffect(ByteReader &reader) {
    ScenarioEffect effect;
    effect.type = reader.i32();
    const int32_t fieldCount = reader.i32();
    if (fieldCount < 0 || fieldCount > kMaxTriggerFields)
        throw FormatError("invalid scenario effect field count");
    effect.fields = reader.vec<int32_t>((size_t)fieldCount);
    effect.emptyMessageTerminated = reader.data()[reader.pos()] == 1 && reader.remaining() >= 4 &&
                                    reader.data()[reader.pos() + 1] == 0 &&
                                    reader.data()[reader.pos() + 2] == 0 &&
                                    reader.data()[reader.pos() + 3] == 0;
    effect.message = sizedString32(reader);
    effect.emptySoundTerminated = reader.data()[reader.pos()] == 1 && reader.remaining() >= 4 &&
                                  reader.data()[reader.pos() + 1] == 0 &&
                                  reader.data()[reader.pos() + 2] == 0 &&
                                  reader.data()[reader.pos() + 3] == 0;
    effect.sound = sizedString32(reader);
    const int32_t selectedCount = effect.fields.size() > 4 ? effect.fields[4] : 0;
    if (selectedCount > kMaxTriggerRecords ||
        (selectedCount > 0 && (uint64_t)selectedCount * sizeof(uint32_t) > reader.remaining()))
        throw FormatError("invalid scenario effect selection count");
    if (selectedCount > 0) effect.selectedUnitIds = reader.vec<uint32_t>((size_t)selectedCount);
    return effect;
}

ScenarioCondition readCondition(ByteReader &reader) {
    ScenarioCondition condition;
    condition.type = reader.i32();
    const int32_t fieldCount = reader.i32();
    if (fieldCount < 0 || fieldCount > kMaxTriggerFields)
        throw FormatError("invalid scenario condition field count");
    condition.fields = reader.vec<int32_t>((size_t)fieldCount);
    return condition;
}

ScenarioTrigger readTrigger(ByteReader &reader) {
    ScenarioTrigger trigger;
    trigger.enabled = reader.i32() != 0;
    trigger.looping = reader.u8() != 0;
    trigger.descriptionStringId = reader.i32();
    trigger.objective = reader.u8() != 0;
    trigger.objectiveOrder = reader.i32();
    trigger.objectiveStringId = reader.i32();
    trigger.description = sizedString32(reader);
    trigger.name = sizedString32(reader);

    const int32_t effectCount = reader.i32();
    if (effectCount < 0 || effectCount > kMaxTriggerRecords)
        throw FormatError("invalid scenario trigger effect count");
    trigger.effects.reserve((size_t)effectCount);
    for (int32_t i = 0; i < effectCount; i++) trigger.effects.push_back(readEffect(reader));
    trigger.effectOrder = reader.vec<int32_t>((size_t)effectCount);

    const int32_t conditionCount = reader.i32();
    if (conditionCount < 0 || conditionCount > kMaxTriggerRecords)
        throw FormatError("invalid scenario trigger condition count");
    trigger.conditions.reserve((size_t)conditionCount);
    for (int32_t i = 0; i < conditionCount; i++) trigger.conditions.push_back(readCondition(reader));
    trigger.conditionOrder = reader.vec<int32_t>((size_t)conditionCount);
    return trigger;
}

void readTriggers(ByteReader &reader, Scenario &scenario) {
    trace("triggers", reader);
    scenario.triggerSystemVersion = reader.get<double>();
    if (!std::isfinite(scenario.triggerSystemVersion))
        throw FormatError("invalid scenario trigger-system version");
    scenario.objectiveState = reader.u8();
    const uint32_t triggerCount = reader.u32();
    if (triggerCount > (uint32_t)kMaxTriggerRecords)
        throw FormatError("invalid scenario trigger count");
    scenario.triggers.reserve(triggerCount);
    for (uint32_t i = 0; i < triggerCount; i++) scenario.triggers.push_back(readTrigger(reader));
    scenario.triggerOrder = reader.vec<uint32_t>(triggerCount);

    trace("files", reader);
    const int32_t filesIncluded = reader.i32();
    const int32_t compatibilityIncluded = reader.i32();
    if ((filesIncluded != 0 && filesIncluded != 1) ||
        (compatibilityIncluded != 0 && compatibilityIncluded != 1))
        throw FormatError("invalid scenario included-file flags");
    if (compatibilityIncluded) {
        const uint8_t *data = reader.ptr(396);
        scenario.compatibilityBlock.assign(data, data + 396);
    }
    if (filesIncluded) {
        const int32_t fileCount = reader.i32();
        if (fileCount < 0 || fileCount > kMaxTriggerRecords)
            throw FormatError("invalid scenario included-file count");
        for (int32_t i = 0; i < fileCount; i++) {
            Scenario::IncludedFile file;
            file.name = sizedString32(reader);
            const uint32_t size = reader.u32();
            if (size > reader.remaining()) throw FormatError("scenario included file exceeds remaining data");
            const char *data = reinterpret_cast<const char *>(reader.ptr(size));
            file.data.assign(data, size);
            scenario.includedFiles.push_back(std::move(file));
        }
    }
    if (reader.remaining() != 0) throw FormatError("unexpected data after scenario triggers");
}

void readScenarioObjects(ByteReader &reader, Scenario &scenario) {
    trace("objects", reader);
    const uint32_t playerBlocks = reader.u32();
    if (!playerBlocks || playerBlocks > 16) throw FormatError("invalid scenario player-unit block count");

    // SWGB SCX 1.21 uses internal scenario version 1.15: food, carbon,
    // nova, ore, an additional ore slot, and population limit.
    for (size_t player = 0; player < kPlayablePlayers; player++) {
        for (size_t resource = 0; resource < 5; resource++) {
            const float value = reader.f32();
            if (!std::isfinite(value)) throw FormatError("scenario player has a non-finite resource");
            scenario.players[player].resources[resource] = value;
        }
        const float populationLimit = reader.f32();
        if (!std::isfinite(populationLimit))
            throw FormatError("scenario player has a non-finite population limit");
        scenario.players[player].populationLimit = populationLimit;
    }

    scenario.units.clear();
    for (uint32_t player = 0; player < playerBlocks; player++) {
        const uint32_t unitCount = reader.u32();
        if (unitCount > 20000) throw FormatError("scenario player contains too many units");
        if (unitCount > std::numeric_limits<size_t>::max() - scenario.units.size())
            throw FormatError("scenario unit count overflow");
        scenario.units.reserve(scenario.units.size() + unitCount);
        for (uint32_t i = 0; i < unitCount; i++) {
            ScenarioUnit unit;
            unit.x = reader.f32();
            unit.y = reader.f32();
            unit.z = reader.f32();
            unit.spawnId = reader.u32();
            unit.unitId = reader.u16();
            unit.state = reader.u8();
            unit.rotation = reader.f32();
            unit.initialFrame = reader.u16();
            unit.garrisonedInId = reader.i32();
            unit.player = (uint8_t)player;
            if (!std::isfinite(unit.x) || !std::isfinite(unit.y) || !std::isfinite(unit.z) ||
                !std::isfinite(unit.rotation))
                throw FormatError("scenario unit has a non-finite value");
            scenario.units.push_back(unit);
        }
    }

    trace("players2", reader);
    const uint32_t playerCount = reader.u32();
    if (playerCount < 2 || playerCount > 16) throw FormatError("invalid secondary scenario player count");
    const size_t recordCount = playerCount - 1;
    for (size_t player = 0; player < recordCount; player++) {
        ScenarioPlayer &state = scenario.players[player];
        state.recordName = sizedString16(reader);
        state.cameraX = reader.f32();
        state.cameraY = reader.f32();
        if (!std::isfinite(state.cameraX) || !std::isfinite(state.cameraY))
            throw FormatError("scenario player has a non-finite camera");
        state.recordView[0] = reader.i16();
        state.recordView[1] = reader.i16();
        state.alliedVictory = reader.u8() != 0;
        const int16_t diplomacyCount = reader.i16();
        if (diplomacyCount < 0 || diplomacyCount > 16)
            throw FormatError("invalid secondary scenario diplomacy count");
        for (int16_t i = 0; i < diplomacyCount; i++) state.diplomacy[(size_t)i] = reader.u8();
        for (int16_t i = 0; i < diplomacyCount; i++) state.recordDiplomacy[(size_t)i] = reader.u32();
        const int32_t color = reader.i32();
        if (color < 0 || color > 255) throw FormatError("invalid scenario player color");
        state.color = (uint32_t)color;
        const size_t tailStart = reader.pos();
        const float recordVersion = reader.f32();
        if (!std::isfinite(recordVersion)) throw FormatError("invalid secondary player-data version");
        const int16_t extraCount = reader.i16();
        if (extraCount < 0) throw FormatError("invalid secondary player-data record count");
        if (std::fabs(recordVersion - 2.0f) < 0.0001f) reader.skip(8);
        if ((uint64_t)(uint16_t)extraCount * 44 > reader.remaining())
            throw FormatError("secondary player data exceeds remaining data");
        reader.skip((size_t)extraCount * 44);
        reader.skip(7);
        reader.i32();
        state.recordTail.assign(reader.data() + tailStart, reader.data() + reader.pos());
    }
    if (scenario.players[0].cameraX >= 0 && scenario.players[0].cameraY >= 0) {
        scenario.cameraX = scenario.players[0].cameraX;
        scenario.cameraY = scenario.players[0].cameraY;
    }
    readTriggers(reader, scenario);
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
        trace("map", reader);
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
        readScenarioObjects(reader, *this);
    } catch (const std::exception &e) {
        if (err) *err = std::string("invalid SCX: ") + e.what();
        *this = Scenario{};
        return false;
    }
    return true;
}


namespace {

class ByteWriter {
public:
    std::vector<uint8_t> bytes;
    template <typename T> void put(T value) {
        uint8_t raw[sizeof(T)];
        std::memcpy(raw, &value, sizeof(T));
        bytes.insert(bytes.end(), raw, raw + sizeof(T));
    }
    void u8(uint8_t v) { put(v); }
    void u16(uint16_t v) { put(v); }
    void i16(int16_t v) { put(v); }
    void u32(uint32_t v) { put(v); }
    void i32(int32_t v) { put(v); }
    void f32(float v) { put(v); }
    void zeros(size_t n) { bytes.insert(bytes.end(), n, 0); }
    void fixed(const std::string &value, size_t n) {
        const size_t length = std::min(value.size(), n - 1);
        bytes.insert(bytes.end(), value.begin(), value.begin() + (ptrdiff_t)length);
        zeros(n - length);
    }
    // 16-bit sized strings have no terminating NUL.
    void str16(const std::string &value) {
        const uint16_t size = (uint16_t)std::min<size_t>(value.size(), 65535);
        u16(size);
        bytes.insert(bytes.end(), value.begin(), value.begin() + size);
    }
    // With a terminating NUL when not empty.
    void str16z(const std::string &value) {
        if (value.empty()) {
            u16(0);
            return;
        }
        const uint16_t size = (uint16_t)std::min<size_t>(value.size() + 1, 65535);
        u16(size);
        bytes.insert(bytes.end(), value.begin(), value.begin() + (size - 1));
        u8(0);
    }
    // Always NUL-terminated, even when empty (file names).
    void str16n(const std::string &value) {
        const uint16_t size = (uint16_t)std::min<size_t>(value.size() + 1, 65535);
        u16(size);
        bytes.insert(bytes.end(), value.begin(), value.begin() + (size - 1));
        u8(0);
    }
    // NUL-terminated when not empty; an empty string is size 0.
    void str32z(const std::string &value) {
        if (value.empty()) {
            u32(0);
            return;
        }
        str32(value);
    }
    // Always NUL-terminated (an empty string is size 1).
    void str32(const std::string &value) {
        u32((uint32_t)value.size() + 1);
        bytes.insert(bytes.end(), value.begin(), value.end());
        u8(0);
    }
    void raw(const std::string &value) { bytes.insert(bytes.end(), value.begin(), value.end()); }
};

bool deflateRaw(const std::vector<uint8_t> &in, std::vector<uint8_t> &out) {
    z_stream stream{};
    if (deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, -15, 9, Z_DEFAULT_STRATEGY) != Z_OK)
        return false;
    out.resize(deflateBound(&stream, (uLong)in.size()));
    stream.next_in = const_cast<Bytef *>(in.data());
    stream.avail_in = (uInt)in.size();
    stream.next_out = out.data();
    stream.avail_out = (uInt)out.size();
    const int result = deflate(&stream, Z_FINISH);
    out.resize(stream.total_out);
    deflateEnd(&stream);
    return result == Z_STREAM_END;
}

} // namespace

bool Scenario::save(std::vector<uint8_t> &scx, std::string *err) const {
    constexpr float kPlayerDataVersion = 1.22f;
    ByteWriter body;
    body.u32(nextUnitId);
    body.f32(kPlayerDataVersion);
    for (const ScenarioPlayer &player : players) body.fixed(player.name, 256);
    for (const ScenarioPlayer &player : players) body.i32(player.nameStringId);
    for (size_t i = 0; i < kPlayers; i++) {
        const ScenarioPlayer &player = players[i];
        body.u32(player.active ? 1 : 0);
        body.u32(player.human ? 1 : 0);
        body.u32(player.civilization);
        body.u32(4); // CTY_A? (constant 4 in the editor's saves)
    }
    body.u8(playerDataByte);
    body.u16(0); // timeline: no entries
    body.u16(0);
    body.f32(timelineValue);
    body.str16(originalFilename);
    for (int32_t id : messageStringIds) body.i32(id);
    {
        const std::string *messages[10] = {&instructions,     &hints,           &victoryMessage,
                                           &lossMessage,      &history,         &scouts,
                                           &pregameCinematic, &victoryCinematic, &lossCinematic,
                                           &background};
        for (int i = 0; i < 10; ++i) {
            if (messageTerminatedMask & (1u << i)) body.str16n(*messages[i]);
            else body.str16(*messages[i]);
        }
    }
    body.u32(0); // no bitmap
    body.u32(0);
    body.u32(0);
    body.i16(1);
    for (const ScenarioPlayer &player : players) body.str16(player.aiName);
    for (const ScenarioPlayer &player : players) body.str16(player.cityName);
    for (const ScenarioPlayer &player : players) body.str16(player.personalityName);
    for (const ScenarioPlayer &player : players) {
        body.u32((uint32_t)player.aiFilename.size());
        body.u32((uint32_t)player.cityFilename.size());
        body.u32((uint32_t)player.personality.size());
        body.raw(player.aiFilename);
        body.raw(player.cityFilename);
        body.raw(player.personality);
    }
    for (const ScenarioPlayer &player : players) body.u8(player.aiType);
    body.u32(kSeparator);
    for (const ScenarioPlayer &player : players)
        for (size_t field = 0; field < 6; ++field) body.u32(player.primaryResources[field]);
    body.u32(kSeparator);
    body.u32(victory.conquestRequired ? 1 : 0);
    body.u32(0);
    body.u32(victory.requiredHolocrons);
    body.u32(0);
    body.u32(victory.requiredExploredPercent);
    body.u32(0);
    body.u32(victory.allConditionsRequired ? 1 : 0);
    body.u32(victory.mode);
    body.u32(victory.requiredScore);
    body.u32(victory.timeLimit);
    for (const ScenarioPlayer &player : players)
        for (uint32_t stance : player.primaryDiplomacy) body.u32(stance);
    body.zeros(kPlayers * 180 * 4); // individual victory conditions (unused)
    body.u32(kSeparator);
    for (const ScenarioPlayer &player : players) body.u32(player.alliedVictory ? 1 : 0);
    auto fixedIds = [&](std::vector<uint32_t> ScenarioPlayer::*member, size_t slots) {
        for (const ScenarioPlayer &player : players)
            body.u32((uint32_t)std::min((player.*member).size(), slots));
        for (const ScenarioPlayer &player : players)
            for (size_t slot = 0; slot < slots; ++slot)
                body.u32(slot < (player.*member).size() ? (player.*member)[slot] : 0xFFFFFFFFu);
    };
    fixedIds(&ScenarioPlayer::disabledTechnologies, 30);
    fixedIds(&ScenarioPlayer::disabledUnits, 30);
    fixedIds(&ScenarioPlayer::disabledBuildings, 20);
    body.u32(0);
    body.u32(0);
    body.u32(allTechnologies ? 1 : 0);
    for (const ScenarioPlayer &player : players) body.i32(player.startingAge);
    body.u32(kSeparator);
    body.i32(headerCameraX);
    body.i32(headerCameraY);
    body.i32(headerCameraExtra);
    // Map.
    body.u32(map.width);
    body.u32(map.height);
    if (map.tiles.size() != (size_t)map.width * map.height) {
        if (err) *err = "scenario map size does not match its tiles";
        return false;
    }
    for (const ScenarioTile &tile : map.tiles) {
        body.u8(tile.terrain);
        body.u8(tile.elevation);
        body.u8(0);
    }
    // Objects: gaia and players 1-8.
    constexpr uint32_t kBlocks = 9;
    body.u32(kBlocks);
    for (size_t player = 0; player < kPlayablePlayers; ++player) {
        for (size_t resource = 0; resource < 5; ++resource)
            body.f32(players[player].resources[resource]);
        body.f32(players[player].populationLimit);
    }
    for (uint32_t block = 0; block < kBlocks; ++block) {
        uint32_t count = 0;
        for (const ScenarioUnit &unit : units) count += unit.player == block ? 1 : 0;
        body.u32(count);
        for (const ScenarioUnit &unit : units) {
            if (unit.player != block) continue;
            body.f32(unit.x);
            body.f32(unit.y);
            body.f32(unit.z);
            body.u32(unit.spawnId);
            body.u16(unit.unitId);
            body.u8(unit.state);
            body.f32(unit.rotation);
            body.u16(unit.initialFrame);
            body.i32(unit.garrisonedInId);
        }
    }
    body.u32(kBlocks);
    for (size_t player = 0; player + 1 < kBlocks; ++player) {
        const ScenarioPlayer &state = players[player];
        body.str16z(state.recordName);
        body.f32(state.cameraX);
        body.f32(state.cameraY);
        body.i16(state.recordView[0]);
        body.i16(state.recordView[1]);
        body.u8(state.alliedVictory ? 1 : 0);
        body.i16((int16_t)kBlocks);
        for (uint32_t i = 0; i < kBlocks; ++i) body.u8((uint8_t)state.diplomacy[i]);
        for (uint32_t i = 0; i < kBlocks; ++i) body.u32(state.recordDiplomacy[i]);
        body.i32((int32_t)state.color);
        if (!state.recordTail.empty()) {
            body.bytes.insert(body.bytes.end(), state.recordTail.begin(), state.recordTail.end());
        } else {
            body.f32(2.0f);
            body.i16(0);
            body.zeros(8);
            body.zeros(7);
            body.i32(-1);
        }
    }
    // Triggers.
    body.put<double>(triggerSystemVersion > 0 ? triggerSystemVersion : 1.6);
    body.u8(objectiveState);
    body.u32((uint32_t)triggers.size());
    for (const ScenarioTrigger &trigger : triggers) {
        body.i32(trigger.enabled ? 1 : 0);
        body.u8(trigger.looping ? 1 : 0);
        body.i32(trigger.descriptionStringId);
        body.u8(trigger.objective ? 1 : 0);
        body.i32(trigger.objectiveOrder);
        body.i32(trigger.objectiveStringId);
        body.str32(trigger.description);
        body.str32(trigger.name);
        body.i32((int32_t)trigger.effects.size());
        for (const ScenarioEffect &effect : trigger.effects) {
            body.i32(effect.type);
            std::vector<int32_t> fields = effect.fields;
            if (fields.size() > 4 && !effect.selectedUnitIds.empty())
                fields[4] = (int32_t)effect.selectedUnitIds.size();
            body.i32((int32_t)fields.size());
            for (int32_t field : fields) body.i32(field);
            if (effect.message.empty() && effect.emptyMessageTerminated) body.str32(effect.message);
            else body.str32z(effect.message);
            if (effect.sound.empty() && effect.emptySoundTerminated) body.str32(effect.sound);
            else body.str32z(effect.sound);
            for (uint32_t id : effect.selectedUnitIds) body.u32(id);
        }
        for (size_t i = 0; i < trigger.effects.size(); ++i)
            body.i32(i < trigger.effectOrder.size() ? trigger.effectOrder[i] : (int32_t)i);
        body.i32((int32_t)trigger.conditions.size());
        for (const ScenarioCondition &condition : trigger.conditions) {
            body.i32(condition.type);
            body.i32((int32_t)condition.fields.size());
            for (int32_t field : condition.fields) body.i32(field);
        }
        for (size_t i = 0; i < trigger.conditions.size(); ++i)
            body.i32(i < trigger.conditionOrder.size() ? trigger.conditionOrder[i] : (int32_t)i);
    }
    for (size_t i = 0; i < triggers.size(); ++i)
        body.u32(i < triggerOrder.size() ? triggerOrder[i] : (uint32_t)i);
    // Included files (embedded AI scripts) and the compatibility block.
    body.i32(includedFiles.empty() ? 0 : 1);
    body.i32(compatibilityBlock.size() == 396 ? 1 : 0);
    if (compatibilityBlock.size() == 396)
        body.bytes.insert(body.bytes.end(), compatibilityBlock.begin(), compatibilityBlock.end());
    if (!includedFiles.empty()) {
        body.i32((int32_t)includedFiles.size());
        for (const IncludedFile &file : includedFiles) {
            body.str32(file.name);
            body.u32((uint32_t)file.data.size());
            body.raw(file.data);
        }
    }

    std::vector<uint8_t> compressed;
    if (!deflateRaw(body.bytes, compressed)) {
        if (err) *err = "could not compress the scenario";
        return false;
    }
    ByteWriter header;
    header.i32(saveType ? saveType : 2);
    header.u32(lastSaveTime);
    header.str32(instructions);
    header.i32(victoryType);
    header.u32(enabledPlayerCount);
    ByteWriter file;
    file.raw("1.21");
    file.u32((uint32_t)header.bytes.size());
    file.bytes.insert(file.bytes.end(), header.bytes.begin(), header.bytes.end());
    file.bytes.insert(file.bytes.end(), compressed.begin(), compressed.end());
    scx = std::move(file.bytes);
    return true;
}

} // namespace swgb
