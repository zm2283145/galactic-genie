// SPDX-License-Identifier: GPL-3.0-or-later
// Parser for Genie AI personality (.per) rule files.
#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace swgb {

struct AiNode {
    std::string value;
    std::vector<AiNode> children;
    bool quoted = false;
    int line = 0;

    bool isList() const { return !children.empty(); }
    const std::string &name() const {
        static const std::string empty;
        return children.empty() ? empty : children.front().value;
    }
};

struct AiRule {
    std::vector<AiNode> conditions;
    std::vector<AiNode> actions;
    std::string source;
    int line = 0;
    bool enabled = true;
};

class AiProgram {
public:
    bool load(
        const std::string &entryPath,
        const std::unordered_set<std::string> &defines,
        std::string *err = nullptr);
    bool loadSource(
        const std::string &name,
        const std::string &source,
        const std::unordered_set<std::string> &defines,
        std::string *err = nullptr);

    int constant(
        const std::string &name,
        int fallback = 0) const;
    bool hasConstant(const std::string &name) const;

    std::vector<AiRule> rules;
    std::unordered_map<std::string, int> constants;
    std::vector<std::string> files;

private:
    std::unordered_set<std::string> defines_;
    std::unordered_set<std::string> loaded_;

    bool loadFile(
        const std::string &path,
        std::string *err);
    bool processSource(
        const std::string &name,
        const std::string &directory,
        const std::string &source,
        std::string *err);
};

std::string normalizeAiSymbol(
    const std::string &symbol);

} // namespace swgb
