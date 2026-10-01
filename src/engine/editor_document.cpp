// SPDX-License-Identifier: GPL-3.0-or-later
#include "editor_document.h"

#include "assets.h"
#include "game.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace swgb {
namespace {

constexpr uint8_t kMagic[8] = {'S', 'W', 'G', 'S', 'C', 'E', 'N', '\0'};
constexpr size_t kHeaderBytes = 8 + 4 + 8 + 8;
constexpr uint64_t kFnvOffset = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

class DocumentError : public std::runtime_error {
public:
    explicit DocumentError(const std::string &message) : std::runtime_error(message) {}
};

void setError(std::string *error, const std::string &message) {
    if (error) *error = message;
}

uint64_t fnv1a(const uint8_t *data, size_t size) {
    uint64_t hash = kFnvOffset;
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= kFnvPrime;
    }
    return hash;
}

uint64_t fnv1a(const std::vector<uint8_t> &bytes) {
    return fnv1a(bytes.data(), bytes.size());
}

class Writer {
public:
    std::vector<uint8_t> bytes;

    void u8(uint8_t value) { bytes.push_back(value); }
    void boolean(bool value) { u8(value ? 1u : 0u); }
    void u16(uint16_t value) {
        u8((uint8_t)value);
        u8((uint8_t)(value >> 8));
    }
    void u32(uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8)
            u8((uint8_t)(value >> shift));
    }
    void i32(int32_t value) { u32((uint32_t)value); }
    void u64(uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8)
            u8((uint8_t)(value >> shift));
    }
    void i64(int64_t value) { u64((uint64_t)value); }
    void f32(float value) {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        u32(bits);
    }
    void f64(double value) {
        uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        u64(bits);
    }
    void count(size_t value) {
        if (value > std::numeric_limits<uint32_t>::max())
            throw DocumentError("collection is too large to encode");
        u32((uint32_t)value);
    }
    void string(const std::string &value) {
        count(value.size());
        bytes.insert(bytes.end(), value.begin(), value.end());
    }
    void blob(const std::vector<uint8_t> &value) {
        count(value.size());
        bytes.insert(bytes.end(), value.begin(), value.end());
    }
};

class Reader {
public:
    Reader(const uint8_t *data, size_t size, const NativeScenarioLimits &limits)
        : data_(data), size_(size), limits_(limits) {}

    size_t remaining() const { return size_ - position_; }
    size_t position() const { return position_; }
    const NativeScenarioLimits &limits() const { return limits_; }

