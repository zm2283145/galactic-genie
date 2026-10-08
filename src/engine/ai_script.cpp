// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai_script.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <sstream>

namespace swgb {

namespace {

struct Token {
    std::string text;
    bool quoted = false;
    int line = 0;
};

std::string trim(const std::string &text) {
    size_t begin = 0;
    while (begin < text.size() &&
           std::isspace((unsigned char)text[begin]))
        begin++;
    size_t end = text.size();
    while (end > begin &&
           std::isspace((unsigned char)text[end - 1]))
        end--;
    return text.substr(begin, end - begin);
}

// The original's load catalog (FUN_005ff440): data\load\<name> is
// extracted from gamedata_x1.drs "bina" 60001 + index.
constexpr const char *kBuiltinAiModules[] = {
    "age-advancement.per", "aggressive.per", "airbase.per", "animal.per",
    "attack.per", "building-count.per", "carbon.per", "cheats.per",
    "civ-loads.per", "combat-arm.per", "constants.per", "deathmatch.per",
    "defensive.per", "difficulty-loads.per", "dip-boomer.per", "dip-bully.per",
    "dip-feeder.per", "diplomacy.per", "fishboat.per", "food.per",
    "fortress.per", "ground.per", "groups.per", "heavy-weapons.per",
    "homebase.per", "init-goals.per", "jedi-temple.per", "map-loads.per",
    "map-specs.per", "mech-factory.per", "military-population.per", "monument.per",
    "no-diplomacy.per", "nova.per", "ore.per", "population.per",
    "randomgame.per", "research-center.per", "research.per", "rush.per",
    "shipyard.per", "sn-gather.per", "sn-homebase.per", "sn-soldiers.per",
    "spaceport.per", "supplement.per", "tower.per", "troop-center.per",
    "war-center.per", "warboat-island.per", "warboat.per", "wonder-kill.per",
    "wonder-rush.per", "resign.per", "escrow.per", "military-population-hard.per",
};

AiProgram::BuiltinModuleSource &builtinModuleSource() {
    static AiProgram::BuiltinModuleSource source;
    return source;
}

std::string directoryOf(const std::string &path) {
    const size_t separator =
        path.find_last_of("/\\");
    return separator == std::string::npos
               ? std::string()
               : path.substr(0, separator);
}

std::string joinPath(
    const std::string &directory,
    const std::string &name) {
    if (directory.empty()) return name;
    const char last = directory.back();
    return directory +
           (last == '/' || last == '\\' ? "" : "/") +
           name;
}

bool readTextFile(
    const std::string &path,
    std::string &text,
    std::string *err) {
    FILE *file = fopen(path.c_str(), "rb");
    if (!file) {
        if (err)
            *err = "could not open AI script '" +
                   path + "'";
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        if (err)
            *err = "could not seek AI script '" +
                   path + "'";
        return false;
    }
    const long size = ftell(file);
    if (size < 0 ||
        size > 16 * 1024 * 1024) {
        fclose(file);
        if (err)
            *err = "invalid AI script size for '" +
                   path + "'";
        return false;
    }
    rewind(file);
    text.resize((size_t)size);
    if (size > 0 &&
        fread(text.data(), 1, (size_t)size, file) !=
            (size_t)size) {
        fclose(file);
        if (err)
            *err = "could not read AI script '" +
                   path + "'";
        return false;
    }
    fclose(file);
    return true;
}

bool parseInteger(
    const std::string &text,
    int &value) {
    if (text.empty()) return false;
    errno = 0;
    char *end = nullptr;
    const long parsed =
        std::strtol(text.c_str(), &end, 10);
    if (errno != 0 || !end || *end ||
        parsed < std::numeric_limits<int>::min() ||
        parsed > std::numeric_limits<int>::max())
        return false;
    value = (int)parsed;
    return true;
}

bool preprocess(
    const std::string &name,
    const std::string &source,
    const std::unordered_set<std::string> &defines,
    std::string &result,
    std::string *err) {
    struct Conditional {
        bool parent = true;
        bool condition = true;
        bool sawElse = false;
    };
    std::vector<Conditional> stack;
    bool active = true;
    std::istringstream stream(source);
    std::string line;
    int lineNumber = 0;
    while (std::getline(stream, line)) {
        lineNumber++;
        bool quoted = false;
        for (size_t index = 0;
             index < line.size(); ++index) {
            if (line[index] == '"' &&
                (index == 0 ||
                 line[index - 1] != '\\'))
                quoted = !quoted;
            if (!quoted && line[index] == ';') {
                line.resize(index);
                break;
            }
        }
        const std::string clean = trim(line);
        if (clean.rfind(
                "#load-if-defined", 0) == 0 ||
            clean.rfind(
                "#load-if-not-defined", 0) == 0) {
            const bool negate =
                clean.rfind(
                    "#load-if-not-defined", 0) == 0;
            const size_t prefix =
                negate ? 20 : 16;
            const std::string symbol =
                normalizeAiSymbol(
                    trim(clean.substr(prefix)));
            if (symbol.empty()) {
                if (err)
                    *err = name + ":" +
                           std::to_string(
                               lineNumber) +
                           ": missing conditional symbol";
                return false;
            }
            const bool defined =
                defines.count(symbol) != 0;
            stack.push_back(
                {active,
                 negate ? !defined : defined,
                 false});
            active =
                stack.back().parent &&
                stack.back().condition;
            result.push_back('\n');
            continue;
        }
        if (clean == "#else") {
            if (stack.empty() ||
                stack.back().sawElse) {
                if (err)
                    *err = name + ":" +
                           std::to_string(
                               lineNumber) +
                           ": unmatched #else";
                return false;
            }
            stack.back().sawElse = true;
            stack.back().condition =
                !stack.back().condition;
            active =
                stack.back().parent &&
                stack.back().condition;
            result.push_back('\n');
            continue;
        }
        if (clean == "#end-if") {
            if (stack.empty()) {
                if (err)
                    *err = name + ":" +
                           std::to_string(
                               lineNumber) +
                           ": unmatched #end-if";
                return false;
            }
            active = stack.back().parent;
            stack.pop_back();
            result.push_back('\n');
            continue;
        }
        if (!clean.empty() && clean[0] == '#') {
            if (err)
                *err = name + ":" +
                       std::to_string(lineNumber) +
                       ": unsupported preprocessor directive '" +
                       clean + "'";
            return false;
        }
        if (active) result += line;
        result.push_back('\n');
    }
    if (!stack.empty()) {
        if (err)
            *err = name +
                   ": unterminated AI conditional";
        return false;
    }
    return true;
}

bool tokenize(
    const std::string &name,
    const std::string &source,
    std::vector<Token> &tokens,
    std::string *err) {
    int line = 1;
    for (size_t index = 0;
         index < source.size();) {
        const char character = source[index];
        if (character == '\n') {
            line++;
            index++;
            continue;
        }
        if (std::isspace(
                (unsigned char)character)) {
            index++;
            continue;
        }
        if (character == '(' ||
            character == ')') {
            tokens.push_back(
                {std::string(1, character),
                 false, line});
            index++;
            continue;
        }
        if (character == '"') {
            const int startLine = line;
            std::string value;
            index++;
            bool closed = false;
            while (index < source.size()) {
                const char next = source[index++];
                if (next == '\n') line++;
                if (next == '"') {
                    closed = true;
                    break;
                }
                if (next == '\\' &&
                    index < source.size()) {
                    const char escaped =
                        source[index++];
                    value.push_back(
                        escaped == 'n'
                            ? '\n'
                            : escaped);
                    continue;
                }
                value.push_back(next);
            }
            if (!closed) {
                if (err)
                    *err = name + ":" +
                           std::to_string(
                               startLine) +
                           ": unterminated string";
                return false;
            }
            tokens.push_back(
                {std::move(value), true,
                 startLine});
            continue;
        }
        const size_t begin = index;
        while (index < source.size() &&
               !std::isspace(
                   (unsigned char)source[index]) &&
               source[index] != '(' &&
               source[index] != ')')
            index++;
        tokens.push_back(
            {source.substr(begin, index - begin),
             false, line});
    }
    return true;
}

bool parseNode(
    const std::string &name,
    const std::vector<Token> &tokens,
    size_t &index,
    AiNode &node,
    std::string *err) {
    if (index >= tokens.size()) {
        if (err)
            *err = name +
                   ": unexpected end of AI script";
        return false;
    }
    const Token &token = tokens[index++];
    node.line = token.line;
    if (token.text != "(") {
        if (token.text == ")") {
            if (err)
                *err = name + ":" +
                       std::to_string(token.line) +
                       ": unexpected ')'";
            return false;
        }
        node.value = token.text;
        node.quoted = token.quoted;
        return true;
    }
    while (index < tokens.size() &&
           tokens[index].text != ")") {
        AiNode child;
        if (!parseNode(
                name, tokens, index,
                child, err))
            return false;
        node.children.push_back(
            std::move(child));
    }
    if (index >= tokens.size()) {
        if (err)
            *err = name + ":" +
                   std::to_string(token.line) +
                   ": unterminated list";
        return false;
    }
    index++;
    if (node.children.empty()) {
        if (err)
            *err = name + ":" +
                   std::to_string(token.line) +
                   ": empty expression";
        return false;
    }
    return true;
}

} // namespace

std::string normalizeAiSymbol(
    const std::string &symbol) {
    // ASCII lower case (the symbols are ASCII; no locale lookup per char).
    std::string normalized = symbol;
    for (char &character : normalized)
        if (character >= 'A' && character <= 'Z') character = (char)(character - 'A' + 'a');
    return normalized;
}

void AiProgram::setBuiltinModuleSource(BuiltinModuleSource source) {
    builtinModuleSource() = std::move(source);
}

int AiProgram::builtinModuleResource(const std::string &fileName) {
    std::string normalized = normalizeAiSymbol(fileName);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    const size_t separator = normalized.find_last_of('/');
    if (separator != std::string::npos) normalized = normalized.substr(separator + 1);
    for (size_t index = 0; index < std::size(kBuiltinAiModules); ++index)
        if (normalized == kBuiltinAiModules[index]) return 60001 + (int)index;
    return -1;
}

bool AiProgram::load(
    const std::string &entryPath,
    const std::unordered_set<std::string> &defines,
    std::string *err) {
    rules.clear();
    constants.clear();
    files.clear();
    missingFiles.clear();
    loaded_.clear();
    virtualFiles_.clear();
    allowMissingIncludes_ = false;
    defines_.clear();
    for (const std::string &define : defines)
        defines_.insert(
            normalizeAiSymbol(define));
    constants = {
        {"no", 0},
        {"yes", 1},
        {"true", 1},
        {"false", 0},
        {"tech-level-1", 1},
        {"tech-level-2", 2},
        {"tech-level-3", 3},
        {"tech-level-4", 4},
        {"food", 0},
        {"carbon", 1},
        {"metal", 2},
        {"ore", 2},
        {"nova", 3},
        {"ally", 0},
        {"neutral", 1},
        {"enemy", 3},
        {"easiest", 0},
        {"easy", 1},
        {"moderate", 2},
        {"hard", 3},
        {"hardest", 4},
    };
    return loadFile(entryPath, err);
}

bool AiProgram::loadSource(
    const std::string &name,
    const std::string &source,
    const std::unordered_set<std::string> &defines,
    std::string *err,
    const std::unordered_map<
        std::string, std::string>
        *virtualFiles) {
    rules.clear();
    constants.clear();
    files.clear();
    missingFiles.clear();
    loaded_.clear();
    virtualFiles_.clear();
    if (virtualFiles)
        for (const auto &file :
             *virtualFiles)
            virtualFiles_[
                normalizeAiSymbol(
                    file.first)] =
                file.second;
    allowMissingIncludes_ = true;
    defines_.clear();
    for (const std::string &define : defines)
        defines_.insert(
            normalizeAiSymbol(define));
    constants = {
        {"no", 0},
        {"yes", 1},
        {"true", 1},
        {"false", 0},
        {"tech-level-1", 1},
        {"tech-level-2", 2},
        {"tech-level-3", 3},
        {"tech-level-4", 4},
        {"food", 0},
        {"carbon", 1},
        {"metal", 2},
        {"ore", 2},
        {"nova", 3},
        {"ally", 0},
        {"neutral", 1},
        {"enemy", 3},
        {"easiest", 0},
        {"easy", 1},
        {"moderate", 2},
        {"hard", 3},
        {"hardest", 4},
    };
    loaded_.insert(name);
    files.push_back(name);
    return processSource(
        name, directoryOf(name),
        source, err);
}

bool AiProgram::loadFile(
    const std::string &path,
    std::string *err) {
    std::string key =
        normalizeAiSymbol(path);
    std::replace(
        key.begin(), key.end(), '\\', '/');
    if (!loaded_.insert(key).second)
        return true;
    std::string source;
    std::string normalizedPath = key;
    const size_t separator =
        normalizedPath.find_last_of('/');
    const std::string baseName =
        separator == std::string::npos
            ? normalizedPath
            : normalizedPath.substr(
                  separator + 1);
    auto embedded =
        virtualFiles_.find(normalizedPath);
    if (embedded == virtualFiles_.end())
        embedded =
            virtualFiles_.find(baseName);
    if (embedded != virtualFiles_.end()) {
        files.push_back(path);
        return processSource(
            path, directoryOf(path),
            embedded->second, err);
    }
    if (!readTextFile(path, source, err)) {
        if (!allowMissingIncludes_)
            return false;
        missingFiles.push_back(path);
        if (err) err->clear();
        return true;
    }
    files.push_back(path);
    return processSource(
        path, directoryOf(path),
        source, err);
}

bool AiProgram::processSource(
    const std::string &name,
    const std::string &directory,
    const std::string &source,
    std::string *err) {
    std::string processed;
    if (!preprocess(
            name, source, defines_,
            processed, err))
        return false;
    std::vector<Token> tokens;
    if (!tokenize(
            name, processed, tokens, err))
        return false;
    size_t index = 0;
    while (index < tokens.size()) {
        AiNode form;
        if (!parseNode(
                name, tokens, index,
                form, err))
            return false;
        if (!form.isList()) {
            if (err)
                *err = name + ":" +
                       std::to_string(form.line) +
                       ": expected top-level list";
            return false;
        }
        const std::string command =
            normalizeAiSymbol(form.name());
        if (command == "load") {
            if (form.children.size() != 2 ||
                form.children[1].value.empty()) {
                if (err)
                    *err = name + ":" +
                           std::to_string(
                               form.line) +
                           ": malformed load";
                return false;
            }
            std::string fileName =
                form.children[1].value;
            const std::string normalized =
                normalizeAiSymbol(fileName);
            if (normalized.size() < 4 ||
                normalized.substr(
                    normalized.size() - 4) !=
                    ".per")
                fileName += ".per";
            if (const int resource = builtinModuleResource(fileName); resource >= 0) {
                const std::string key = "data/load/" + normalizeAiSymbol(fileName);
                std::string text;
                if (!builtinModuleSource() || !loaded_.insert(key).second ||
                    !builtinModuleSource()(resource, text))
                    continue;
                files.push_back(key);
                if (!processSource(key, directory, text, err))
                    return false;
                continue;
            }
            if (!loadFile(
                    joinPath(
                        directory, fileName),
                    err))
                return false;
            continue;
        }
        if (command == "defconst") {
            if (form.children.size() != 3) {
                if (err)
                    *err = name + ":" +
                           std::to_string(
                               form.line) +
                           ": malformed defconst";
                return false;
            }
            int value = 0;
            if (!parseInteger(
                    form.children[2].value,
                    value)) {
                const std::string reference =
                    normalizeAiSymbol(
                        form.children[2].value);
                const auto found =
                    constants.find(reference);
                if (found == constants.end()) {
                    if (err)
                        *err = name + ":" +
                               std::to_string(
                                   form.line) +
                               ": unresolved defconst value '" +
                               form.children[2].value +
                               "'";
                    return false;
                }
                value = found->second;
            }
            constants[
                normalizeAiSymbol(
                    form.children[1].value)] =
                value;
            continue;
        }
        if (command != "defrule")
            continue;
        size_t arrow = 0;
        for (size_t child = 1;
             child < form.children.size();
             ++child)
            if (!form.children[child].isList() &&
                form.children[child].value == "=>") {
                arrow = child;
                break;
            }
        if (arrow <= 1 ||
            arrow + 1 >=
                form.children.size()) {
            if (err)
                *err = name + ":" +
                       std::to_string(form.line) +
                       ": malformed defrule";
            return false;
        }
        AiRule rule;
        rule.source = name;
        rule.line = form.line;
        rule.conditions.assign(
            form.children.begin() + 1,
            form.children.begin() +
                (std::ptrdiff_t)arrow);
        rule.actions.assign(
            form.children.begin() +
                (std::ptrdiff_t)arrow + 1,
            form.children.end());
        rules.push_back(std::move(rule));
    }
    return true;
}

int AiProgram::constant(
    const std::string &name,
    int fallback) const {
    int number = 0;
    if (parseInteger(name, number))
        return number;
    const auto found =
        constants.find(
            normalizeAiSymbol(name));
    return found == constants.end()
               ? fallback
               : found->second;
}

bool AiProgram::hasConstant(
    const std::string &name) const {
    int number = 0;
    return parseInteger(name, number) ||
           constants.count(
               normalizeAiSymbol(name)) != 0;
}

} // namespace swgb
