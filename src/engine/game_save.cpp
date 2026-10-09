// SPDX-License-Identifier: GPL-3.0-or-later
#include "game.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <sstream>
#include <tuple>
#include <type_traits>

namespace swgb {
namespace {

constexpr char kSaveMagic[8] = {
    'S', 'W', 'G', 'B', 'S', 'A', 'V', 'E'};
constexpr uint32_t kSaveVersion = 8;
constexpr uint32_t kOldestSaveVersion = 1;
constexpr size_t kMaxSaveBytes = 32u * 1024u * 1024u;
constexpr uint32_t kMaxObjects = 20000;
constexpr uint32_t kMaxCollection = 1000000;

class Writer {
public:
    template <typename T>
    void scalar(const T &value) {
        static_assert(
            std::is_trivially_copyable<T>::value,
            "save scalar must be trivially copyable");
        const uint8_t *bytes =
            reinterpret_cast<const uint8_t *>(
                &value);
        data.insert(
            data.end(), bytes, bytes + sizeof(T));
    }

    void string(const std::string &value) {
        const uint32_t size =
            (uint32_t)value.size();
        scalar(size);
        data.insert(
            data.end(), value.begin(), value.end());
    }

    template <typename T>
    void scalars(const std::vector<T> &values) {
        const uint32_t size =
            (uint32_t)values.size();
        scalar(size);
        for (const T &value : values)
            scalar(value);
    }

    std::vector<uint8_t> data;
};

class Reader {
public:
    Reader(
        const uint8_t *data, size_t size)
        : data_(data), size_(size) {}

    template <typename T>
    bool scalar(T &value) {
        static_assert(
            std::is_trivially_copyable<T>::value,
            "save scalar must be trivially copyable");
        if (offset_ > size_ ||
            sizeof(T) > size_ - offset_)
            return fail("truncated save payload");
        std::memcpy(
            &value, data_ + offset_, sizeof(T));
        offset_ += sizeof(T);
        return true;
    }

    bool string(
        std::string &value,
        uint32_t maximum = 1024 * 1024) {
        uint32_t size = 0;
        if (!scalar(size)) return false;
        if (size > maximum ||
            offset_ > size_ ||
            size > size_ - offset_)
            return fail("invalid save string length");
        value.assign(
            reinterpret_cast<const char *>(
                data_ + offset_),
            size);
        offset_ += size;
        return true;
    }

    template <typename T>
    bool scalars(
        std::vector<T> &values,
        uint32_t maximum = kMaxCollection) {
        uint32_t size = 0;
        if (!scalar(size)) return false;
        if (size > maximum)
            return fail("save collection exceeds limit");
        values.resize(size);
        for (T &value : values)
            if (!scalar(value)) return false;
        return true;
    }

    bool finished() const {
        return ok_ && offset_ == size_;
    }
    bool ok() const { return ok_; }
    const std::string &error() const {
        return error_;
    }
    bool fail(const std::string &error) {
        if (ok_) error_ = error;
        ok_ = false;
        return false;
    }

private:
    const uint8_t *data_ = nullptr;
    size_t size_ = 0;
    size_t offset_ = 0;
    bool ok_ = true;
    std::string error_;
};

uint32_t checksum(
    const uint8_t *data, size_t size) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

void writeSettings(
    Writer &writer,
    const SkirmishSettings &settings) {
    writer.scalar(settings.seed);
    writer.scalar(settings.mapSize);
    writer.scalar(settings.playerCivilization);
    writer.scalar(settings.computerCivilization);
    writer.scalar(settings.difficulty);
    writer.scalar(settings.personality);
    writer.scalar(settings.allied);
    writer.scalar(settings.mapStyle);
    writer.scalar(settings.startingResources);
    writer.scalar(settings.populationCap);
    writer.scalar(settings.victory);
    for (const SkirmishSlot &slot :
         settings.slots) {
        writer.scalar(slot.type);
        writer.string(slot.name);
        writer.scalar(slot.color);
        writer.scalar(slot.civilization);
        writer.scalar(slot.personality);
        writer.scalar(slot.difficulty);
        writer.scalar(slot.team);
        writer.scalar(slot.alliedVictory);
    }
    writer.scalar(settings.startingTechLevel);
    writer.scalar(settings.endingTechLevel);
    writer.scalar(settings.reveal);
    writer.scalar(settings.teamsLocked);
    writer.scalar(settings.cheatsEnabled);
    writer.scalar(settings.gameSpeed);
    writer.scalar(settings.timeLimitMinutes);
    writer.scalar(settings.scoreLimit);
    writer.scalar(settings.mapType);
    writer.scalar(settings.mapSizeIndex);
    writer.scalar(settings.gameType);
    writer.scalar(settings.resourceLevel);
    writer.scalar(settings.fixedPositions);
}

bool readSettings(
    Reader &reader,
    SkirmishSettings &settings,
    uint32_t version) {
    uint8_t encodedMapStyle = 0;
    if (!(reader.scalar(settings.seed) &&
           reader.scalar(settings.mapSize) &&
           reader.scalar(
               settings.playerCivilization) &&
           reader.scalar(
               settings.computerCivilization) &&
           reader.scalar(settings.difficulty) &&
           reader.scalar(settings.personality) &&
           reader.scalar(settings.allied) &&
           reader.scalar(encodedMapStyle) &&
           reader.scalar(
               settings.startingResources) &&
           reader.scalar(settings.populationCap) &&
           reader.scalar(settings.victory)))
        return false;
    if (version <= 5) {
        switch (encodedMapStyle) {
        case 0:
            settings.mapStyle =
                SkirmishMapStyle::Grasslands;
            break;
        case 1:
            settings.mapStyle =
                SkirmishMapStyle::Archipelago;
            break;
        case 2:
            settings.mapStyle =
                SkirmishMapStyle::CompactIslands;
            break;
        case 3:
            settings.mapStyle =
                SkirmishMapStyle::LegacyRandom;
            break;
        default:
            return reader.fail(
                "save contains invalid legacy map style");
        }
        settings.slots = SkirmishSettings().slots;
        settings.slots[0].civilization =
            (uint8_t)settings.playerCivilization;
        settings.slots[1].civilization =
            (uint8_t)settings.computerCivilization;
        settings.slots[1].difficulty =
            (uint8_t)settings.difficulty;
        settings.slots[1].personality =
            settings.personality;
        settings.slots[0].team =
            settings.allied ? 1 : 1;
        settings.slots[1].team =
            settings.allied ? 1 : 2;
        settings.slots[0].alliedVictory =
            settings.allied;
        settings.slots[1].alliedVictory =
            settings.allied;
        // These options were not serialized before v6. Match the
        // historical unrestricted generated-match behavior so a
        // migrated save compares against its legacy initialization.
        settings.teamsLocked = false;
        settings.cheatsEnabled = true;
    } else {
        settings.mapStyle =
            (SkirmishMapStyle)encodedMapStyle;
        for (SkirmishSlot &slot :
             settings.slots) {
            if (!reader.scalar(slot.type) ||
                !reader.string(slot.name, 64) ||
                !reader.scalar(slot.color) ||
                !reader.scalar(slot.civilization) ||
                !reader.scalar(slot.personality) ||
                !reader.scalar(slot.difficulty) ||
                !reader.scalar(slot.team) ||
                !reader.scalar(
                    slot.alliedVictory))
                return false;
        }
        if (!reader.scalar(
                settings.startingTechLevel) ||
            !reader.scalar(
                settings.endingTechLevel) ||
            !reader.scalar(settings.reveal) ||
            !reader.scalar(settings.teamsLocked) ||
            !reader.scalar(
                settings.cheatsEnabled) ||
            !reader.scalar(settings.gameSpeed) ||
            !reader.scalar(
                settings.timeLimitMinutes) ||
            !reader.scalar(settings.scoreLimit))
            return false;
    }
    if (version >= 8 &&
        !(reader.scalar(settings.mapType) &&
          reader.scalar(settings.mapSizeIndex) &&
          reader.scalar(settings.gameType) &&
          reader.scalar(settings.resourceLevel) &&
          reader.scalar(settings.fixedPositions)))
        return false;
    if (version == 1) {
        const uint8_t old =
            (uint8_t)settings.victory;
        if (old == 0)
            settings.victory =
                SkirmishVictory::Conquest;
        else if (old == 1)
            settings.victory =
                SkirmishVictory::CommandCenter;
        else
            return reader.fail(
                "save contains invalid legacy victory mode");
    }
    return true;
}

bool sameSettings(
    const SkirmishSettings &left,
    const SkirmishSettings &right) {
    return left.seed == right.seed &&
           left.mapSize == right.mapSize &&
           left.mapType == right.mapType &&
           left.mapSizeIndex == right.mapSizeIndex &&
           left.gameType == right.gameType &&
           left.resourceLevel == right.resourceLevel &&
           left.fixedPositions == right.fixedPositions &&
           left.playerCivilization ==
               right.playerCivilization &&
           left.computerCivilization ==
               right.computerCivilization &&
           left.difficulty == right.difficulty &&
           left.personality == right.personality &&
           left.allied == right.allied &&
           left.mapStyle == right.mapStyle &&
           left.startingResources ==
               right.startingResources &&
           left.populationCap ==
               right.populationCap &&
           left.victory == right.victory &&
           left.startingTechLevel ==
               right.startingTechLevel &&
           left.endingTechLevel ==
               right.endingTechLevel &&
           left.reveal == right.reveal &&
           left.teamsLocked ==
               right.teamsLocked &&
           left.cheatsEnabled ==
               right.cheatsEnabled &&
           left.gameSpeed == right.gameSpeed &&
           left.timeLimitMinutes ==
               right.timeLimitMinutes &&
           left.scoreLimit ==
               right.scoreLimit &&
           std::equal(
               left.slots.begin(),
               left.slots.end(),
               right.slots.begin(),
               [](const SkirmishSlot &a,
                  const SkirmishSlot &b) {
                   return a.type == b.type &&
                          a.name == b.name &&
                          a.color == b.color &&
                          a.civilization ==
                              b.civilization &&
                          a.personality ==
                              b.personality &&
                          a.difficulty ==
                              b.difficulty &&
                          a.team == b.team &&
                          a.alliedVictory ==
                              b.alliedVictory;
               });
}

bool readFile(
    const std::string &path,
    std::vector<uint8_t> &bytes,
    std::string *err) {
    FILE *file = std::fopen(path.c_str(), "rb");
    if (!file) {
        if (err)
            *err = "could not open save: " +
                   std::string(std::strerror(errno));
        return false;
    }
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        if (err) *err = "could not size save file";
        return false;
    }
    const long length = std::ftell(file);
    if (length < 0 ||
        (size_t)length > kMaxSaveBytes) {
        std::fclose(file);
        if (err) *err = "save file exceeds 32 MiB limit";
        return false;
    }
    std::rewind(file);
    bytes.resize((size_t)length);
    const bool read =
        bytes.empty() ||
        std::fread(
            bytes.data(), 1, bytes.size(), file) ==
            bytes.size();
    const bool failed = std::ferror(file) != 0;
    std::fclose(file);
    if (!read || failed) {
        if (err) *err = "could not read complete save file";
        return false;
    }
    return true;
}

bool payloadFromFile(
    const std::string &path,
    std::vector<uint8_t> &bytes,
    const uint8_t *&payload,
    size_t &payloadSize,
    uint32_t &version,
    std::string *err) {
    if (!readFile(path, bytes, err)) return false;
    constexpr size_t headerSize = 20;
    if (bytes.size() < headerSize ||
        std::memcmp(
            bytes.data(), kSaveMagic,
            sizeof kSaveMagic) != 0) {
        if (err) *err = "save file has invalid magic";
        return false;
    }
    Reader header(
        bytes.data() + sizeof kSaveMagic,
        headerSize - sizeof kSaveMagic);
    version = 0;
    uint32_t encodedSize = 0;
    uint32_t encodedChecksum = 0;
    if (!header.scalar(version) ||
        !header.scalar(encodedSize) ||
        !header.scalar(encodedChecksum) ||
        version < kOldestSaveVersion ||
        version > kSaveVersion) {
        if (err)
            *err = version < kOldestSaveVersion ||
                           version > kSaveVersion
                       ? "unsupported save version"
                       : "truncated save header";
        return false;
    }
    if (encodedSize > kMaxSaveBytes ||
        encodedSize != bytes.size() - headerSize) {
        if (err) *err = "save payload size mismatch";
        return false;
    }
    payload = bytes.data() + headerSize;
    payloadSize = encodedSize;
    if (checksum(payload, payloadSize) !=
        encodedChecksum) {
        if (err) *err = "save checksum mismatch";
        return false;
    }
    return true;
}

bool writeAtomic(
    const std::string &path,
    const std::vector<uint8_t> &bytes,
    std::string *err) {
    const std::string temporary = path + ".tmp";
    const std::string backup = path + ".bak";
    FILE *file =
        std::fopen(temporary.c_str(), "wb");
    if (!file) {
        if (err)
            *err = "could not create temporary save: " +
                   std::string(std::strerror(errno));
        return false;
    }
    const bool written =
        std::fwrite(
            bytes.data(), 1, bytes.size(), file) ==
            bytes.size() &&
        std::fflush(file) == 0;
    const int closed = std::fclose(file);
    if (!written || closed != 0) {
        std::remove(temporary.c_str());
        if (err) *err = "could not write complete save";
        return false;
    }
    std::remove(backup.c_str());
    const bool hadOriginal =
        std::rename(
            path.c_str(), backup.c_str()) == 0;
    if (std::rename(
            temporary.c_str(), path.c_str()) != 0) {
        if (hadOriginal)
            std::rename(
                backup.c_str(), path.c_str());
        std::remove(temporary.c_str());
        if (err)
            *err = "could not replace save: " +
                   std::string(std::strerror(errno));
        return false;
    }
    if (hadOriginal) std::remove(backup.c_str());
    return true;
}

template <typename K, typename V>
void writeMap(
    Writer &writer,
    const std::map<K, V> &values) {
    writer.scalar((uint32_t)values.size());
    for (const auto &entry : values) {
        writer.scalar(entry.first);
        writer.scalar(entry.second);
    }
}

template <typename K, typename V>
bool readMap(
    Reader &reader, std::map<K, V> &values,
    uint32_t maximum = kMaxCollection) {
    uint32_t size = 0;
    if (!reader.scalar(size) || size > maximum)
        return size <= maximum
                   ? false
                   : reader.fail(
                         "save map exceeds limit");
    values.clear();
    for (uint32_t i = 0; i < size; ++i) {
        K key{};
        V value{};
        if (!reader.scalar(key) ||
            !reader.scalar(value))
            return false;
        values[key] = value;
    }
    return true;
}

template <typename T>
void writeSet(
    Writer &writer,
    const std::set<T> &values) {
    writer.scalar((uint32_t)values.size());
    for (const T &value : values)
        writer.scalar(value);
}

template <typename T>
bool readSet(
    Reader &reader, std::set<T> &values,
    uint32_t maximum = kMaxCollection) {
    uint32_t size = 0;
    if (!reader.scalar(size) || size > maximum)
        return size <= maximum
                   ? false
                   : reader.fail(
                         "save set exceeds limit");
    values.clear();
    for (uint32_t i = 0; i < size; ++i) {
        T value{};
        if (!reader.scalar(value)) return false;
        values.insert(value);
    }
    return true;
}

} // namespace