    uint8_t u8() {
        require(1, "byte");
        return data_[position_++];
    }
    bool boolean() {
        const uint8_t value = u8();
        if (value > 1) throw DocumentError("invalid boolean value");
        return value != 0;
    }
    uint16_t u16() {
        uint16_t value = 0;
        for (unsigned shift = 0; shift < 16; shift += 8)
            value |= (uint16_t)u8() << shift;
        return value;
    }
    uint32_t u32() {
        uint32_t value = 0;
        for (unsigned shift = 0; shift < 32; shift += 8)
            value |= (uint32_t)u8() << shift;
        return value;
    }
    int32_t i32() { return (int32_t)u32(); }
    uint64_t u64() {
        uint64_t value = 0;
        for (unsigned shift = 0; shift < 64; shift += 8)
            value |= (uint64_t)u8() << shift;
        return value;
    }
    int64_t i64() { return (int64_t)u64(); }
    float f32() {
        const uint32_t bits = u32();
        float value = 0;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    double f64() {
        const uint64_t bits = u64();
        double value = 0;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    uint32_t count(const char *what, size_t cap = 0) {
        const uint32_t value = u32();
        const size_t effectiveCap = cap ? cap : limits_.maxCollectionEntries;
        if ((uint64_t)value > effectiveCap)
            throw DocumentError(std::string(what) + " count exceeds native limit");
        return value;
    }
    std::string string(const char *what) {
        const uint32_t size = count(what, limits_.maxStringBytes);
        require(size, what);
        const char *begin = reinterpret_cast<const char *>(data_ + position_);
        position_ += size;
        return std::string(begin, begin + size);
    }
    std::vector<uint8_t> blob(const char *what, size_t cap) {
        const uint32_t size = count(what, cap);
        require(size, what);
        std::vector<uint8_t> result(data_ + position_, data_ + position_ + size);
        position_ += size;
        return result;
    }

private:
    void require(size_t bytes, const char *what) const {
        if (bytes > size_ - position_)
            throw DocumentError(std::string("truncated native ") + what);
    }

    const uint8_t *data_;
    size_t size_;
    size_t position_ = 0;
    const NativeScenarioLimits &limits_;
};

void writeU32Vector(Writer &writer, const std::vector<uint32_t> &values) {
    writer.count(values.size());
    for (uint32_t value : values) writer.u32(value);
}

void writeI32Vector(Writer &writer, const std::vector<int32_t> &values) {
    writer.count(values.size());
    for (int32_t value : values) writer.i32(value);
}

std::vector<uint32_t> readU32Vector(Reader &reader, const char *what) {
    const uint32_t count = reader.count(what);
    std::vector<uint32_t> values;
    values.reserve(count);
    for (uint32_t i = 0; i < count; ++i) values.push_back(reader.u32());
    return values;
}

std::vector<int32_t> readI32Vector(Reader &reader, const char *what) {
    const uint32_t count = reader.count(what);
    std::vector<int32_t> values;
    values.reserve(count);
    for (uint32_t i = 0; i < count; ++i) values.push_back(reader.i32());
    return values;
}

void writeScenarioPlayer(Writer &writer, const ScenarioPlayer &player) {
    writer.string(player.name);
    writer.string(player.aiName);
    writer.string(player.cityName);
    writer.string(player.personalityName);
    writer.string(player.aiFilename);
    writer.string(player.cityFilename);
    writer.string(player.personality);
    writer.u32(player.civilization);
    writer.u32(player.color);
    writer.u8(player.aiType);
    writer.boolean(player.active);
    writer.boolean(player.human);
    writer.boolean(player.alliedVictory);
    writer.f32(player.cameraX);
    writer.f32(player.cameraY);
    for (float value : player.resources) writer.f32(value);
    for (uint32_t value : player.diplomacy) writer.u32(value);
    writeU32Vector(writer, player.disabledTechnologies);
    writeU32Vector(writer, player.disabledUnits);
    writeU32Vector(writer, player.disabledBuildings);
    writer.i32(player.startingAge);
    writer.f32(player.populationLimit);
}

ScenarioPlayer readScenarioPlayer(Reader &reader) {
    ScenarioPlayer player;
    player.name = reader.string("player name");
    player.aiName = reader.string("AI name");
    player.cityName = reader.string("city name");
    player.personalityName = reader.string("personality name");
    player.aiFilename = reader.string("AI filename");
    player.cityFilename = reader.string("city filename");
    player.personality = reader.string("personality");
    player.civilization = reader.u32();
    player.color = reader.u32();
    player.aiType = reader.u8();
    player.active = reader.boolean();
    player.human = reader.boolean();
    player.alliedVictory = reader.boolean();
    player.cameraX = reader.f32();
    player.cameraY = reader.f32();
    for (float &value : player.resources) value = reader.f32();
    for (uint32_t &value : player.diplomacy) value = reader.u32();
    player.disabledTechnologies = readU32Vector(reader, "disabled technologies");
    player.disabledUnits = readU32Vector(reader, "disabled units");
    player.disabledBuildings = readU32Vector(reader, "disabled buildings");
    player.startingAge = reader.i32();
    player.populationLimit = reader.f32();
    return player;
}

void writeCondition(Writer &writer, const EditorTriggerCondition &condition) {
    writer.i32(condition.scenario.type);
    writeI32Vector(writer, condition.scenario.fields);
    writer.boolean(condition.supported);
    writer.boolean(condition.readOnlyUnknown);
    writer.u32(condition.referencedAreaId);
}

EditorTriggerCondition readCondition(Reader &reader) {
    EditorTriggerCondition condition;
    condition.scenario.type = reader.i32();
    condition.scenario.fields = readI32Vector(reader, "condition fields");
    condition.supported = reader.boolean();
    condition.readOnlyUnknown = reader.boolean();
    condition.referencedAreaId = reader.u32();
    return condition;
}

void writeEffect(Writer &writer, const EditorTriggerEffect &effect) {
    writer.i32(effect.scenario.type);
    writeI32Vector(writer, effect.scenario.fields);
    writer.string(effect.scenario.message);
    writer.string(effect.scenario.sound);
    writeU32Vector(writer, effect.scenario.selectedUnitIds);
    writer.boolean(effect.supported);
    writer.boolean(effect.readOnlyUnknown);
    writer.u32(effect.referencedAreaId);
}

EditorTriggerEffect readEffect(Reader &reader) {
    EditorTriggerEffect effect;
    effect.scenario.type = reader.i32();
    effect.scenario.fields = readI32Vector(reader, "effect fields");
    effect.scenario.message = reader.string("effect message");
    effect.scenario.sound = reader.string("effect sound");
    effect.scenario.selectedUnitIds = readU32Vector(reader, "selected object IDs");
    effect.supported = reader.boolean();
    effect.readOnlyUnknown = reader.boolean();
    effect.referencedAreaId = reader.u32();
    return effect;
}

void writeVictory(Writer &writer, const EditorVictory &victory) {
    writer.boolean(victory.scenario.conquestRequired);
    writer.u32(victory.scenario.requiredHolocrons);
    writer.u32(victory.scenario.requiredExploredPercent);
    writer.boolean(victory.scenario.allConditionsRequired);
    writer.u32(victory.scenario.mode);
    writer.u32(victory.scenario.requiredScore);
    writer.u32(victory.scenario.timeLimit);
    writer.i32(victory.type);
}

EditorVictory readVictory(Reader &reader) {
    EditorVictory victory;
    victory.scenario.conquestRequired = reader.boolean();
    victory.scenario.requiredHolocrons = reader.u32();
    victory.scenario.requiredExploredPercent = reader.u32();
    victory.scenario.allConditionsRequired = reader.boolean();
    victory.scenario.mode = reader.u32();
    victory.scenario.requiredScore = reader.u32();
    victory.scenario.timeLimit = reader.u32();
    victory.type = reader.i32();
    return victory;
}

void writeDocumentPayload(
    Writer &writer,
    const EditableScenarioDocument &document,
    bool includeImportSource) {
    writer.u32(document.documentVersion);
    writer.string(includeImportSource
                      ? document.metadata.title
                      : std::string());
    writer.string(includeImportSource
                      ? document.metadata.author
                      : std::string());
    writer.string(includeImportSource
                      ? document.metadata.description
                      : std::string());
    writer.string(includeImportSource
                      ? document.metadata.templateName
                      : std::string());
    writer.string(includeImportSource
                      ? document.metadata.sourceName
                      : std::string());
    writer.i64(includeImportSource
                   ? document.metadata.createdTimestamp
                   : 0);
    writer.i64(includeImportSource
                   ? document.metadata.modifiedTimestamp
                   : 0);
    writer.i64(includeImportSource
                   ? document.metadata.sourceTimestamp
                   : 0);
    writer.u32(includeImportSource
                   ? document.metadata.editorVersion
                   : 0);

    writer.u32(document.map.width);
    writer.u32(document.map.height);
    writer.count(document.map.tiles.size());
    for (const ScenarioTile &tile : document.map.tiles) {
        writer.u8(tile.terrain);
        writer.u8(tile.elevation);
    }

    writer.count(document.players.size());
    for (const EditorPlayer &player : document.players) {
        writeScenarioPlayer(writer, player.scenario);
        writer.i32(player.team);
        writer.i32(player.difficulty);
        writeU32Vector(writer, player.researchedTechnologies);
        writeU32Vector(writer, player.researchedUnits);
        writeU32Vector(writer, player.researchedBuildings);
    }

    writer.count(document.objects.size());
    for (const EditorObject &object : document.objects) {
        writer.f32(object.scenario.x);
        writer.f32(object.scenario.y);
        writer.f32(object.scenario.z);
        writer.u32(object.scenario.spawnId);
        writer.u16(object.scenario.unitId);
        writer.u8(object.scenario.state);
        writer.f32(object.scenario.rotation);
        writer.u16(object.scenario.initialFrame);
        writer.i32(object.scenario.garrisonedInId);
        writer.u8(object.scenario.player);
        writer.f32(object.hitPoints);
        writer.f32(object.resourceAmount);
        writer.count(object.initialOrders.size());
        for (const EditorInitialOrder &order : object.initialOrders) {
            writer.i32(order.type);
            writer.u32(order.targetSpawnId);
            writer.f32(order.targetX);
            writer.f32(order.targetY);
            writer.i32(order.argument);
        }
        writer.string(object.customName);
    }

    writer.count(document.triggers.size());
    for (const EditorTrigger &trigger : document.triggers) {
        writer.boolean(trigger.enabled);
        writer.boolean(trigger.looping);
        writer.boolean(trigger.objective);
        writer.i32(trigger.objectiveOrder);
        writer.i32(trigger.objectiveStringId);
        writer.string(trigger.description);
        writer.string(trigger.name);
        writer.count(trigger.effects.size());
        for (const EditorTriggerEffect &effect : trigger.effects)
            writeEffect(writer, effect);
        writeI32Vector(writer, trigger.effectOrder);
        writer.count(trigger.conditions.size());
        for (const EditorTriggerCondition &condition : trigger.conditions)
            writeCondition(writer, condition);
        writeI32Vector(writer, trigger.conditionOrder);
    }
    writeU32Vector(writer, document.triggerOrder);

    writer.string(document.messages.instructions);
    writer.string(document.messages.objectives);
    writer.string(document.messages.hints);
    writer.string(document.messages.victory);
    writer.string(document.messages.loss);
    writer.string(document.messages.history);
    writer.string(document.messages.scouts);
    writer.string(document.messages.pregameCinematic);
    writer.string(document.messages.victoryCinematic);
    writer.string(document.messages.lossCinematic);
    writer.string(document.messages.background);
    writer.f32(document.camera.x);
    writer.f32(document.camera.y);
    writer.f32(document.camera.mapX);
    writer.f32(document.camera.mapY);
    writeVictory(writer, document.victory);

    writer.count(document.areas.size());
    for (const EditorArea &area : document.areas) {
        writer.u32(area.id);
        writer.string(area.name);
        writer.i32(area.left);
        writer.i32(area.top);
        writer.i32(area.right);
        writer.i32(area.bottom);
    }

    writer.count(document.ai.size());
    for (const EditorAiMetadata &ai : document.ai) {
        writer.u32(ai.player);
        writer.u8((uint8_t)ai.storage);
        writer.string(ai.name);
        writer.string(ai.path);
        writer.string(ai.cityName);
        writer.string(ai.personalityName);
        writer.string(ai.script);
        writer.string(ai.city);
        writer.string(ai.personality);
    }

    writer.string(document.scx.version);
    writer.i32(document.scx.saveType);
    writer.u32(document.scx.lastSaveTime);
    writer.u32(document.scx.enabledPlayerCount);
    writer.u32(document.scx.nextUnitId);
    writer.f32(document.scx.playerDataVersion);
    writer.string(document.scx.originalFilename);
    writer.boolean(document.scx.allTechnologies);
    writer.f64(document.scx.triggerSystemVersion);
    writer.u8(document.scx.objectiveState);
    for (uint32_t civilization : document.scx.civilizations)
        writer.u32(civilization);
    for (const ScenarioPlayer &player : document.scx.auxiliaryPlayers)
        writeScenarioPlayer(writer, player);

    writer.count(
        includeImportSource
            ? document.unknownRecords.size()
            : 0);
    if (includeImportSource) {
        for (const EditorOpaqueRecord &record :
             document.unknownRecords) {
            writer.u32(record.type);
            writer.blob(record.bytes);
        }
    }

    writer.boolean(includeImportSource && document.hasImportFingerprint);
    writer.u64(includeImportSource ? document.importSemanticFingerprint : 0);
    if (includeImportSource)
        writer.blob(document.originalScxBytes);
    else
        writer.blob({});
}

EditableScenarioDocument readDocumentPayload(Reader &reader) {
    EditableScenarioDocument document;
    document.documentVersion = reader.u32();
    if (document.documentVersion != kEditableScenarioDocumentVersion)
        throw DocumentError(
            "unsupported editable document version " +
            std::to_string(document.documentVersion));

    document.metadata.title = reader.string("metadata title");
    document.metadata.author = reader.string("metadata author");
    document.metadata.description = reader.string("metadata description");
    document.metadata.templateName = reader.string("metadata template");
    document.metadata.sourceName = reader.string("metadata source");
    document.metadata.createdTimestamp = reader.i64();
    document.metadata.modifiedTimestamp = reader.i64();
    document.metadata.sourceTimestamp = reader.i64();
    document.metadata.editorVersion = reader.u32();

    document.map.width = reader.u32();
    document.map.height = reader.u32();
    const uint32_t tileCount =
        reader.count("map tiles", (size_t)reader.limits().maxMapTiles);
    document.map.tiles.reserve(tileCount);
    for (uint32_t i = 0; i < tileCount; ++i) {
        ScenarioTile tile;
        tile.terrain = reader.u8();
        tile.elevation = reader.u8();
        document.map.tiles.push_back(tile);
    }

    const uint32_t playerCount = reader.count("editor players", 8);
    document.players.reserve(playerCount);
    for (uint32_t i = 0; i < playerCount; ++i) {
        EditorPlayer player;
        player.scenario = readScenarioPlayer(reader);
        player.team = reader.i32();
        player.difficulty = reader.i32();
        player.researchedTechnologies =
            readU32Vector(reader, "researched technologies");
        player.researchedUnits = readU32Vector(reader, "researched units");
        player.researchedBuildings =
            readU32Vector(reader, "researched buildings");
        document.players.push_back(std::move(player));
    }

    const uint32_t objectCount = reader.count("editor objects");
    document.objects.reserve(objectCount);
    for (uint32_t i = 0; i < objectCount; ++i) {
        EditorObject object;
        object.scenario.x = reader.f32();
        object.scenario.y = reader.f32();
        object.scenario.z = reader.f32();
        object.scenario.spawnId = reader.u32();
        object.scenario.unitId = reader.u16();
        object.scenario.state = reader.u8();
        object.scenario.rotation = reader.f32();
        object.scenario.initialFrame = reader.u16();
        object.scenario.garrisonedInId = reader.i32();
        object.scenario.player = reader.u8();
        object.hitPoints = reader.f32();
        object.resourceAmount = reader.f32();
        const uint32_t orderCount = reader.count("initial orders");
        object.initialOrders.reserve(orderCount);
        for (uint32_t j = 0; j < orderCount; ++j) {
            EditorInitialOrder order;
            order.type = reader.i32();
            order.targetSpawnId = reader.u32();
            order.targetX = reader.f32();
            order.targetY = reader.f32();
            order.argument = reader.i32();
            object.initialOrders.push_back(order);
        }
        object.customName = reader.string("object custom name");
        document.objects.push_back(std::move(object));
    }

    const uint32_t triggerCount = reader.count("triggers");
    document.triggers.reserve(triggerCount);
    for (uint32_t i = 0; i < triggerCount; ++i) {
        EditorTrigger trigger;
        trigger.enabled = reader.boolean();
        trigger.looping = reader.boolean();
        trigger.objective = reader.boolean();
        trigger.objectiveOrder = reader.i32();
        trigger.objectiveStringId = reader.i32();
        trigger.description = reader.string("trigger description");
        trigger.name = reader.string("trigger name");
        const uint32_t effectCount = reader.count("trigger effects");
        trigger.effects.reserve(effectCount);
        for (uint32_t j = 0; j < effectCount; ++j)
            trigger.effects.push_back(readEffect(reader));
        trigger.effectOrder = readI32Vector(reader, "effect order");
        const uint32_t conditionCount = reader.count("trigger conditions");
        trigger.conditions.reserve(conditionCount);
        for (uint32_t j = 0; j < conditionCount; ++j)
            trigger.conditions.push_back(readCondition(reader));
        trigger.conditionOrder = readI32Vector(reader, "condition order");
        document.triggers.push_back(std::move(trigger));
    }
    document.triggerOrder = readU32Vector(reader, "trigger order");

    document.messages.instructions = reader.string("instructions");
    document.messages.objectives = reader.string("objectives");
    document.messages.hints = reader.string("hints");
    document.messages.victory = reader.string("victory message");
    document.messages.loss = reader.string("loss message");
    document.messages.history = reader.string("history");
    document.messages.scouts = reader.string("scouts");
    document.messages.pregameCinematic = reader.string("pregame cinematic");
    document.messages.victoryCinematic = reader.string("victory cinematic");
    document.messages.lossCinematic = reader.string("loss cinematic");
    document.messages.background = reader.string("background");
    document.camera.x = reader.f32();
    document.camera.y = reader.f32();
    document.camera.mapX = reader.f32();
    document.camera.mapY = reader.f32();
    document.victory = readVictory(reader);

    const uint32_t areaCount = reader.count("areas");
    document.areas.reserve(areaCount);
    for (uint32_t i = 0; i < areaCount; ++i) {
        EditorArea area;
        area.id = reader.u32();
        area.name = reader.string("area name");
        area.left = reader.i32();
        area.top = reader.i32();
        area.right = reader.i32();
        area.bottom = reader.i32();
        document.areas.push_back(std::move(area));
    }

    const uint32_t aiCount = reader.count("AI metadata", 8);
    document.ai.reserve(aiCount);
    for (uint32_t i = 0; i < aiCount; ++i) {
        EditorAiMetadata ai;
        ai.player = reader.u32();
        const uint8_t storage = reader.u8();
        if (storage > (uint8_t)EditorAiStorage::Embedded)
            throw DocumentError("invalid AI storage kind");
        ai.storage = (EditorAiStorage)storage;
        ai.name = reader.string("AI metadata name");
        ai.path = reader.string("AI reference path");
        ai.cityName = reader.string("AI city name");
        ai.personalityName = reader.string("AI personality name");
        ai.script = reader.string("embedded AI script");
        ai.city = reader.string("embedded city script");
        ai.personality = reader.string("embedded personality");
        document.ai.push_back(std::move(ai));
    }

    document.scx.version = reader.string("SCX version");
    document.scx.saveType = reader.i32();
    document.scx.lastSaveTime = reader.u32();
    document.scx.enabledPlayerCount = reader.u32();
    document.scx.nextUnitId = reader.u32();
    document.scx.playerDataVersion = reader.f32();
    document.scx.originalFilename = reader.string("original filename");
    document.scx.allTechnologies = reader.boolean();
    document.scx.triggerSystemVersion = reader.f64();
    document.scx.objectiveState = reader.u8();
    for (uint32_t &civilization : document.scx.civilizations)
        civilization = reader.u32();
    for (ScenarioPlayer &player : document.scx.auxiliaryPlayers)
        player = readScenarioPlayer(reader);

    const uint32_t opaqueCount = reader.count("opaque records");
    document.unknownRecords.reserve(opaqueCount);
    for (uint32_t i = 0; i < opaqueCount; ++i) {
        EditorOpaqueRecord record;
        record.type = reader.u32();
        record.bytes = reader.blob(
            "opaque record", reader.limits().maxOpaqueRecordBytes);
        document.unknownRecords.push_back(std::move(record));
    }

    document.hasImportFingerprint = reader.boolean();
    document.importSemanticFingerprint = reader.u64();
    document.originalScxBytes = reader.blob(
        "original SCX", reader.limits().maxOriginalScxBytes);
    if (!document.hasImportFingerprint &&
        (!document.originalScxBytes.empty() ||
         document.importSemanticFingerprint != 0))
        throw DocumentError("native document has inconsistent SCX import state");
    return document;
}

bool isFinite(float value) { return std::isfinite(value); }

void checkString(
    const std::string &value,
    const char *what,
    const NativeScenarioLimits &limits) {
    if (value.size() > limits.maxStringBytes)
        throw DocumentError(std::string(what) + " exceeds native string limit");
}

template <typename T>
void checkCollection(
    const std::vector<T> &values,
    const char *what,
    const NativeScenarioLimits &limits) {
    if (values.size() > limits.maxCollectionEntries)
        throw DocumentError(std::string(what) + " exceeds native collection limit");
}

void checkPlayerBounds(
    const ScenarioPlayer &player,
    const NativeScenarioLimits &limits) {
    checkString(player.name, "player name", limits);
    checkString(player.aiName, "AI name", limits);
    checkString(player.cityName, "city name", limits);
    checkString(player.personalityName, "personality name", limits);
    checkString(player.aiFilename, "AI filename", limits);
    checkString(player.cityFilename, "city filename", limits);
    checkString(player.personality, "personality", limits);
    checkCollection(
        player.disabledTechnologies, "disabled technologies", limits);
    checkCollection(player.disabledUnits, "disabled units", limits);
    checkCollection(player.disabledBuildings, "disabled buildings", limits);
}

void checkDocumentBounds(
    const EditableScenarioDocument &document,
    const NativeScenarioLimits &limits) {
    if (!limits.maxFileBytes || !limits.maxPayloadBytes ||
        limits.maxFileBytes < kHeaderBytes)
        throw DocumentError("invalid native storage limits");
    if (document.documentVersion != kEditableScenarioDocumentVersion)
        throw DocumentError("unsupported editable document version");
    if (document.map.width > limits.maxMapDimension ||
        document.map.height > limits.maxMapDimension)
        throw DocumentError("map dimensions exceed native limit");
    const uint64_t expected =
        (uint64_t)document.map.width * document.map.height;
    if (expected > limits.maxMapTiles ||
        document.map.tiles.size() > limits.maxMapTiles)
        throw DocumentError("map tile count exceeds native limit");
    if (expected != document.map.tiles.size())
        throw DocumentError(
            "map tile count does not match map dimensions");
    if (document.players.size() > 8)
        throw DocumentError("editor player count exceeds 8");
    checkCollection(document.objects, "objects", limits);
    checkCollection(document.triggers, "triggers", limits);
    checkCollection(document.areas, "areas", limits);
    if (document.ai.size() > 8)
        throw DocumentError("AI metadata count exceeds 8");
    checkCollection(document.unknownRecords, "opaque records", limits);
    if (document.originalScxBytes.size() > limits.maxOriginalScxBytes)
        throw DocumentError("original SCX exceeds native limit");

    checkString(document.metadata.title, "metadata title", limits);
    checkString(document.metadata.author, "metadata author", limits);
    checkString(document.metadata.description, "metadata description", limits);
    checkString(document.metadata.templateName, "metadata template", limits);
    checkString(document.metadata.sourceName, "metadata source", limits);
    for (const EditorPlayer &player : document.players) {
        checkPlayerBounds(player.scenario, limits);
        checkCollection(
            player.researchedTechnologies, "researched technologies", limits);
        checkCollection(player.researchedUnits, "researched units", limits);
        checkCollection(
            player.researchedBuildings, "researched buildings", limits);
    }
    for (const EditorObject &object : document.objects) {
        checkCollection(object.initialOrders, "initial orders", limits);
        checkString(object.customName, "object custom name", limits);
    }
    for (const EditorTrigger &trigger : document.triggers) {
        checkString(trigger.description, "trigger description", limits);
        checkString(trigger.name, "trigger name", limits);
        checkCollection(trigger.effects, "trigger effects", limits);
        checkCollection(trigger.conditions, "trigger conditions", limits);
        checkCollection(trigger.effectOrder, "effect order", limits);
        checkCollection(trigger.conditionOrder, "condition order", limits);
        for (const EditorTriggerEffect &effect : trigger.effects) {
            checkCollection(effect.scenario.fields, "effect fields", limits);
            checkCollection(
                effect.scenario.selectedUnitIds, "selected object IDs", limits);
            checkString(effect.scenario.message, "effect message", limits);
            checkString(effect.scenario.sound, "effect sound", limits);
        }
        for (const EditorTriggerCondition &condition : trigger.conditions)
            checkCollection(condition.scenario.fields, "condition fields", limits);
    }
    checkCollection(document.triggerOrder, "trigger order", limits);
    const std::string *messages[] = {
        &document.messages.instructions, &document.messages.objectives,
        &document.messages.hints, &document.messages.victory,
        &document.messages.loss, &document.messages.history,
        &document.messages.scouts, &document.messages.pregameCinematic,
        &document.messages.victoryCinematic,
        &document.messages.lossCinematic, &document.messages.background,
    };
    for (const std::string *message : messages)
        checkString(*message, "scenario message", limits);
    for (const EditorArea &area : document.areas)
        checkString(area.name, "area name", limits);
    for (const EditorAiMetadata &ai : document.ai) {
        checkString(ai.name, "AI name", limits);
        checkString(ai.path, "AI path", limits);
        checkString(ai.cityName, "AI city name", limits);
        checkString(ai.personalityName, "AI personality name", limits);
        checkString(ai.script, "AI script", limits);
        checkString(ai.city, "AI city", limits);
        checkString(ai.personality, "AI personality", limits);
    }
    checkString(document.scx.version, "SCX version", limits);
    checkString(document.scx.originalFilename, "original filename", limits);
    for (const ScenarioPlayer &player : document.scx.auxiliaryPlayers)
        checkPlayerBounds(player, limits);
    for (const EditorOpaqueRecord &record : document.unknownRecords)
        if (record.bytes.size() > limits.maxOpaqueRecordBytes)
            throw DocumentError("opaque record exceeds native limit");
}

uint32_t getU32(const uint8_t *data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

uint64_t getU64(const uint8_t *data) {
    uint64_t value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8)
        value |= (uint64_t)data[shift / 8] << shift;
    return value;
}

bool readFileBounded(
    const std::string &path,
    size_t cap,
    std::vector<uint8_t> &bytes,
    std::string *error) {
    bytes.clear();
    FILE *file = std::fopen(path.c_str(), "rb");
    if (!file) {
        setError(
            error,
            "could not open '" + path + "': " + std::strerror(errno));
        return false;
    }
    if (std::fseek(file, 0, SEEK_END) != 0) {
        const std::string reason = std::strerror(errno);
        std::fclose(file);
        setError(error, "could not size '" + path + "': " + reason);
        return false;
    }
    const long end = std::ftell(file);
    if (end < 0) {
        const std::string reason = std::strerror(errno);
        std::fclose(file);
        setError(error, "could not size '" + path + "': " + reason);
        return false;
    }
    if ((uint64_t)end > cap) {
        std::fclose(file);
        setError(error, "file '" + path + "' exceeds the configured size limit");
        return false;
    }
    if (std::fseek(file, 0, SEEK_SET) != 0) {
        const std::string reason = std::strerror(errno);
        std::fclose(file);
        setError(error, "could not rewind '" + path + "': " + reason);
        return false;
    }
    bytes.resize((size_t)end);
    const size_t got =
        bytes.empty() ? 0 : std::fread(bytes.data(), 1, bytes.size(), file);
    const bool readOk = got == bytes.size() && std::ferror(file) == 0;
    const int closeResult = std::fclose(file);
    if (!readOk || closeResult != 0) {
        bytes.clear();
        setError(error, "could not read complete file '" + path + "'");
        return false;
    }
    return true;
}

bool fileExists(const std::string &path) {
    FILE *file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    std::fclose(file);
    return true;
}

bool writeAtomic(
    const std::string &path,
    const std::vector<uint8_t> &bytes,
    std::string *error) {
    if (path.empty()) {
        setError(error, "output path is empty");
        return false;
    }
    const std::string temporary = path + ".tmp";
    const std::string backup = path + ".bak";
    std::remove(temporary.c_str());
    FILE *file = std::fopen(temporary.c_str(), "wb");
    if (!file) {
        setError(
            error,
            "could not create temporary file '" + temporary + "': " +
                std::strerror(errno));
        return false;
    }
    const bool wrote =
        (bytes.empty() ||
         std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size()) &&
        std::fflush(file) == 0;
    const int closeResult = std::fclose(file);
    if (!wrote || closeResult != 0) {
        const std::string reason = std::strerror(errno);
        std::remove(temporary.c_str());
        setError(
            error,
            "could not write complete temporary file '" + temporary +
                "': " + reason);
        return false;
    }

    const bool hadOriginal = fileExists(path);
    if (hadOriginal) {
        std::remove(backup.c_str());
        if (std::rename(path.c_str(), backup.c_str()) != 0) {
            const std::string reason = std::strerror(errno);
            std::remove(temporary.c_str());
            setError(
                error,
                "could not create backup '" + backup + "': " + reason);
            return false;
        }
    }
    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
        const std::string replaceReason = std::strerror(errno);
        bool rolledBack = true;
        if (hadOriginal)
            rolledBack = std::rename(backup.c_str(), path.c_str()) == 0;
        std::remove(temporary.c_str());
        setError(
            error,
            "could not replace '" + path + "': " + replaceReason +
                (rolledBack ? "; original restored"
                            : "; rollback failed and backup remains at '" +
                                  backup + "'"));
        return false;
    }
    if (hadOriginal && std::remove(backup.c_str()) != 0) {
        setError(
            error,
            "saved '" + path + "', but could not remove backup '" + backup +
                "': " + std::strerror(errno));
        return false;
    }
    return true;
}

EditorTrigger importTrigger(const ScenarioTrigger &source) {
    EditorTrigger trigger;
    trigger.enabled = source.enabled;
    trigger.looping = source.looping;
    trigger.objective = source.objective;
    trigger.objectiveOrder = source.objectiveOrder;
    trigger.objectiveStringId = source.objectiveStringId;
    trigger.description = source.description;
    trigger.name = source.name;
    trigger.effectOrder = source.effectOrder;
    trigger.conditionOrder = source.conditionOrder;
    trigger.effects.reserve(source.effects.size());
    for (const ScenarioEffect &sourceEffect : source.effects) {
        EditorTriggerEffect effect;
        effect.scenario = sourceEffect;
        effect.supported = Game::supportsTriggerEffect(sourceEffect.type);
        effect.readOnlyUnknown = !effect.supported;
        trigger.effects.push_back(std::move(effect));
    }
    trigger.conditions.reserve(source.conditions.size());
    for (const ScenarioCondition &sourceCondition : source.conditions) {
        EditorTriggerCondition condition;
        condition.scenario = sourceCondition;
        condition.supported =
            Game::supportsTriggerCondition(sourceCondition.type);
        condition.readOnlyUnknown = !condition.supported;
        trigger.conditions.push_back(std::move(condition));
    }
    return trigger;
}

EditableScenarioDocument importScenario(
    const Scenario &scenario,
    const std::vector<uint8_t> &sourceBytes) {
    EditableScenarioDocument document;
    document.metadata.title =
        scenario.originalFilename.empty() ? "Imported Scenario"
                                          : scenario.originalFilename;
    document.metadata.sourceName = scenario.originalFilename;
    document.metadata.sourceTimestamp = scenario.lastSaveTime;
    document.map = scenario.map;
    document.players.reserve(8);
    for (size_t i = 0; i < 8; ++i) {
        EditorPlayer player;
        player.scenario = scenario.players[i];
        player.team = 0;
        player.difficulty = 2;
        document.players.push_back(std::move(player));
    }
    document.objects.reserve(scenario.units.size());
    for (const ScenarioUnit &unit : scenario.units) {
        EditorObject object;
        object.scenario = unit;
        document.objects.push_back(std::move(object));
    }
    document.triggers.reserve(scenario.triggers.size());
    for (const ScenarioTrigger &trigger : scenario.triggers)
        document.triggers.push_back(importTrigger(trigger));
    document.triggerOrder = scenario.triggerOrder;
    document.messages.instructions = scenario.instructions;
    document.messages.objectives = scenario.instructions;
    document.messages.hints = scenario.hints;
    document.messages.victory = scenario.victoryMessage;
    document.messages.loss = scenario.lossMessage;
    document.messages.history = scenario.history;
    document.messages.scouts = scenario.scouts;
    document.messages.pregameCinematic = scenario.pregameCinematic;
    document.messages.victoryCinematic = scenario.victoryCinematic;
    document.messages.lossCinematic = scenario.lossCinematic;
    document.messages.background = scenario.background;
    document.camera.x = scenario.cameraX;
    document.camera.y = scenario.cameraY;
    document.camera.mapX = scenario.mapCameraX;
    document.camera.mapY = scenario.mapCameraY;
    document.victory.scenario = scenario.victory;
    document.victory.type = scenario.victoryType;
    for (size_t i = 0; i < 8; ++i) {
        const ScenarioPlayer &player = scenario.players[i];
        if (player.aiFilename.empty() && player.cityFilename.empty() &&
            player.personality.empty() && player.aiName.empty())
            continue;
        EditorAiMetadata ai;
        ai.player = (uint32_t)i + 1;
        ai.storage =
            (!player.aiFilename.empty() || !player.cityFilename.empty() ||
             !player.personality.empty())
                ? EditorAiStorage::Embedded
                : EditorAiStorage::Referenced;
        ai.name = player.aiName;
        ai.path = player.aiName;
        ai.cityName = player.cityName;
        ai.personalityName = player.personalityName;
        ai.script = player.aiFilename;
        ai.city = player.cityFilename;
        ai.personality = player.personality;
        document.ai.push_back(std::move(ai));
    }
    document.scx.version = scenario.version;
    document.scx.saveType = scenario.saveType;
    document.scx.lastSaveTime = scenario.lastSaveTime;
    document.scx.enabledPlayerCount = scenario.enabledPlayerCount;
    document.scx.nextUnitId = scenario.nextUnitId;
    document.scx.playerDataVersion = scenario.playerDataVersion;
    document.scx.originalFilename = scenario.originalFilename;
    document.scx.allTechnologies = scenario.allTechnologies;
    document.scx.triggerSystemVersion = scenario.triggerSystemVersion;
    document.scx.objectiveState = scenario.objectiveState;
    document.scx.civilizations = scenario.civilizations;
    for (size_t i = 0; i < document.scx.auxiliaryPlayers.size(); ++i)
        document.scx.auxiliaryPlayers[i] = scenario.players[i + 8];
    document.originalScxBytes = sourceBytes;
    document.hasImportFingerprint = true;
    document.importSemanticFingerprint =
        editableScenarioSemanticFingerprint(document);
    return document;
}

uint32_t deterministicHash(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t value =
        x * 374761393u + y * 668265263u + seed * 2246822519u;
    value = (value ^ (value >> 13)) * 1274126177u;
    return value ^ (value >> 16);
}

std::pair<float, float> playerStart(uint32_t index, uint32_t count, uint32_t size) {
    static constexpr float kPi = 3.14159265358979323846f;
    const float angle =
        -kPi * 0.5f + 2.0f * kPi * (float)index / (float)count;
    const float radius = (float)size * 0.32f;
    const float center = ((float)size - 1.0f) * 0.5f;
    return {
        center + std::cos(angle) * radius,
        center + std::sin(angle) * radius,
    };
}

bool validPermutation(
    const std::vector<uint32_t> &order,
    size_t count) {
    if (order.size() != count) return false;
    std::vector<bool> seen(count, false);
    for (uint32_t value : order) {
        if (value >= count || seen[value]) return false;
        seen[value] = true;
    }
    return true;
}

bool validPermutation(
    const std::vector<int32_t> &order,
    size_t count) {
    if (order.size() != count) return false;
    std::vector<bool> seen(count, false);
    for (int32_t value : order) {
        if (value < 0 || (size_t)value >= count || seen[(size_t)value])
            return false;
        seen[(size_t)value] = true;
    }
    return true;
}

const dat::Unit *findUnit(
    const dat::DatFile *data,
    uint32_t civilization,
    uint32_t unitId) {
    if (!data || civilization >= data->civs.size()) return nullptr;
    const std::vector<dat::Unit> &units = data->civs[civilization].units;
    if (unitId >= units.size() || !units[unitId].exists) return nullptr;
    return &units[unitId];
}

void addIssue(
    ValidationReport &report,
    ValidationSeverity severity,
    const std::string &code,
    const std::string &message,
    ValidationFocusKind kind,
    uint64_t id,
    const std::string &text = {}) {
    ValidationIssue issue;
    issue.severity = severity;
    issue.code = code;
    issue.message = message;
    issue.focus.kind = kind;
    issue.focus.id = id;
    issue.focus.text = text;
    report.issues.push_back(std::move(issue));
}

} // namespace

