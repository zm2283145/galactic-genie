// SPDX-License-Identifier: GPL-3.0-or-later
// Reader for Genie blendomatic.dat terrain transition masks.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace swgb {

struct BlendMask {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> alpha;
};

struct BlendMode {
    std::vector<BlendMask> masks;
};

class Blendomatic {
public:
    bool load(const std::string &path, std::string *err);
    bool parse(std::vector<uint8_t> data, std::string *err);

    const std::vector<BlendMode> &modes() const { return modes_; }
    size_t modeCount() const { return modes_.size(); }
    size_t maskCount() const { return modes_.empty() ? 0 : modes_[0].masks.size(); }

private:
    std::vector<BlendMode> modes_;
};

} // namespace swgb
