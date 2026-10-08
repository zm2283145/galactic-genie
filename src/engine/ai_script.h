// SPDX-License-Identifier: GPL-3.0-or-later
// Parser for Genie AI personality (.per) rule files.
#pragma once

#include <cstddef>
#include <functional>
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
        std::string *err = nullptr,
        const std::unordered_map<
            std::string, std::string>
            *virtualFiles = nullptr);

    int constant(
        const std::string &name,
        int fallback = 0) const;
    bool hasConstant(const std::string &name) const;

    std::vector<AiRule> rules;
    std::unordered_map<std::string, int> constants;
    std::vector<std::string> files;
    std::vector<std::string> missingFiles;

    // The original's standard modules ((load "constants"), (load "sn-gather")
    // ...) are not files in Game\AI: the exe extracts them from
    // gamedata_x1.drs ("bina" 60001-60056) into data\load. The source of a
    // module by DRS id; without one the modules are skipped.
    using BuiltinModuleSource = std::function<bool(int resourceId, std::string &text)>;
    static void setBuiltinModuleSource(BuiltinModuleSource source);
    // DRS id of a standard module (e.g. "constants.per" -> 60011), or -1.
    static int builtinModuleResource(const std::string &fileName);

private:
    std::unordered_set<std::string> defines_;
    std::unordered_set<std::string> loaded_;
    std::unordered_map<
        std::string, std::string>
        virtualFiles_;
    bool allowMissingIncludes_ = false;

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
