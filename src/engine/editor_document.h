// SPDX-License-Identifier: GPL-3.0-or-later
// Scenario editor document model, native storage, validation and snapshot undo.
#pragma once

#include "../core/scenario.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace swgb {

class Assets;
namespace dat {
struct DatFile;
}

constexpr uint32_t kEditableScenarioDocumentVersion = 1;
constexpr uint32_t kSwScenarioFormatVersion = 1;

struct NativeScenarioLimits {
    size_t maxFileBytes = 32u * 1024u * 1024u;
    size_t maxPayloadBytes = 32u * 1024u * 1024u;
    size_t maxOriginalScxBytes = 16u * 1024u * 1024u;
    size_t maxStringBytes = 1024u * 1024u;
    size_t maxCollectionEntries = 100000u;
    size_t maxOpaqueRecordBytes = 4u * 1024u * 1024u;
    uint32_t maxMapDimension = 160;
    uint64_t maxMapTiles = 160u * 160u;
};

struct EditorMetadata {
    std::string title;
    std::string author;
    std::string description;
    std::string templateName;
    std::string sourceName;
    int64_t createdTimestamp = 0;
    int64_t modifiedTimestamp = 0;
    int64_t sourceTimestamp = 0;
    uint32_t editorVersion = kEditableScenarioDocumentVersion;
};

struct EditorPlayer {
    ScenarioPlayer scenario;
    int32_t team = 0;
    int32_t difficulty = 2;
    std::vector<uint32_t> researchedTechnologies;
    std::vector<uint32_t> researchedUnits;
    std::vector<uint32_t> researchedBuildings;
};

struct EditorInitialOrder {
    int32_t type = 0;
    uint32_t targetSpawnId = 0;
    float targetX = 0;
    float targetY = 0;
    int32_t argument = 0;
};

struct EditorObject {
    ScenarioUnit scenario;
    float hitPoints = -1;
    float resourceAmount = 0;
    std::vector<EditorInitialOrder> initialOrders;
    std::string customName;
};

struct EditorTriggerCondition {
    ScenarioCondition scenario;
    bool supported = true;
    bool readOnlyUnknown = false;
    uint32_t referencedAreaId = 0;
};

struct EditorTriggerEffect {
    ScenarioEffect scenario;
    bool supported = true;
    bool readOnlyUnknown = false;
    uint32_t referencedAreaId = 0;
};

struct EditorTrigger {
    bool enabled = false;
    bool looping = false;
    bool objective = false;
    int32_t objectiveOrder = -1;
    int32_t objectiveStringId = -1;
    std::string description;
    std::string name;
    std::vector<EditorTriggerEffect> effects;
    std::vector<int32_t> effectOrder;
    std::vector<EditorTriggerCondition> conditions;
    std::vector<int32_t> conditionOrder;
};

struct EditorMessages {
    std::string instructions;
    std::string objectives;
    std::string hints;
    std::string victory;
    std::string loss;
    std::string history;
    std::string scouts;
    std::string pregameCinematic;
    std::string victoryCinematic;
    std::string lossCinematic;
    std::string background;
};

struct EditorCamera {
    float x = -1;
    float y = -1;
    float mapX = -1;
    float mapY = -1;
};

struct EditorVictory {
    ScenarioVictory scenario;
    int32_t type = 0;
};

struct EditorArea {
    uint32_t id = 0;
    std::string name;
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;
};

enum class EditorAiStorage : uint8_t {
    None = 0,
    Referenced = 1,
    Embedded = 2,
};

struct EditorAiMetadata {
    uint32_t player = 0;
    EditorAiStorage storage = EditorAiStorage::None;
    std::string name;
    std::string path;
    std::string cityName;
    std::string personalityName;
    std::string script;
    std::string city;
    std::string personality;
};

struct EditorOpaqueRecord {
    uint32_t type = 0;
    std::vector<uint8_t> bytes;
};

// SCX parser fields that do not have a direct editor concept. They remain
// semantic and round-trip through the native sidecar.
struct EditorScxCompatibility {
    std::string version;
    int32_t saveType = 0;
    uint32_t lastSaveTime = 0;
    uint32_t enabledPlayerCount = 0;
    uint32_t nextUnitId = 0;
    float playerDataVersion = 0;
    std::string originalFilename;
    bool allTechnologies = false;
    double triggerSystemVersion = 0;
    uint8_t objectiveState = 0;
    std::array<uint32_t, 16> civilizations{};
    std::array<ScenarioPlayer, 8> auxiliaryPlayers{};
};

struct EditableScenarioDocument {
    uint32_t documentVersion = kEditableScenarioDocumentVersion;
    EditorMetadata metadata;
    ScenarioMap map;
    std::vector<EditorPlayer> players;
    std::vector<EditorObject> objects;
    std::vector<EditorTrigger> triggers;
    std::vector<uint32_t> triggerOrder;
    EditorMessages messages;
    EditorCamera camera;
    EditorVictory victory;
    std::vector<EditorArea> areas;
    std::vector<EditorAiMetadata> ai;
    EditorScxCompatibility scx;
    std::vector<EditorOpaqueRecord> unknownRecords;

    // Immutable-by-convention source data. Editing APIs never rewrite it.
    std::vector<uint8_t> originalScxBytes;
    bool hasImportFingerprint = false;
    uint64_t importSemanticFingerprint = 0;
};

uint64_t editableScenarioSemanticFingerprint(
    const EditableScenarioDocument &document);

