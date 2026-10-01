// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace swgb {

enum class SkirmishMapStyle : uint8_t {
    Grasslands,
    Archipelago,
    CompactIslands,
    LegacyRandom,
};

enum class AiPersonality : uint8_t {
    Expanded,
    Classic,
};

enum class SkirmishVictory : uint8_t {
    Conquest,
    CommandCenter,
};

struct SkirmishSettings {
    uint32_t seed = 0x5A17u;
    int mapSize = 96;
    int playerCivilization = 1;
    int computerCivilization = 3;
    int difficulty = 2;
    AiPersonality personality = AiPersonality::Expanded;
    bool allied = false;
    SkirmishMapStyle mapStyle = SkirmishMapStyle::Grasslands;
    int startingResources = 500;
    int populationCap = 200;
    SkirmishVictory victory = SkirmishVictory::Conquest;
};

const char *civilizationName(int civilization);
const char *difficultyName(int difficulty);
const char *personalityName(AiPersonality personality);
const char *mapStyleName(SkirmishMapStyle style);
const char *victoryName(SkirmishVictory victory);

} // namespace swgb
