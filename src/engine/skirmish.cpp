// SPDX-License-Identifier: GPL-3.0-or-later
#include "skirmish.h"

#include "rms.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>

namespace swgb {
namespace {

float hash2(int x, int y, uint32_t seed) {
    uint32_t h =
        (uint32_t)x * 374761393u +
        (uint32_t)y * 668265263u +
        seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xffffu) /
           65535.0f;
}

float valueNoise(float x, float y, uint32_t seed) {
    const int xi = (int)std::floor(x);
    const int yi = (int)std::floor(y);
    float fx = x - xi;
    float fy = y - yi;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    const float a = hash2(xi, yi, seed);
    const float b = hash2(xi + 1, yi, seed);
    const float c = hash2(xi, yi + 1, seed);
    const float d = hash2(xi + 1, yi + 1, seed);
    return (a + (b - a) * fx) +
           ((c + (d - c) * fx) -
            (a + (b - a) * fx)) *
               fy;
}

float terrainNoise(
    int x, int y, uint32_t seed, float scale) {
    return valueNoise(
               x / scale, y / scale, seed) *
               0.65f +
           valueNoise(
               x / (scale * 0.47f),
               y / (scale * 0.47f),
               seed + 41u) *
               0.35f;
}

bool isActive(const SkirmishSlot &slot) {
    return slot.type != SkirmishSlotType::Closed;
}

} // namespace

SkirmishSettings::SkirmishSettings() {
    slots[0].type = SkirmishSlotType::Human;
    slots[0].name = "Player";
    slots[0].color = 0;
    slots[0].civilization = 1;
    slots[0].team = 1;
    slots[1].type = SkirmishSlotType::Computer;
    slots[1].name = "Computer 2";
    slots[1].color = 1;
    slots[1].civilization = 3;
    slots[1].team = 2;
    for (int slot = 2; slot < kMaxSkirmishSlots; ++slot) {
        slots[(size_t)slot].name =
            "Computer " + std::to_string(slot + 1);
        slots[(size_t)slot].color = (uint8_t)slot;
        slots[(size_t)slot].civilization =
            (uint8_t)(1 + slot % 8);
        slots[(size_t)slot].team =
            (uint8_t)(slot + 1);
    }
}

const char *civilizationName(int civilization) {
    static constexpr std::array<const char *, 9> names{{
        "Gaia", "Galactic Empire", "Gungans", "Rebel Alliance",
        "Royal Naboo", "Trade Federation", "Wookiees",
        "Galactic Republic", "Confederacy",
    }};
    return civilization >= 0 &&
                   civilization < (int)names.size()
               ? names[(std::size_t)civilization]
               : "Unknown";
}

const char *difficultyName(int difficulty) {
    static constexpr std::array<const char *, 5> names{{
        "Hardest", "Hard", "Moderate", "Easy", "Easiest",
    }};
    return difficulty >= 0 &&
                   difficulty < (int)names.size()
               ? names[(std::size_t)difficulty]
               : "Moderate";
}

const char *personalityName(AiPersonality personality) {
    switch (personality) {
    case AiPersonality::Expanded:
        return "Computer Expanded";
    case AiPersonality::Classic:
        return "Computer Classic";
    }
    return "Computer Expanded";
}

const char *mapStyleName(SkirmishMapStyle style) {
    switch (style) {
    case SkirmishMapStyle::Grasslands:
        return "Savannah";
    case SkirmishMapStyle::Forest:
        return "Forest";
    case SkirmishMapStyle::Desert:
        return "Desert";
    case SkirmishMapStyle::Ice:
        return "Tundra";
    case SkirmishMapStyle::Swamp:
        return "Swamp";
    case SkirmishMapStyle::InlandWater:
        return "Rivers";
    case SkirmishMapStyle::Coastal:
        return "Shoreline";
    case SkirmishMapStyle::Archipelago:
        return "Sea";
    case SkirmishMapStyle::LandMass:
        return "Land Mass";
    case SkirmishMapStyle::CompactIslands:
        return "Compact Two Islands";
    case SkirmishMapStyle::LegacyRandom:
        return "Legacy Development";
    }
    return "Grasslands";
}

const char *victoryName(SkirmishVictory victory) {
    switch (victory) {
    case SkirmishVictory::Standard:
        return "Standard";
    case SkirmishVictory::Conquest:
        return "Conquest";
    case SkirmishVictory::TimeLimit:
        return "Time Limit";
    case SkirmishVictory::Score:
        return "Score";
    case SkirmishVictory::CommandCenter:
        return "Command Center";
    }
    return "Conquest";
}