uint64_t editableScenarioSemanticFingerprint(
    const EditableScenarioDocument &document) {
    try {
        Writer writer;
        writeDocumentPayload(writer, document, false);
        return fnv1a(writer.bytes);
    } catch (...) {
        return 0;
    }
}

bool encodeEditableScenario(
    const EditableScenarioDocument &document,
    std::vector<uint8_t> &bytes,
    std::string *error,
    const NativeScenarioLimits &limits) {
    bytes.clear();
    try {
        checkDocumentBounds(document, limits);
        Writer payload;
        writeDocumentPayload(payload, document, true);
        if (payload.bytes.size() > limits.maxPayloadBytes)
            throw DocumentError("native payload exceeds configured size limit");
        if (payload.bytes.size() > limits.maxFileBytes - kHeaderBytes)
            throw DocumentError("native file exceeds configured size limit");

        Writer file;
        file.bytes.insert(file.bytes.end(), std::begin(kMagic), std::end(kMagic));
        file.u32(kSwScenarioFormatVersion);
        file.u64(payload.bytes.size());
        file.u64(fnv1a(payload.bytes));
        file.bytes.insert(
            file.bytes.end(), payload.bytes.begin(), payload.bytes.end());
        bytes.swap(file.bytes);
        return true;
    } catch (const std::exception &exception) {
        setError(
            error,
            std::string("could not encode native scenario: ") +
                exception.what());
        return false;
    }
}

