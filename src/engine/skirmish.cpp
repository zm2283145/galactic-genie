// SPDX-License-Identifier: GPL-3.0-or-later
#include "skirmish.h"

#include <array>

namespace swgb {

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
        return "Grasslands";
    case SkirmishMapStyle::Archipelago:
        return "Archipelago";
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

} // namespace swgb