const char *slotTypeName(SkirmishSlotType type) {
    switch (type) {
    case SkirmishSlotType::Closed: return "CLOSED";
    case SkirmishSlotType::Human: return "HUMAN";
    case SkirmishSlotType::Computer: return "COMPUTER";
    }
    return "CLOSED";
}

const char *revealName(SkirmishReveal reveal) {
    switch (reveal) {
    case SkirmishReveal::Normal: return "NORMAL";
    case SkirmishReveal::Explored: return "EXPLORED";
    case SkirmishReveal::AllVisible: return "ALL VISIBLE";
    }
    return "NORMAL";
}

const char *gameSpeedName(SkirmishGameSpeed speed) {
    switch (speed) {
    case SkirmishGameSpeed::Slow: return "SLOW";
    case SkirmishGameSpeed::Normal: return "NORMAL";
    case SkirmishGameSpeed::Fast: return "FAST";
    }
    return "NORMAL";
}

int activeSkirmishSlotCount(
    const SkirmishSettings &settings) {
    return (int)std::count_if(
        settings.slots.begin(),
        settings.slots.end(),
        isActive);
}

bool validateSkirmishSettings(
    const SkirmishSettings &settings,
    std::string *error) {
    const auto fail = [&](const char *message) {
        if (error) *error = message;
        return false;
    };
    if (settings.mapType != 0) {
        if (!rmsScriptForMapType(settings.mapType) || settings.mapSizeIndex < 0 ||
            settings.mapSizeIndex > 5 || settings.gameType > 7 || settings.gameType == 3 ||
            settings.gameType == 4 || settings.resourceLevel > 3 ||
            (settings.gameType == 7 && (settings.mapType == 17 || settings.mapType == 37))) {
            if (error) *error = "unsupported original random map settings";
            return false;
        }
    } else if (settings.mapSize < 48 ||
        settings.mapSize > 160 ||
        (settings.mapSize != 48 &&
         settings.mapSize % 32))
        return fail(
            "map size must be the 48 regression size or a supported 64, 96, 128, or 160 preset");
    if ((int)settings.mapStyle <
            (int)SkirmishMapStyle::Grasslands ||
        settings.mapStyle >
            SkirmishMapStyle::LegacyRandom)
        return fail("invalid random map family");
    const int active =
        activeSkirmishSlotCount(settings);
    if (active < 2 || active > kMaxSkirmishSlots)
        return fail("random maps require 2 to 8 active slots");
    if ((active > 2 && settings.mapSize < 64) ||
        (active > 4 && settings.mapSize < 96) ||
        (active > 6 && settings.mapSize < 128))
        return fail(
            "selected player count requires a larger map preset");
    std::set<int> colors;
    int humans = 0;
    int firstHumanTeam = -1;
    bool opponent = false;
    for (const SkirmishSlot &slot : settings.slots) {
        if (!isActive(slot)) continue;
        if (slot.civilization < 1 ||
            slot.civilization > 8)
            return fail(
                "active slot civilization must be between 1 and 8");
        if (slot.color >= kMaxSkirmishSlots ||
            !colors.insert(slot.color).second)
            return fail(
                "active player colors must be unique");
        if (slot.team > kMaxSkirmishSlots)
            return fail("team must be none or 1 through 8");
        if (slot.difficulty > 4)
            return fail("AI difficulty is out of range");
        if (slot.type == SkirmishSlotType::Human) {
            ++humans;
            if (firstHumanTeam < 0)
                firstHumanTeam = slot.team;
        }
    }
    if (humans != 1)
        return fail(
            "exactly one local human slot is required");
    for (const SkirmishSlot &slot : settings.slots)
        if (isActive(slot) &&
            slot.type != SkirmishSlotType::Human &&
            (firstHumanTeam == 0 ||
             slot.team == 0 ||
             slot.team != firstHumanTeam))
            opponent = true;
    if (!opponent && !settings.allied)
        return fail(
            "at least one non-allied computer opponent is required");
    if (settings.populationCap < 25 ||
        settings.populationCap > 250 ||
        settings.startingResources < 0 ||
        settings.startingResources > 50000)
        return fail("invalid economy limits");
    if (settings.startingTechLevel < 1 ||
        settings.startingTechLevel > 4 ||
        settings.endingTechLevel <
            settings.startingTechLevel ||
        settings.endingTechLevel > 4)
        return fail("invalid Tech Level range");
    if (settings.timeLimitMinutes < 5 ||
        settings.timeLimitMinutes > 240 ||
        settings.scoreLimit < 500 ||
        settings.scoreLimit > 20000)
        return fail("invalid victory target");
    return true;
}

