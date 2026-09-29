// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <map>
#include <string>

namespace swgb {

class LanguageStrings {
public:
    bool load(const std::string &path, std::string *err, bool overlay = false);
    const std::string &get(int id) const;
    size_t size() const { return strings_.size(); }

private:
    std::map<int, std::string> strings_;
};

} // namespace swgb