bool Game::readSaveSettings(
    const std::string &path,
    SkirmishSettings &settings,
    std::string *err) {
    MatchSaveMetadata metadata;
    if (!readSaveMetadata(path, metadata, err))
        return false;
    if (metadata.kind != MatchSaveKind::Skirmish) {
        if (err) *err = "save contains a campaign match";
        return false;
    }
    settings = metadata.skirmish;
    return true;
}

bool Game::readSaveMetadata(
    const std::string &path,
    MatchSaveMetadata &metadata,
    std::string *err) {
    std::vector<uint8_t> bytes;
    const uint8_t *payload = nullptr;
    size_t payloadSize = 0;
    uint32_t version = 0;
    if (!payloadFromFile(
            path, bytes, payload, payloadSize,
            version, err))
        return false;
    Reader reader(payload, payloadSize);
    MatchSaveMetadata loaded;
    if (version >= 3) {
        uint8_t kind = 0;
        if (!reader.scalar(kind) ||
            kind > (uint8_t)MatchSaveKind::Campaign ||
            !reader.string(
                loaded.campaignArchive, 128) ||
            !reader.scalar(loaded.campaignEntry)) {
            if (err)
                *err = reader.ok()
                           ? "save contains invalid match metadata"
                           : reader.error();
            return false;
        }
        loaded.kind = (MatchSaveKind)kind;
        if (loaded.kind == MatchSaveKind::Skirmish &&
            (!loaded.campaignArchive.empty() ||
             loaded.campaignEntry != 0)) {
            if (err)
                *err = "skirmish save contains campaign metadata";
            return false;
        }
    }
    if (!readSettings(
            reader, loaded.skirmish, version)) {
        if (err) *err = reader.error();
        return false;
    }
    const SkirmishSettings &settings =
        loaded.skirmish;
    if (loaded.kind == MatchSaveKind::Skirmish &&
        ((version >= 6 &&
          !validateSkirmishSettings(
              settings, err)) ||
         (version < 6 &&
          (settings.mapSize < 48 ||
           settings.mapSize > 192)) ||
         settings.playerCivilization < 1 ||
         settings.playerCivilization > 8 ||
         settings.computerCivilization < 1 ||
         settings.computerCivilization > 8 ||
         settings.populationCap < 25 ||
         settings.populationCap > 250 ||
         settings.startingResources < 0 ||
         settings.victory <
             SkirmishVictory::Standard ||
         settings.victory >
             SkirmishVictory::CommandCenter)) {
        if (err && err->empty())
            *err = "save contains invalid match settings";
        return false;
    }
    if (loaded.kind == MatchSaveKind::Campaign &&
        (loaded.campaignArchive.empty() ||
         loaded.campaignEntry >= 64)) {
        if (err) *err = "save contains invalid campaign metadata";
        return false;
    }
    metadata = std::move(loaded);
    return true;
}