std::array<float, 2> skirmishStartPosition(
    const SkirmishSettings &settings, int slot) {
    std::array<int, kMaxSkirmishSlots> active{};
    int activeCount = 0;
    int activeIndex = -1;
    for (int index = 0;
         index < kMaxSkirmishSlots; ++index) {
        if (!isActive(settings.slots[(size_t)index]))
            continue;
        active[(size_t)activeCount] = index;
        if (index == slot) activeIndex = activeCount;
        ++activeCount;
    }
    if (activeIndex < 0 || activeCount < 1)
        return {-1.0f, -1.0f};
    if (settings.mapSize == 48 &&
        activeCount == 2 &&
        settings.mapStyle ==
            SkirmishMapStyle::CompactIslands)
        return activeIndex == 0
                   ? std::array<float, 2>{
                         14.0f, 17.0f}
                   : std::array<float, 2>{
                         30.0f, 29.0f};
    const float angle =
        -1.57079632679f +
        6.28318530718f *
            (float)activeIndex /
            (float)activeCount +
        (float)(settings.seed & 255u) *
            (6.28318530718f / 65536.0f);
    float radius =
        std::min(
            settings.mapSize * 0.31f,
            settings.mapSize * 0.5f -
                14.0f);
    if (settings.mapStyle ==
        SkirmishMapStyle::CompactIslands) {
        const bool left =
            activeIndex < (activeCount + 1) / 2;
        const int groupIndex =
            left ? activeIndex
                 : activeIndex -
                       (activeCount + 1) / 2;
        const int groupCount =
            left ? (activeCount + 1) / 2
                 : activeCount / 2;
        return {
            settings.mapSize *
                (left ? 0.25f : 0.75f),
            settings.mapSize *
                (0.25f +
                 0.5f *
                     (groupIndex + 0.5f) /
                     std::max(1, groupCount))};
    }
    if (settings.mapStyle ==
        SkirmishMapStyle::Archipelago)
        radius =
            std::min(
                settings.mapSize * 0.34f,
                settings.mapSize * 0.5f -
                    14.0f);
    return {
        settings.mapSize * 0.5f +
            std::cos(angle) * radius,
        settings.mapSize * 0.5f +
            std::sin(angle) * radius};
}

uint8_t skirmishTerrainClassAt(
    const SkirmishSettings &settings, int x, int y) {
    if (x < 0 || y < 0 ||
        x >= settings.mapSize ||
        y >= settings.mapSize)
        return 2;
    const float nx =
        ((float)x + 0.5f) / settings.mapSize;
    const float ny =
        ((float)y + 0.5f) / settings.mapSize;
    const float noise =
        terrainNoise(x, y, settings.seed, 11.0f);
    const uint8_t landClass =
        (uint8_t)(3 +
                  std::min(
                      2,
                      (int)(noise * 3.0f)));
    switch (settings.mapStyle) {
    case SkirmishMapStyle::CompactIslands: {
        const float distance =
            std::abs(nx - 0.5f);
        if (distance < 0.035f) return 2;
        if (distance < 0.085f) return 1;
        return landClass;
    }
    case SkirmishMapStyle::Archipelago: {
        float closest = 10.0f;
        for (int slot = 0;
             slot < kMaxSkirmishSlots; ++slot) {
            const auto start =
                skirmishStartPosition(settings, slot);
            if (start[0] < 0.0f) continue;
            const float dx =
                (x - start[0]) /
                (settings.mapSize * 0.18f);
            const float dy =
                (y - start[1]) /
                (settings.mapSize * 0.18f);
            closest = std::min(
                closest,
                std::sqrt(dx * dx + dy * dy));
        }
        closest += (noise - 0.5f) * 0.18f;
        if (closest > 1.12f) return 2;
        if (closest > 0.98f) return 1;
        return landClass;
    }
    case SkirmishMapStyle::Coastal: {
        const float edge =
            std::min(
                std::min(nx, 1.0f - nx),
                std::min(ny, 1.0f - ny));
        const float coast =
            0.085f +
            (noise - 0.5f) * 0.035f;
        if (edge < coast) return 2;
        if (edge < coast + 0.045f) return 1;
        return landClass;
    }
    case SkirmishMapStyle::InlandWater:
    case SkirmishMapStyle::Swamp: {
        const float dx = nx - 0.5f;
        const float dy = ny - 0.5f;
        const float lake =
            std::sqrt(dx * dx + dy * dy) +
            (noise - 0.5f) * 0.13f;
        if (lake < 0.13f) return 2;
        if (lake < 0.18f) return 1;
        if (settings.mapStyle ==
                SkirmishMapStyle::Swamp &&
            noise < 0.28f)
            return 1;
        return landClass;
    }
    case SkirmishMapStyle::LegacyRandom:
        if (noise < 0.24f) return 2;
        if (noise < 0.30f) return 1;
        return landClass;
    default:
        return landClass;
    }
}

