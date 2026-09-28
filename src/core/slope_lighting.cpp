// SPDX-License-Identifier: GPL-3.0-or-later
#include "slope_lighting.h"

namespace swgb {

namespace {

enum : int8_t {
    FLAT = 0,
    S_UP = 1,
    N_UP = 2,
    W_UP = 3,
    E_UP = 4,
    SW_UP = 5,
    NW_UP = 6,
    SE_UP = 7,
    NE_UP = 8,
    S_UP2 = 9,
    N_UP2 = 10,
    W_UP2 = 11,
    E_UP2 = 12,
    SWE_UP = 13,
    NWE_UP = 14,
    NSE_UP = 15,
    NSW_UP = 16,
};

bool flat(int slope) { return slope == FLAT; }
bool nUp(int slope) { return slope == N_UP || slope == N_UP2; }
bool sUp(int slope) { return slope == S_UP || slope == S_UP2; }
bool wUp(int slope) { return slope == W_UP || slope == W_UP2; }
bool eUp(int slope) { return slope == E_UP || slope == E_UP2; }
bool anyWUp(int slope) { return wUp(slope) || slope == SW_UP || slope == NW_UP; }
bool anyEUp(int slope) { return eUp(slope) || slope == SE_UP || slope == NE_UP; }
bool sOrWUp(int slope) { return sUp(slope) || wUp(slope) || slope == SW_UP; }
bool nOrWUp(int slope) { return nUp(slope) || wUp(slope) || slope == NW_UP; }
bool sOrEUp(int slope) { return sUp(slope) || eUp(slope) || slope == SE_UP; }
bool nOrEUp(int slope) { return nUp(slope) || eUp(slope) || slope == NE_UP; }
bool anySwUp(int slope) { return slope == SW_UP || slope == SWE_UP || slope == NSW_UP; }
bool anyNwUp(int slope) { return slope == NW_UP || slope == NWE_UP || slope == NSW_UP; }
bool anySeUp(int slope) { return slope == SE_UP || slope == SWE_UP || slope == NSE_UP; }
bool anyNeUp(int slope) { return slope == NE_UP || slope == NWE_UP || slope == NSW_UP; }

struct Builder {
    SlopeLighting result;
    void add(uint8_t pattern) {
        if (result.count < result.patterns.size()) result.patterns[result.count++] = pattern;
    }
};

} // namespace

SlopeLighting selectSlopeLighting(uint8_t slope, const std::array<int8_t, 8> &n) {
    Builder p;
    switch (slope) {
    case S_UP:
    case S_UP2:
        p.add(2);
        if (flat(n[SLOPE_NEIGHBOR_S])) p.add(18);
        if (flat(n[SLOPE_NEIGHBOR_NE]) + flat(n[SLOPE_NEIGHBOR_NW]) +
                flat(n[SLOPE_NEIGHBOR_N]) >
            1)
            p.add(19);
        if (flat(n[SLOPE_NEIGHBOR_NW]) || nOrWUp(n[SLOPE_NEIGHBOR_NW])) p.add(8);
        else if (flat(n[SLOPE_NEIGHBOR_W])) p.add(26);
        if (flat(n[SLOPE_NEIGHBOR_NE]) || nOrEUp(n[SLOPE_NEIGHBOR_NE])) p.add(7);
        else if (flat(n[SLOPE_NEIGHBOR_E])) p.add(28);
        if (n[SLOPE_NEIGHBOR_NE] == SWE_UP || n[SLOPE_NEIGHBOR_SE] == SWE_UP ||
            flat(n[SLOPE_NEIGHBOR_E]))
            p.add(22);
        if (n[SLOPE_NEIGHBOR_NW] == SWE_UP || n[SLOPE_NEIGHBOR_SW] == SWE_UP ||
            flat(n[SLOPE_NEIGHBOR_W]))
            p.add(21);
        break;
    case N_UP:
    case N_UP2:
        p.add(2);
        if (flat(n[SLOPE_NEIGHBOR_N])) p.add(16);
        if (flat(n[SLOPE_NEIGHBOR_SE]) + flat(n[SLOPE_NEIGHBOR_SW]) +
                flat(n[SLOPE_NEIGHBOR_S]) >
            1)
            p.add(17);
        if (flat(n[SLOPE_NEIGHBOR_SW]) || sOrWUp(n[SLOPE_NEIGHBOR_SW])) p.add(10);
        else if (flat(n[SLOPE_NEIGHBOR_W])) p.add(26);
        if (flat(n[SLOPE_NEIGHBOR_SE]) || sOrEUp(n[SLOPE_NEIGHBOR_SE])) p.add(5);
        else if (flat(n[SLOPE_NEIGHBOR_E])) p.add(28);
        if (n[SLOPE_NEIGHBOR_NW] == NWE_UP || n[SLOPE_NEIGHBOR_SW] == NWE_UP ||
            flat(n[SLOPE_NEIGHBOR_W]))
            p.add(21);
        if (n[SLOPE_NEIGHBOR_NE] == NWE_UP || n[SLOPE_NEIGHBOR_SE] == NWE_UP ||
            flat(n[SLOPE_NEIGHBOR_E]))
            p.add(22);
        break;
    case W_UP:
    case W_UP2:
        p.add(0);
        if (flat(n[SLOPE_NEIGHBOR_W])) p.add(12);
        if (flat(n[SLOPE_NEIGHBOR_NE]) + flat(n[SLOPE_NEIGHBOR_SE]) +
                flat(n[SLOPE_NEIGHBOR_E]) >
            1)
            p.add(13);
        if (flat(n[SLOPE_NEIGHBOR_NE]) || nOrEUp(n[SLOPE_NEIGHBOR_NE])) p.add(7);
        else if (flat(n[SLOPE_NEIGHBOR_N])) p.add(34);
        if (flat(n[SLOPE_NEIGHBOR_SE]) || sOrEUp(n[SLOPE_NEIGHBOR_SE])) p.add(5);
        else if (flat(n[SLOPE_NEIGHBOR_S])) p.add(35);
        break;
    case E_UP:
    case E_UP2:
        p.add(1);
        if (flat(n[SLOPE_NEIGHBOR_E])) p.add(14);
        if (flat(n[SLOPE_NEIGHBOR_NW]) + flat(n[SLOPE_NEIGHBOR_SW]) +
                flat(n[SLOPE_NEIGHBOR_W]) >
            1)
            p.add(15);
        if (flat(n[SLOPE_NEIGHBOR_SW]) || sOrWUp(n[SLOPE_NEIGHBOR_SW])) p.add(10);
        else if (flat(n[SLOPE_NEIGHBOR_S])) p.add(37);
        if (flat(n[SLOPE_NEIGHBOR_NW]) || nOrWUp(n[SLOPE_NEIGHBOR_NW])) p.add(8);
        else if (flat(n[SLOPE_NEIGHBOR_N])) p.add(36);
        break;
    case SW_UP:
        p.add(0);
        if (flat(n[SLOPE_NEIGHBOR_SW]) || anyNeUp(n[SLOPE_NEIGHBOR_SW])) p.add(6);
        else {
            if (flat(n[SLOPE_NEIGHBOR_W])) p.add(12);
            if (flat(n[SLOPE_NEIGHBOR_S])) p.add(35);
        }
        if (flat(n[SLOPE_NEIGHBOR_NE]) || nOrEUp(n[SLOPE_NEIGHBOR_NE])) p.add(7);
        else {
            if (flat(n[SLOPE_NEIGHBOR_N]) || nUp(n[SLOPE_NEIGHBOR_N])) p.add(34);
            if (flat(n[SLOPE_NEIGHBOR_E]) || eUp(n[SLOPE_NEIGHBOR_E])) p.add(28);
        }
        if (sUp(n[SLOPE_NEIGHBOR_S]) && wUp(n[SLOPE_NEIGHBOR_SW])) p.add(35);
        if (sUp(n[SLOPE_NEIGHBOR_SW]) && wUp(n[SLOPE_NEIGHBOR_W])) p.add(12);
        if (n[SLOPE_NEIGHBOR_NE] == SWE_UP || n[SLOPE_NEIGHBOR_SE] == SWE_UP) p.add(22);
        if (sUp(n[SLOPE_NEIGHBOR_NW])) p.add(23);
        if (flat(n[SLOPE_NEIGHBOR_E])) p.add(28);
        break;
    case NW_UP:
        p.add(0);
        if (flat(n[SLOPE_NEIGHBOR_NW]) || anySeUp(n[SLOPE_NEIGHBOR_NW])) p.add(4);
        else {
            if (flat(n[SLOPE_NEIGHBOR_W])) p.add(12);
            if (flat(n[SLOPE_NEIGHBOR_N])) p.add(34);
        }
        if (flat(n[SLOPE_NEIGHBOR_SE]) || sOrEUp(n[SLOPE_NEIGHBOR_SE])) p.add(5);
        else {
            if (flat(n[SLOPE_NEIGHBOR_S]) || sUp(n[SLOPE_NEIGHBOR_S])) p.add(35);
            if (flat(n[SLOPE_NEIGHBOR_E]) || eUp(n[SLOPE_NEIGHBOR_E])) p.add(28);
        }
        if (nUp(n[SLOPE_NEIGHBOR_SW])) p.add(23);
        if (flat(n[SLOPE_NEIGHBOR_W])) p.add(12);
        if (n[SLOPE_NEIGHBOR_NE] == NWE_UP) p.add(22);
        if (flat(n[SLOPE_NEIGHBOR_E])) p.add(28);
        if (flat(n[SLOPE_NEIGHBOR_N])) p.add(34);
        if (wUp(n[SLOPE_NEIGHBOR_W]) && nUp(n[SLOPE_NEIGHBOR_NW])) p.add(12);
        if (wUp(n[SLOPE_NEIGHBOR_NW]) && nUp(n[SLOPE_NEIGHBOR_N])) p.add(34);
        break;
    case SE_UP:
        p.add(1);
        if (flat(n[SLOPE_NEIGHBOR_NW]) || nOrWUp(n[SLOPE_NEIGHBOR_NW])) p.add(8);
        else {
            if (flat(n[SLOPE_NEIGHBOR_W]) || wUp(n[SLOPE_NEIGHBOR_W])) p.add(26);
            if (flat(n[SLOPE_NEIGHBOR_N]) || nUp(n[SLOPE_NEIGHBOR_N])) p.add(36);
        }
        if (flat(n[SLOPE_NEIGHBOR_SE]) || anyNwUp(n[SLOPE_NEIGHBOR_SE])) p.add(9);
        else {
            if (flat(n[SLOPE_NEIGHBOR_E])) p.add(14);
            if (flat(n[SLOPE_NEIGHBOR_S])) p.add(37);
        }
        if (sUp(n[SLOPE_NEIGHBOR_S]) && eUp(n[SLOPE_NEIGHBOR_SE])) p.add(37);
        if (sUp(n[SLOPE_NEIGHBOR_SE]) && eUp(n[SLOPE_NEIGHBOR_E])) p.add(14);
        if (sUp(n[SLOPE_NEIGHBOR_NE])) p.add(20);
        if (sUp(n[SLOPE_NEIGHBOR_W]) || n[SLOPE_NEIGHBOR_NW] == SWE_UP) p.add(21);
        break;
    case NE_UP:
        p.add(1);
        if (flat(n[SLOPE_NEIGHBOR_SW]) || sOrWUp(n[SLOPE_NEIGHBOR_SW])) p.add(10);
        else {
            if (flat(n[SLOPE_NEIGHBOR_W]) || wUp(n[SLOPE_NEIGHBOR_W])) p.add(26);
            if (flat(n[SLOPE_NEIGHBOR_S]) || sUp(n[SLOPE_NEIGHBOR_S])) p.add(37);
        }
        if (flat(n[SLOPE_NEIGHBOR_NE]) || anySwUp(n[SLOPE_NEIGHBOR_NE])) p.add(11);
        else {
            if (flat(n[SLOPE_NEIGHBOR_N])) p.add(36);
            if (flat(n[SLOPE_NEIGHBOR_E])) p.add(14);
        }
        if (eUp(n[SLOPE_NEIGHBOR_E]) && nUp(n[SLOPE_NEIGHBOR_NE])) p.add(14);
        if (eUp(n[SLOPE_NEIGHBOR_NE]) && nUp(n[SLOPE_NEIGHBOR_N])) p.add(36);
        if (nUp(n[SLOPE_NEIGHBOR_SE])) p.add(20);
        if (n[SLOPE_NEIGHBOR_NW] == NWE_UP) p.add(21);
        if (flat(n[SLOPE_NEIGHBOR_W])) p.add(26);
        break;
    case SWE_UP:
        p.add(3);
        if (flat(n[SLOPE_NEIGHBOR_N])) p.add(30);
        if (flat(n[SLOPE_NEIGHBOR_SE]) + flat(n[SLOPE_NEIGHBOR_SW]) +
                flat(n[SLOPE_NEIGHBOR_S]) >
            1)
            p.add(31);
        if (flat(n[SLOPE_NEIGHBOR_SE]) || anyNwUp(n[SLOPE_NEIGHBOR_SE])) p.add(9);
        if (flat(n[SLOPE_NEIGHBOR_SW]) || anyNeUp(n[SLOPE_NEIGHBOR_SW])) p.add(6);
        if (sUp(n[SLOPE_NEIGHBOR_S])) p.add(22);
        if (sUp(n[SLOPE_NEIGHBOR_SW]) || sUp(n[SLOPE_NEIGHBOR_NW])) p.add(23);
        if (sUp(n[SLOPE_NEIGHBOR_NE]) || sUp(n[SLOPE_NEIGHBOR_SE])) p.add(20);
        break;
    case NWE_UP:
        p.add(3);
        if (flat(n[SLOPE_NEIGHBOR_NE]) + flat(n[SLOPE_NEIGHBOR_NW]) +
                flat(n[SLOPE_NEIGHBOR_N]) >
            1)
            p.add(33);
        if (flat(n[SLOPE_NEIGHBOR_S])) p.add(32);
        if (flat(n[SLOPE_NEIGHBOR_NW]) || anySeUp(n[SLOPE_NEIGHBOR_NW])) p.add(4);
        if (flat(n[SLOPE_NEIGHBOR_NE]) || anySwUp(n[SLOPE_NEIGHBOR_NE])) p.add(11);
        if (flat(n[SLOPE_NEIGHBOR_E])) p.add(20);
        if (flat(n[SLOPE_NEIGHBOR_W]) || nUp(n[SLOPE_NEIGHBOR_SW]) ||
            nUp(n[SLOPE_NEIGHBOR_NW]))
            p.add(23);
        if (nUp(n[SLOPE_NEIGHBOR_SE]) || nUp(n[SLOPE_NEIGHBOR_NE])) p.add(20);
        break;
    case NSE_UP:
        p.add(1);
        if (flat(n[SLOPE_NEIGHBOR_W]) || anyWUp(n[SLOPE_NEIGHBOR_W])) p.add(26);
        if (flat(n[SLOPE_NEIGHBOR_NE]) + flat(n[SLOPE_NEIGHBOR_SE]) +
                flat(n[SLOPE_NEIGHBOR_E]) >
            1)
            p.add(27);
        if (flat(n[SLOPE_NEIGHBOR_NE]) || anySwUp(n[SLOPE_NEIGHBOR_NE])) p.add(11);
        else {
            if (flat(n[SLOPE_NEIGHBOR_N]) || nUp(n[SLOPE_NEIGHBOR_N])) p.add(36);
            if (nUp(n[SLOPE_NEIGHBOR_NE])) p.add(14);
        }
        if (flat(n[SLOPE_NEIGHBOR_SE]) || anyNwUp(n[SLOPE_NEIGHBOR_SE])) p.add(9);
        else {
            if (flat(n[SLOPE_NEIGHBOR_S])) p.add(37);
            if (sUp(n[SLOPE_NEIGHBOR_SE])) p.add(14);
        }
        if (n[SLOPE_NEIGHBOR_NW] == NWE_UP) p.add(21);
        if (n[SLOPE_NEIGHBOR_SW] == SWE_UP) p.add(26);
        break;
    case NSW_UP:
        p.add(0);
        if (flat(n[SLOPE_NEIGHBOR_E]) || anyEUp(n[SLOPE_NEIGHBOR_E])) p.add(28);
        if (flat(n[SLOPE_NEIGHBOR_NW]) + flat(n[SLOPE_NEIGHBOR_SW]) +
                flat(n[SLOPE_NEIGHBOR_W]) >
            1)
            p.add(29);
        if (flat(n[SLOPE_NEIGHBOR_NW]) || anySeUp(n[SLOPE_NEIGHBOR_NW])) p.add(4);
        else {
            if (flat(n[SLOPE_NEIGHBOR_N]) || nUp(n[SLOPE_NEIGHBOR_N])) p.add(34);
            if (nUp(n[SLOPE_NEIGHBOR_NW])) p.add(12);
        }
        if (flat(n[SLOPE_NEIGHBOR_SW]) || anyNeUp(n[SLOPE_NEIGHBOR_SW])) p.add(6);
        else {
            if (flat(n[SLOPE_NEIGHBOR_S])) p.add(35);
            if (sUp(n[SLOPE_NEIGHBOR_SW])) p.add(12);
        }
        if (n[SLOPE_NEIGHBOR_NE] == NWE_UP) p.add(22);
        if (n[SLOPE_NEIGHBOR_SE] == SWE_UP) p.add(28);
        break;
    default:
        break;
    }
    return p.result;
}

} // namespace swgb