bool Game::saveMatch(
    const std::string &path,
    std::string *err) const {
    if (mapSize_ <= 0) {
        if (err)
            *err = "no active match can be saved";
        return false;
    }
    Writer writer;
    writer.scalar((uint8_t)saveKind_);
    writer.string(campaignArchive_);
    writer.scalar(campaignEntry_);
    writeSettings(writer, currentSkirmishSettings_);
    writer.scalar(simulationTime_);
    writer.scalar(mapSize_);
    writer.scalar(localPlayer_);
    writer.scalar(nextSpawnId_);
    writer.scalar(nextMoveGroupId_);
    writer.scalar(difficulty_);
    writer.scalar(victoryCondition_);
    writer.scalar(victoryState_);
    writer.scalar(conquestEnabled_);
    writer.scalar(reveal_);
    writer.scalar(gameSpeed_);
    writer.scalar(cheatsEnabled_);
    writer.scalar(teamsLocked_);
    writer.scalar(conquestCheckTime_);
    writer.scalar(victoryCountdownPlayer_);
    writer.scalar(victoryCountdownKind_);
    writer.scalar(victoryCountdownRemaining_);
    writer.scalar(standardVictoryCountdown_);
    writer.scalar(timeLimitSeconds_);
    writer.scalar(scoreLimit_);
    writer.scalar(
        monumentVictoryCountdowns_);
    writer.scalar(
        holocronVictoryCountdowns_);
    writer.scalar(camX_);
    writer.scalar(camY_);
    writer.scalar(zoom_);
    writer.scalar(animClock_);
    writer.scalar(selectedFormation_);
    writer.scalar(forceBuildCheat_);
    writer.scalar(fullTechTreeCheat_);
    writer.scalar(forceExploreCheat_);
    writer.scalar(forceSightCheat_);
    writer.scalar(enemyIntelligenceCheat_);
    writer.scalar(techGeneration_);
    writer.scalar(reseedQueue_);

    std::ostringstream randomState;
    randomState << rng_;
    writer.string(randomState.str());

    writer.scalars(terrain_);
    writer.scalars(cornerElevation_);
    writer.scalars(tileElevation_);
    writer.scalars(tileSlope_);
    for (const auto &tiles : exploredTiles_)
        writer.scalars(tiles);
    for (const auto &tiles : visibleTiles_)
        writer.scalars(tiles);

    for (const ScenarioPlayer &player : players_) {
        writer.string(player.name);
        writer.scalar(player.civilization);
        writer.scalar(player.color);
        writer.scalar(player.active);
        writer.scalar(player.human);
        writer.scalar(player.alliedVictory);
        writer.scalar(player.cameraX);
        writer.scalar(player.cameraY);
        writer.scalar(player.resources);
        writer.scalar(player.diplomacy);
        writer.scalar(player.populationLimit);
    }
    for (const auto &values : resources_)
        writeMap(writer, values);
    for (size_t i = 0; i < 3; ++i) {
        writer.scalar(commodityPrice_[i]);
        writer.scalar(commodityCounter_[i]);
        writer.scalar(commodityAccumulator_[i]);
    }
    for (const auto &values : researchedTechs_)
        writeSet(writer, values);
    for (const auto &values : disabledTechs_)
        writeSet(writer, values);
    for (const auto &values : disabledUnits_)
        writeSet(writer, values);

    writer.scalar((uint32_t)objects_.size());
    for (const Object &object : objects_) {
        const int unitId =
            object.unit ? object.unit->id : -1;
        const int gateClosedId =
            object.gateClosedUnit
                ? object.gateClosedUnit->id
                : -1;
        const int gateOpenId =
            object.gateOpenUnit
                ? object.gateOpenUnit->id
                : -1;
        const int gateEndId =
            object.gateEndUnit
                ? object.gateEndUnit->id
                : -1;
        const int jobUnitId =
            object.jobUnit ? object.jobUnit->id : -1;
        writer.scalar(unitId);
        writer.scalar(gateClosedId);
        writer.scalar(gateOpenId);
        writer.scalar(gateEndId);
        writer.scalar(jobUnitId);
        writer.scalar(
            (uint32_t)object.productionQueue.size());
        for (const ProductionItem &item :
             object.productionQueue) {
            const int itemUnit =
                item.unit ? item.unit->id : -1;
            writer.scalar(itemUnit);
            writer.scalar(item.technologyId);
            writer.scalar(item.duration);
        }
#define SAVE_FIELD(name) writer.scalar(object.name)
        SAVE_FIELD(player);
        SAVE_FIELD(x);
        SAVE_FIELD(y);
        SAVE_FIELD(facing);
        SAVE_FIELD(state);
        SAVE_FIELD(animTime);
        SAVE_FIELD(stateTime);
        SAVE_FIELD(targetX);
        SAVE_FIELD(targetY);
        SAVE_FIELD(hitPoints);
        SAVE_FIELD(maxHitPoints);
        SAVE_FIELD(shieldPoints);
        SAVE_FIELD(maxShieldPoints);
        SAVE_FIELD(shieldRegenerationTime);
        SAVE_FIELD(shieldDrainTime);
        SAVE_FIELD(resourceAmount);
        SAVE_FIELD(carriedAmount);
        SAVE_FIELD(stash);
        SAVE_FIELD(resourceType);
        SAVE_FIELD(carriedResourceType);
        writer.scalar((uint32_t)object.path.size());
        for (const auto &point : object.path)
            writer.scalar(point);
        writer.scalar(
            (uint64_t)object.pathIndex);
        SAVE_FIELD(blockedTime);
        SAVE_FIELD(moveRetryTime);
        SAVE_FIELD(moveStallTime);
        SAVE_FIELD(moveBestDistance);
        SAVE_FIELD(moveSpeedLimit);
        SAVE_FIELD(attackCooldown);
        SAVE_FIELD(attackRepathTime);
        SAVE_FIELD(attackApproachAngle);
        SAVE_FIELD(attackApproachDistance);
        SAVE_FIELD(attackStallTime);
        SAVE_FIELD(attackBestDistance);
        SAVE_FIELD(autoAcquireTime);
        SAVE_FIELD(attackTargetId);
        SAVE_FIELD(moveGroupId);
        SAVE_FIELD(attackSlotRetries);
        SAVE_FIELD(moveSpreadRetries);
        SAVE_FIELD(homeX);
        SAVE_FIELD(homeY);
        SAVE_FIELD(moveAnchorX);
        SAVE_FIELD(moveAnchorY);
        SAVE_FIELD(attackMode);
        SAVE_FIELD(attackAutomatic);
        SAVE_FIELD(attackShotPending);
        SAVE_FIELD(attackGroundActive);
        SAVE_FIELD(attackGroundX);
        SAVE_FIELD(attackGroundY);
        SAVE_FIELD(patrolActive);
        SAVE_FIELD(patrolTowardEnd);
        SAVE_FIELD(patrolStartX);
        SAVE_FIELD(patrolStartY);
        SAVE_FIELD(patrolEndX);
        SAVE_FIELD(patrolEndY);
        SAVE_FIELD(guardTargetId);
        SAVE_FIELD(followTargetId);
        SAVE_FIELD(moveGoalActive);
        SAVE_FIELD(wander);
        SAVE_FIELD(drawShadows);
        SAVE_FIELD(active);
        SAVE_FIELD(hidden);
        SAVE_FIELD(draw);
        SAVE_FIELD(locked);
        SAVE_FIELD(gate);
        SAVE_FIELD(underConstruction);
        SAVE_FIELD(manualDropOff);
        SAVE_FIELD(selected);
        SAVE_FIELD(triggerAddressable);
        SAVE_FIELD(customKind);
        SAVE_FIELD(flashTime);
        SAVE_FIELD(gateOpenAmount);
        SAVE_FIELD(gateCloseTimer);
        SAVE_FIELD(felled);
        SAVE_FIELD(jobKind);
        SAVE_FIELD(annexParentId);
        SAVE_FIELD(carcassId);
        SAVE_FIELD(carcassClass);
        SAVE_FIELD(carcassDecay);
        SAVE_FIELD(carcassHidden);
        SAVE_FIELD(huntTimer);
        SAVE_FIELD(resourceWorkTargetId);
        SAVE_FIELD(resourceWorkTime);
        SAVE_FIELD(damageSoundTime);
        SAVE_FIELD(farmStage);
        writer.scalars(object.farmUnderlay);
        SAVE_FIELD(farmMoveTime);
        SAVE_FIELD(farmMoveStep);
        SAVE_FIELD(farmMoveActive);
        SAVE_FIELD(pathGoalId);
        SAVE_FIELD(pathGoalClearance);
        SAVE_FIELD(repathCount);
        SAVE_FIELD(detourTime);
        SAVE_FIELD(approachRetry);
        SAVE_FIELD(rallyActive);
        SAVE_FIELD(rallyX);
        SAVE_FIELD(rallyY);
        SAVE_FIELD(rallyTargetId);
        SAVE_FIELD(volleyRemaining);
        SAVE_FIELD(volleyTimer);
        SAVE_FIELD(volleyTargetId);
        SAVE_FIELD(lastGatherClass);
        SAVE_FIELD(lastGatherType);
        SAVE_FIELD(lastGatherX);
        SAVE_FIELD(lastGatherY);
        SAVE_FIELD(marchGroupId);
        SAVE_FIELD(marchSpeed);
        SAVE_FIELD(marchRepath);
        SAVE_FIELD(blockerId);
        SAVE_FIELD(productionRemaining);
        SAVE_FIELD(productionPopulationBlocked);
        SAVE_FIELD(constructionRemaining);
        SAVE_FIELD(constructionTotal);
        SAVE_FIELD(constructionBuilderId);
        SAVE_FIELD(constructionTargetId);
        SAVE_FIELD(gatherTargetId);
        SAVE_FIELD(dropOffTargetId);
        SAVE_FIELD(repairTargetId);
        SAVE_FIELD(garrisonTargetId);
        SAVE_FIELD(garrisonDamageLocked);
        SAVE_FIELD(spawnId);
        SAVE_FIELD(discoveredByPlayers);
        SAVE_FIELD(garrisonedInId);
        SAVE_FIELD(initialFrame);
        SAVE_FIELD(conversionTargetId);
        SAVE_FIELD(conversionProgress);
        SAVE_FIELD(conversionRecharge);
        SAVE_FIELD(holocronTargetId);
        SAVE_FIELD(carriedHolocronId);
        SAVE_FIELD(carriedById);
        SAVE_FIELD(triggerAttack);
        SAVE_FIELD(frozen);
        writer.string(object.triggerName);
        SAVE_FIELD(tradeMarketId);
        SAVE_FIELD(tradeHomeId);
        SAVE_FIELD(tradeCarrying);
        SAVE_FIELD(conversionCountdown);
#undef SAVE_FIELD
    }

    writer.scalar((uint32_t)projectiles_.size());
    for (const Projectile &projectile : projectiles_) {
        const int unitId =
            projectile.unit
                ? projectile.unit->id
                : -1;
        writer.scalar(unitId);
#define SAVE_PROJECTILE(name) writer.scalar(projectile.name)
        SAVE_PROJECTILE(player);
        SAVE_PROJECTILE(x);
        SAVE_PROJECTILE(y);
        SAVE_PROJECTILE(z);
        SAVE_PROJECTILE(targetZ);
        SAVE_PROJECTILE(facing);
        SAVE_PROJECTILE(animTime);
        SAVE_PROJECTILE(targetId);
        SAVE_PROJECTILE(sourceId);
        SAVE_PROJECTILE(damage);
        SAVE_PROJECTILE(groundAimed);
        SAVE_PROJECTILE(aimX);
        SAVE_PROJECTILE(aimY);
        SAVE_PROJECTILE(blastWidth);
        SAVE_PROJECTILE(blastLevel);
        SAVE_PROJECTILE(arcHeight);
        SAVE_PROJECTILE(arcVelocity);
        SAVE_PROJECTILE(arcGravity);
        SAVE_PROJECTILE(homing);
        SAVE_PROJECTILE(accuracy);
        SAVE_PROJECTILE(dispersion);
#undef SAVE_PROJECTILE
    }
    writer.scalar((uint32_t)remains_.size());
    for (const Remains &remains : remains_) {
        const int deadUnit =
            remains.deadUnit
                ? remains.deadUnit->id
                : -1;
        writer.scalar(deadUnit);
#define SAVE_REMAINS(name) writer.scalar(remains.name)
        SAVE_REMAINS(dyingGraphic);
        SAVE_REMAINS(player);
        SAVE_REMAINS(x);
        SAVE_REMAINS(y);
        SAVE_REMAINS(facing);
        SAVE_REMAINS(age);
        SAVE_REMAINS(dyingDuration);
        SAVE_REMAINS(remainsDuration);
        SAVE_REMAINS(drawShadows);
#undef SAVE_REMAINS
    }

    for (const AiPlayerState &state :
         aiPlayers_) {
        writer.scalar(state.loaded);
        writer.scalar(
            (uint32_t)state.program.rules.size());
        for (const AiRule &rule :
             state.program.rules)
            writer.scalar(rule.enabled);
        writer.scalar(
            (uint32_t)state.goals.size());
        for (const auto &goal : state.goals) {
            writer.scalar(goal.first);
            writer.scalar(goal.second);
        }
        writer.scalar(
            (uint32_t)state.strategicNumbers.size());
        for (const auto &entry :
             state.strategicNumbers) {
            writer.string(entry.first);
            writer.scalar(entry.second);
        }
        writer.scalar(
            (uint32_t)state.timers.size());
        for (const auto &entry : state.timers) {
            writer.scalar(entry.first);
            writer.scalar(entry.second);
        }
        writer.scalar(state.escrowPercent);
        writer.scalar(state.escrowResources);
        writer.scalar(
            (uint64_t)state.ruleCursor);
        writer.scalar(state.ruleTime);
        writer.scalar(state.economyTime);
        writer.scalar(state.militaryTime);
        writer.scalar(state.defenseTime);
        writer.scalar(state.strategyTime);
        writer.scalar(state.townSafeTime);
        writer.scalar(state.scoutTime);
        writer.scalar(state.surrenderTime);
        writer.scalar(state.rebuildUntil);
        writer.scalar(state.gameTime);
        writer.scalar(state.ageTime);
        writer.scalar(state.age);
        writer.scalar(state.nextMilitaryGroupId);
        writer.scalar(state.attacksIssued);
        writer.scalar(state.formationOrders);
        writer.scalar(state.transportLandings);
        writer.scalar(state.replenishmentQueued);
        writer.scalar(state.retreats);
        writer.scalar(state.regroupOrders);
        writer.scalar(state.escortAssignments);
        writer.scalar(state.forceTargets);
        writer.scalar(state.forceCounts);
        writer.scalar(state.surrendered);
        writer.scalar(
            (uint32_t)state.shelteredWorkers.size());
        for (uint32_t id :
             state.shelteredWorkers)
            writer.scalar(id);
        writer.scalar(
            (uint32_t)state.militaryGroups.size());
        for (const AiMilitaryGroup &group :
             state.militaryGroups) {
            writer.scalar(group.id);
            writer.scalar(group.targetId);
            writer.scalars(group.members);
            writer.scalars(group.transports);
            writer.scalars(group.escorts);
            writer.scalar(
                (uint32_t)group
                    .boardingAssignments.size());
            for (const auto &assignment :
                 group.boardingAssignments) {
                writer.scalar(assignment.first);
                writer.scalar(assignment.second);
            }
            writer.scalar(group.phase);
            writer.scalar(group.formation);
            writer.scalar(group.destinationX);
            writer.scalar(group.destinationY);
            writer.scalar(group.retryTime);
            writer.scalar(group.initialStrength);
        }
        writer.scalar(state.randomNumber);
        writer.scalar(
            (uint32_t)state.events.size());
        for (int event : state.events)
            writer.scalar(event);
        writer.scalar(
            (uint32_t)state.signals.size());
        for (int signal : state.signals)
            writer.scalar(signal);
    }

    writer.scalars(selectionOrder_);
    for (const auto &group : controlGroups_)
        writer.scalars(group);
    writer.scalar(
        (uint32_t)moveGroupDestinations_.size());
    for (const auto &entry :
         moveGroupDestinations_) {
        writer.scalar(entry.first);
        writer.scalar(entry.second);
    }
    writer.scalar((uint32_t)marchGroups_.size());
    for (const MarchGroup &group : marchGroups_) {
        writer.scalar(group.id);
        writer.scalars(group.members);
        writer.scalar(
            (uint32_t)group.offsets.size());
        for (const auto &point : group.offsets)
            writer.scalar(point);
        writer.scalar(
            (uint32_t)group.path.size());
        for (const auto &point : group.path)
            writer.scalar(point);
        writer.scalar(
            (uint64_t)group.pathIndex);
        writer.scalar(group.x);
        writer.scalar(group.y);
        writer.scalar(group.dirX);
        writer.scalar(group.dirY);
        writer.scalar(
            (uint32_t)group.trail.size());
        for (const auto &point : group.trail)
            writer.scalar(point);
        writer.scalar(group.spacing);
        writer.scalar(group.straggling);
    }
    writer.scalar(
        (uint32_t)triggerRuntime_.size());
    for (const TriggerRuntime &runtime :
         triggerRuntime_) {
        writer.scalar(runtime.enabled);
        writer.scalar(runtime.fired);
        writer.scalar(runtime.elapsed);
    }
    writer.string(currentInstruction_);
    writer.scalar(currentInstructionPlayer_);
    writer.scalar(instructionTime_);
    writer.scalar(
        (uint32_t)instructions_.size());
    for (const Instruction &instruction :
         instructions_) {
        writer.string(instruction.text);
        writer.string(instruction.sound);
        writer.scalar(instruction.duration);
        writer.scalar(instruction.player);
    }

    if (writer.data.size() > kMaxSaveBytes - 20) {
        if (err) *err = "save payload exceeds 32 MiB limit";
        return false;
    }
    Writer file;
    file.data.insert(
        file.data.end(), kSaveMagic,
        kSaveMagic + sizeof kSaveMagic);
    file.scalar(kSaveVersion);
    file.scalar((uint32_t)writer.data.size());
    file.scalar(checksum(
        writer.data.data(), writer.data.size()));
    file.data.insert(
        file.data.end(),
        writer.data.begin(), writer.data.end());
    return writeAtomic(path, file.data, err);
}