SkirmishPreview generateSkirmishPreview(
    const SkirmishSettings &settings) {
    SkirmishPreview preview;
    if (!validateSkirmishSettings(
            settings, &preview.error))
        return preview;
    uint64_t hash = 1469598103934665603ull;
    for (int y = 0; y < SkirmishPreview::kHeight;
         ++y)
        for (int x = 0;
             x < SkirmishPreview::kWidth; ++x) {
            const int tx =
                x * settings.mapSize /
                SkirmishPreview::kWidth;
            const int ty =
                y * settings.mapSize /
                SkirmishPreview::kHeight;
            const uint8_t terrain =
                skirmishTerrainClassAt(
                    settings, tx, ty);
            preview.terrain[
                (size_t)y *
                    SkirmishPreview::kWidth +
                x] = terrain;
            hash ^= terrain;
            hash *= 1099511628211ull;
        }
    for (int slot = 0;
         slot < kMaxSkirmishSlots; ++slot) {
        preview.starts[(size_t)slot] =
            skirmishStartPosition(
                settings, slot);
        for (float value :
             preview.starts[(size_t)slot]) {
            const uint32_t fixed =
                value < 0.0f
                    ? 0xffffffffu
                    : (uint32_t)std::lround(
                          value * 1024.0f);
            hash ^= fixed;
            hash *= 1099511628211ull;
        }
    }
    preview.hash = hash;
    return preview;
}

const char *gameTypeName(int gameType) {
    switch (gameType) {
    case kGameTerminateCommander: return "TERMINATE THE COMMANDER";
    case kGameDeathMatch: return "DEATH MATCH";
    case kGameCommanderOfTheBase: return "COMMANDER OF THE BASE";
    case kGameMonumentRace: return "MONUMENT RACE";
    case kGameDefendTheMonument: return "DEFEND THE MONUMENT";
    default: return "RANDOM MAP";
    }
}

const char *resourceLevelName(int level) {
    switch (level) {
    case 1: return "LOW";
    case 2: return "MEDIUM";
    case 3: return "HIGH";
    default: return "STANDARD";
    }
}

std::array<int, 4> originalStartingResources(int gameType, int resourceLevel) {
    if (gameType == kGameDeathMatch) return {{20000, 20000, 5000, 10000}};
    if (gameType == kGameTerminateCommander) return {{500, 500, 150, 0}};
    switch (resourceLevel) {
    case 2: return {{500, 500, 450, 300}};
    case 3: return {{1000, 1000, 850, 700}};
    default: return {{200, 200, 250, 100}};
    }
}