bool decodeEditableScenario(
    const std::vector<uint8_t> &bytes,
    EditableScenarioDocument &document,
    std::string *error,
    const NativeScenarioLimits &limits) {
    try {
        if (bytes.size() > limits.maxFileBytes)
            throw DocumentError("native file exceeds configured size limit");
        if (bytes.size() < kHeaderBytes)
            throw DocumentError("truncated native header");
        if (!std::equal(std::begin(kMagic), std::end(kMagic), bytes.begin()))
            throw DocumentError("invalid native magic");
        const uint32_t version = getU32(bytes.data() + 8);
        if (version != kSwScenarioFormatVersion)
            throw DocumentError(
                "unsupported native format version " +
                std::to_string(version));
        const uint64_t payloadSize = getU64(bytes.data() + 12);
        const uint64_t checksum = getU64(bytes.data() + 20);
        if (payloadSize > limits.maxPayloadBytes)
            throw DocumentError("native payload exceeds configured size limit");
        if (payloadSize > std::numeric_limits<size_t>::max())
            throw DocumentError("native payload cannot fit in memory");
        if (payloadSize != bytes.size() - kHeaderBytes)
            throw DocumentError(
                payloadSize > bytes.size() - kHeaderBytes
                    ? "truncated native payload"
                    : "trailing data after native payload");
        const uint8_t *payload = bytes.data() + kHeaderBytes;
        if (fnv1a(payload, (size_t)payloadSize) != checksum)
            throw DocumentError("native payload checksum mismatch");
        Reader reader(payload, (size_t)payloadSize, limits);
        EditableScenarioDocument decoded = readDocumentPayload(reader);
        if (reader.remaining() != 0)
            throw DocumentError("trailing data inside native payload");
        checkDocumentBounds(decoded, limits);
        document = std::move(decoded);
        return true;
    } catch (const std::exception &exception) {
        setError(
            error,
            std::string("could not decode native scenario: ") +
                exception.what());
        return false;
    }
}

bool saveEditableScenario(
    const std::string &path,
    const EditableScenarioDocument &document,
    std::string *error,
    const NativeScenarioLimits &limits) {
    std::vector<uint8_t> bytes;
    if (!encodeEditableScenario(document, bytes, error, limits)) return false;
    return writeAtomic(path, bytes, error);
}

bool loadEditableScenario(
    const std::string &path,
    EditableScenarioDocument &document,
    std::string *error,
    const NativeScenarioLimits &limits) {
    std::vector<uint8_t> bytes;
    std::string primaryError;
    if (readFileBounded(
            path, limits.maxFileBytes, bytes,
            &primaryError) &&
        decodeEditableScenario(
            bytes, document, &primaryError, limits))
        return true;
    bytes.clear();
    std::string backupError;
    const std::string backup = path + ".bak";
    if (readFileBounded(
            backup, limits.maxFileBytes, bytes,
            &backupError) &&
        decodeEditableScenario(
            bytes, document, &backupError, limits))
        return true;
    setError(
        error,
        primaryError + "; backup recovery failed: " +
            backupError);
    return false;
}

bool importScxBytes(
    const std::vector<uint8_t> &bytes,
    EditableScenarioDocument &document,
    std::string *error,
    const NativeScenarioLimits &limits) {
    if (bytes.size() > limits.maxOriginalScxBytes) {
        setError(error, "SCX source exceeds configured import size limit");
        return false;
    }
    Scenario scenario;
    std::string parserError;
    if (!scenario.load(bytes, &parserError)) {
        setError(error, parserError);
        return false;
    }
    EditableScenarioDocument imported = importScenario(scenario, bytes);
    try {
        checkDocumentBounds(imported, limits);
    } catch (const std::exception &exception) {
        setError(
            error,
            std::string("imported SCX exceeds editor limits: ") +
                exception.what());
        return false;
    }
    document = std::move(imported);
    return true;
}

bool importScxFile(
    const std::string &path,
    EditableScenarioDocument &document,
    std::string *error,
    const NativeScenarioLimits &limits) {
    std::vector<uint8_t> bytes;
    if (!readFileBounded(
            path, limits.maxOriginalScxBytes, bytes, error))
        return false;
    EditableScenarioDocument imported;
    if (!importScxBytes(bytes, imported, error, limits)) return false;
    imported.metadata.sourceName = path;
    imported.importSemanticFingerprint =
        editableScenarioSemanticFingerprint(imported);
    document = std::move(imported);
    return true;
}

bool exportScxBytes(
    const EditableScenarioDocument &document,
    std::vector<uint8_t> &bytes,
    std::string *error) {
    bytes.clear();
    if (!document.hasImportFingerprint ||
        document.originalScxBytes.empty()) {
        setError(
            error,
            "SCX export is unavailable for generated documents; save a "
            "native .swscenario sidecar");
        return false;
    }
    const uint64_t current =
        editableScenarioSemanticFingerprint(document);
    if (current == 0 ||
        current != document.importSemanticFingerprint) {
        setError(
            error,
            "SCX export refused because editable scenario semantics changed; "
            "save a native .swscenario sidecar");
        return false;
    }
    bytes = document.originalScxBytes;
    return true;
}

bool exportScxFile(
    const std::string &path,
    const EditableScenarioDocument &document,
    std::string *error) {
    std::vector<uint8_t> bytes;
    if (!exportScxBytes(document, bytes, error)) return false;
    return writeAtomic(path, bytes, error);
}

