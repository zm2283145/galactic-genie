// SPDX-License-Identifier: GPL-3.0-or-later
#include "palette.h"

#include <cstdlib>
#include <cstring>

namespace swgb {

bool Palette::parse(const std::vector<uint8_t> &data, std::string *err) {
    std::string s(data.begin(), data.end());
    const char *p = s.c_str();
    if (strncmp(p, "JASC-PAL", 8) != 0) {
        if (err) *err = "not a JASC-PAL palette";
        return false;
    }
    // Tokenise on whitespace: "JASC-PAL" "0100" "<count>" then r g b triples.
    std::vector<long> nums;
    char *end = nullptr;
    const char *q = p + 8;
    while (*q) {
        while (*q && (*q == ' ' || *q == '\r' || *q == '\n' || *q == '\t')) q++;
        if (!*q) break;
        long v = strtol(q, &end, 10);
        if (end == q) break;
        nums.push_back(v);
        q = end;
    }
    if (nums.size() < 2) {
        if (err) *err = "truncated palette";
        return false;
    }
    long count = nums[1];
    if (count > 256 || (long)nums.size() < 2 + count * 3) {
        if (err) *err = "bad palette color count";
        return false;
    }
    for (long i = 0; i < count; i++) {
        colors[i] = Rgba{(uint8_t)nums[2 + i * 3], (uint8_t)nums[3 + i * 3], (uint8_t)nums[4 + i * 3], 255};
    }
    return true;
}

} // namespace swgb