std::unordered_set<std::string> skirmishAiDefines(const SkirmishSettings &settings, int slot) {
    static const char *difficulties[] = {"DIFFICULTY-HARDEST", "DIFFICULTY-HARD",
                                         "DIFFICULTY-MODERATE", "DIFFICULTY-EASY",
                                         "DIFFICULTY-EASIEST"};
    const SkirmishSlot &player = settings.slots[(size_t)std::max(0, std::min(7, slot))];
    std::unordered_set<std::string> defines{
        difficulties[std::max(0, std::min(4, (int)player.difficulty))],
        "POPULATION-CAP-" + std::to_string(settings.populationCap),
    };
    switch (settings.victory) {
    case SkirmishVictory::Standard: defines.insert("VICTORY-STANDARD"); break;
    case SkirmishVictory::Conquest: defines.insert("VICTORY-CONQUEST"); break;
    case SkirmishVictory::TimeLimit: defines.insert("VICTORY-TIME-LIMIT"); break;
    case SkirmishVictory::Score: defines.insert("VICTORY-SCORE"); break;
    default: break;
    }
    if (settings.teamsLocked) defines.insert("TEAMS-LOCKED");
    static const char *starts[] = {"TECH-LEVEL-1-START", "TECH-LEVEL-2-START",
                                   "TECH-LEVEL-3-START", "TECH-LEVEL-4-START"};
    defines.insert(starts[std::max(0, std::min(3, settings.startingTechLevel - 1))]);
    if (settings.gameType == kGameTerminateCommander) defines.insert("TERMINATE-THE-COMMANDER");
    if (settings.gameType == kGameDeathMatch) defines.insert("DEATH-MATCH");
    if (settings.gameType == kGameCommanderOfTheBase) defines.insert("EMPEROR");
    if (settings.gameType == kGameMonumentRace) defines.insert("MONUMENT-RACE");
    if (settings.gameType == kGameDefendTheMonument) defines.insert("DEFEND-MONUMENT");
    if (settings.mapType == 0) {
        if (settings.mapStyle == SkirmishMapStyle::Archipelago ||
            settings.mapStyle == SkirmishMapStyle::CompactIslands)
            defines.insert(player.team > 0 ? "TEAM-LAND-SATELLITES-MAP" : "LAND-SATELLITES-MAP");
        return defines;
    }
    static const char *sizes[] = {"TINY-MAP", "SMALL-MAP", "MEDIUM-MAP",
                                  "NORMAL-MAP", "LARGE-MAP", "GIANT-MAP"};
    defines.insert(sizes[std::max(0, std::min(5, settings.mapSizeIndex))]);
    static const std::pair<int, const char *> maps[] = {
        {9, "DESERT-MAP"}, {10, "WATER-MASS-MAP"}, {11, "SEA-MAP"}, {12, "FOREST-MAP"},
        {13, "SHORELINE-MAP"}, {14, "LAND-MASS-MAP"}, {15, "SPACE-MASS-MAP"},
        {16, "NOVA-LAKE-MAP"}, {17, "FORTRESS-MAP"}, {18, "NOVA-ASSAULT-MAP"},
        {19, "PRECIPICE-MAP"}, {20, "LAND-SATELLITES-MAP"}, {21, "SPACE-SATELLITES-MAP"},
        {22, "LARGE-SEA-MAP"}, {23, "PLANETS-AND-MOONS-MAP"}, {24, "SEARCH-AND-DESTROY-MAP"},
        {25, "RIVERS-MAP"}, {26, "TEAM-LAND-SATELLITES-MAP"}, {27, "TEAM-SPACE-SATELLITES-MAP"},
        {29, "TUNDRA-MAP"}, {30, "FLATS-MAP"}, {31, "SAVANNAH-MAP"}, {32, "SWAMP-MAP"},
        {33, "ARENA-MAP"}, {35, "MOTHERLODE-MAP"}, {36, "ICE-LAKE-MAP"}, {37, "RAIDERS-MAP"},
        {38, "STAR-WARS-WORLD-KASHYYYK-MAP"}, {39, "STAR-WARS-WORLD-ENDOR-MAP"},
        {40, "STAR-WARS-WORLD-YAVIN-MAP"}, {41, "STAR-WARS-WORLD-HOTH-MAP"},
        {42, "STAR-WARS-WORLD-KRANT-MAP"}, {43, "STAR-WARS-WORLD-HANOON-MAP"},
        {44, "STAR-WARS-WORLD-GEDDES-MAP"}, {45, "STAR-WARS-WORLD-KESSEL-MAP"},
        {46, "STAR-WARS-WORLD-TATOOINE-MAP"}, {47, "STAR-WARS-WORLD-REYTHA-MAP"},
        {48, "STAR-WARS-WORLD-ZALORIIS-MAP"}, {49, "STAR-WARS-WORLD-DAGOBAH-MAP"},
        {50, "STAR-WARS-WORLD-NABOO-MAP"}, {55, "STAR-WARS-WORLD-MOSESPA-MAP"},
        {56, "STAR-WARS-WORLD-SARAPIN-MAP"}, {57, "STAR-WARS-WORLD-AEREEN-MAP"},
        {58, "STAR-WARS-WORLD-EREDENN-MAP"}, {59, "STAR-WARS-WORLD-GEONOSIS-MAP"},
        {60, "STAR-WARS-WORLD-TATOOINE-NEW-MAP"}};
    for (const auto &entry : maps)
        if (entry.first == settings.mapType) defines.insert(entry.second);
    return defines;
}

} // namespace swgb