bool makeScenarioTemplate(
    ScenarioTemplate scenarioTemplate,
    uint32_t mapSize,
    uint32_t seed,
    uint32_t playerCount,
    EditableScenarioDocument &document,
    std::string *error) {
    if (mapSize < 32 || mapSize > 160) {
        setError(error, "template map size must be between 32 and 160");
        return false;
    }
    if (playerCount < 1 || playerCount > 8) {
        setError(error, "template player count must be between 1 and 8");
        return false;
    }
    if ((uint8_t)scenarioTemplate > (uint8_t)ScenarioTemplate::TriggerTutorial) {
        setError(error, "unknown scenario template");
        return false;
    }

    EditableScenarioDocument result;
    const char *names[] = {
        "Blank Land", "Islands", "Skirmish Base", "Trigger Tutorial",
    };
    result.metadata.title = names[(uint8_t)scenarioTemplate];
    result.metadata.templateName = result.metadata.title;
    result.map.width = mapSize;
    result.map.height = mapSize;
    result.map.tiles.resize((size_t)mapSize * mapSize);
    result.players.reserve(playerCount);
    std::vector<std::pair<float, float>> starts;
    for (uint32_t i = 0; i < playerCount; ++i) {
        EditorPlayer player;
        player.scenario.name =
            i == 0 ? "Player 1" : "Computer " + std::to_string(i + 1);
        player.scenario.active = true;
        player.scenario.human = i == 0;
        player.scenario.civilization = 1 + i % 8;
        player.scenario.color = i;
        player.scenario.populationLimit = 200;
        player.scenario.resources = {{200, 200, 100, 200, 0, 0}};
        player.scenario.startingAge = 1;
        player.team = (int32_t)i + 1;
        player.difficulty = 2;
        const auto start = playerStart(i, playerCount, mapSize);
        starts.push_back(start);
        player.scenario.cameraX = start.first;
        player.scenario.cameraY = start.second;
        for (uint32_t j = 0; j < playerCount; ++j)
            player.scenario.diplomacy[j + 1] =
                i == j ? 0u : 3u;
        result.players.push_back(std::move(player));
    }
    result.camera.x = starts.front().first;
    result.camera.y = starts.front().second;
    result.camera.mapX = result.camera.x;
    result.camera.mapY = result.camera.y;
    result.victory.scenario.conquestRequired = playerCount > 1;
    result.victory.scenario.mode = 4;
    result.victory.type = 0;

    for (uint32_t y = 0; y < mapSize; ++y) {
        for (uint32_t x = 0; x < mapSize; ++x) {
            ScenarioTile &tile = result.map.tiles[(size_t)y * mapSize + x];
            tile.terrain = 0;
            tile.elevation = 0;
            if (scenarioTemplate == ScenarioTemplate::Islands) {
                float nearest = std::numeric_limits<float>::max();
                for (const auto &start : starts) {
                    const float dx = (float)x - start.first;
                    const float dy = (float)y - start.second;
                    nearest = std::min(nearest, std::sqrt(dx * dx + dy * dy));
                }
                const float noise =
                    (float)(deterministicHash(x, y, seed) & 1023u) /
                        1023.0f -
                    0.5f;
                tile.terrain =
                    nearest < (float)mapSize * 0.18f + noise * 3.0f ? 0 : 1;
            } else if (scenarioTemplate == ScenarioTemplate::SkirmishBase) {
                const uint32_t value = deterministicHash(x, y, seed);
                tile.terrain = (value % 29u == 0u) ? 9u : 0u;
                tile.elevation = (uint8_t)((value >> 16) % 3u);
            }
        }
    }

    if (scenarioTemplate == ScenarioTemplate::TriggerTutorial) {
        result.messages.instructions =
            "Use the trigger editor to complete this tutorial scenario.";
        result.messages.objectives = "Inspect the welcome trigger.";
        EditorTrigger trigger;
        trigger.enabled = true;
        trigger.name = "Welcome";
        trigger.description =
            "A minimal supported message trigger for editor practice.";
        EditorTriggerCondition condition;
        condition.scenario.type = 10;
        condition.scenario.fields.assign(16, -1);
        condition.scenario.fields[7] = 1;
        trigger.conditions.push_back(std::move(condition));
        trigger.conditionOrder.push_back(0);
        EditorTriggerEffect effect;
        effect.scenario.type = 3;
        effect.scenario.fields.assign(23, -1);
        effect.scenario.message = "Welcome to the trigger tutorial.";
        trigger.effects.push_back(std::move(effect));
        trigger.effectOrder.push_back(0);
        result.triggers.push_back(std::move(trigger));
        result.triggerOrder.push_back(0);
    }
    document = std::move(result);
    return true;
}

bool ValidationReport::hasErrors() const {
    return std::any_of(
        issues.begin(), issues.end(), [](const ValidationIssue &issue) {
            return issue.severity == ValidationSeverity::Error;
        });
}