bool Game::loadMatch(
    const std::string &path,
    std::string *err) {
    std::vector<uint8_t> bytes;
    const uint8_t *payload = nullptr;
    size_t payloadSize = 0;
    uint32_t version = 0;
    if (!payloadFromFile(
            path, bytes, payload, payloadSize,
            version, err))
        return false;
    Reader reader(payload, payloadSize);
    MatchSaveKind saveKind = MatchSaveKind::Skirmish;
    std::string campaignArchive;
    uint32_t campaignEntry = 0;
    if (version >= 3) {
        uint8_t kind = 0;
        if (!reader.scalar(kind) ||
            kind > (uint8_t)MatchSaveKind::Campaign ||
            !reader.string(campaignArchive, 128) ||
            !reader.scalar(campaignEntry)) {
            if (err)
                *err = reader.ok()
                           ? "save contains invalid match metadata"
                           : reader.error();
            return false;
        }
        saveKind = (MatchSaveKind)kind;
    }
    SkirmishSettings settings;
    if (!readSettings(reader, settings, version) ||
        saveKind != saveKind_ ||
        (saveKind == MatchSaveKind::Skirmish &&
         (!generatedMatch_ ||
          !sameSettings(
              settings,
              currentSkirmishSettings_))) ||
        (saveKind == MatchSaveKind::Campaign &&
         (generatedMatch_ ||
          campaignArchive != campaignArchive_ ||
          campaignEntry != campaignEntry_))) {
        if (err)
            *err = reader.ok()
                       ? "save context does not match initialized match"
                       : reader.error();
        return false;
    }

    float simulationTime = 0;
    int mapSize = 0;
    int localPlayer = 0;
    uint32_t nextSpawnId = 0;
    uint32_t nextMoveGroupId = 0;
    int difficulty = 0;
    SkirmishVictory victoryCondition =
        SkirmishVictory::Conquest;
    int victoryState = -1;
    bool conquestEnabled = false;
    SkirmishReveal reveal =
        SkirmishReveal::Normal;
    SkirmishGameSpeed gameSpeed =
        SkirmishGameSpeed::Normal;
    bool cheatsEnabled = false;
    bool teamsLocked = false;
    float conquestCheckTime = 0;
    int victoryCountdownPlayer = -1;
    int victoryCountdownKind = 0;
    float victoryCountdownRemaining = 0.0f;
    float standardVictoryCountdown =
        600.0f;
    float timeLimitSeconds = 3600.0f;
    int scoreLimit = 4000;
    std::array<float, 17>
        monumentVictoryCountdowns{};
    std::array<float, 17>
        holocronVictoryCountdowns{};
    float camX = 0, camY = 0, zoom = 1;
    float animClock = 0;
    FormationType formation = FormationType::Line;
    bool forceBuild = false, fullTech = false;
    bool forceExplore = false, forceSight = false;
    bool enemyIntelligence = false;
    uint64_t techGeneration = 0;
    std::array<int, 17> reseedQueue{};
    if (!reader.scalar(simulationTime) ||
        !reader.scalar(mapSize) ||
        !reader.scalar(localPlayer) ||
        !reader.scalar(nextSpawnId) ||
        !reader.scalar(nextMoveGroupId) ||
        !reader.scalar(difficulty) ||
        !reader.scalar(victoryCondition) ||
        !reader.scalar(victoryState) ||
        !reader.scalar(conquestEnabled) ||
        (version >= 6 &&
         (!reader.scalar(reveal) ||
          !reader.scalar(gameSpeed) ||
          !reader.scalar(cheatsEnabled) ||
          !reader.scalar(teamsLocked))) ||
        !reader.scalar(conquestCheckTime) ||
        (version >= 2 &&
         (!reader.scalar(
              victoryCountdownPlayer) ||
          !reader.scalar(
              victoryCountdownKind) ||
          !reader.scalar(
              victoryCountdownRemaining) ||
          !reader.scalar(
              standardVictoryCountdown) ||
          !reader.scalar(
              timeLimitSeconds) ||
          !reader.scalar(scoreLimit) ||
          !reader.scalar(
              monumentVictoryCountdowns) ||
          !reader.scalar(
              holocronVictoryCountdowns))) ||
        !reader.scalar(camX) ||
        !reader.scalar(camY) ||
        !reader.scalar(zoom) ||
        !reader.scalar(animClock) ||
        !reader.scalar(formation) ||
        !reader.scalar(forceBuild) ||
        !reader.scalar(fullTech) ||
        !reader.scalar(forceExplore) ||
        !reader.scalar(forceSight) ||
        !reader.scalar(enemyIntelligence) ||
        !reader.scalar(techGeneration) ||
        !reader.scalar(reseedQueue) ||
        (saveKind == MatchSaveKind::Skirmish &&
         mapSize != settings.mapSize) ||
        localPlayer <= 0 || localPlayer >= 17 ||
        zoom < 0.4f || zoom > 1.0f ||
        victoryCondition <
            SkirmishVictory::Standard ||
        victoryCondition >
            SkirmishVictory::CommandCenter ||
        victoryCountdownPlayer < -1 ||
        victoryCountdownPlayer >= 17 ||
        victoryCountdownKind < 0 ||
        victoryCountdownKind > 2 ||
        victoryCountdownRemaining < 0.0f ||
        standardVictoryCountdown <= 0.0f ||
        timeLimitSeconds <= 0.0f ||
        scoreLimit <= 0 ||
        reveal < SkirmishReveal::Normal ||
        reveal > SkirmishReveal::AllVisible ||
        gameSpeed < SkirmishGameSpeed::Slow ||
        gameSpeed > SkirmishGameSpeed::Fast) {
        if (err)
            *err = reader.ok()
                       ? "save contains invalid global state"
                       : reader.error();
        return false;
    }
    if (version == 1) {
        const uint8_t old =
            (uint8_t)victoryCondition;
        if (old == 0)
            victoryCondition =
                SkirmishVictory::Conquest;
        else if (old == 1)
            victoryCondition =
                SkirmishVictory::CommandCenter;
        else {
            if (err)
                *err =
                    "save contains invalid legacy victory mode";
            return false;
        }
    }
    const auto invalidCountdown =
        [](float value) {
            return !std::isfinite(value) ||
                   value < 0.0f;
        };
    if (std::any_of(
            monumentVictoryCountdowns.begin(),
            monumentVictoryCountdowns.end(),
            invalidCountdown) ||
        std::any_of(
            holocronVictoryCountdowns.begin(),
            holocronVictoryCountdowns.end(),
            invalidCountdown)) {
        if (err)
            *err =
                "save contains invalid victory countdown";
        return false;
    }
    std::string randomState;
    if (!reader.string(randomState, 100000)) {
        if (err) *err = reader.error();
        return false;
    }
    std::mt19937 loadedRandom;
    std::istringstream randomStream(randomState);
    if (!(randomStream >> loadedRandom)) {
        if (err) *err = "save contains invalid random state";
        return false;
    }

    std::vector<uint8_t> terrain;
    std::vector<uint8_t> cornerElevation;
    std::vector<uint8_t> tileElevation;
    std::vector<uint8_t> tileSlope;
    std::array<std::vector<uint8_t>, 17>
        exploredTiles;
    std::array<std::vector<uint8_t>, 17>
        visibleTiles;
    const uint32_t expectedTiles =
        (uint32_t)mapSize * mapSize;
    if (!reader.scalars(terrain, expectedTiles) ||
        !reader.scalars(
            cornerElevation,
            (uint32_t)(mapSize + 1) *
                (mapSize + 1)) ||
        !reader.scalars(
            tileElevation, expectedTiles) ||
        !reader.scalars(tileSlope, expectedTiles)) {
        if (err) *err = reader.error();
        return false;
    }
    if (terrain.size() != expectedTiles ||
        tileElevation.size() != expectedTiles ||
        tileSlope.size() != expectedTiles ||
        cornerElevation.size() !=
            (size_t)(mapSize + 1) *
                (mapSize + 1)) {
        if (err) *err = "save map array size mismatch";
        return false;
    }
    for (auto &tiles : exploredTiles)
        if (!reader.scalars(
                tiles, expectedTiles) ||
            tiles.size() != expectedTiles) {
            if (err)
                *err = reader.ok()
                           ? "save explored fog size mismatch"
                           : reader.error();
            return false;
        }
    for (auto &tiles : visibleTiles)
        if (!reader.scalars(
                tiles, expectedTiles) ||
            tiles.size() != expectedTiles) {
            if (err)
                *err = reader.ok()
                           ? "save visible fog size mismatch"
                           : reader.error();
            return false;
        }

    std::array<ScenarioPlayer, 16> players;
    for (size_t playerIndex = 0;
         playerIndex < players.size();
         ++playerIndex) {
        ScenarioPlayer &player = players[playerIndex];
        if (!reader.string(player.name, 4096) ||
            !reader.scalar(player.civilization) ||
            !reader.scalar(player.color) ||
            !reader.scalar(player.active) ||
            !reader.scalar(player.human) ||
            !reader.scalar(player.alliedVictory) ||
            !reader.scalar(player.cameraX) ||
            !reader.scalar(player.cameraY) ||
            !reader.scalar(player.resources) ||
            !reader.scalar(player.diplomacy) ||
            !reader.scalar(player.populationLimit)) {
            if (err)
               *err = reader.ok()
                          ? "save contains invalid player state"
                          : reader.error();
            return false;
        }
        const size_t maximumCivilization =
            saveKind == MatchSaveKind::Campaign
                ? 64
                : assets_.dat().civs.size();
        if (player.civilization >
            maximumCivilization) {
            if (err)
               *err =
                   "save player " +
                   std::to_string(playerIndex + 1) +
                   " has invalid civilization " +
                   std::to_string(player.civilization);
            return false;
        }
    }
    std::array<std::map<int, float>, 17>
        resources;
    std::array<std::set<int>, 17>
        researchedTechs;
    std::array<std::set<int>, 17>
        disabledTechs;
    std::array<std::set<int>, 17>
        disabledUnits;
    for (auto &values : resources)
        if (!readMap(reader, values, 2048)) {
            if (err) *err = reader.error();
            return false;
        }
    std::array<float, 3> commodityPrice{{1.0f, 1.0f, 1.3f}};
    std::array<int, 3> commodityCounter{};
    std::array<float, 3> commodityAccumulator{};
    if (version >= 7)
        for (size_t i = 0; i < 3; ++i)
            if (!reader.scalar(commodityPrice[i]) || !reader.scalar(commodityCounter[i]) ||
                !reader.scalar(commodityAccumulator[i]) ||
                !std::isfinite(commodityPrice[i])) {
                if (err) *err = reader.error();
                return false;
            }
    for (auto &values : researchedTechs)
        if (!readSet(reader, values, 8192)) {
            if (err) *err = reader.error();
            return false;
        }
    for (auto &values : disabledTechs)
        if (!readSet(reader, values, 8192)) {
            if (err) *err = reader.error();
            return false;
        }
    for (auto &values : disabledUnits)
        if (!readSet(reader, values, 8192)) {
            if (err) *err = reader.error();
            return false;
        }

    const auto resolveUnit =
        [&](int player, int unitId)
        -> const dat::Unit * {
        if (unitId < 0) return nullptr;
        const int civilization =
            player > 0 &&
                    (size_t)player <=
                        players.size()
                ? (int)players[
                      (size_t)player - 1]
                      .civilization
                : 0;
        const dat::Unit *unit =
            findUnit(civilization, unitId);
        if (!unit && unitId == 285)
            unit = findUnit(0, unitId);
        return unit;
    };
    uint32_t objectCount = 0;
    if (!reader.scalar(objectCount) ||
        objectCount > kMaxObjects) {
        if (err)
            *err = reader.ok()
                       ? "save object count exceeds limit"
                       : reader.error();
        return false;
    }
    std::vector<Object> objects;
    objects.reserve(objectCount);
    for (uint32_t index = 0;
         index < objectCount; ++index) {
        int unitId = -1, gateClosedId = -1;
        int gateOpenId = -1, gateEndId = -1;
        int jobUnitId = -1;
        uint32_t queueSize = 0;
        if (!reader.scalar(unitId) ||
            !reader.scalar(gateClosedId) ||
            !reader.scalar(gateOpenId) ||
            !reader.scalar(gateEndId) ||
            !reader.scalar(jobUnitId) ||
            !reader.scalar(queueSize) ||
            queueSize > 1000) {
            if (err)
                *err = reader.ok()
                           ? "save production queue exceeds limit"
                           : reader.error();
            return false;
        }
        Object object;
        std::vector<
            std::tuple<int, int, float>>
            queue;
        queue.reserve(queueSize);
        for (uint32_t item = 0;
             item < queueSize; ++item) {
            int queuedUnit = -1;
            int technology = -1;
            float duration = 0;
            if (!reader.scalar(queuedUnit) ||
                !reader.scalar(technology) ||
                !reader.scalar(duration)) {
                if (err) *err = reader.error();
                return false;
            }
            queue.emplace_back(
                queuedUnit, technology, duration);
        }
#define LOAD_FIELD(name) reader.scalar(object.name)
        uint32_t pathSize = 0;
        uint64_t objectPathIndex = 0;
        if (!LOAD_FIELD(player) ||
            !LOAD_FIELD(x) ||
            !LOAD_FIELD(y) ||
            !LOAD_FIELD(facing) ||
            !LOAD_FIELD(state) ||
            !LOAD_FIELD(animTime) ||
            !LOAD_FIELD(stateTime) ||
            !LOAD_FIELD(targetX) ||
            !LOAD_FIELD(targetY) ||
            !LOAD_FIELD(hitPoints) ||
            !LOAD_FIELD(maxHitPoints) ||
            !LOAD_FIELD(shieldPoints) ||
            !LOAD_FIELD(maxShieldPoints)) {
            if (err) *err = reader.error();
            return false;
        }
        if (version >= 5 &&
            (!LOAD_FIELD(
                 shieldRegenerationTime) ||
             !LOAD_FIELD(shieldDrainTime))) {
            if (err) *err = reader.error();
            return false;
        }
        if (!LOAD_FIELD(resourceAmount) ||
            !LOAD_FIELD(carriedAmount) ||
            !LOAD_FIELD(stash) ||
            !LOAD_FIELD(resourceType) ||
            !LOAD_FIELD(carriedResourceType) ||
            !reader.scalar(pathSize) ||
            pathSize > kMaxCollection) {
            if (err)
                *err = reader.ok()
                           ? "save object path exceeds limit"
                           : reader.error();
            return false;
        }
        object.path.resize(pathSize);
        for (auto &point : object.path)
            if (!reader.scalar(point)) {
                if (err) *err = reader.error();
                return false;
            }
        if (!reader.scalar(objectPathIndex) ||
            !LOAD_FIELD(blockedTime) ||
            !LOAD_FIELD(moveRetryTime) ||
            !LOAD_FIELD(moveStallTime) ||
            !LOAD_FIELD(moveBestDistance) ||
            !LOAD_FIELD(moveSpeedLimit) ||
            !LOAD_FIELD(attackCooldown) ||
            !LOAD_FIELD(attackRepathTime) ||
            !LOAD_FIELD(attackApproachAngle) ||
            !LOAD_FIELD(attackApproachDistance) ||
            !LOAD_FIELD(attackStallTime) ||
            !LOAD_FIELD(attackBestDistance) ||
            !LOAD_FIELD(autoAcquireTime) ||
            !LOAD_FIELD(attackTargetId) ||
            !LOAD_FIELD(moveGroupId) ||
            !LOAD_FIELD(attackSlotRetries) ||
            !LOAD_FIELD(moveSpreadRetries) ||
            !LOAD_FIELD(homeX) ||
            !LOAD_FIELD(homeY) ||
            !LOAD_FIELD(moveAnchorX) ||
            !LOAD_FIELD(moveAnchorY) ||
            !LOAD_FIELD(attackMode) ||
            !LOAD_FIELD(attackAutomatic) ||
            !LOAD_FIELD(attackShotPending) ||
            !LOAD_FIELD(attackGroundActive) ||
            !LOAD_FIELD(attackGroundX) ||
            !LOAD_FIELD(attackGroundY) ||
            !LOAD_FIELD(patrolActive) ||
            !LOAD_FIELD(patrolTowardEnd) ||
            !LOAD_FIELD(patrolStartX) ||
            !LOAD_FIELD(patrolStartY) ||
            !LOAD_FIELD(patrolEndX) ||
            !LOAD_FIELD(patrolEndY) ||
            !LOAD_FIELD(guardTargetId) ||
            !LOAD_FIELD(followTargetId) ||
            !LOAD_FIELD(moveGoalActive) ||
            !LOAD_FIELD(wander) ||
            !LOAD_FIELD(drawShadows) ||
            !LOAD_FIELD(active) ||
            !LOAD_FIELD(hidden) ||
            !LOAD_FIELD(draw) ||
            !LOAD_FIELD(locked) ||
            !LOAD_FIELD(gate) ||
            !LOAD_FIELD(underConstruction) ||
            !LOAD_FIELD(manualDropOff) ||
            !LOAD_FIELD(selected) ||
            !LOAD_FIELD(triggerAddressable) ||
            !LOAD_FIELD(customKind) ||
            !LOAD_FIELD(flashTime) ||
            !LOAD_FIELD(gateOpenAmount) ||
            !LOAD_FIELD(gateCloseTimer) ||
            !LOAD_FIELD(felled) ||
            !LOAD_FIELD(jobKind) ||
            !LOAD_FIELD(annexParentId) ||
            !LOAD_FIELD(carcassId) ||
            !LOAD_FIELD(carcassClass) ||
            !LOAD_FIELD(carcassDecay) ||
            !LOAD_FIELD(carcassHidden) ||
            !LOAD_FIELD(huntTimer) ||
            !LOAD_FIELD(resourceWorkTargetId) ||
            !LOAD_FIELD(resourceWorkTime) ||
            !LOAD_FIELD(damageSoundTime) ||
            !LOAD_FIELD(farmStage) ||
            !reader.scalars(
                object.farmUnderlay,
                expectedTiles) ||
            !LOAD_FIELD(farmMoveTime) ||
            !LOAD_FIELD(farmMoveStep) ||
            !LOAD_FIELD(farmMoveActive) ||
            !LOAD_FIELD(pathGoalId) ||
            !LOAD_FIELD(pathGoalClearance) ||
            !LOAD_FIELD(repathCount) ||
            !LOAD_FIELD(detourTime) ||
            !LOAD_FIELD(approachRetry) ||
            !LOAD_FIELD(rallyActive) ||
            !LOAD_FIELD(rallyX) ||
            !LOAD_FIELD(rallyY) ||
            !LOAD_FIELD(rallyTargetId) ||
            !LOAD_FIELD(volleyRemaining) ||
            !LOAD_FIELD(volleyTimer) ||
            !LOAD_FIELD(volleyTargetId) ||
            !LOAD_FIELD(lastGatherClass) ||
            !LOAD_FIELD(lastGatherType) ||
            !LOAD_FIELD(lastGatherX) ||
            !LOAD_FIELD(lastGatherY) ||
            !LOAD_FIELD(marchGroupId) ||
            !LOAD_FIELD(marchSpeed) ||
            !LOAD_FIELD(marchRepath) ||
            !LOAD_FIELD(blockerId) ||
            !LOAD_FIELD(productionRemaining) ||
            !LOAD_FIELD(productionPopulationBlocked) ||
            !LOAD_FIELD(constructionRemaining) ||
            !LOAD_FIELD(constructionTotal) ||
            !LOAD_FIELD(constructionBuilderId) ||
            !LOAD_FIELD(constructionTargetId) ||
            !LOAD_FIELD(gatherTargetId) ||
            !LOAD_FIELD(dropOffTargetId) ||
            !LOAD_FIELD(repairTargetId) ||
            !LOAD_FIELD(garrisonTargetId) ||
            !LOAD_FIELD(garrisonDamageLocked) ||
            !LOAD_FIELD(spawnId) ||
            !LOAD_FIELD(discoveredByPlayers) ||
            !LOAD_FIELD(garrisonedInId) ||
            !LOAD_FIELD(initialFrame)) {
            if (err) *err = reader.error();
            return false;
        }
        if (version >= 2 &&
            (!LOAD_FIELD(
                 conversionTargetId) ||
             !LOAD_FIELD(
                 conversionProgress) ||
             !LOAD_FIELD(
                 conversionRecharge) ||
             !LOAD_FIELD(
                 holocronTargetId) ||
             !LOAD_FIELD(
                 carriedHolocronId) ||
             !LOAD_FIELD(carriedById))) {
            if (err) *err = reader.error();
            return false;
        }
        if (version >= 4 &&
            (!LOAD_FIELD(triggerAttack) ||
             !LOAD_FIELD(frozen) ||
             !reader.string(
                 object.triggerName,
                 65536))) {
            if (err) *err = reader.error();
            return false;
        }
        if (version >= 7 &&
            (!LOAD_FIELD(tradeMarketId) ||
             !LOAD_FIELD(tradeHomeId) ||
             !LOAD_FIELD(tradeCarrying) ||
             !LOAD_FIELD(conversionCountdown))) {
            if (err) *err = reader.error();
            return false;
        }
#undef LOAD_FIELD
        object.unit =
            resolveUnit(object.player, unitId);
        object.gateClosedUnit =
            resolveUnit(
                object.player, gateClosedId);
        object.gateOpenUnit =
            resolveUnit(
                object.player, gateOpenId);
        object.gateEndUnit =
            resolveUnit(
                object.player, gateEndId);
        object.jobUnit =
            resolveUnit(
                object.player, jobUnitId);
        if (!object.unit) {
            if (err)
                *err = "save references missing unit " +
                       std::to_string(unitId);
            return false;
        }
        for (const auto &item : queue) {
            ProductionItem production;
            production.unit =
                resolveUnit(
                    object.player,
                    std::get<0>(item));
            production.technologyId =
                std::get<1>(item);
            production.duration =
                std::get<2>(item);
            if (std::get<0>(item) >= 0 &&
                !production.unit) {
                if (err)
                    *err = "save references missing queued unit";
                return false;
            }
            object.productionQueue.push_back(
                production);
        }
        if (objectPathIndex >
            object.path.size()) {
            if (err) *err = "save has invalid path cursor";
            return false;
        }
        object.pathIndex =
            (size_t)objectPathIndex;
        objects.push_back(std::move(object));
    }
    const auto savedObject =
        [&](uint32_t spawnId)
        -> const Object * {
        if (!spawnId) return nullptr;
        const auto found =
            std::find_if(
                objects.begin(),
                objects.end(),
                [&](const Object &object) {
                    return object.spawnId ==
                           spawnId;
                });
        return found == objects.end()
                   ? nullptr
                   : &*found;
    };
    for (const Object &object : objects) {
        if (object.state > State::Convert) {
            if (err)
                *err =
                    "save contains invalid object state";
            return false;
        }
        if (!std::isfinite(
                object
                    .shieldRegenerationTime) ||
            !std::isfinite(
                object.shieldDrainTime) ||
            object.shieldRegenerationTime <
                0.0f ||
            object.shieldDrainTime < 0.0f) {
            if (err)
                *err =
                    "save contains invalid shield timer";
            return false;
        }
        if (object.conversionTargetId &&
            !savedObject(
                object.conversionTargetId)) {
            if (err)
                *err =
                    "save contains stale conversion target";
            return false;
        }
        if (object.holocronTargetId &&
            !savedObject(
                object.holocronTargetId)) {
            if (err)
                *err =
                    "save contains stale holocron target";
            return false;
        }
        if (object.carriedHolocronId) {
            const Object *holocron =
                savedObject(
                    object.carriedHolocronId);
            if (!holocron ||
                holocron->carriedById !=
                    object.spawnId) {
                if (err)
                    *err =
                        "save contains inconsistent carried holocron";
                return false;
            }
        }
        if (object.carriedById) {
            const Object *carrier =
                savedObject(
                    object.carriedById);
            if (!carrier ||
                carrier
                        ->carriedHolocronId !=
                    object.spawnId) {
                if (err)
                    *err =
                        "save contains inconsistent holocron carrier";
                return false;
            }
        }
    }

    uint32_t projectileCount = 0;
    if (!reader.scalar(projectileCount) ||
        projectileCount > kMaxObjects) {
        if (err)
            *err = reader.ok()
                       ? "save projectile count exceeds limit"
                       : reader.error();
        return false;
    }
    std::vector<Projectile> projectiles;
    projectiles.reserve(projectileCount);
    for (uint32_t index = 0;
         index < projectileCount; ++index) {
        int unitId = -1;
        Projectile projectile;
        if (!reader.scalar(unitId) ||
            !reader.scalar(projectile.player) ||
            !reader.scalar(projectile.x) ||
            !reader.scalar(projectile.y) ||
            !reader.scalar(projectile.z) ||
            !reader.scalar(projectile.targetZ) ||
            !reader.scalar(projectile.facing) ||
            !reader.scalar(projectile.animTime) ||
            !reader.scalar(projectile.targetId) ||
            !reader.scalar(projectile.sourceId) ||
            !reader.scalar(projectile.damage) ||
            !reader.scalar(projectile.groundAimed) ||
            !reader.scalar(projectile.aimX) ||
            !reader.scalar(projectile.aimY) ||
            !reader.scalar(projectile.blastWidth) ||
            !reader.scalar(projectile.blastLevel)) {
            if (err) *err = reader.error();
            return false;
        }
        if (version >= 7 &&
            (!reader.scalar(projectile.arcHeight) ||
             !reader.scalar(projectile.arcVelocity) ||
             !reader.scalar(projectile.arcGravity))) {
            if (err) *err = reader.error();
            return false;
        }
        if (version >= 8 &&
            (!reader.scalar(projectile.homing) ||
             !reader.scalar(projectile.accuracy) ||
             !reader.scalar(projectile.dispersion))) {
            if (err) *err = reader.error();
            return false;
        }
        projectile.unit =
            resolveUnit(projectile.player, unitId);
        if (!projectile.unit) {
            if (err) *err = "save references missing projectile unit";
            return false;
        }
        projectiles.push_back(projectile);
    }
    uint32_t remainsCount = 0;
    if (!reader.scalar(remainsCount) ||
        remainsCount > kMaxObjects) {
        if (err)
            *err = reader.ok()
                       ? "save remains count exceeds limit"
                       : reader.error();
        return false;
    }
    std::vector<Remains> remains;
    remains.reserve(remainsCount);
    for (uint32_t index = 0;
         index < remainsCount; ++index) {
        int deadUnitId = -1;
        Remains dead;
        if (!reader.scalar(deadUnitId) ||
            !reader.scalar(dead.dyingGraphic) ||
            !reader.scalar(dead.player) ||
            !reader.scalar(dead.x) ||
            !reader.scalar(dead.y) ||
            !reader.scalar(dead.facing) ||
            !reader.scalar(dead.age) ||
            !reader.scalar(dead.dyingDuration) ||
            !reader.scalar(dead.remainsDuration) ||
            !reader.scalar(dead.drawShadows)) {
            if (err) *err = reader.error();
            return false;
        }
        dead.deadUnit =
            resolveUnit(dead.player, deadUnitId);
        if (deadUnitId >= 0 && !dead.deadUnit) {
            if (err) *err = "save references missing remains unit";
            return false;
        }
        remains.push_back(dead);
    }

    std::array<AiPlayerState, 17>
        loadedAi = aiPlayers_;
    for (AiPlayerState &state : loadedAi) {
        bool loaded = false;
        uint32_t ruleCount = 0;
        if (!reader.scalar(loaded) ||
            !reader.scalar(ruleCount)) {
            if (err) *err = reader.error();
            return false;
        }
        const bool preserveCampaignAi =
            version < 4 &&
            saveKind ==
                MatchSaveKind::Campaign &&
            ruleCount == 0 &&
            !state.program.rules.empty();
        if (!preserveCampaignAi &&
            ruleCount !=
                state.program.rules.size()) {
            if (err)
                *err =
                    "save AI personality does not match loaded script";
            return false;
        }
        if (!preserveCampaignAi)
            state.loaded = loaded;
        for (uint32_t rule = 0;
             rule < ruleCount; ++rule) {
            bool enabled = false;
            if (!reader.scalar(enabled)) {
                if (err) *err = reader.error();
                return false;
            }
            state.program.rules[
                (size_t)rule]
                .enabled = enabled;
        }
        uint32_t size = 0;
        if (!reader.scalar(size) || size > 100000) {
            if (err) *err = "save AI goals exceed limit";
            return false;
        }
        state.goals.clear();
        for (uint32_t i = 0; i < size; ++i) {
            int key = 0, value = 0;
            if (!reader.scalar(key) ||
                !reader.scalar(value)) {
                if (err) *err = reader.error();
                return false;
            }
            state.goals[key] = value;
        }
        if (!reader.scalar(size) || size > 100000) {
            if (err) *err = "save strategic numbers exceed limit";
            return false;
        }
        state.strategicNumbers.clear();
        for (uint32_t i = 0; i < size; ++i) {
            std::string key;
            int value = 0;
            if (!reader.string(key, 4096) ||
                !reader.scalar(value)) {
                if (err) *err = reader.error();
                return false;
            }
            state.strategicNumbers[key] = value;
        }
        if (!reader.scalar(size) || size > 100000) {
            if (err) *err = "save AI timers exceed limit";
            return false;
        }
        state.timers.clear();
        for (uint32_t i = 0; i < size; ++i) {
            int key = 0;
            float value = 0;
            if (!reader.scalar(key) ||
                !reader.scalar(value)) {
                if (err) *err = reader.error();
                return false;
            }
            state.timers[key] = value;
        }
        uint64_t ruleCursor = 0;
        if (!reader.scalar(state.escrowPercent) ||
            !reader.scalar(state.escrowResources) ||
            !reader.scalar(ruleCursor) ||
            !reader.scalar(state.ruleTime) ||
            !reader.scalar(state.economyTime) ||
            !reader.scalar(state.militaryTime) ||
            !reader.scalar(state.defenseTime) ||
            !reader.scalar(state.strategyTime) ||
            !reader.scalar(state.townSafeTime) ||
            !reader.scalar(state.scoutTime) ||
            !reader.scalar(state.surrenderTime) ||
            !reader.scalar(state.rebuildUntil) ||
            !reader.scalar(state.gameTime) ||
            !reader.scalar(state.ageTime) ||
            !reader.scalar(state.age) ||
            !reader.scalar(state.nextMilitaryGroupId) ||
            !reader.scalar(state.attacksIssued) ||
            !reader.scalar(state.formationOrders) ||
            !reader.scalar(state.transportLandings) ||
            !reader.scalar(state.replenishmentQueued) ||
            !reader.scalar(state.retreats) ||
            !reader.scalar(state.regroupOrders) ||
            !reader.scalar(state.escortAssignments) ||
            !reader.scalar(state.forceTargets) ||
            !reader.scalar(state.forceCounts) ||
            !reader.scalar(state.surrendered) ||
            !reader.scalar(size) ||
            size > kMaxObjects) {
            if (err)
                *err = reader.ok()
                           ? "save AI sheltered worker count exceeds limit"
                           : reader.error();
            return false;
        }
        if (ruleCursor >
            state.program.rules.size()) {
            if (err) *err = "save has invalid AI rule cursor";
            return false;
        }
        state.ruleCursor = (size_t)ruleCursor;
        state.shelteredWorkers.clear();
        for (uint32_t i = 0; i < size; ++i) {
            uint32_t id = 0;
            if (!reader.scalar(id)) {
                if (err) *err = reader.error();
                return false;
            }
            state.shelteredWorkers.insert(id);
        }
        if (!reader.scalar(size) ||
            size > kMaxObjects) {
            if (err) *err = "save AI group count exceeds limit";
            return false;
        }
        state.militaryGroups.clear();
        for (uint32_t i = 0; i < size; ++i) {
            AiMilitaryGroup group;
            uint32_t assignmentCount = 0;
            if (!reader.scalar(group.id) ||
                !reader.scalar(group.targetId) ||
                !reader.scalars(
                    group.members, kMaxObjects) ||
                !reader.scalars(
                    group.transports, kMaxObjects) ||
                !reader.scalars(
                    group.escorts, kMaxObjects) ||
                !reader.scalar(assignmentCount) ||
                assignmentCount > kMaxObjects) {
                if (err)
                    *err = reader.ok()
                               ? "save boarding assignments exceed limit"
                               : reader.error();
                return false;
            }
            for (uint32_t assignment = 0;
                 assignment < assignmentCount;
                 ++assignment) {
                uint32_t unit = 0, transport = 0;
                if (!reader.scalar(unit) ||
                    !reader.scalar(transport)) {
                    if (err) *err = reader.error();
                    return false;
                }
                group.boardingAssignments[unit] =
                    transport;
            }
            if (!reader.scalar(group.phase) ||
                !reader.scalar(group.formation) ||
                !reader.scalar(group.destinationX) ||
                !reader.scalar(group.destinationY) ||
                !reader.scalar(group.retryTime) ||
                !reader.scalar(group.initialStrength)) {
                if (err) *err = reader.error();
                return false;
            }
            state.militaryGroups.push_back(
                std::move(group));
        }
        if (version >= 4 &&
            !reader.scalar(
                state.randomNumber)) {
            if (err) *err = reader.error();
            return false;
        }
        if (version >= 4) {
            uint32_t eventCount = 0;
            if (!reader.scalar(eventCount) ||
                eventCount > 100000) {
                if (err)
                    *err =
                        "save AI event count exceeds limit";
                return false;
            }
            state.events.clear();
            for (uint32_t event = 0;
                 event < eventCount;
                 ++event) {
                int value = 0;
                if (!reader.scalar(value)) {
                    if (err)
                        *err = reader.error();
                    return false;
                }
                state.events.insert(value);
            }
            uint32_t signalCount = 0;
            if (!reader.scalar(signalCount) ||
                signalCount > 100000) {
                if (err)
                    *err =
                        "save AI signal count exceeds limit";
                return false;
            }
            state.signals.clear();
            for (uint32_t signal = 0;
                 signal < signalCount;
                 ++signal) {
                int value = 0;
                if (!reader.scalar(value)) {
                    if (err)
                        *err = reader.error();
                    return false;
                }
                state.signals.insert(value);
            }
        }
    }

    std::vector<uint32_t> selectionOrder;
    std::array<std::vector<uint32_t>, 10>
        controlGroups;
    if (!reader.scalars(
            selectionOrder, kMaxObjects)) {
        if (err) *err = reader.error();
        return false;
    }
    for (auto &group : controlGroups)
        if (!reader.scalars(
                group, kMaxObjects)) {
            if (err) *err = reader.error();
            return false;
        }
    uint32_t destinationCount = 0;
    if (!reader.scalar(destinationCount) ||
        destinationCount > kMaxObjects) {
        if (err) *err = "save move-group destination count exceeds limit";
        return false;
    }
    std::unordered_map<
        uint32_t, std::array<float, 2>>
        moveGroupDestinations;
    for (uint32_t i = 0;
         i < destinationCount; ++i) {
        uint32_t id = 0;
        std::array<float, 2> point{};
        if (!reader.scalar(id) ||
            !reader.scalar(point)) {
            if (err) *err = reader.error();
            return false;
        }
        moveGroupDestinations[id] = point;
    }
    uint32_t marchCount = 0;
    if (!reader.scalar(marchCount) ||
        marchCount > kMaxObjects) {
        if (err) *err = "save march-group count exceeds limit";
        return false;
    }
    std::vector<MarchGroup> marchGroups;
    marchGroups.reserve(marchCount);
    for (uint32_t i = 0; i < marchCount; ++i) {
        MarchGroup group;
        uint32_t size = 0;
        if (!reader.scalar(group.id) ||
            !reader.scalars(
                group.members, kMaxObjects) ||
            !reader.scalar(size) ||
            size > kMaxObjects) {
            if (err) *err = "save formation offsets exceed limit";
            return false;
        }
        group.offsets.resize(size);
        for (auto &point : group.offsets)
            if (!reader.scalar(point)) {
                if (err) *err = reader.error();
                return false;
            }
        if (!reader.scalar(size) ||
            size > kMaxCollection) {
            if (err) *err = "save formation path exceeds limit";
            return false;
        }
        group.path.resize(size);
        for (auto &point : group.path)
            if (!reader.scalar(point)) {
                if (err) *err = reader.error();
                return false;
            }
        uint64_t pathIndex = 0;
        if (!reader.scalar(pathIndex) ||
            !reader.scalar(group.x) ||
            !reader.scalar(group.y) ||
            !reader.scalar(group.dirX) ||
            !reader.scalar(group.dirY) ||
            !reader.scalar(size) ||
            size > kMaxCollection) {
            if (err) *err = "save formation trail exceeds limit";
            return false;
        }
        group.trail.resize(size);
        for (auto &point : group.trail)
            if (!reader.scalar(point)) {
                if (err) *err = reader.error();
                return false;
            }
        if (!reader.scalar(group.spacing) ||
            !reader.scalar(group.straggling) ||
            pathIndex > group.path.size()) {
            if (err) *err = "save has invalid formation cursor";
            return false;
        }
        group.pathIndex = (size_t)pathIndex;
        marchGroups.push_back(std::move(group));
    }
    std::vector<TriggerRuntime>
        loadedTriggerRuntime;
    std::string loadedInstruction;
    int loadedInstructionPlayer = -1;
    float loadedInstructionTime = 0.0f;
    std::deque<Instruction>
        loadedInstructions;
    if (version >= 4) {
        uint32_t triggerCount = 0;
        if (!reader.scalar(triggerCount) ||
            triggerCount != triggers_.size()) {
            if (err)
                *err =
                    "save trigger state does not match initialized scenario";
            return false;
        }
        loadedTriggerRuntime.resize(
            triggerCount);
        for (TriggerRuntime &runtime :
             loadedTriggerRuntime)
            if (!reader.scalar(runtime.enabled) ||
                !reader.scalar(runtime.fired) ||
                !reader.scalar(runtime.elapsed) ||
                !std::isfinite(runtime.elapsed) ||
                runtime.elapsed < 0.0f) {
                if (err)
                    *err =
                        "save contains invalid trigger state";
                return false;
            }
        if (!reader.string(
                loadedInstruction, 65536) ||
            !reader.scalar(
                loadedInstructionPlayer) ||
            !reader.scalar(
                loadedInstructionTime) ||
            loadedInstructionPlayer < -1 ||
            loadedInstructionPlayer >= 17 ||
            !std::isfinite(
                loadedInstructionTime) ||
            loadedInstructionTime < 0.0f) {
            if (err)
                *err =
                    "save contains invalid active instruction";
            return false;
        }
        uint32_t instructionCount = 0;
        if (!reader.scalar(instructionCount) ||
            instructionCount > 4096) {
            if (err)
                *err =
                    "save instruction queue exceeds limit";
            return false;
        }
        for (uint32_t index = 0;
             index < instructionCount;
             ++index) {
            Instruction instruction;
            if (!reader.string(
                    instruction.text, 65536) ||
                !reader.string(
                    instruction.sound, 4096) ||
                !reader.scalar(
                    instruction.duration) ||
                !reader.scalar(
                    instruction.player) ||
                instruction.player < -1 ||
                instruction.player >= 17 ||
                !std::isfinite(
                    instruction.duration) ||
                instruction.duration < 0.0f) {
                if (err)
                    *err =
                        "save contains invalid queued instruction";
                return false;
            }
            loadedInstructions.push_back(
                std::move(instruction));
        }
    }
    if (!reader.finished()) {
        if (err)
            *err = reader.ok()
                       ? "save contains trailing data"
                       : reader.error();
        return false;
    }

    rng_ = loadedRandom;
    simulationTime_ = simulationTime;
    mapSize_ = mapSize;
    localPlayer_ = localPlayer;
    nextSpawnId_ = nextSpawnId;
    nextMoveGroupId_ = nextMoveGroupId;
    difficulty_ = difficulty;
    victoryCondition_ = victoryCondition;
    victoryState_ = victoryState;
    conquestEnabled_ = conquestEnabled;
    reveal_ = reveal;
    gameSpeed_ = gameSpeed;
    cheatsEnabled_ = cheatsEnabled;
    teamsLocked_ = teamsLocked;
    conquestCheckTime_ = conquestCheckTime;
    victoryCountdownPlayer_ =
        victoryCountdownPlayer;
    victoryCountdownKind_ =
        victoryCountdownKind;
    victoryCountdownRemaining_ =
        victoryCountdownRemaining;
    standardVictoryCountdown_ =
        standardVictoryCountdown;
    timeLimitSeconds_ = timeLimitSeconds;
    scoreLimit_ = scoreLimit;
    monumentVictoryCountdowns_ =
        monumentVictoryCountdowns;
    holocronVictoryCountdowns_ =
        holocronVictoryCountdowns;
    camX_ = camX;
    camY_ = camY;
    zoom_ = zoom;
    animClock_ = animClock;
    selectedFormation_ = formation;
    forceBuildCheat_ = forceBuild;
    fullTechTreeCheat_ = fullTech;
    forceExploreCheat_ = forceExplore;
    forceSightCheat_ = forceSight;
    enemyIntelligenceCheat_ =
        enemyIntelligence;
    techGeneration_ = techGeneration;
    lineageMemo_.clear();
    produceMemo_.clear();
    attributeCacheGeneration_ = {};
    unitAttributeOps_.clear();
    reseedQueue_ = reseedQueue;
    terrain_ = std::move(terrain);
    preloadTerrainArt();
    cornerElevation_ =
        std::move(cornerElevation);
    tileElevation_ =
        std::move(tileElevation);
    tileSlope_ = std::move(tileSlope);
    exploredTiles_ = std::move(exploredTiles);
    visibleTiles_ = std::move(visibleTiles);
    visibilityTouched_.fill(true);
    players_ = std::move(players);
    resources_ = std::move(resources);
    commodityPrice_ = commodityPrice;
    commodityCounter_ = commodityCounter;
    commodityAccumulator_ = commodityAccumulator;
    researchedTechs_ =
        std::move(researchedTechs);
    disabledTechs_ =
        std::move(disabledTechs);
    disabledUnits_ =
        std::move(disabledUnits);
    objects_ = std::move(objects);
    dynamicObjectIndices_.clear();
    dynamicObjectsScanned_ = 0;
    objectIndices_.clear();
    for (size_t index = 0;
         index < objects_.size(); ++index)
        objectIndices_[objects_[index].spawnId] =
            index;
    projectiles_ = std::move(projectiles);
    remains_ = std::move(remains);
    aiPlayers_ = std::move(loadedAi);
    selectionOrder_ =
        std::move(selectionOrder);
    controlGroups_ = std::move(controlGroups);
    moveGroupDestinations_ =
        std::move(moveGroupDestinations);
    marchGroups_ = std::move(marchGroups);
    if (version >= 4)
        triggerRuntime_ =
            std::move(loadedTriggerRuntime);

    pathGridCache_.clear();
    obstructionSnapshot_.clear();
    pendingCarcasses_.clear();
    instructions_.clear();
    currentInstruction_.clear();
    currentInstructionPlayer_ = -1;
    instructionTime_ = 0.0f;
    if (version >= 4) {
        instructions_ =
            std::move(loadedInstructions);
        currentInstruction_ =
            std::move(loadedInstruction);
        currentInstructionPlayer_ =
            loadedInstructionPlayer;
        instructionTime_ =
            loadedInstructionTime;
    }
    actionMenuOpen_ = false;
    actionMenuObjectId_ = 0;
    placementUnit_ = nullptr;
    placementBuilderId_ = 0;
    gatherPointBuildingId_ = 0;
    wallDragActive_ = false;
    cheatMenuOpen_ = false;
    garrisonCursorActive_ = false;
    repairCursorActive_ = false;
    screenPickValid_ = false;
    attackGroundCursorActive_ = false;
    unitCommandCursorActive_ = false;
    boxSelectActive_ = false;
    minimapDragging_ = false;
    minimapRefreshTime_ = 0.0f;
    minimapAlerts_.clear();
    statusMessage_ = "MATCH LOADED";
    statusTime_ = 3.0f;
    attackAlertMessage_.clear();
    attackAlertCooldown_ = 0.0f;
    availableCache_ = {};
    availableCacheGeneration_ = {};
    upgradeCache_ = {};
    upgradeCacheGeneration_ = {};
    researchMenuCache_.clear();
    shieldGeneratorIndices_.clear();
    shieldGeneratorCacheSize_ = (size_t)-1;
    civilizationGraphicCache_.clear();
    rebuildAdjacency();
    rebuildMobileOccupancy();
    syncSelectionOrder();
    syncControlGroups();
    clampCamera();
    return true;
}

} // namespace swgb
