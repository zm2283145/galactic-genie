// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace swgb {

constexpr int kMaxSkirmishSlots = 8;

enum class SkirmishMapStyle : uint8_t {
    Grasslands,
    Forest,
    Desert,
    Ice,
    Swamp,
    InlandWater,
    Coastal,
    Archipelago,
    LandMass,
    CompactIslands,
    LegacyRandom,
};

enum class AiPersonality : uint8_t {
    Expanded,
    Classic,
};

enum class SkirmishVictory : uint8_t {
    Standard,
    Conquest,
    TimeLimit,
    Score,
    CommandCenter,
};

enum class SkirmishSlotType : uint8_t {
    Closed,
    Human,
    Computer,
};

enum class SkirmishReveal : uint8_t {
    Normal,
    Explored,
    AllVisible,
};

enum class SkirmishGameSpeed : uint8_t {
    Slow,
    Normal,
    Fast,
};

struct SkirmishSlot {
    SkirmishSlotType type = SkirmishSlotType::Closed;
    std::string name;
    uint8_t color = 0;
    uint8_t civilization = 1;
    AiPersonality personality = AiPersonality::Expanded;
    uint8_t difficulty = 2;
    uint8_t team = 0;
    bool alliedVictory = false;
};

struct SkirmishSettings {
    SkirmishSettings();

    uint32_t seed = 0x5A17u;
    int mapSize = 96;
    std::array<SkirmishSlot, kMaxSkirmishSlots> slots{};

    // Kept in the v1-v5 save prefix. New matches use slots; old saves are
    // migrated into slots during load.
    int playerCivilization = 1;
    int computerCivilization = 3;
    int difficulty = 2;
    AiPersonality personality = AiPersonality::Expanded;
    bool allied = false;

    SkirmishMapStyle mapStyle = SkirmishMapStyle::Grasslands;
    int startingResources = 500;
    int populationCap = 200;
    SkirmishVictory victory = SkirmishVictory::Standard;
    int startingTechLevel = 1;
    int endingTechLevel = 4;
    SkirmishReveal reveal = SkirmishReveal::Normal;
    bool teamsLocked = true;
    bool cheatsEnabled = false;
    SkirmishGameSpeed gameSpeed = SkirmishGameSpeed::Normal;
    int timeLimitMinutes = 60;
    int scoreLimit = 4000;

    // The original random maps (save v8): mapType 9..61 runs that map's
    // script (rms.h) at mapSizeIndex (0 tiny .. 5 giant); 0 uses mapStyle
    // and mapSize with the port's own generator.
    int mapType = 0;
    int mapSizeIndex = 1;
    // Game type (options+0x1445): 0 Random Map, 1 Terminate the Commander,
    // 2 Death Match, 5 Commander of the Base, 6 Monument Race, 7 Defend the
    // Monument.
    uint8_t gameType = 0;
    // Original resource levels (0 Standard, 1 Low, 2 Medium, 3 High) for
    // the original maps.
    uint8_t resourceLevel = 0;
    // "Team Together": allies start next to each other (FIXED_POSITIONS).
    bool fixedPositions = true;
};

enum SkirmishGameType : uint8_t {
    kGameRandomMap = 0,
    kGameTerminateCommander = 1,
    kGameDeathMatch = 2,
    kGameCommanderOfTheBase = 5,
    kGameMonumentRace = 6,
    kGameDefendTheMonument = 7,
};
const char *gameTypeName(int gameType);
const char *resourceLevelName(int level);
// Starting food/carbon/ore/nova for a game type and resource level
// (TRIBE_World new_game 0x600310).
std::array<int, 4> originalStartingResources(int gameType, int resourceLevel);
// The symbols the exe defines for a computer player's AI script
// (difficulty, population, victory, map type and size, game type, starting
// tech level, teams).
std::unordered_set<std::string> skirmishAiDefines(const SkirmishSettings &settings, int slot);

struct SkirmishPreview {
    static constexpr int kWidth = 48;
    static constexpr int kHeight = 32;
    std::array<uint8_t, kWidth * kHeight> terrain{};
    std::array<std::array<float, 2>, kMaxSkirmishSlots> starts{};
    uint64_t hash = 0;
    std::string error;
};

const char *civilizationName(int civilization);
const char *difficultyName(int difficulty);
const char *personalityName(AiPersonality personality);
const char *mapStyleName(SkirmishMapStyle style);
const char *victoryName(SkirmishVictory victory);
const char *slotTypeName(SkirmishSlotType type);
const char *revealName(SkirmishReveal reveal);
const char *gameSpeedName(SkirmishGameSpeed speed);
int activeSkirmishSlotCount(const SkirmishSettings &settings);
bool validateSkirmishSettings(
    const SkirmishSettings &settings,
    std::string *error = nullptr);
std::array<float, 2> skirmishStartPosition(
    const SkirmishSettings &settings, int slot);
uint8_t skirmishTerrainClassAt(
    const SkirmishSettings &settings, int x, int y);
SkirmishPreview generateSkirmishPreview(
    const SkirmishSettings &settings);

} // namespace swgb