ValidationReport validateEditableScenario(
    const EditableScenarioDocument &document,
    const dat::DatFile *data,
    const ValidationOptions &options) {
    ValidationReport report;
    const uint64_t tileCount =
        (uint64_t)document.map.width * document.map.height;
    if (!document.map.width || !document.map.height)
        addIssue(
            report, ValidationSeverity::Error, "map.dimensions.empty",
            "Map dimensions must both be nonzero.",
            ValidationFocusKind::Map, 0);
    if (document.map.width > options.nativeLimits.maxMapDimension ||
        document.map.height > options.nativeLimits.maxMapDimension)
        addIssue(
            report, ValidationSeverity::Error, "map.dimensions.native_limit",
            "Map dimensions exceed the native document limit.",
            ValidationFocusKind::Map, 0);
    if (document.map.width > options.vitaMapDimensionCap ||
        document.map.height > options.vitaMapDimensionCap ||
        tileCount > options.vitaTileCap)
        addIssue(
            report, ValidationSeverity::Error, "map.vita_cap",
            "Map exceeds the configured Vita-safe dimension or tile cap.",
            ValidationFocusKind::Map, 0);
    if (tileCount != document.map.tiles.size())
        addIssue(
            report, ValidationSeverity::Error, "map.tile_count",
            "Map tile count does not match width multiplied by height.",
            ValidationFocusKind::Map, 0);
    const size_t terrainCount =
        data ? data->terrainBlock.terrains.size() : 256u;
    for (size_t i = 0; i < document.map.tiles.size(); ++i) {
        const ScenarioTile &tile = document.map.tiles[i];
        if ((size_t)tile.terrain >= terrainCount)
            addIssue(
                report, ValidationSeverity::Error, "tile.terrain.invalid",
                "Tile references a terrain ID absent from the DAT.",
                ValidationFocusKind::Tile, i,
                std::to_string(i));
        else if (data && !data->terrainBlock.terrains[tile.terrain].enabled)
            addIssue(
                report, ValidationSeverity::Warning, "tile.terrain.disabled",
                "Tile references a DAT terrain marked disabled.",
                ValidationFocusKind::Tile, i,
                std::to_string(i));
        if (tile.elevation > 7)
            addIssue(
                report, ValidationSeverity::Error, "tile.elevation.range",
                "Tile elevation must be between 0 and 7.",
                ValidationFocusKind::Tile, i,
                std::to_string(i));
    }

    if (document.players.empty() || document.players.size() > 8)
        addIssue(
            report, ValidationSeverity::Error, "players.count",
            "A scenario must contain between 1 and 8 editor players.",
            ValidationFocusKind::Document, 0);
    std::set<uint32_t> activeColors;
    size_t activePlayers = 0;
    size_t activeHumans = 0;
    for (size_t i = 0; i < document.players.size(); ++i) {
        const EditorPlayer &player = document.players[i];
        const ScenarioPlayer &state = player.scenario;
        const uint64_t focusId = i + 1;
        if (state.active) {
            ++activePlayers;
            if (state.human) ++activeHumans;
            if (state.name.empty())
                addIssue(
                    report, ValidationSeverity::Warning, "player.name.empty",
                    "Active player has no display name.",
                    ValidationFocusKind::Player, focusId);
            if (state.color >= 8 || !activeColors.insert(state.color).second)
                addIssue(
                    report, ValidationSeverity::Error, "player.color",
                    "Active player colors must be unique IDs from 0 through 7.",
                    ValidationFocusKind::Player, focusId);
            if (state.civilization == 0 ||
                (data && state.civilization >= data->civs.size()) ||
                (!data &&
                 (state.civilization < 1 || state.civilization > 8)))
                addIssue(
                    report, ValidationSeverity::Error,
                    "player.civilization",
                    "Active player civilization ID is unavailable.",
                    ValidationFocusKind::Player, focusId);
            if (!isFinite(state.populationLimit) ||
                state.populationLimit < 1 ||
                state.populationLimit > 250)
                addIssue(
                    report, ValidationSeverity::Error,
                    "player.population",
                    "Active player population limit must be finite and from 1 through 250.",
                    ValidationFocusKind::Player, focusId);
            if (!isFinite(state.cameraX) || !isFinite(state.cameraY) ||
                state.cameraX < 0 || state.cameraY < 0 ||
                state.cameraX >= document.map.width ||
                state.cameraY >= document.map.height)
                addIssue(
                    report, ValidationSeverity::Error, "player.camera",
                    "Active player camera must be finite and inside the map.",
                    ValidationFocusKind::Player, focusId);
        }
        if (player.team < 0 || player.team > 8)
            addIssue(
                report, ValidationSeverity::Error, "player.team",
                "Player team must be none (0) or 1 through 8.",
                ValidationFocusKind::Player, focusId);
        if (player.difficulty < 0 || player.difficulty > 4)
            addIssue(
                report, ValidationSeverity::Error, "player.difficulty",
                "AI difficulty must be between 0 and 4.",
                ValidationFocusKind::Player, focusId);
        if (state.startingAge < -1 || state.startingAge > 4)
            addIssue(
                report, ValidationSeverity::Error, "player.starting_age",
                "Starting age must be automatic (-1) or between 0 and 4.",
                ValidationFocusKind::Player, focusId);
        for (float resource : state.resources)
            if (!isFinite(resource) || resource < 0 ||
                resource > 100000000.0f) {
                addIssue(
                    report, ValidationSeverity::Error,
                    "player.resources",
                    "Player resources must be finite, nonnegative, and bounded.",
                    ValidationFocusKind::Player, focusId);
                break;
            }
        if (data) {
            const auto checkTechs = [&](const std::vector<uint32_t> &ids,
                                        const char *code,
                                        const char *message) {
                for (uint32_t id : ids)
                    if (id >= data->techs.size()) {
                        addIssue(
                            report, ValidationSeverity::Error, code, message,
                            ValidationFocusKind::Player, focusId,
                            std::to_string(id));
                        break;
                    }
            };
            checkTechs(
                state.disabledTechnologies, "player.disabled_tech",
                "Disabled technology ID is absent from the DAT.");
            checkTechs(
                player.researchedTechnologies, "player.researched_tech",
                "Researched technology ID is absent from the DAT.");
            for (uint32_t id :
                 player.researchedTechnologies)
                if (std::find(
                        state.disabledTechnologies.begin(),
                        state.disabledTechnologies.end(),
                        id) !=
                    state.disabledTechnologies.end()) {
                    addIssue(
                        report, ValidationSeverity::Error,
                        "player.tech_state_conflict",
                        "Technology cannot be both disabled and initially researched.",
                        ValidationFocusKind::Player,
                        focusId, std::to_string(id));
                    break;
                }
            const auto checkUnits =
                [&](const std::vector<uint32_t> &ids,
                    const char *code,
                    const char *message,
                    bool requireBuilding) {
                    for (uint32_t id : ids) {
                        const dat::Unit *unit =
                            findUnit(data, state.civilization, id);
                        if (!unit ||
                            (requireBuilding &&
                             unit->type != dat::UT_Building)) {
                            addIssue(
                                report, ValidationSeverity::Error, code,
                                message, ValidationFocusKind::Player,
                                focusId, std::to_string(id));
                            break;
                        }
                    }
                };
            checkUnits(
                state.disabledUnits, "player.disabled_unit",
                "Disabled unit ID is absent from the player's civilization.",
                false);
            checkUnits(
                state.disabledBuildings, "player.disabled_building",
                "Disabled building ID is absent or is not a building.", true);
            checkUnits(
                player.researchedUnits, "player.researched_unit",
                "Researched unit-state ID is absent from the player's civilization.",
                false);
            checkUnits(
                player.researchedBuildings, "player.researched_building",
                "Researched building-state ID is absent or is not a building.",
                true);
            const auto stateConflict =
                [](const std::vector<uint32_t> &disabled,
                   const std::vector<uint32_t> &enabled) {
                    return std::find_first_of(
                               disabled.begin(),
                               disabled.end(),
                               enabled.begin(),
                               enabled.end()) !=
                           disabled.end();
                };
            if (stateConflict(
                    state.disabledUnits,
                    player.researchedUnits) ||
                stateConflict(
                    state.disabledBuildings,
                    player.researchedBuildings))
                addIssue(
                    report, ValidationSeverity::Error,
                    "player.unit_state_conflict",
                    "Unit or building cannot be both disabled and explicitly enabled.",
                    ValidationFocusKind::Player,
                    focusId);
        }
    }
    if (!activePlayers)
        addIssue(
            report, ValidationSeverity::Error, "players.none_active",
            "At least one player must be active.",
            ValidationFocusKind::Document, 0);
    if (!activeHumans)
        addIssue(
            report, ValidationSeverity::Warning, "players.no_human",
            "No active player is marked human.",
            ValidationFocusKind::Document, 0);

    for (size_t i = 0; i < document.players.size(); ++i) {
        for (size_t j = i + 1; j < document.players.size(); ++j) {
            const EditorPlayer &a = document.players[i];
            const EditorPlayer &b = document.players[j];
            if (!a.scenario.active || !b.scenario.active) continue;
            const uint32_t ab = a.scenario.diplomacy[j + 1];
            const uint32_t ba = b.scenario.diplomacy[i + 1];
            if (ab != ba)
                addIssue(
                    report, ValidationSeverity::Warning,
                    "diplomacy.asymmetric",
                    "Players have contradictory asymmetric diplomacy stances.",
                    ValidationFocusKind::Player, i + 1,
                    std::to_string(j + 1));
            if (a.team > 0 && a.team == b.team &&
                (ab >= 3 || ba >= 3))
                addIssue(
                    report, ValidationSeverity::Warning,
                    "diplomacy.team_enemy",
                    "Players on the same team are marked as enemies.",
                    ValidationFocusKind::Player, i + 1,
                    std::to_string(j + 1));
            if ((a.scenario.alliedVictory ||
                 b.scenario.alliedVictory) &&
                (ab >= 3 || ba >= 3))
                addIssue(
                    report, ValidationSeverity::Warning,
                    "diplomacy.allied_victory_enemy",
                    "Allied victory is enabled across an enemy stance.",
                    ValidationFocusKind::Player, i + 1,
                    std::to_string(j + 1));
        }
    }

    if (!isFinite(document.camera.x) || !isFinite(document.camera.y) ||
        document.camera.x < 0 || document.camera.y < 0 ||
        document.camera.x >= document.map.width ||
        document.camera.y >= document.map.height)
        addIssue(
            report, ValidationSeverity::Error, "camera.global",
            "Global camera must be finite and inside the map.",
            ValidationFocusKind::Document, 0);
    if (!isFinite(document.camera.mapX) || !isFinite(document.camera.mapY) ||
        document.camera.mapX < 0 || document.camera.mapY < 0 ||
        document.camera.mapX >= document.map.width ||
        document.camera.mapY >= document.map.height)
        addIssue(
            report, ValidationSeverity::Error, "camera.map",
            "Map camera coordinates must be finite and inside the map.",
            ValidationFocusKind::Document, 0);

    std::unordered_map<uint32_t, size_t> objectsById;
    std::unordered_map<uint32_t, size_t> garrisonCounts;
    std::vector<const dat::Unit *> objectUnits(
        document.objects.size(), nullptr);
    for (size_t i = 0; i < document.objects.size(); ++i) {
        const EditorObject &object = document.objects[i];
        const uint32_t spawnId = object.scenario.spawnId;
        if (!objectsById.emplace(spawnId, i).second)
            addIssue(
                report, ValidationSeverity::Error, "object.id.duplicate",
                "Object stable spawn ID is duplicated.",
                ValidationFocusKind::Object, spawnId);
        if (object.scenario.player > document.players.size())
            addIssue(
                report, ValidationSeverity::Error, "object.player",
                "Object owner is outside Gaia/player range.",
                ValidationFocusKind::Object, spawnId);
        const uint32_t civilization =
            object.scenario.player == 0
                ? 0
                : object.scenario.player <= document.players.size()
                      ? document.players[object.scenario.player - 1]
                            .scenario.civilization
                      : std::numeric_limits<uint32_t>::max();
        objectUnits[i] =
            findUnit(data, civilization, object.scenario.unitId);
        if (data && !objectUnits[i])
            addIssue(
                report, ValidationSeverity::Error, "object.unit",
                "Object DAT unit ID is unavailable for its owner civilization.",
                ValidationFocusKind::Object, spawnId,
                std::to_string(object.scenario.unitId));
        const float clearanceX =
            objectUnits[i] ? std::max(0.0f, objectUnits[i]->clearanceSize[0])
                           : 0.0f;
        const float clearanceY =
            objectUnits[i] ? std::max(0.0f, objectUnits[i]->clearanceSize[1])
                           : 0.0f;
        if (!isFinite(object.scenario.x) || !isFinite(object.scenario.y) ||
            !isFinite(object.scenario.z) ||
            !isFinite(object.scenario.rotation) ||
            !isFinite(object.hitPoints) ||
            !isFinite(object.resourceAmount))
            addIssue(
                report, ValidationSeverity::Error, "object.transform.finite",
                "Object transform, HP, and resource values must be finite.",
                ValidationFocusKind::Object, spawnId);
        else if (object.scenario.x - clearanceX < 0 ||
                 object.scenario.y - clearanceY < 0 ||
                 object.scenario.x + clearanceX >= document.map.width ||
                 object.scenario.y + clearanceY >= document.map.height)
            addIssue(
                report, ValidationSeverity::Error, "object.map_footprint",
                "Object DAT clearance footprint extends outside the map.",
                ValidationFocusKind::Object, spawnId);
        if (object.hitPoints < -1)
            addIssue(
                report, ValidationSeverity::Error, "object.hit_points",
                "Object HP must be -1 (DAT default) or nonnegative.",
                ValidationFocusKind::Object, spawnId);
        else if (objectUnits[i] && object.hitPoints >= 0 &&
                 object.hitPoints > objectUnits[i]->hitPoints)
            addIssue(
                report, ValidationSeverity::Warning,
                "object.hit_points.dat",
                "Object HP exceeds the DAT unit's base hit points.",
                ValidationFocusKind::Object, spawnId);
        if (object.resourceAmount < 0)
            addIssue(
                report, ValidationSeverity::Error, "object.resource",
                "Object resource amount must be nonnegative.",
                ValidationFocusKind::Object, spawnId);
        if (object.scenario.garrisonedInId >= 0)
            ++garrisonCounts[(uint32_t)object.scenario.garrisonedInId];
        for (const EditorInitialOrder &order : object.initialOrders) {
            if (!isFinite(order.targetX) || !isFinite(order.targetY))
                addIssue(
                    report, ValidationSeverity::Error,
                    "object.order.coordinates",
                    "Initial-order coordinates must be finite.",
                    ValidationFocusKind::Object, spawnId);
            if (order.targetSpawnId &&
                !objectsById.count(order.targetSpawnId)) {
                // A forward reference may not have been visited; checked again below.
            }
        }
    }
    for (size_t i = 0; i < document.objects.size(); ++i) {
        const EditorObject &object = document.objects[i];
        const uint32_t spawnId = object.scenario.spawnId;
        if (object.scenario.garrisonedInId >= 0) {
            const uint32_t parent =
                (uint32_t)object.scenario.garrisonedInId;
            if (parent == spawnId || !objectsById.count(parent))
                addIssue(
                    report, ValidationSeverity::Error,
                    "object.garrison.reference",
                    "Object garrison reference is missing or self-referential.",
                    ValidationFocusKind::Object, spawnId);
        }
        for (const EditorInitialOrder &order : object.initialOrders)
            if (order.targetSpawnId &&
                !objectsById.count(order.targetSpawnId))
                addIssue(
                    report, ValidationSeverity::Error,
                    "object.order.target",
                    "Initial order references a missing object stable ID.",
                    ValidationFocusKind::Object, spawnId,
                    std::to_string(order.targetSpawnId));
        const auto count = garrisonCounts.find(spawnId);
        if (count != garrisonCounts.end() && objectUnits[i] &&
            count->second > objectUnits[i]->garrisonCapacity)
            addIssue(
                report, ValidationSeverity::Error,
                "object.garrison.capacity",
                "Garrison contains more objects than the DAT unit capacity.",
                ValidationFocusKind::Object, spawnId);
    }
    // Clearance overlap is meaningful only with DAT geometry.
    if (data) {
        float maximumClearance = 0;
        for (const dat::Unit *unit : objectUnits)
            if (unit)
                maximumClearance = std::max(
                    maximumClearance,
                    std::max(
                        std::max(0.0f, unit->clearanceSize[0]),
                        std::max(0.0f, unit->clearanceSize[1])));
        const float cellSize =
            std::max(1.0f, maximumClearance * 2.0f);
        std::unordered_map<uint64_t, std::vector<size_t>> cells;
        for (size_t i = 0; i < document.objects.size(); ++i) {
            if (!objectUnits[i]) continue;
            const float ax =
                std::max(0.0f, objectUnits[i]->clearanceSize[0]);
            const float ay =
                std::max(0.0f, objectUnits[i]->clearanceSize[1]);
            const int32_t cellX = (int32_t)std::floor(
                document.objects[i].scenario.x / cellSize);
            const int32_t cellY = (int32_t)std::floor(
                document.objects[i].scenario.y / cellSize);
            bool reported = false;
            if (ax > 0 || ay > 0) {
                for (int dy = -1; dy <= 1 && !reported; ++dy) {
                    for (int dx = -1; dx <= 1 && !reported; ++dx) {
                        const uint64_t key =
                            ((uint64_t)(uint32_t)(cellX + dx) << 32) |
                            (uint32_t)(cellY + dy);
                        const auto found = cells.find(key);
                        if (found == cells.end()) continue;
                        for (size_t j : found->second) {
                            if (!objectUnits[j] ||
                                document.objects[i]
                                        .scenario.garrisonedInId >= 0 ||
                                document.objects[j]
                                        .scenario.garrisonedInId >= 0)
                                continue;
                            const float bx = std::max(
                                0.0f,
                                objectUnits[j]->clearanceSize[0]);
                            const float by = std::max(
                                0.0f,
                                objectUnits[j]->clearanceSize[1]);
                            if (std::fabs(
                                    document.objects[i].scenario.x -
                                    document.objects[j].scenario.x) <
                                    ax + bx &&
                                std::fabs(
                                    document.objects[i].scenario.y -
                                    document.objects[j].scenario.y) <
                                    ay + by) {
                                addIssue(
                                    report,
                                    ValidationSeverity::Warning,
                                    "object.clearance.overlap",
                                    "Object DAT clearance footprints overlap.",
                                    ValidationFocusKind::Object,
                                    document.objects[i]
                                        .scenario.spawnId,
                                    std::to_string(
                                        document.objects[j]
                                            .scenario.spawnId));
                                reported = true;
                                break;
                            }
                        }
                    }
                }
            }
            const uint64_t ownKey =
                ((uint64_t)(uint32_t)cellX << 32) |
                (uint32_t)cellY;
            cells[ownKey].push_back(i);
        }
    }

    std::set<uint32_t> areaIds;
    for (const EditorArea &area : document.areas) {
        if (!area.id || !areaIds.insert(area.id).second)
            addIssue(
                report, ValidationSeverity::Error, "area.id",
                "Named area IDs must be nonzero and unique.",
                ValidationFocusKind::Area, area.id);
        if (area.name.empty())
            addIssue(
                report, ValidationSeverity::Warning, "area.name",
                "Named area has an empty name.",
                ValidationFocusKind::Area, area.id);
        if (area.left < 0 || area.top < 0 ||
            area.right < area.left || area.bottom < area.top ||
            area.right >= (int32_t)document.map.width ||
            area.bottom >= (int32_t)document.map.height)
            addIssue(
                report, ValidationSeverity::Error, "area.rectangle",
                "Named area rectangle is invalid or outside the map.",
                ValidationFocusKind::Area, area.id);
    }

    if (!validPermutation(
            document.triggerOrder, document.triggers.size()))
        addIssue(
            report, ValidationSeverity::Error, "trigger.order",
            "Trigger order must contain each trigger index exactly once.",
            ValidationFocusKind::Document, 0);
    for (size_t i = 0; i < document.triggers.size(); ++i) {
        const EditorTrigger &trigger = document.triggers[i];
        if (!validPermutation(
                trigger.effectOrder, trigger.effects.size()))
            addIssue(
                report, ValidationSeverity::Error,
                "trigger.effect_order",
                "Effect order must contain each effect index exactly once.",
                ValidationFocusKind::Trigger, i);
        if (!validPermutation(
                trigger.conditionOrder, trigger.conditions.size()))
            addIssue(
                report, ValidationSeverity::Error,
                "trigger.condition_order",
                "Condition order must contain each condition index exactly once.",
                ValidationFocusKind::Trigger, i);
        if (trigger.objective && trigger.objectiveOrder < 0)
            addIssue(
                report, ValidationSeverity::Warning,
                "trigger.objective_order",
                "Objective trigger has no objective ordering value.",
                ValidationFocusKind::Trigger, i);
        for (size_t j = 0; j < trigger.conditions.size(); ++j) {
            const EditorTriggerCondition &condition =
                trigger.conditions[j];
            const bool runtimeSupported =
                Game::supportsTriggerCondition(condition.scenario.type);
            if (!runtimeSupported || !condition.supported ||
                condition.readOnlyUnknown)
                addIssue(
                    report, ValidationSeverity::Warning,
                    "trigger.condition.unsupported",
                    "Unsupported trigger condition is preserved as a visible read-only form.",
                    ValidationFocusKind::TriggerCondition,
                    ((uint64_t)i << 32) | j,
                    std::to_string(condition.scenario.type));
            if (condition.scenario.fields.size() > 64)
                addIssue(
                    report, ValidationSeverity::Error,
                    "trigger.condition.fields",
                    "Trigger condition has more than 64 fields.",
                    ValidationFocusKind::TriggerCondition,
                    ((uint64_t)i << 32) | j);
            if (condition.referencedAreaId &&
                !areaIds.count(condition.referencedAreaId))
                addIssue(
                    report, ValidationSeverity::Error,
                    "trigger.condition.area",
                    "Trigger condition references a missing named area.",
                    ValidationFocusKind::TriggerCondition,
                    ((uint64_t)i << 32) | j,
                    std::to_string(condition.referencedAreaId));
            if (condition.scenario.fields.size() > 12) {
                const int x1 = condition.scenario.fields[9];
                const int y1 = condition.scenario.fields[10];
                const int x2 = condition.scenario.fields[11];
                const int y2 = condition.scenario.fields[12];
                if ((x1 >= 0 || y1 >= 0 || x2 >= 0 || y2 >= 0) &&
                    (x1 < 0 || y1 < 0 || x2 < x1 || y2 < y1 ||
                     x2 >= (int)document.map.width ||
                     y2 >= (int)document.map.height))
                    addIssue(
                        report, ValidationSeverity::Error,
                        "trigger.condition.rectangle",
                        "Trigger condition area fields form an invalid map rectangle.",
                        ValidationFocusKind::TriggerCondition,
                        ((uint64_t)i << 32) | j);
            }
        }
        for (size_t j = 0; j < trigger.effects.size(); ++j) {
            const EditorTriggerEffect &effect = trigger.effects[j];
            const bool runtimeSupported =
                Game::supportsTriggerEffect(effect.scenario.type);
            if (!runtimeSupported || !effect.supported ||
                effect.readOnlyUnknown)
                addIssue(
                    report, ValidationSeverity::Warning,
                    "trigger.effect.unsupported",
                    "Unsupported trigger effect is preserved as a visible read-only form.",
                    ValidationFocusKind::TriggerEffect,
                    ((uint64_t)i << 32) | j,
                    std::to_string(effect.scenario.type));
            if (effect.scenario.fields.size() > 64)
                addIssue(
                    report, ValidationSeverity::Error,
                    "trigger.effect.fields",
                    "Trigger effect has more than 64 fields.",
                    ValidationFocusKind::TriggerEffect,
                    ((uint64_t)i << 32) | j);
            if ((effect.scenario.type == 8 ||
                 effect.scenario.type == 9) &&
                (effect.scenario.fields.size() <= 13 ||
                 effect.scenario.fields[13] < 0 ||
                 (size_t)effect.scenario.fields[13] >=
                     document.triggers.size()))
                addIssue(
                    report, ValidationSeverity::Error,
                    "trigger.effect.trigger_reference",
                    "Activate/deactivate effect references a missing trigger.",
                    ValidationFocusKind::TriggerEffect,
                    ((uint64_t)i << 32) | j);
            for (uint32_t selected : effect.scenario.selectedUnitIds)
                if (!objectsById.count(selected))
                    addIssue(
                        report, ValidationSeverity::Error,
                        "trigger.effect.selected_object",
                        "Trigger effect references a missing selected object.",
                        ValidationFocusKind::TriggerEffect,
                        ((uint64_t)i << 32) | j,
                        std::to_string(selected));
            if (effect.referencedAreaId &&
                !areaIds.count(effect.referencedAreaId))
                addIssue(
                    report, ValidationSeverity::Error,
                    "trigger.effect.area",
                    "Trigger effect references a missing named area.",
                    ValidationFocusKind::TriggerEffect,
                    ((uint64_t)i << 32) | j,
                    std::to_string(effect.referencedAreaId));
            if (effect.scenario.fields.size() > 19) {
                const int x1 = effect.scenario.fields[16];
                const int y1 = effect.scenario.fields[17];
                const int x2 = effect.scenario.fields[18];
                const int y2 = effect.scenario.fields[19];
                if ((x1 >= 0 || y1 >= 0 || x2 >= 0 || y2 >= 0) &&
                    (x1 < 0 || y1 < 0 || x2 < x1 || y2 < y1 ||
                     x2 >= (int)document.map.width ||
                     y2 >= (int)document.map.height))
                    addIssue(
                        report, ValidationSeverity::Error,
                        "trigger.effect.rectangle",
                        "Trigger effect area fields form an invalid map rectangle.",
                        ValidationFocusKind::TriggerEffect,
                        ((uint64_t)i << 32) | j);
            }
        }
    }

    std::set<uint32_t> aiPlayers;
    for (size_t i = 0; i < document.ai.size(); ++i) {
        const EditorAiMetadata &ai = document.ai[i];
        if (!ai.player || ai.player > document.players.size() ||
            !aiPlayers.insert(ai.player).second)
            addIssue(
                report, ValidationSeverity::Error, "ai.player",
                "AI metadata player must be unique and reference an editor player.",
                ValidationFocusKind::Ai, i);
        if (ai.storage == EditorAiStorage::Referenced &&
            ai.path.empty())
            addIssue(
                report, ValidationSeverity::Error, "ai.reference",
                "Referenced AI metadata requires a nonempty path.",
                ValidationFocusKind::Ai, i);
        if (ai.storage == EditorAiStorage::Embedded &&
            ai.script.empty() && ai.city.empty() &&
            ai.personality.empty())
            addIssue(
                report, ValidationSeverity::Error, "ai.embedded",
                "Embedded AI metadata contains no embedded content.",
                ValidationFocusKind::Ai, i);
    }

    const ScenarioVictory &victory = document.victory.scenario;
    if (victory.requiredExploredPercent > 100)
        addIssue(
            report, ValidationSeverity::Error, "victory.exploration",
            "Required explored percentage cannot exceed 100.",
            ValidationFocusKind::Victory, 0);
    if (victory.conquestRequired && activePlayers < 2)
        addIssue(
            report, ValidationSeverity::Error, "victory.conquest_impossible",
            "Conquest victory is impossible with fewer than two active players.",
            ValidationFocusKind::Victory, 0);
    if (victory.allConditionsRequired &&
        !victory.conquestRequired &&
        victory.requiredHolocrons == 0 &&
        victory.requiredExploredPercent == 0 &&
        victory.requiredScore == 0 && victory.timeLimit == 0)
        addIssue(
            report, ValidationSeverity::Error, "victory.empty_all",
            "All-conditions victory has no satisfiable configured condition.",
            ValidationFocusKind::Victory, 0);
    if (activePlayers > 1) {
        bool anyOpponent = false;
        for (size_t i = 0; i < document.players.size(); ++i)
            for (size_t j = i + 1; j < document.players.size(); ++j)
                if (document.players[i].scenario.active &&
                    document.players[j].scenario.active &&
                    (document.players[i].scenario.diplomacy[j + 1] >= 3 ||
                     document.players[j].scenario.diplomacy[i + 1] >= 3))
                    anyOpponent = true;
        if (victory.conquestRequired && !anyOpponent)
            addIssue(
                report, ValidationSeverity::Warning,
                "victory.no_opponent",
                "Conquest victory has no hostile player relationship.",
                ValidationFocusKind::Victory, 0);
    }

    const auto playerDynamicBytes =
        [](const ScenarioPlayer &player) {
            return (uint64_t)
                       player.name.size() +
                   player.aiName.size() +
                   player.cityName.size() +
                   player.personalityName.size() +
                   player.aiFilename.size() +
                   player.cityFilename.size() +
                   player.personality.size() +
                   (player.disabledTechnologies.size() +
                    player.disabledUnits.size() +
                    player.disabledBuildings.size() +
                    player.researchedTechnologies.size() +
                    player.researchedUnits.size() +
                    player.researchedBuildings.size()) *
                       sizeof(uint32_t);
        };
    uint64_t estimatedMemory =
        sizeof(document) +
        document.map.tiles.size() *
            sizeof(ScenarioTile) +
        document.players.size() *
            sizeof(EditorPlayer) +
        document.objects.size() *
            sizeof(EditorObject) +
        document.triggers.size() *
            sizeof(EditorTrigger) +
        document.areas.size() * sizeof(EditorArea) +
        document.ai.size() *
            sizeof(EditorAiMetadata) +
        document.unknownRecords.size() *
            sizeof(EditorOpaqueRecord) +
        document.originalScxBytes.size();
    estimatedMemory +=
        document.metadata.title.size() +
        document.metadata.author.size() +
        document.metadata.description.size() +
        document.metadata.templateName.size() +
        document.metadata.sourceName.size() +
        document.messages.instructions.size() +
        document.messages.objectives.size() +
        document.messages.hints.size() +
        document.messages.victory.size() +
        document.messages.loss.size() +
        document.messages.history.size() +
        document.messages.scouts.size() +
        document.messages.pregameCinematic.size() +
        document.messages.victoryCinematic.size() +
        document.messages.lossCinematic.size() +
        document.messages.background.size() +
        document.scx.version.size() +
        document.scx.originalFilename.size() +
        document.triggerOrder.size() *
            sizeof(uint32_t);
    for (const EditorPlayer &player :
         document.players)
        estimatedMemory +=
            playerDynamicBytes(player.scenario) +
            (player.researchedTechnologies.size() +
             player.researchedUnits.size() +
             player.researchedBuildings.size()) *
                sizeof(uint32_t);
    for (const ScenarioPlayer &player :
         document.scx.auxiliaryPlayers)
        estimatedMemory +=
            playerDynamicBytes(player);
    for (const EditorObject &object :
         document.objects)
        estimatedMemory +=
            object.customName.size() +
            object.initialOrders.size() *
                sizeof(EditorInitialOrder);
    for (const EditorTrigger &trigger :
         document.triggers) {
        estimatedMemory +=
            trigger.name.size() +
            trigger.description.size() +
            trigger.effects.size() *
                sizeof(EditorTriggerEffect) +
            trigger.conditions.size() *
                sizeof(EditorTriggerCondition) +
            (trigger.effectOrder.size() +
             trigger.conditionOrder.size()) *
                sizeof(int32_t);
        for (const EditorTriggerEffect &effect :
             trigger.effects)
            estimatedMemory +=
                effect.scenario.message.size() +
                effect.scenario.sound.size() +
                effect.scenario.fields.size() *
                    sizeof(int32_t) +
                effect.scenario.selectedUnitIds.size() *
                    sizeof(uint32_t);
        for (const EditorTriggerCondition &condition :
             trigger.conditions)
            estimatedMemory +=
                condition.scenario.fields.size() *
                sizeof(int32_t);
    }
    for (const EditorArea &area :
         document.areas)
        estimatedMemory += area.name.size();
    for (const EditorAiMetadata &ai :
         document.ai)
        estimatedMemory +=
            ai.name.size() + ai.path.size() +
            ai.cityName.size() +
            ai.personalityName.size() +
            ai.script.size() + ai.city.size() +
            ai.personality.size();
    for (const EditorOpaqueRecord &record :
         document.unknownRecords)
        estimatedMemory += record.bytes.size();
    std::vector<uint8_t> native;
    std::string nativeError;
    if (!encodeEditableScenario(
            document, native, &nativeError,
            options.nativeLimits))
        addIssue(
            report, ValidationSeverity::Error, "storage.native",
            nativeError, ValidationFocusKind::Storage, 0);
    else {
        const uint64_t peakMemory =
            estimatedMemory +
            2u * (uint64_t)native.size();
        if (peakMemory >
            options.estimatedMemoryCapBytes)
            addIssue(
                report, ValidationSeverity::Error,
                "storage.memory_cap",
                "Estimated live document and serialization peak exceeds the configured cap.",
                ValidationFocusKind::Storage,
                peakMemory);
    }

    std::vector<uint8_t> scx;
    std::string scxError;
    if (!exportScxBytes(document, scx, &scxError))
        addIssue(
            report,
            options.requireScxExportSupport
                ? ValidationSeverity::Error
                : ValidationSeverity::Warning,
            "scx.export_unavailable", scxError,
            ValidationFocusKind::ScxExport, 0);
    return report;
}