bool encodeEditableScenario(
    const EditableScenarioDocument &document,
    std::vector<uint8_t> &bytes,
    std::string *error = nullptr,
    const NativeScenarioLimits &limits = NativeScenarioLimits{});
bool decodeEditableScenario(
    const std::vector<uint8_t> &bytes,
    EditableScenarioDocument &document,
    std::string *error = nullptr,
    const NativeScenarioLimits &limits = NativeScenarioLimits{});
bool saveEditableScenario(
    const std::string &path,
    const EditableScenarioDocument &document,
    std::string *error = nullptr,
    const NativeScenarioLimits &limits = NativeScenarioLimits{});
bool loadEditableScenario(
    const std::string &path,
    EditableScenarioDocument &document,
    std::string *error = nullptr,
    const NativeScenarioLimits &limits = NativeScenarioLimits{});

bool importScxBytes(
    const std::vector<uint8_t> &bytes,
    EditableScenarioDocument &document,
    std::string *error = nullptr,
    const NativeScenarioLimits &limits = NativeScenarioLimits{});
bool importScxFile(
    const std::string &path,
    EditableScenarioDocument &document,
    std::string *error = nullptr,
    const NativeScenarioLimits &limits = NativeScenarioLimits{});
bool exportScxBytes(
    const EditableScenarioDocument &document,
    std::vector<uint8_t> &bytes,
    std::string *error = nullptr);
bool exportScxFile(
    const std::string &path,
    const EditableScenarioDocument &document,
    std::string *error = nullptr);

enum class ScenarioTemplate : uint8_t {
    BlankLand = 0,
    Islands = 1,
    SkirmishBase = 2,
    TriggerTutorial = 3,
};

bool makeScenarioTemplate(
    ScenarioTemplate scenarioTemplate,
    uint32_t mapSize,
    uint32_t seed,
    uint32_t playerCount,
    EditableScenarioDocument &document,
    std::string *error = nullptr);

enum class ValidationSeverity : uint8_t {
    Warning = 0,
    Error = 1,
};

enum class ValidationFocusKind : uint8_t {
    Document = 0,
    Metadata,
    Map,
    Tile,
    Player,
    Object,
    Trigger,
    TriggerCondition,
    TriggerEffect,
    Area,
    Ai,
    Victory,
    Storage,
    ScxExport,
};

struct ValidationFocus {
    ValidationFocusKind kind = ValidationFocusKind::Document;
    uint64_t id = 0;
    std::string text;
};

struct ValidationIssue {
    ValidationSeverity severity = ValidationSeverity::Error;
    std::string code;
    std::string message;
    ValidationFocus focus;
};

struct ValidationOptions {
    uint32_t vitaMapDimensionCap = 160;
    uint64_t vitaTileCap = 160u * 160u;
    size_t estimatedMemoryCapBytes = 96u * 1024u * 1024u;
    NativeScenarioLimits nativeLimits;
    bool requireScxExportSupport = false;
};

struct ValidationReport {
    std::vector<ValidationIssue> issues;
    bool hasErrors() const;
};

ValidationReport validateEditableScenario(
    const EditableScenarioDocument &document,
    const dat::DatFile *dat = nullptr,
    const ValidationOptions &options = ValidationOptions{});
ValidationReport validateEditableScenario(
    const EditableScenarioDocument &document,
    const Assets &assets,
    const ValidationOptions &options = ValidationOptions{});

std::string sanitizeScenarioFilename(const std::string &name);
std::string joinScenarioPath(const std::string &directory, const std::string &leaf);

struct EditorStoragePaths {
    std::string userCreated;
    std::string recent;
    std::string autosave;
    std::string recovery;
    std::string backup;
};

EditorStoragePaths scenarioStoragePaths(
    const std::string &rootDirectory,
    const std::string &scenarioName);

class EditableScenarioUndo {
public:
    explicit EditableScenarioUndo(
        size_t maxDepth = 64,
        size_t byteBudget = 32u * 1024u * 1024u,
        NativeScenarioLimits limits = NativeScenarioLimits{});

    bool begin(
        const EditableScenarioDocument &document,
        std::string *error = nullptr);
    bool commit(
        const EditableScenarioDocument &document,
        std::string *error = nullptr);
    void cancel();
    bool undo(
        EditableScenarioDocument &document,
        std::string *error = nullptr);
    bool redo(
        EditableScenarioDocument &document,
        std::string *error = nullptr);
    void clear();
    void markClean();

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    bool transactionOpen() const { return transactionOpen_; }
    bool dirty() const { return generation_ != cleanGeneration_; }
    uint64_t generation() const { return generation_; }
    size_t snapshotBytes() const { return snapshotBytes_; }

private:
    bool encode(
        const EditableScenarioDocument &document,
        std::vector<uint8_t> &bytes,
        std::string *error) const;
    bool restore(
        const std::vector<uint8_t> &bytes,
        EditableScenarioDocument &document,
        std::string *error) const;
    void trim();

    size_t maxDepth_;
    size_t byteBudget_;
    NativeScenarioLimits limits_;
    std::vector<std::vector<uint8_t>> undo_;
    std::vector<std::vector<uint8_t>> redo_;
    std::vector<uint64_t> undoGenerations_;
    std::vector<uint64_t> redoGenerations_;
    std::vector<uint8_t> transaction_;
    bool transactionOpen_ = false;
    size_t snapshotBytes_ = 0;
    uint64_t generation_ = 0;
    uint64_t cleanGeneration_ = 0;
    uint64_t nextGeneration_ = 0;
};

} // namespace swgb
