// SPDX-License-Identifier: GPL-3.0-or-later
// Campaign mission screen layouts, from the game's campaign screen tables
// (interfac.drs 53021-53025 and 53028, one per campaign: mission button
// x y w h, then the mission title box x y w h, in 800x600 screen pixels).
// The buttons are frames 1 + 4 * mission (+0 normal, +1 selected,
// +2 completed, +3 locked) of the campaign background SLP 53100 + n.
#pragma once

#include "assets.h"
#include "frontend.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace swgb {

struct OriginalMissionLayout {
    int theme;
    std::array<std::array<int16_t, 8>, 8> rows;
    size_t count;
};

inline const OriginalMissionLayout *originalMissionLayout(int theme) {
    static const OriginalMissionLayout kLayouts[] = {
        {1, {{{75, 90, 160, 83, 75, 185, 160, 60}, {304, 94, 139, 83, 289, 187, 169, 60},
              {53, 225, 165, 83, 38, 318, 195, 60}, {297, 228, 168, 83, 282, 321, 198, 60},
              {46, 370, 89, 136, 21, 520, 139, 60}, {184, 377, 145, 92, 194, 482, 125, 60},
              {379, 371, 96, 121, 364, 502, 126, 60}}}, 7},
        {2, {{{62, 100, 101, 88, 27, 201, 170, 60}, {219, 90, 95, 132, 204, 233, 125, 60},
              {351, 99, 101, 88, 345, 196, 120, 60}, {41, 268, 137, 80, 26, 363, 167, 60},
              {240, 290, 173, 67, 225, 373, 203, 60}, {66, 404, 100, 106, 51, 520, 130, 60},
              {245, 421, 134, 86, 231, 519, 164, 60}}}, 7},
        {3, {{{72, 99, 174, 76, 83, 185, 180, 30}, {322, 87, 152, 91, 325, 190, 150, 30},
              {46, 235, 134, 105, 45, 350, 120, 60}, {219, 234, 137, 83, 200, 335, 140, 50},
              {373, 233, 128, 105, 370, 350, 145, 30}, {81, 419, 195, 90, 107, 520, 160, 40},
              {327, 406, 118, 98, 330, 513, 160, 50}}}, 7},
        {4, {{{48, 98, 154, 76, 48, 191, 154, 60}, {245, 90, 95, 105, 217, 210, 155, 60},
              {375, 95, 107, 86, 390, 200, 127, 60}, {43, 240, 135, 105, 28, 365, 165, 60},
              {228, 250, 120, 105, 213, 370, 150, 60}, {390, 241, 124, 105, 407, 355, 124, 60},
              {41, 415, 180, 92, 50, 530, 160, 60}, {261, 415, 180, 92, 246, 522, 210, 60}}}, 8},
        {5, {{{62, 97, 140, 77, 50, 188, 140, 60}, {322, 108, 111, 77, 310, 197, 141, 60},
              {37, 247, 129, 77, 37, 336, 129, 60}, {200, 220, 91, 119, 185, 351, 121, 60},
              {328, 248, 123, 91, 313, 349, 153, 60}, {62, 420, 110, 77, 47, 512, 140, 60},
              {263, 415, 129, 77, 248, 504, 159, 60}}}, 7},
        {8, {{{58, 94, 92, 124, 23, 231, 162, 60}, {198, 107, 132, 77, 183, 197, 162, 60},
              {377, 149, 103, 103, 362, 265, 133, 60}, {218, 246, 103, 103, 203, 361, 133, 60},
              {51, 291, 92, 124, 36, 425, 122, 60}, {181, 418, 132, 77, 166, 515, 162, 60},
              {355, 380, 103, 103, 340, 496, 133, 60}}}, 7},
                };
    for (const OriginalMissionLayout &layout : kLayouts)
        if (layout.theme == theme) return &layout;
    return nullptr;
}

inline void applyOriginalMissionLayout(Frontend &frontend, Assets &assets,
                                       const OriginalMissionLayout &layout) {
    const int slp = 53100 + layout.theme;
    const int palette = 53110 + layout.theme;
    for (size_t mission = 0; mission < layout.count; ++mission) {
        const auto &row = layout.rows[mission];
        const size_t frame = 1 + mission * 4;
        frontend.setOriginalMissionNode(
            mission, row[0], row[1],
            assets.interfaceFrame(slp, frame, palette),
            assets.interfaceFrame(slp, frame + 1, palette),
            assets.interfaceFrame(slp, frame + 2, palette),
            assets.interfaceFrame(slp, frame + 3, palette),
            row[4], row[5], row[6], row[7]);
    }
}

} // namespace swgb