ValidationReport validateEditableScenario(
    const EditableScenarioDocument &document,
    const Assets &assets,
    const ValidationOptions &options) {
    return validateEditableScenario(
        document, &assets.dat(), options);
}

std::string sanitizeScenarioFilename(const std::string &name) {
    std::string result;
    result.reserve(std::min<size_t>(name.size(), 96));
    bool replacing = false;
    for (unsigned char c : name) {
        const bool invalid =
            c < 32 || c == 127 || c == '<' || c == '>' || c == ':' ||
            c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' ||
            c == '*';
        if (invalid) {
            if (!replacing && !result.empty()) result.push_back('_');
            replacing = true;
            continue;
        }
        replacing = false;
        result.push_back((char)c);
        if (result.size() >= 96) break;
    }
    if (!result.empty()) {
        size_t sequence = result.size() - 1;
        while (sequence > 0 &&
               ((unsigned char)result[sequence] & 0xc0u) == 0x80u)
            --sequence;
        const unsigned char lead =
            (unsigned char)result[sequence];
        const size_t expectedBytes =
            (lead & 0x80u) == 0 ? 1
            : (lead & 0xe0u) == 0xc0u ? 2
            : (lead & 0xf0u) == 0xe0u ? 3
            : (lead & 0xf8u) == 0xf0u ? 4
                                      : 1;
        if (sequence + expectedBytes > result.size())
            result.resize(sequence);
    }
    while (!result.empty() &&
           (result.back() == ' ' || result.back() == '.'))
        result.pop_back();
    size_t first = 0;
    while (first < result.size() && result[first] == ' ') ++first;
    if (first) result.erase(0, first);
    if (result.empty() || result == "." || result == "..")
        result = "scenario";

    std::string stem = result;
    const size_t dot = stem.find('.');
    if (dot != std::string::npos) stem.resize(dot);
    std::transform(
        stem.begin(), stem.end(), stem.begin(),
        [](unsigned char c) {
            return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : (char)c;
        });
    static const std::set<std::string> reserved = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5",
        "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5",
        "LPT6", "LPT7", "LPT8", "LPT9",
    };
    if (reserved.count(stem)) result.insert(result.begin(), '_');
    return result;
}

std::string joinScenarioPath(
    const std::string &directory,
    const std::string &leaf) {
    if (directory.empty()) return leaf;
    if (leaf.empty()) return directory;
    const char tail = directory.back();
    if (tail == '\\' || tail == '/') return directory + leaf;
    return directory + "/" + leaf;
}

EditorStoragePaths scenarioStoragePaths(
    const std::string &rootDirectory,
    const std::string &scenarioName) {
    const std::string clean = sanitizeScenarioFilename(scenarioName);
    const std::string native = clean + ".swscenario";
    EditorStoragePaths paths;
    paths.userCreated =
        joinScenarioPath(
            joinScenarioPath(rootDirectory, "scenarios"), native);
    paths.recent =
        joinScenarioPath(
            joinScenarioPath(rootDirectory, "recent"),
            "last.swscenario");
    paths.autosave =
        joinScenarioPath(
            joinScenarioPath(rootDirectory, "autosave"),
            clean + ".autosave.swscenario");
    paths.recovery =
        joinScenarioPath(
            joinScenarioPath(rootDirectory, "recovery"),
            "autosave.swscenario");
    paths.backup = paths.userCreated + ".bak";
    return paths;
}

EditableScenarioUndo::EditableScenarioUndo(
    size_t maxDepth,
    size_t byteBudget,
    NativeScenarioLimits limits)
    : maxDepth_(maxDepth),
      byteBudget_(byteBudget),
      limits_(std::move(limits)) {}

bool EditableScenarioUndo::encode(
    const EditableScenarioDocument &document,
    std::vector<uint8_t> &bytes,
    std::string *error) const {
    if (!encodeEditableScenario(document, bytes, error, limits_))
        return false;
    if (bytes.size() > byteBudget_) {
        setError(
            error,
            "undo snapshot exceeds the configured byte budget");
        bytes.clear();
        return false;
    }
    return true;
}

bool EditableScenarioUndo::restore(
    const std::vector<uint8_t> &bytes,
    EditableScenarioDocument &document,
    std::string *error) const {
    EditableScenarioDocument restored;
    if (!decodeEditableScenario(bytes, restored, error, limits_))
        return false;
    document = std::move(restored);
    return true;
}

bool EditableScenarioUndo::begin(
    const EditableScenarioDocument &document,
    std::string *error) {
    if (transactionOpen_) {
        setError(error, "an undo transaction is already open");
        return false;
    }
    if (!maxDepth_ || !byteBudget_) {
        setError(error, "undo is disabled by its depth or byte budget");
        return false;
    }
    std::vector<uint8_t> snapshot;
    if (!encode(document, snapshot, error)) return false;
    transaction_ = std::move(snapshot);
    transactionOpen_ = true;
    snapshotBytes_ += transaction_.size();
    return true;
}

bool EditableScenarioUndo::commit(
    const EditableScenarioDocument &document,
    std::string *error) {
    if (!transactionOpen_) {
        setError(error, "no undo transaction is open");
        return false;
    }
    std::vector<uint8_t> current;
    if (!encode(document, current, error)) {
        cancel();
        return false;
    }
    if (current == transaction_) {
        cancel();
        return true;
    }
    for (const std::vector<uint8_t> &snapshot : redo_)
        snapshotBytes_ -= snapshot.size();
    redo_.clear();
    redoGenerations_.clear();
    undo_.push_back(std::move(transaction_));
    undoGenerations_.push_back(generation_);
    transaction_.clear();
    transactionOpen_ = false;
    generation_ = ++nextGeneration_;
    trim();
    return true;
}

void EditableScenarioUndo::cancel() {
    if (transactionOpen_) snapshotBytes_ -= transaction_.size();
    transaction_.clear();
    transactionOpen_ = false;
}

bool EditableScenarioUndo::undo(
    EditableScenarioDocument &document,
    std::string *error) {
    if (transactionOpen_) {
        setError(error, "cannot undo while a transaction is open");
        return false;
    }
    if (undo_.empty()) {
        setError(error, "there is no undo snapshot");
        return false;
    }
    std::vector<uint8_t> current;
    if (!encode(document, current, error)) return false;
    EditableScenarioDocument restored;
    if (!decodeEditableScenario(
            undo_.back(), restored, error, limits_))
        return false;
    const size_t oldSize = undo_.back().size();
    const uint64_t oldGeneration = undoGenerations_.back();
    redo_.push_back(std::move(current));
    redoGenerations_.push_back(generation_);
    snapshotBytes_ += redo_.back().size();
    snapshotBytes_ -= oldSize;
    undo_.pop_back();
    undoGenerations_.pop_back();
    generation_ = oldGeneration;
    document = std::move(restored);
    trim();
    return true;
}

bool EditableScenarioUndo::redo(
    EditableScenarioDocument &document,
    std::string *error) {
    if (transactionOpen_) {
        setError(error, "cannot redo while a transaction is open");
        return false;
    }
    if (redo_.empty()) {
        setError(error, "there is no redo snapshot");
        return false;
    }
    std::vector<uint8_t> current;
    if (!encode(document, current, error)) return false;
    EditableScenarioDocument restored;
    if (!decodeEditableScenario(
            redo_.back(), restored, error, limits_))
        return false;
    const size_t oldSize = redo_.back().size();
    const uint64_t nextGeneration = redoGenerations_.back();
    undo_.push_back(std::move(current));
    undoGenerations_.push_back(generation_);
    snapshotBytes_ += undo_.back().size();
    snapshotBytes_ -= oldSize;
    redo_.pop_back();
    redoGenerations_.pop_back();
    generation_ = nextGeneration;
    document = std::move(restored);
    trim();
    return true;
}

void EditableScenarioUndo::clear() {
    undo_.clear();
    redo_.clear();
    undoGenerations_.clear();
    redoGenerations_.clear();
    transaction_.clear();
    transactionOpen_ = false;
    snapshotBytes_ = 0;
}

void EditableScenarioUndo::markClean() {
    cleanGeneration_ = generation_;
}

void EditableScenarioUndo::trim() {
    while (undo_.size() > maxDepth_) {
        snapshotBytes_ -= undo_.front().size();
        undo_.erase(undo_.begin());
        undoGenerations_.erase(undoGenerations_.begin());
    }
    while (redo_.size() > maxDepth_) {
        snapshotBytes_ -= redo_.front().size();
        redo_.erase(redo_.begin());
        redoGenerations_.erase(redoGenerations_.begin());
    }
    while (snapshotBytes_ > byteBudget_ && !undo_.empty()) {
        snapshotBytes_ -= undo_.front().size();
        undo_.erase(undo_.begin());
        undoGenerations_.erase(undoGenerations_.begin());
    }
    while (snapshotBytes_ > byteBudget_ && !redo_.empty()) {
        snapshotBytes_ -= redo_.front().size();
        redo_.erase(redo_.begin());
        redoGenerations_.erase(redoGenerations_.begin());
    }
}

} // namespace swgb
