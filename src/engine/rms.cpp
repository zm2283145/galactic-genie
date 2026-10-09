// SPDX-License-Identifier: GPL-3.0-or-later
#include "rms.h"

#include "../core/genie_dat.h"
#include "rms_civmap.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

namespace swgb {
namespace {

// MSVC rand (0x632bdd); the generator reseeds it with the game seed.
struct MsvcRand {
    uint32_t state = 1;
    int next() {
        state = state * 214013u + 2531011u;
        return (int)((state >> 16) & 0x7fff);
    }
    // R(n) = rand()*n/32767 (C int math).
    int range(int n) { return (int)((int64_t)next() * n / 32767); }
};

// One list node per map tile shared by every list (RGE_RMM tile nodes):
// inserting a node into a list first unlinks it from the list it is in.
class TileLists {
public:
    void reset(int w, int h) {
        w_ = w;
        const size_t n = (size_t)w * h;
        prev_.assign(n, -1);
        next_.assign(n, -1);
        owner_.assign(n, -1);
        prio_.assign(n, 0.0f);
        head_.clear();
        tail_.clear();
    }
    int newList() {
        head_.push_back(-1);
        tail_.push_back(-1);
        return (int)head_.size() - 1;
    }
    int node(int x, int y) const { return y * w_ + x; }
    int x(int node) const { return node % w_; }
    int y(int node) const { return node / w_; }
    int owner(int node) const { return owner_[(size_t)node]; }
    bool empty(int list) const { return head_[(size_t)list] < 0; }
    void unlink(int n) {
        const int list = owner_[(size_t)n];
        if (list < 0) return;
        const int p = prev_[(size_t)n], q = next_[(size_t)n];
        if (p >= 0) next_[(size_t)p] = q; else head_[(size_t)list] = q;
        if (q >= 0) prev_[(size_t)q] = p; else tail_[(size_t)list] = p;
        prev_[(size_t)n] = next_[(size_t)n] = -1;
        owner_[(size_t)n] = -1;
    }
    void pushFront(int list, int n) {
        unlink(n);
        const int h = head_[(size_t)list];
        next_[(size_t)n] = h;
        prev_[(size_t)n] = -1;
        if (h >= 0) prev_[(size_t)h] = n; else tail_[(size_t)list] = n;
        head_[(size_t)list] = n;
        owner_[(size_t)n] = list;
    }
    void append(int list, int n) {
        unlink(n);
        const int t = tail_[(size_t)list];
        prev_[(size_t)n] = t;
        next_[(size_t)n] = -1;
        if (t >= 0) next_[(size_t)t] = n; else head_[(size_t)list] = n;
        tail_[(size_t)list] = n;
        owner_[(size_t)n] = list;
    }
    // Ascending by priority; before the first node with prio >= p (LIFO
    // among equals).
    void insert(int list, int n, float p) {
        unlink(n);
        prio_[(size_t)n] = p;
        int at = head_[(size_t)list];
        while (at >= 0 && prio_[(size_t)at] < p) at = next_[(size_t)at];
        if (at < 0) {
            append(list, n);
            prio_[(size_t)n] = p;
            return;
        }
        const int before = prev_[(size_t)at];
        prev_[(size_t)n] = before;
        next_[(size_t)n] = at;
        prev_[(size_t)at] = n;
        if (before >= 0) next_[(size_t)before] = n; else head_[(size_t)list] = n;
        owner_[(size_t)n] = list;
    }
    int pop(int list) {
        const int n = head_[(size_t)list];
        if (n >= 0) unlink(n);
        return n;
    }
    // shuffle100 (0x4d6b90): two passes of 100 buckets.
    void shuffle100(int list, MsvcRand &rng) {
        for (int pass = 0; pass < 2; ++pass) {
            std::vector<int> buckets(100);
            for (int &b : buckets) b = newList();
            while (!empty(list)) {
                const int n = pop(list);
                pushFront(buckets[(size_t)std::min(rng.range(100), 99)], n);
            }
            for (int j = 0; j < 50; ++j) {
                const int a = buckets[(size_t)(2 * j)], b = buckets[(size_t)(2 * j + 1)];
                while (!empty(a) || !empty(b)) {
                    if (!empty(a)) pushFront(list, pop(a));
                    if (!empty(b)) pushFront(list, pop(b));
                }
            }
            head_.resize(head_.size() - 100);
            tail_.resize(tail_.size() - 100);
        }
    }

private:
    int w_ = 0;
    std::vector<int> prev_, next_, owner_;
    std::vector<float> prio_;
    std::vector<int> head_, tail_;
};

// --- Script records ---------------------------------------------------------

struct LandRec {
    int tiles = 0;
    int baseSize = 3, clumping = 8, fuzziness = 20;
    int left = 0, top = 0, right = 0, bottom = 0;
    int zone = 0, avoid = 0;
    int x = -1, y = -1;
    int minPlacement = -1;
    int terrain = 0;
    int landId = 0;
    int player = 0;
    bool deleted = false;
};

struct TerrainRec {
    int terrain = 0, base = 0;
    int tiles = 0, clumps = 1, spacing = 0, clumping = 20;
    bool avoid = false, flat = false;
    int minHeight = 0, maxHeight = 255;
    int scale = 0; // 1 by size, 2 by groups
};

struct ElevationRec {
    int height = 1, level = 0, tiles = 0, clumps = 1, spacing = 1, base = 0;
    int scale = 0; // 1 set_scale_by_groups (tiles), 2 set_scale_by_size (clumps)
};

struct ConnectionRec {
    int kind = 0; // 0 all players, 1 teams, 2 same land zones, 3 all lands
    std::array<float, 99> cost{};
    std::array<int, 99> size{}, variance{}, replace{};
    ConnectionRec() {
        cost.fill(1.0f);
        size.fill(1);
        variance.fill(0);
        replace.fill(-1);
    }
};

struct ObjectRec {
    int type = -1;
    int perGroup = 1;     // A: number_of_objects
    int groups = 1;       // B: number_of_groups
    bool groupsSet = false;
    int variance = 0;
    int grouping = 0;     // 0 none, 1 loose, 2 tight
    int radius = 3;
    int terrainOn = -1;
    bool gaiaOnly = false;
    int place = -1;       // -1 global, -2 global+min distance, 1 every player, N+10 land id
    int minDistance = -1, maxDistance = -1;
    int minGroupDistance = 0, tempMinGroupDistance = 0;
    int maxZoneDistance = 0;
    bool matchCiv = false, scaleMap = false, scalePlayers = false;
};

struct Symbol {
    int type = 1; // 1 #define, 2 #const
    int value = 0;
};

enum Section { kNone, kPlayer, kLand, kTerrain, kObjects, kConnection, kElevation, kCliff };

struct Keyword {
    const char *name;
    std::vector<int> args;
};

// The keyword table (language strings 8500-8594; 8590 not registered) with
// argument types: 1 symbol name, 2 integer, 3 existing symbol (value),
// 4 condition symbol, 5 file name.
const std::vector<Keyword> &keywords() {
    static const std::vector<Keyword> table = {
        {"#define", {1}}, {"#undefine", {1}}, {"#const", {1, 2}}, {"if", {4}},
        {"elseif", {4}}, {"else", {}}, {"endif", {}}, {"start_random", {}},
        {"percent_chance", {2}}, {"end_random", {}}, {"#include", {5}},
        {"<PLAYER_SETUP>", {}}, {"random_placement", {}}, {"grouped_by_team", {}},
        {"min_distance", {}}, {"max_distance", {}}, {"set_position", {}},
        {"<LAND_GENERATION>", {}}, {"land_percent", {2}}, {"base_terrain", {3}},
        {"create_player_lands", {}}, {"terrain_type", {3}}, {"base_size", {2}},
        {"left_border", {2}}, {"right_border", {2}}, {"top_border", {2}},
        {"bottom_border", {2}}, {"border_fuzziness", {2}}, {"zone", {2}},
        {"set_zone_by_team", {}}, {"set_zone_randomly", {}},
        {"other_zone_avoidance_distance", {2}}, {"create_land", {}},
        {"assign_to_player", {2}}, {"<CLIFF_GENERATION>", {}},
        {"min_number_of_cliffs", {2}}, {"max_number_of_cliffs", {2}},
        {"min_length_of_cliff", {2}}, {"max_length_of_cliff", {2}},
        {"cliff_curliness", {2}}, {"min_distance_cliffs", {2}},
        {"min_terrain_distance", {2}}, {"<TERRAIN_GENERATION>", {}},
        {"create_terrain", {3}}, {"percent_of_land", {2}}, {"number_of_clumps", {2}},
        {"spacing_to_other_terrain_types", {2}}, {"<OBJECTS_GENERATION>", {}},
        {"create_object", {3}}, {"set_scaling_to_map_size", {}},
        {"number_of_groups", {2}}, {"number_of_objects", {2}}, {"group_variance", {2}},
        {"group_placement_radius", {2}}, {"set_loose_grouping", {}},
        {"set_tight_grouping", {}}, {"terrain_to_place_on", {3}},
        {"set_gaia_object_only", {}}, {"set_place_for_every_player", {}},
        {"place_on_specific_land_id", {2}}, {"min_distance_to_players", {2}},
        {"max_distance_to_players", {2}}, {"<CONNECTION_GENERATION>", {}},
        {"create_connect_all_players_land", {}}, {"create_connect_teams_lands", {}},
        {"create_connect_same_land_zones", {}}, {"create_connect_all_lands", {}},
        {"{", {}}, {"}", {}}, {"/*", {}}, {"*/", {}}, {"land_position", {2, 2}},
        {"land_id", {2}}, {"clumping_factor", {2}}, {"number_of_tiles", {2}},
        {"set_scale_by_groups", {}}, {"set_scale_by_size", {}},
        {"set_avoid_player_start_areas", {}}, {"min_distance_group_placement", {2}},
        {"<ELEVATION_GENERATION>", {}}, {"create_elevation", {2}}, {"spacing", {2}},
        {"default_terrain_replacement", {3}}, {"replace_terrain", {3, 3}},
        {"terrain_cost", {3, 2}}, {"terrain_size", {3, 2, 2}},
        {"min_placement_distance", {2}}, {"set_scaling_to_player_number", {}},
        {"height_limits", {2, 2}}, {"set_flat_terrain_only", {}},
        {"max_distance_to_other_zones", {2}}, {"#include_drs", {5, 2}},
        {"temp_min_distance_group_placement", {2}}, {"match_player_civ", {}},
    };
    return table;
}

const Keyword *findKeyword(const std::string &word) {
    static std::map<std::string, const Keyword *> index;
    if (index.empty())
        for (const Keyword &k : keywords()) index[k.name] = &k;
    const auto found = index.find(word);
    return found == index.end() ? nullptr : found->second;
}

constexpr uint8_t kUnclaimed = 0xFE;
constexpr uint8_t kRejected = 0xFF;

class Generator {
public:
    Generator(const dat::DatFile &dat, const RmsSettings &settings, const RmsLoader &loader)
        : dat_(dat), s_(settings), loader_(loader), w_(settings.width), h_(settings.height),
          players_((int)settings.players.size()) {}

    bool run(const std::string &script, RmsResult &result);

private:
    const dat::DatFile &dat_;
    const RmsSettings &s_;
    const RmsLoader &loader_;
    int w_, h_, players_;
    MsvcRand rng_;
    std::string error_;

    // Map.
    std::vector<uint8_t> terrain_, elevation_, slope_;
    // Objects (with a per-tile index for overlap tests).
    std::vector<RmsObject> objects_;
    std::vector<std::vector<int>> tileObjects_;

    // Parsed script.
    std::map<std::string, Symbol> symbols_;
    std::vector<std::string> includeStack_;
    std::vector<int> ifStack_;
    std::vector<std::pair<int, int>> randomStack_; // (state, value)
    int commentDepth_ = 0;
    Section section_ = kNone;
    bool inBlock_ = false;
    bool seenLand_ = false, seenTerrain_ = false, seenObjects_ = false;
    bool seenConnection_ = false, seenElevation_ = false, seenCliff_ = false;
    int baseTerrain_ = 0;
    std::vector<LandRec> lands_;
    size_t landRangeBegin_ = 0, landRangeEnd_ = 0;
    bool landRangePlayers_ = false;
    std::vector<TerrainRec> terrains_;
    std::vector<ElevationRec> elevations_;
    size_t elevationRangeBegin_ = 0;
    std::vector<ConnectionRec> connections_;
    std::vector<ObjectRec> objectRecs_;
    int cliffMin_ = 3, cliffMax_ = 9, cliffMinLength_ = 5, cliffMaxLength_ = 9;
    int cliffCurliness_ = 36, cliffTerrainDistance_ = 2, cliffDistance_ = 2;

    // Post-parse.
    std::vector<int> team_; // by player
    struct ExtLand {
        int x, y, landId, player;
    };
    std::vector<ExtLand> extLands_;
    std::vector<std::array<int, 3>> cliffAvoid_;
    std::vector<std::array<int, 4>> playerAvoid_;
    std::vector<uint8_t> landZones_;

    TileLists lists_;

    // Helpers.
    bool inMap(int x, int y) const { return x >= 0 && y >= 0 && x < w_ && y < h_; }
    size_t at(int x, int y) const { return (size_t)y * (size_t)w_ + (size_t)x; }
    bool isAlly(int p, int q) const {
        if (p == q) return true;
        if (p < 1 || q < 1 || p > players_ || q > players_) return false;
        const int team = s_.players[(size_t)p - 1].team;
        return team > 0 && team == s_.players[(size_t)q - 1].team;
    }
    int civOf(int player) const {
        return player <= 0 || player > players_ ? 0 : s_.players[(size_t)player - 1].civilization;
    }
    const dat::Unit *unitFor(int player, int id) const;

    // Parser.
    bool parse(const std::string &text, const std::string &name);
    bool includeDrs(const std::string &name, int id);
    void execute(const std::string &word, const std::vector<std::string> &args,
                 const std::vector<int> &values);
    bool executing() const {
        const bool ifOk = ifStack_.empty() || ifStack_.back() == 2;
        const bool randomOk = randomStack_.empty() || randomStack_.back().first == 2;
        return ifOk && randomOk;
    }
    void predefine();
    void postParse();

    // Engine helpers.
    void setTerrainSingle(int x, int y, int t);
    void cleanup(int t);
    void elevationFixup();
    void recomputeSlopes();
    int slopeAt(int x, int y) const;
    void removeTerrainObjects(int x, int y, int oldTerrain);
    void addTerrainObjects(int x, int y, bool fullRecreate);
    int canPlace(const dat::Unit &unit, int player, float x, float y, bool hill,
                 bool clearance, bool forceObstruction, bool obstruction, int mode) const;
    int createObject(int unitId, int player, float x, float y, int facet = -1);
    void deleteObject(int index);
    std::vector<uint8_t> zoneMap(const dat::Unit &unit, int forcedTerrain) const;

    // Modules.
    void landModule();
    void elevationModule();
    void cliffModule();
    void terrainModule();
    void connectionModule();
    void objectModule();
    void shorePass();
    // Cliff drawing (TRIBE_Map 0x5cbbc0 / 0x5cba10).
    int cliffLastBX_ = -1, cliffLastBY_ = -1, cliffCurBX_ = -1, cliffCurBY_ = -1, cliffLastDir_ = -1;
    void cliffLine(int x0, int y0, int x1, int y1);
    void cliffPoint(int x, int y);
    int cliffEdge(int bx, int by, int d, int val, int keep);
    void cliffFixNeighbour(int bx, int by, int j);
    void placeCliff(int bx, int by, std::array<int, 4> e);
    int findCliff(int bx, int by) const;
    std::array<int, 4> cliffEdgesOf(int object) const;
    void removeOverlapping(int unitId, float x, float y);
    // Objects.
    int buildCandidates(int list, int x, int y, int r, const std::vector<uint8_t> &g);
    void placeGroup(const ObjectRec &rec, int type, int player, int gx, int gy,
                    std::vector<uint8_t> &g);
    int resolveCiv(int type, int player) const;
};

const dat::Unit *Generator::unitFor(int player, int id) const {
    const int civ = civOf(player);
    if (civ < 0 || (size_t)civ >= dat_.civs.size()) return nullptr;
    const auto &units = dat_.civs[(size_t)civ].units;
    if (id < 0 || (size_t)id >= units.size() || !units[(size_t)id].exists) {
        // Fall back to gaia's copy.
        const auto &gaia = dat_.civs[0].units;
        if (id < 0 || (size_t)id >= gaia.size() || !gaia[(size_t)id].exists) return nullptr;
        return &gaia[(size_t)id];
    }
    return &units[(size_t)id];
}

int Generator::resolveCiv(int type, int player) const {
    const int civ = civOf(player);
    if (civ < 1 || civ > 8) return type;
    for (const auto &row : rms_tables::kCivMap)
        if (row.generic == type) return row.perCiv[civ - 1];
    return type;
}


// --- Parser (0x4e16f0 / 0x4e1fa0) -------------------------------------------

namespace {
std::vector<std::string> tokenize(const std::string &text) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace((unsigned char)text[i])) ++i;
        const size_t start = i;
        while (i < text.size() && !std::isspace((unsigned char)text[i])) ++i;
        if (i > start) out.push_back(text.substr(start, i - start));
    }
    return out;
}
} // namespace

bool Generator::includeDrs(const std::string &name, int id) {
    if (s_.customScript) return true;
    for (const std::string &open : includeStack_)
        if (open == name) return true; // recursion refused
    if (id == -1) {
        std::string def;
        if (loader_ && loader_(54000, "random_map.def", def)) parse(def, "random_map.def");
    }
    std::string text;
    if (!loader_ || !loader_(id, name, text)) return true; // missing: ignored
    return parse(text, name);
}

bool Generator::parse(const std::string &text, const std::string &name) {
    includeStack_.push_back(name);
    const std::vector<std::string> tokens = tokenize(text);
    size_t i = 0;
    while (i < tokens.size()) {
        const std::string word = tokens[i++];
        const Keyword *keyword = findKeyword(word);
        if (!keyword) continue;
        std::vector<std::string> args;
        std::vector<int> values;
        bool drop = false;
        for (int type : keyword->args) {
            if (i >= tokens.size() || findKeyword(tokens[i])) {
                drop = true; // the next word is processed as a token
                break;
            }
            std::string arg = tokens[i++];
            if (type == 5 && !arg.empty() && arg[0] == '"') {
                while (arg.size() < 2 || arg.back() != '"') {
                    if (i >= tokens.size()) break;
                    arg += " " + tokens[i++];
                }
                arg = arg.substr(1, arg.size() >= 2 ? arg.size() - 2 : 0);
            }
            if (type == 2) {
                values.push_back(std::atoi(arg.c_str()));
            } else if (type == 3) {
                const auto found = symbols_.find(arg);
                if (found == symbols_.end()) drop = true;
                else values.push_back(found->second.value);
            }
            args.push_back(arg);
        }
        if (word == "/*") {
            commentDepth_++;
            continue;
        }
        if (word == "*/") {
            if (commentDepth_ > 0) commentDepth_--;
            continue;
        }
        if (commentDepth_ > 0) continue;
        const bool randomTopActive = randomStack_.empty() || randomStack_.back().first == 2;
        const bool ifActive = ifStack_.empty() || ifStack_.back() == 2;
        if (word == "if" || word == "elseif" || word == "else" || word == "endif") {
            if (!randomTopActive) continue;
            const bool defined = !args.empty() && symbols_.count(args[0]) != 0;
            if (word == "if") {
                if (args.empty()) continue;
                ifStack_.push_back(!ifActive ? 3 : (defined ? 2 : 1));
            } else if (word == "elseif") {
                if (ifStack_.empty() || args.empty()) continue;
                int &state = ifStack_.back();
                if (state == 1 && defined) state = 2;
                else if (state != 1) state = 3;
            } else if (word == "else") {
                if (ifStack_.empty()) continue;
                int &state = ifStack_.back();
                state = state == 1 ? 2 : 3;
            } else if (!ifStack_.empty()) {
                ifStack_.pop_back();
            }
            continue;
        }
        if (word == "start_random" || word == "percent_chance" || word == "end_random") {
            if (!ifActive) continue;
            if (word == "start_random") {
                randomStack_.push_back({1, rng_.range(100)});
            } else if (word == "percent_chance") {
                if (randomStack_.empty() || values.empty()) continue;
                auto &top = randomStack_.back();
                if (top.first == 1) {
                    if (top.second <= values[0]) top.first = 2;
                    else top.second -= values[0];
                } else {
                    top.first = 3;
                }
            } else if (!randomStack_.empty()) {
                randomStack_.pop_back();
            }
            continue;
        }
        if (drop || !executing()) continue;
        if (word == "#include_drs") {
            if (args.size() >= 2) includeDrs(args[0], values.empty() ? -1 : values[0]);
            continue;
        }
        if (word == "#include") {
            if (!s_.customScript && !args.empty()) {
                bool open = false;
                for (const std::string &n : includeStack_) open = open || n == args[0];
                std::string included;
                if (!open && loader_ && loader_(-1, args[0], included)) parse(included, args[0]);
            }
            continue;
        }
        execute(word, args, values);
    }
    includeStack_.pop_back();
    return true;
}

void Generator::predefine() {
    static const char *sizes[] = {"TINY_MAP", "SMALL_MAP", "MEDIUM_MAP",
                                  "LARGE_MAP", "HUGE_MAP", "GIGANTIC_MAP"};
    if (s_.mapSizeIndex >= 0 && s_.mapSizeIndex < 6) symbols_[sizes[s_.mapSizeIndex]] = {1, 0};
    if (s_.fixedPositions) symbols_["FIXED_POSITIONS"] = {1, 0};
    if (s_.gameType == 1) symbols_["TERMINATE"] = {1, 0};
    else if (s_.gameType == 2) symbols_["DEATH_MATCH"] = {1, 0};
    else if (s_.resourceLevel == 1) symbols_["LOW_RESOURCES"] = {1, 0};
    else if (s_.resourceLevel == 2) symbols_["MEDIUM_RESOURCES"] = {1, 0};
    else if (s_.resourceLevel == 3) symbols_["HIGH_RESOURCES"] = {1, 0};
    else symbols_["DEFAULT_RESOURCES"] = {1, 0};
}

void Generator::execute(const std::string &word, const std::vector<std::string> &args,
                        const std::vector<int> &values) {
    const int v0 = values.empty() ? 0 : values[0];
    const int v1 = values.size() > 1 ? values[1] : 0;
    const int v2 = values.size() > 2 ? values[2] : 0;
    if (word == "#define") {
        if (!args.empty() && !symbols_.count(args[0])) symbols_[args[0]] = {1, 0};
        return;
    }
    if (word == "#const") {
        if (!args.empty() && !symbols_.count(args[0])) symbols_[args[0]] = {2, v0};
        return;
    }
    if (word == "#undefine") return;
    // Section headers.
    static const std::map<std::string, Section> headers = {
        {"<PLAYER_SETUP>", kPlayer}, {"<LAND_GENERATION>", kLand},
        {"<TERRAIN_GENERATION>", kTerrain}, {"<OBJECTS_GENERATION>", kObjects},
        {"<CONNECTION_GENERATION>", kConnection}, {"<ELEVATION_GENERATION>", kElevation},
        {"<CLIFF_GENERATION>", kCliff}};
    const auto header = headers.find(word);
    if (header != headers.end()) {
        section_ = header->second;
        inBlock_ = false;
        switch (section_) {
        case kLand: seenLand_ = true; break;
        case kTerrain: seenTerrain_ = true; break;
        case kObjects: seenObjects_ = true; break;
        case kConnection: seenConnection_ = true; break;
        case kElevation: seenElevation_ = true; break;
        case kCliff: seenCliff_ = true; break;
        default: break;
        }
        return;
    }
    if (word == "{") {
        inBlock_ = true;
        return;
    }
    if (word == "}") {
        inBlock_ = false;
        return;
    }
    const int area = w_ * h_;
    switch (section_) {
    case kLand: {
        if (word == "base_terrain" && !inBlock_) {
            baseTerrain_ = v0;
            return;
        }
        if (word == "create_player_lands") {
            landRangeBegin_ = lands_.size();
            for (int p = 1; p <= players_; ++p) {
                LandRec land;
                land.tiles = area;
                land.right = w_;
                land.bottom = h_;
                land.zone = p;
                land.player = p;
                land.landId = 1;
                lands_.push_back(land);
            }
            landRangeEnd_ = lands_.size();
            landRangePlayers_ = true;
            return;
        }
        if (word == "create_land") {
            landRangePlayers_ = false;
            landRangeBegin_ = lands_.size();
            LandRec land;
            land.tiles = area;
            land.right = w_;
            land.bottom = h_;
            lands_.push_back(land);
            landRangeEnd_ = lands_.size();
            return;
        }
        if (!inBlock_) return;
        const bool playerLands = landRangePlayers_;
        for (size_t i = landRangeBegin_; i < landRangeEnd_ && i < lands_.size(); ++i) {
            LandRec &land = lands_[i];
            const int index = (int)(i - landRangeBegin_) + 1;
            if (word == "land_percent")
                land.tiles = playerLands ? area * v0 / (100 * std::max(1, players_))
                                         : area * v0 / 100;
            else if (word == "number_of_tiles") land.tiles = v0;
            else if (word == "terrain_type") land.terrain = v0;
            else if (word == "base_size") land.baseSize = v0;
            else if (word == "left_border") land.left = w_ * v0 / 100;
            else if (word == "right_border") land.right = w_ - w_ * v0 / 100;
            else if (word == "top_border") land.top = h_ * v0 / 100;
            else if (word == "bottom_border") land.bottom = h_ - h_ * v0 / 100;
            else if (word == "border_fuzziness") land.fuzziness = v0;
            else if (word == "zone") land.zone = v0 + 10;
            else if (word == "set_zone_by_team") {
                int q = 1;
                while (q < index && !isAlly(q, index)) ++q;
                land.zone = q + 1;
            } else if (word == "set_zone_randomly") {
                land.zone = rng_.range(players_) + 2;
            } else if (word == "other_zone_avoidance_distance") land.avoid = v0;
            else if (word == "land_id") land.landId = v0 + 10;
            else if (word == "clumping_factor") land.clumping = v0;
            else if (word == "min_placement_distance") land.minPlacement = v0;
            else if (word == "land_position" && land.player == 0) {
                land.x = v0 * w_ / 100;
                land.y = v1 * h_ / 100;
            } else if (word == "assign_to_player" && !playerLands) {
                land.player = v0;
                if (v0 >= players_ + 1) land.deleted = true;
            }
        }
        return;
    }
    case kElevation: {
        if (word == "create_elevation") {
            elevationRangeBegin_ = elevations_.size();
            for (int k = 0; k < v0; ++k) {
                ElevationRec rec;
                rec.height = std::min(k + 1, 7);
                rec.level = k;
                rec.tiles = w_;
                rec.clumps = 1;
                rec.spacing = k == 0 ? 2 : 1;
                elevations_.push_back(rec);
            }
            return;
        }
        if (!inBlock_) return;
        for (size_t i = elevationRangeBegin_; i < elevations_.size(); ++i) {
            ElevationRec &rec = elevations_[i];
            if (word == "number_of_tiles") rec.tiles = v0;
            else if (word == "number_of_clumps") rec.clumps = v0;
            else if (word == "base_terrain") rec.base = v0;
            else if (word == "spacing" && v0 > 0 && rec.level >= 1) rec.spacing = v0;
            else if (word == "set_scale_by_groups") rec.scale |= 1;
            else if (word == "set_scale_by_size") rec.scale |= 2;
        }
        return;
    }
    case kCliff:
        if (word == "min_number_of_cliffs") cliffMin_ = v0;
        else if (word == "max_number_of_cliffs") cliffMax_ = v0;
        else if (word == "min_length_of_cliff") cliffMinLength_ = v0;
        else if (word == "max_length_of_cliff") cliffMaxLength_ = v0;
        else if (word == "cliff_curliness") cliffCurliness_ = v0;
        else if (word == "min_distance_cliffs") cliffDistance_ = v0;
        else if (word == "min_terrain_distance") cliffTerrainDistance_ = v0;
        return;
    case kTerrain: {
        if (word == "create_terrain") {
            TerrainRec rec;
            rec.terrain = v0;
            rec.tiles = w_;
            // SWGB: shallow water is always placed on flat land.
            if (v0 == 1) rec.flat = true;
            terrains_.push_back(rec);
            return;
        }
        if (!inBlock_ || terrains_.empty()) return;
        TerrainRec &rec = terrains_.back();
        if (word == "base_terrain") rec.base = v0;
        else if (word == "land_percent") rec.tiles = -v0;
        else if (word == "number_of_tiles") rec.tiles = v0;
        else if (word == "number_of_clumps") rec.clumps = v0;
        else if (word == "spacing_to_other_terrain_types") rec.spacing = v0;
        else if (word == "clumping_factor") rec.clumping = v0;
        else if (word == "set_avoid_player_start_areas") rec.avoid = true;
        else if (word == "set_scale_by_size") rec.scale |= 1;
        else if (word == "set_scale_by_groups") rec.scale |= 2;
        else if (word == "height_limits") {
            rec.minHeight = v0;
            rec.maxHeight = v1;
        } else if (word == "set_flat_terrain_only") rec.flat = true;
        return;
    }
    case kConnection: {
        static const std::map<std::string, int> kinds = {
            {"create_connect_all_players_land", 0}, {"create_connect_teams_lands", 1},
            {"create_connect_same_land_zones", 2}, {"create_connect_all_lands", 3}};
        const auto kind = kinds.find(word);
        if (kind != kinds.end()) {
            ConnectionRec rec;
            rec.kind = kind->second;
            connections_.push_back(rec);
            return;
        }
        if (!inBlock_ || connections_.empty()) return;
        ConnectionRec &rec = connections_.back();
        const bool valid = v0 >= 0 && v0 < 99;
        if (word == "default_terrain_replacement") rec.replace.fill(v0);
        else if (word == "replace_terrain" && valid) rec.replace[(size_t)v0] = v1;
        else if (word == "terrain_cost" && valid)
            rec.cost[(size_t)v0] = args.size() > 1 ? (float)std::atof(args[1].c_str()) : 0.0f;
        else if (word == "terrain_size" && valid) {
            rec.size[(size_t)v0] = v1;
            rec.variance[(size_t)v0] = v2;
        }
        return;
    }
    case kObjects: {
        if (word == "create_object") {
            ObjectRec rec;
            rec.type = v0;
            objectRecs_.push_back(rec);
            return;
        }
        if (!inBlock_ || objectRecs_.empty()) return;
        ObjectRec &rec = objectRecs_.back();
        if (word == "number_of_objects") rec.perGroup = v0;
        else if (word == "number_of_groups") {
            rec.groups = v0;
            rec.groupsSet = true;
        } else if (word == "group_variance") rec.variance = v0;
        else if (word == "group_placement_radius") rec.radius = v0;
        else if (word == "set_loose_grouping") rec.grouping = 1;
        else if (word == "set_tight_grouping") rec.grouping = 2;
        else if (word == "terrain_to_place_on") rec.terrainOn = v0;
        else if (word == "set_gaia_object_only") rec.gaiaOnly = true;
        else if (word == "set_place_for_every_player") rec.place = 1;
        else if (word == "place_on_specific_land_id") rec.place = v0 + 10;
        else if (word == "min_distance_to_players") {
            rec.minDistance = v0;
            if (rec.place == -1) rec.place = -2;
        } else if (word == "max_distance_to_players") {
            rec.maxDistance = v0;
            if (rec.place == -1) rec.place = -2;
        } else if (word == "min_distance_group_placement") rec.minGroupDistance = v0;
        else if (word == "temp_min_distance_group_placement") rec.tempMinGroupDistance = v0;
        else if (word == "max_distance_to_other_zones") rec.maxZoneDistance = v0;
        else if (word == "match_player_civ") rec.matchCiv = true;
        else if (word == "set_scaling_to_map_size") rec.scaleMap = true;
        else if (word == "set_scaling_to_player_number") rec.scalePlayers = true;
        return;
    }
    default:
        return;
    }
}

// --- Post-parse: teams, land positions, scaling (0x4dfe90) -----------------

void Generator::postParse() {
    lands_.erase(std::remove_if(lands_.begin(), lands_.end(),
                                [](const LandRec &land) { return land.deleted; }),
                 lands_.end());
    team_.assign((size_t)players_ + 1, 0);
    int nextTeam = 1;
    for (int p = 1; p <= players_; ++p) {
        for (int q = 1; q < p; ++q)
            if (isAlly(p, q)) {
                team_[(size_t)p] = team_[(size_t)q];
                break;
            }
        if (!team_[(size_t)p]) team_[(size_t)p] = nextTeam++;
    }

    std::vector<size_t> playerLands;
    for (size_t i = 0; i < lands_.size(); ++i)
        if (lands_[i].player > 0) playerLands.push_back(i);
    if (!playerLands.empty()) {
        const int n = (int)playerLands.size();
        int uw = w_, uh = h_, ox = 0, oy = 0;
        for (size_t i : playerLands) {
            const LandRec &land = lands_[i];
            uw = std::min(uw, land.right - land.left - 2 * land.baseSize);
            uh = std::min(uh, land.bottom - land.top - 2 * land.baseSize);
            ox = std::max(ox, land.left + land.baseSize);
            oy = std::max(oy, land.top + land.baseSize);
        }
        const int sx = uw * 6 / 10, sy = uh * 6 / 10, m = uw * 2 / 10, hp = sx + sy;
        const int step = 2 * hp / n, jit = step / n;
        std::array<int, 8> perm{};
        for (int i = 0; i < 8; ++i) {
            int v;
            bool repeat;
            do {
                v = rng_.range(8) + 1;
                if (v == 9) v = 8;
                repeat = false;
                for (int j = 0; j < i; ++j) repeat = repeat || perm[(size_t)j] == v;
            } while (repeat);
            perm[(size_t)i] = v;
        }
        int t = rng_.range(2 * hp);
        std::vector<int> ring(lands_.size(), -1);
        std::vector<bool> used(lands_.size(), false);
        if (!s_.fixedPositions) {
            for (int a = 0; a < 8; ++a)
                for (size_t i : playerLands)
                    if (!used[i] && lands_[i].player == perm[(size_t)a]) {
                        ring[i] = t;
                        used[i] = true;
                        t += step;
                        if (t >= 2 * hp) t -= 2 * hp;
                    }
            for (size_t i : playerLands) {
                const int p = ring[i];
                LandRec &land = lands_[i];
                if (p < sx) { land.x = p + m + ox; land.y = m / 2 + oy; }
                else if (p < hp) { land.x = m / 2 + sx + ox; land.y = p - sx + m + oy; }
                else if (p < hp + sx) { land.x = hp - p + m + sx + ox; land.y = m / 2 + sy + oy; }
                else { land.x = m / 2 + ox; land.y = (hp + sx - p) + m + sy + oy; }
            }
        } else {
            for (int a = 0; a < 8; ++a)
                for (int b = 0; b < 8; ++b) {
                    const int pb = perm[(size_t)b];
                    if (pb >= players_ + 1 ||
                        !(isAlly(pb, perm[(size_t)a]) || pb == perm[(size_t)a]))
                        continue;
                    for (size_t i : playerLands) {
                        if (used[i] || lands_[i].player != pb) continue;
                        used[i] = true;
                        int p = t + rng_.range(jit) - jit / 2;
                        if (p < 0) p += 2 * hp;
                        if (p > 2 * hp) p -= 2 * hp;
                        ring[i] = p;
                        t += step;
                        if (t >= 2 * hp) t -= 2 * hp;
                    }
                }
            for (size_t i : playerLands) {
                const int p = ring[i];
                LandRec &land = lands_[i];
                if (p < sx) {
                    land.x = p + m + ox;
                    land.y = m / 2 + oy + rng_.range(m) - m / 2;
                } else if (p < hp) {
                    land.x = m / 2 + m + sx + ox + rng_.range(m) - m / 2;
                    land.y = p - sx + m + oy;
                } else if (p < hp + sx) {
                    land.x = hp - p + m + sx + ox;
                    land.y = m / 2 + m + sy + oy + rng_.range(m) - m / 2;
                } else {
                    land.x = m / 2 + ox + rng_.range(m) - m / 2;
                    land.y = hp + sx - p + m + sy + oy;
                }
            }
        }
        for (size_t i : playerLands) {
            lands_[i].x = std::max(0, std::min(w_ - 1, lands_[i].x));
            lands_[i].y = std::max(0, std::min(h_ - 1, lands_[i].y));
        }
    }
    // Other lands without land_position: random spots in their borders.
    for (size_t i = 0; i < lands_.size(); ++i) {
        LandRec &land = lands_[i];
        if (land.x >= 0 && land.y >= 0) continue;
        const int rx = land.right - land.left - 2 * land.baseSize;
        const int ry = land.bottom - land.top - 2 * land.baseSize;
        int retries = 990;
        bool placed = false;
        while (!placed) {
            int x = rng_.range(rx), y = rng_.range(ry);
            if (!((rx / 3 <= x && x <= rx - rx / 3) || (ry / 3 <= y && y <= ry - ry / 3)))
                continue;
            x += land.left + land.baseSize;
            y += land.top + land.baseSize;
            bool clash = false;
            for (size_t j = 0; j < lands_.size() && !clash; ++j) {
                if (j == i || lands_[j].x <= -1) continue;
                const int d = std::max(land.avoid, lands_[j].avoid);
                const int limit = land.minPlacement >= 0
                                      ? land.minPlacement
                                      : land.baseSize + lands_[j].baseSize + d;
                clash = std::abs(x - lands_[j].x) < limit && std::abs(y - lands_[j].y) < limit;
            }
            if (clash) {
                if (--retries > 0) continue;
                x = w_ / 2;
                y = h_ / 2;
            }
            land.x = std::max(0, std::min(w_ - 1, x));
            land.y = std::max(0, std::min(h_ - 1, y));
            placed = true;
        }
    }
    for (const LandRec &land : lands_) {
        extLands_.push_back({land.x, land.y, land.landId, land.player});
        cliffAvoid_.push_back({land.x, land.y, 15});
        if (land.player > 0) playerAvoid_.push_back({land.x, land.y, 13, 20});
    }
    if (lands_.empty())
        for (int p = 1; p <= players_; ++p) {
            const float angle = 6.2831853f * (p - 1) / std::max(1, players_);
            const int x = (int)(w_ / 2 + std::cos(angle) * w_ * 0.35f);
            const int y = (int)(h_ / 2 + std::sin(angle) * h_ * 0.35f);
            extLands_.push_back({x, y, 1, p});
        }
    // Scaling (sec 8).
    const int64_t area = (int64_t)w_ * h_;
    for (TerrainRec &rec : terrains_) {
        if (rec.scale & 1) rec.tiles = (int)(rec.tiles * area / 10000);
        if (rec.scale & 2) {
            rec.clumps = (int)(rec.clumps * area / 10000);
            if (rec.tiles > 0) rec.tiles = (int)(rec.tiles * area / 10000);
        }
        if (rec.tiles < 0) rec.tiles = (int)(area * -rec.tiles / 100);
    }
    for (ElevationRec &rec : elevations_) {
        if (rec.scale & 1) rec.tiles = (int)(rec.tiles * area / 10000);
        if (rec.scale & 2) rec.clumps = (int)(rec.clumps * area / 10000);
    }
}

// --- Engine helpers ---------------------------------------------------------

int Generator::createObject(int unitId, int player, float x, float y, int facet) {
    RmsObject object;
    object.unitId = unitId;
    object.player = player;
    object.x = x;
    object.y = y;
    object.facet = facet;
    objects_.push_back(object);
    const int index = (int)objects_.size() - 1;
    const int tx = std::max(0, std::min(w_ - 1, (int)x));
    const int ty = std::max(0, std::min(h_ - 1, (int)y));
    tileObjects_[at(tx, ty)].push_back(index);
    return index;
}

void Generator::deleteObject(int index) {
    RmsObject &object = objects_[(size_t)index];
    if (!object.alive) return;
    object.alive = false;
    const int tx = std::max(0, std::min(w_ - 1, (int)object.x));
    const int ty = std::max(0, std::min(h_ - 1, (int)object.y));
    auto &list = tileObjects_[at(tx, ty)];
    list.erase(std::remove(list.begin(), list.end(), index), list.end());
}

// can_place (unit type vtbl+0x38): 0 = OK. hill: a6; clearance: a8;
// forceObstruction: a9; obstruction: a10; mode: a12 (1 = RMS: combat units
// ignore other combat units).
int Generator::canPlace(const dat::Unit &unit, int player, float x, float y, bool hill,
                        bool clearance, bool forceObstruction, bool obstruction,
                        int mode) const {
    (void)player;
    const float rx = clearance ? unit.clearanceSize[0] : unit.collisionSize[0];
    const float ry = clearance ? unit.clearanceSize[1] : unit.collisionSize[1];
    const float x0 = x - rx, y0 = y - ry, x1 = x + rx - 0.001f, y1 = y + ry - 0.001f;
    if (x0 < 0 || (float)w_ <= x1 || y0 < 0 || (float)h_ <= y1) return 7;
    const int tx0 = (int)x0, tx1 = (int)x1, ty0 = (int)y0, ty1 = (int)y1;
    int bx0 = tx0, bx1 = tx1, by0 = ty0, by1 = ty1;
    const int pt1 = unit.placementTerrain[0], pt2 = unit.placementTerrain[1];
    if (pt1 >= 0 || pt2 >= 0) {
        const int cx = (int)x, cy = (int)y;
        const int t = terrain_[at(cx, cy)];
        if (t != pt1 && t != pt2) return 1;
        bx0 = bx1 = cx;
        by0 = by1 = cy;
    }
    const int side1 = unit.placementSideTerrain[0], side2 = unit.placementSideTerrain[1];
    if (side1 >= 0 || side2 >= 0) {
        const int alt = side1 == 35 || side2 == 35 ? 37 : -1;
        bool found = false;
        auto check = [&](int tx, int ty) {
            if (!inMap(tx, ty)) return;
            const int t = terrain_[at(tx, ty)];
            found = found || t == side1 || t == side2 || t == alt;
        };
        for (int tx = bx0; tx <= bx1; ++tx) check(tx, by0 - 1);
        for (int ty = by0; ty <= by1; ++ty) check(bx1 + 1, ty);
        for (int tx = bx0; tx <= bx1; ++tx) check(tx, by1 + 1);
        for (int ty = by0; ty <= by1; ++ty) check(bx0 - 1, ty);
        if (!found) return 1;
    }
    const std::vector<float> *costs =
        unit.terrainRestriction >= 0 &&
                (size_t)unit.terrainRestriction < dat_.terrainRestrictions.size()
            ? &dat_.terrainRestrictions[(size_t)unit.terrainRestriction]
                   .passableBuildableDmgMultiplier
            : nullptr;
    const int ref = elevation_[at(tx0, ty1)];
    for (int tx = tx0; tx <= tx1; ++tx)
        for (int ty = ty0; ty <= ty1; ++ty) {
            if ((unit.id == 665 || unit.id == 666) && ty - ty0 != tx - tx0) continue;
            if ((unit.id == 673 || unit.id == 674) && tx1 - tx != ty - ty0) continue;
            const int t = terrain_[at(tx, ty)];
            if (costs && (size_t)t < costs->size() && (*costs)[(size_t)t] <= 0.05f) return 2;
            if (hill) {
                const int s = slope_[at(tx, ty)], e = elevation_[at(tx, ty)];
                switch (unit.hillMode) {
                case 1:
                    if (!(s == 0 || (s >= 5 && s <= 8))) return 3;
                    break;
                case 2:
                    if (s != 0) return 3;
                    break;
                case 3:
                    if ((tx == tx0 || ty == ty1) && (e < ref - 1 || e > ref + 1)) return 3;
                    break;
                default:
                    break;
                }
            }
        }
    if (rx <= 0 && ry <= 0) return 0;
    if (!obstruction) return 0;
    const bool combat = unit.type == 70;
    if (!forceObstruction && unit.collisionSize[2] <= 0 && !combat) return 0;
    const int sx0 = std::max(0, (int)(x - 8)), sy0 = std::max(0, (int)(y - 8));
    const int sx1 = std::min(w_ - 1, (int)(x + 8)), sy1 = std::min(h_ - 1, (int)(y + 8));
    for (int ty = sy0; ty <= sy1; ++ty)
        for (int tx = sx0; tx <= sx1; ++tx)
            for (int index : tileObjects_[at(tx, ty)]) {
                const RmsObject &other = objects_[(size_t)index];
                const dat::Unit *o = unitFor(other.player, other.unitId);
                if (!o || o->canBeBuiltOn) continue;
                if (mode == 1 && combat && o->type == 70) continue;
                float orz = o->collisionSize[2];
                if (orz == 0 && o->type == 80 && o->id != 50) orz = 1;
                if (o->collisionSize[0] <= 0 || o->collisionSize[1] <= 0 || orz <= 0) continue;
                if (std::fabs(other.x - x) < o->collisionSize[0] + rx &&
                    std::fabs(other.y - y) < o->collisionSize[1] + ry)
                    return 6;
            }
    return 0;
}

void Generator::removeTerrainObjects(int x, int y, int oldTerrain) {
    const auto &terrains = dat_.terrainBlock.terrains;
    auto list = tileObjects_[at(x, y)];
    for (int index : list) {
        const RmsObject &object = objects_[(size_t)index];
        if (object.player != 0) continue;
        if (oldTerrain < 0) {
            deleteObject(index);
            continue;
        }
        if ((size_t)oldTerrain >= terrains.size()) continue;
        const dat::Terrain &terrain = terrains[(size_t)oldTerrain];
        for (int k = std::min<int>(terrain.numTerrainUnitsUsed, 29); k >= 0; --k)
            if (terrain.terrainUnitId[k] == object.unitId) {
                deleteObject(index);
                break;
            }
    }
}

// 0x495020 (fullRecreate: centering 0 random, 1 centred, other keeps the
// previous spot) / 0x495500 (any non-zero centering is centred).
void Generator::addTerrainObjects(int x, int y, bool fullRecreate) {
    const auto &terrains = dat_.terrainBlock.terrains;
    int t = terrain_[at(x, y)];
    if (t > 55 || (size_t)t >= terrains.size() || !terrains[(size_t)t].enabled) t = 0;
    const dat::Terrain &terrain = terrains[(size_t)t];
    float fx = 0, fy = 0;
    for (int i = 0; i < terrain.numTerrainUnitsUsed && i < 30; ++i) {
        const int id = terrain.terrainUnitId[i];
        if (id == -1) continue;
        const dat::Unit *unit = unitFor(0, id);
        int density = terrain.terrainUnitDensity[i];
        const int centering = terrain.terrainUnitCentering[i];
        if (!unit || density <= 0) continue;
        do {
            if (centering == 0) {
                fx = rng_.next() * (1.0f / 32767.0f) + x;
                fy = rng_.next() * (1.0f / 32767.0f) + y;
            } else if (centering == 1 || !fullRecreate) {
                fx = x + 0.5f;
                fy = y + 0.5f;
            }
            const bool roll = density > 999 || (rng_.next() * 1000) / 32767 < density;
            if (roll && canPlace(*unit, 0, fx, fy, true, true, false, true, 0) == 0)
                createObject(id, 0, fx, fy);
            density -= 1000;
        } while (density > 0);
    }
}

void Generator::setTerrainSingle(int x, int y, int t) {
    const int old = terrain_[at(x, y)];
    if (old == t) return;
    terrain_[at(x, y)] = (uint8_t)t;
    // fit_objects 0x495350.
    const auto &terrains = dat_.terrainBlock.terrains;
    auto list = tileObjects_[at(x, y)];
    for (int index : list) {
        const RmsObject &object = objects_[(size_t)index];
        bool remove = false;
        if ((size_t)old < terrains.size()) {
            const dat::Terrain &terrain = terrains[(size_t)old];
            for (int k = std::min<int>(terrain.numTerrainUnitsUsed, 29); k >= 0 && !remove; --k)
                remove = terrain.terrainUnitId[k] == object.unitId;
        }
        if (!remove) {
            const dat::Unit *unit = unitFor(object.player, object.unitId);
            remove = !unit ||
                     canPlace(*unit, object.player, object.x, object.y, true, true, false,
                              false, 0) != 0;
        }
        if (remove) deleteObject(index);
    }
    addTerrainObjects(x, y, false);
}

// 0x492ab0 over the whole map.
void Generator::cleanup(int t) {
    auto is = [&](int x, int y) { return inMap(x, y) && terrain_[at(x, y)] == t; };
    for (int pass = 0; pass < 2; ++pass)
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_; ++x) {
                if (terrain_[at(x, y)] == t) continue;
                const bool U = is(x, y - 1), D = is(x, y + 1), L = is(x - 1, y), R = is(x + 1, y);
                bool set;
                if (pass == 0) {
                    set = (U && D) || (L && R);
                } else {
                    const bool UL = is(x - 1, y - 1), UR = is(x + 1, y - 1);
                    const bool DL = is(x - 1, y + 1), DR = is(x + 1, y + 1);
                    set = (UL && ((UR && !U) || (R && !UR) || (DL && !L) || (D && !DL) ||
                                  (DR && !D && !R))) ||
                          (UR && ((UL && !U) || (L && !UL) || (DR && !R) || (D && !DR) ||
                                  (DL && !L && !D))) ||
                          (DR && ((UR && !R) || (U && !UR) || (DL && !D) || (L && !DL) ||
                                  (UL && !L && !U))) ||
                          (DL && ((UL && !L) || (U && !UL) || (DR && !D) || (R && !DR) ||
                                  (UR && !R && !U)));
                }
                if (set) setTerrainSingle(x, y, t);
            }
}

int Generator::slopeAt(int x, int y) const {
    const int e = elevation_[at(x, y)];
    auto el = [&](int px, int py) { return inMap(px, py) ? (int)elevation_[at(px, py)] : e; };
    const int n = el(x, y - 1), s = el(x, y + 1), w = el(x - 1, y), ee = el(x + 1, y);
    const int ne = el(x + 1, y - 1), sw = el(x - 1, y + 1), nw = el(x - 1, y - 1),
              se = el(x + 1, y + 1);
    const int H = e + 1, L = e - 1;
    if (n == H && ee == H) return 14;
    if (w == H && s == H) return 13;
    if (n == H && w == H) return 16;
    if (ee == H && s == H) return 15;
    if (n == H) return 6;
    if (ee == H) return 8;
    if (w == H) return 5;
    if (s == H) return 7;
    if (ne == H) return (w == L && s == L) ? 2 : 10;
    if (sw == H) return (n == L && ee == L) ? 1 : 9;
    if (nw == H) return (ee == L && s == L) ? 3 : 11;
    if (se == H) return (n == L && w == L) ? 4 : 12;
    return 0;
}

void Generator::recomputeSlopes() {
    for (int y = 0; y < h_; ++y)
        for (int x = 0; x < w_; ++x) slope_[at(x, y)] = (uint8_t)slopeAt(x, y);
}

// 0x4925d0(whole map, 8).
void Generator::elevationFixup() {
    const int maxE = 8;
    auto higher = [&](int x, int y, int e) {
        return inMap(x, y) && elevation_[at(x, y)] > e;
    };
    auto raise = [&](int x, int y) {
        uint8_t &e = elevation_[at(x, y)];
        if (e < maxE) {
            e++;
            return;
        }
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if ((dx || dy) && higher(x + dx, y + dy, e)) elevation_[at(x + dx, y + dy)] = e;
    };
    for (int guard = 0; guard < 64; ++guard) {
        bool changed;
        int inner = 0;
        do {
            changed = false;
            for (int y = 0; y < h_; ++y)
                for (int x = 0; x < w_; ++x) {
                    const int e = elevation_[at(x, y)];
                    if ((higher(x, y - 1, e) && higher(x, y + 1, e)) ||
                        (higher(x + 1, y, e) && higher(x - 1, y, e))) {
                        raise(x, y);
                        changed = true;
                    }
                }
        } while (changed && ++inner < 64);
        bool changedB = false;
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_; ++x) {
                const int e = elevation_[at(x, y)];
                const bool N = higher(x, y - 1, e), S = higher(x, y + 1, e);
                const bool W = higher(x - 1, y, e), E = higher(x + 1, y, e);
                const bool NW = higher(x - 1, y - 1, e), NE = higher(x + 1, y - 1, e);
                const bool SW = higher(x - 1, y + 1, e), SE = higher(x + 1, y + 1, e);
                const bool r =
                    (NW && ((NE && !N) || (E && !NE) || (SW && !W) || (S && !SW) ||
                            (SE && !S && !E))) ||
                    (NE && ((SE && !E) || (S && !SE) || (NW && !N) || (W && !NW) ||
                            (SW && !W && !S))) ||
                    (SE && ((SW && !S) || (W && !SW) || (NE && !E) || (N && !NE) ||
                            (NW && !N && !W))) ||
                    (SW && ((NW && !W) || (N && !NW) || (SE && !S) || (E && !SE) ||
                            (NE && !E && !N)));
                if (r) {
                    raise(x, y);
                    changedB = true;
                }
            }
        bool changedC = false;
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_; ++x) {
                uint8_t &e = elevation_[at(x, y)];
                bool step = false;
                for (int dy = -1; dy <= 1 && !step; ++dy)
                    for (int dx = -1; dx <= 1 && !step; ++dx) {
                        if ((!dx && !dy) || !inMap(x + dx, y + dy)) continue;
                        const int o = elevation_[at(x + dx, y + dy)];
                        if (e < maxE) step = o > e + 1;
                        else if (e > maxE) step = o < e - 1;
                    }
                if (step) {
                    if (e < maxE) e++;
                    else e--;
                    changedC = true;
                }
            }
        if (!changedB && !changedC) break;
    }
    recomputeSlopes();
}

std::vector<uint8_t> Generator::zoneMap(const dat::Unit &unit, int forcedTerrain) const {
    std::vector<uint8_t> zones((size_t)w_ * h_, 0xff);
    const std::vector<float> *costs =
        unit.terrainRestriction >= 0 &&
                (size_t)unit.terrainRestriction < dat_.terrainRestrictions.size()
            ? &dat_.terrainRestrictions[(size_t)unit.terrainRestriction]
                   .passableBuildableDmgMultiplier
            : nullptr;
    auto passable = [&](int x, int y) {
        const int t = terrain_[at(x, y)];
        if (t == forcedTerrain) return true;
        return !costs || (size_t)t >= costs->size() || (*costs)[(size_t)t] > 0.0f;
    };
    int id = 0;
    std::vector<int> queue;
    for (int y = 0; y < h_; ++y)
        for (int x = 0; x < w_; ++x) {
            if (zones[at(x, y)] != 0xff) continue;
            if (id > 254) id = 0;
            const bool pass = passable(x, y);
            zones[at(x, y)] = (uint8_t)id;
            queue.assign(1, (int)at(x, y));
            for (size_t head = 0; head < queue.size(); ++head) {
                const int cx = queue[head] % w_, cy = queue[head] / w_;
                const int nx[4] = {cx - 1, cx, cx + 1, cx}, ny[4] = {cy, cy - 1, cy, cy + 1};
                for (int k = 0; k < 4; ++k)
                    if (inMap(nx[k], ny[k]) && zones[at(nx[k], ny[k])] == 0xff &&
                        passable(nx[k], ny[k]) == pass) {
                        zones[at(nx[k], ny[k])] = (uint8_t)id;
                        queue.push_back((int)at(nx[k], ny[k]));
                    }
            }
            ++id;
        }
    return zones;
}

// --- LAND (0x4da380 / 0x4da6f0) ---------------------------------------------

void Generator::landModule() {
    std::vector<uint8_t> g((size_t)w_ * h_, kUnclaimed);
    std::fill(terrain_.begin(), terrain_.end(), (uint8_t)baseTerrain_);
    lists_.reset(w_, h_);
    std::vector<int> frontier;
    for (size_t i = 0; i < lands_.size(); ++i) frontier.push_back(lists_.newList());
    std::array<int, 256> count{};
    for (size_t i = 0; i < lands_.size(); ++i) {
        const LandRec &land = lands_[i];
        count[i] = 0;
        const int b = land.baseSize;
        const int x0 = std::max(0, land.x - b), x1 = std::min(w_ - 1, land.x + b);
        const int y0 = std::max(0, land.y - b), y1 = std::min(h_ - 1, land.y + b);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                terrain_[at(x, y)] = (uint8_t)land.terrain;
                g[at(x, y)] = (uint8_t)land.zone;
            }
        // (sic) indexed by zone, not by land.
        count[(size_t)(land.zone & 0xff)] = (x1 - x0 + 1) * (y1 - y0 + 1);
        const int list = frontier[i];
        const float prio = (float)i;
        if (x0 > 0)
            for (int y = y0; y <= y1; ++y) lists_.insert(list, lists_.node(x0 - 1, y), prio);
        if (y0 > 0)
            for (int x = x0; x <= x1; ++x) lists_.insert(list, lists_.node(x, y0 - 1), prio);
        if (x1 < w_ - 1)
            for (int y = y0; y <= y1; ++y) lists_.insert(list, lists_.node(x1 + 1, y), prio);
        if (y1 < h_ - 1)
            for (int x = x0; x <= x1; ++x) lists_.insert(list, lists_.node(x, y1 + 1), prio);
    }
    auto fuzz = [&](const LandRec &land, int x, int y) {
        const int fz = land.fuzziness;
        if (fz == 0) return 0;
        const int L = land.left, R = land.right, T = land.top, B = land.bottom;
        const int d1 = std::max(L - x, x - R);
        const int q = (R - L) / 4;
        const int a = (L - x) + std::min(L, q);
        const int m2 = std::min(q, w_ - R);
        const int b = (x - R) + m2;
        int c = std::max(a, b);
        int e;
        if (c > 0) {
            c = std::min(c, (B - T) / 3);
            e = std::max(T - y + c, y - B + c);
        } else {
            e = std::max(T - y, y - B);
        }
        const int v = (std::max(e, 0) + std::max(d1, 0)) * fz;
        return v >= 100 ? 101 : v;
    };
    auto near = [&](const LandRec &land, int x, int y) {
        const int A = land.avoid;
        const int R2 = std::max(2, (int)(A * 0.666666));
        const int S = std::max(2, A);
        if (g[at(x, y)] != kUnclaimed) return 0;
        int n = 0;
        int cx0 = x - R2, cx1 = x + R2;
        for (int row = y - S; row <= y + S; ++row) {
            for (int col = cx0; col <= cx1; ++col) {
                if (!inMap(col, row)) continue;
                const int v = g[at(col, row)];
                const int dx = std::abs(col - x), dy = std::abs(row - y);
                if (v == land.zone) {
                    if (dx <= 2 && dy <= 2) n++;
                } else if (v < kUnclaimed) {
                    if (dx <= A && dy <= A) return 0;
                }
            }
            if (row < y - R2) {
                cx0--;
                cx1++;
            }
            if (row > y + R2) {
                cx0--;
                cx1++;
            }
        }
        return n;
    };
    bool popped = true;
    while (popped) {
        popped = false;
        for (size_t i = 0; i < lands_.size(); ++i) {
            const LandRec &land = lands_[i];
            if (count[i] >= land.tiles) continue;
            const int node = lists_.pop(frontier[i]);
            if (node < 0) continue;
            popped = true;
            const int x = lists_.x(node), y = lists_.y(node);
            const int f = fuzz(land, x, y);
            const int r = rng_.range(100);
            if (f > r) {
                g[at(x, y)] = kRejected;
                continue;
            }
            const int n = near(land, x, y);
            if (g[at(x, y)] == kUnclaimed && n > 0) {
                terrain_[at(x, y)] = (uint8_t)land.terrain;
                g[at(x, y)] = (uint8_t)land.zone;
                const int nx[4] = {x - 1, x + 1, x, x}, ny[4] = {y, y, y - 1, y + 1};
                for (int k = 0; k < 4; ++k)
                    if (inMap(nx[k], ny[k]) && g[at(nx[k], ny[k])] == kUnclaimed)
                        lists_.insert(frontier[i], lists_.node(nx[k], ny[k]),
                                      (float)(rng_.range(100) - land.clumping * n + 250));
                count[i]++;
            }
        }
    }
    for (size_t i = 0; i < lands_.size(); ++i) {
        const LandRec &land = lands_[i];
        for (int node; (node = lists_.pop(frontier[i])) >= 0;) {
            const int x = lists_.x(node), y = lists_.y(node);
            auto zoneAt = [&](int px, int py) {
                return inMap(px, py) && g[at(px, py)] == (uint8_t)land.zone;
            };
            if ((zoneAt(x - 1, y) && zoneAt(x + 1, y)) || (zoneAt(x, y - 1) && zoneAt(x, y + 1)))
                terrain_[at(x, y)] = (uint8_t)land.terrain;
        }
    }
    landZones_ = g;
    cleanup(baseTerrain_);
}

// --- ELEVATION (0x4d9950) ---------------------------------------------------

namespace {
std::vector<uint8_t> avoidGrid(int w, int h, const std::vector<std::array<int, 4>> &points) {
    std::vector<uint8_t> a((size_t)w * h, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int sum = 0;
            for (const auto &p : points) {
                const int dx = x - p[0], dy = y - p[1];
                const int d = (int)std::sqrt((double)(dx * dx + dy * dy));
                if (p[2] - d > 0) sum += (p[2] - d) * p[3];
            }
            a[(size_t)y * w + x] = (uint8_t)(sum > 100 ? 101 : sum);
        }
    return a;
}
} // namespace

void Generator::elevationModule() {
    if (elevations_.empty()) return;
    std::fill(elevation_.begin(), elevation_.end(), 0);
    std::vector<uint8_t> avoid = avoidGrid(w_, h_, playerAvoid_);
    for (const ElevationRec &rec : elevations_) {
        lists_.reset(w_, h_);
        const int T = rec.tiles, H = rec.height, L = rec.level, B = rec.base, S = rec.spacing;
        const int C = std::min(rec.clumps, 999);
        if (C <= 0) continue;
        const int cand = lists_.newList();
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_; ++x)
                if (terrain_[at(x, y)] == B && elevation_[at(x, y)] == L && avoid[at(x, y)] == 0)
                    lists_.append(cand, lists_.node(x, y));
        lists_.shuffle100(cand, rng_);
        const int r = std::max(2, (int)std::sqrt((double)T / C) / 2);
        std::vector<int> clumps;
        for (int k = 0; k < C; ++k) clumps.push_back(lists_.newList());
        auto squareOk = [&](int x, int y, int radius) {
            if (B == 255) return false;
            for (int py = y - radius; py <= y + radius; ++py)
                for (int px = x - radius; px <= x + radius; ++px) {
                    if (!inMap(px, py)) continue;
                    if (terrain_[at(px, py)] != B || elevation_[at(px, py)] < L) return false;
                }
            return true;
        };
        int count = 0, k = 0;
        while (k < C) {
            const int node = lists_.pop(cand);
            if (node < 0) break;
            const int x = lists_.x(node), y = lists_.y(node);
            if (avoid[at(x, y)] != 0 || !squareOk(x, y, S)) continue;
            for (int py = y - r; py <= y + r; ++py)
                for (int px = x - r; px <= x + r; ++px)
                    if (inMap(px, py)) lists_.unlink(lists_.node(px, py));
            elevation_[at(x, y)] = (uint8_t)H;
            const int nx[4] = {x - 1, x, x + 1, x}, ny[4] = {y, y - 1, y, y + 1};
            for (int j = 0; j < 4; ++j)
                if (inMap(nx[j], ny[j])) lists_.insert(clumps[(size_t)k], lists_.node(nx[j], ny[j]), 0.0f);
            count++;
            k++;
        }
        bool popped = true;
        while (popped) {
            popped = false;
            for (int c = 0; c < C; ++c) {
                if (count >= T) continue;
                const int node = lists_.pop(clumps[(size_t)c]);
                if (node < 0) continue;
                popped = true;
                const int x = lists_.x(node), y = lists_.y(node);
                if (avoid[at(x, y)] > rng_.range(100)) {
                    avoid[at(x, y)] = 101;
                    continue;
                }
                int n = 0;
                if (!(S > 0 && !squareOk(x, y, S)))
                    for (int py = y - 2; py <= y + 2; ++py)
                        for (int px = x - 2; px <= x + 2; ++px)
                            if (inMap(px, py) && elevation_[at(px, py)] == H) n++;
                if (elevation_[at(x, y)] == L && n > 0) {
                    elevation_[at(x, y)] = (uint8_t)H;
                    const float p0 = (float)(250 - 15 * n);
                    const int nx[4] = {x - 1, x + 1, x, x}, ny[4] = {y, y, y - 1, y + 1};
                    for (int j = 0; j < 4; ++j)
                        if (inMap(nx[j], ny[j]) && elevation_[at(nx[j], ny[j])] == L)
                            lists_.insert(clumps[(size_t)c], lists_.node(nx[j], ny[j]),
                                          rng_.range(100) + p0);
                    count++;
                }
            }
        }
    }
    elevationFixup();
}

// --- CLIFFS (0x4d76b0) and cliff drawing (0x5cbbc0) -----------------------

namespace {
struct CliffPiece {
    std::array<int, 4> e;
    int unitA, tagA, unitB, tagB;
    float fx, fy;
};
// TRIBE_Map ctor 0x5c9adc: the 20 valid edge combinations.
const std::array<CliffPiece, 20> kCliffPieces = {{
    {{1, 0, 0, 0}, 264, 16, -1, -1, 1.5f, 1.5f},  {{0, 1, 0, 0}, 264, 18, -1, -1, 1.5f, 1.5f},
    {{0, 0, 1, 0}, 264, 17, -1, -1, 1.5f, 1.5f},  {{0, 0, 0, 1}, 264, 19, -1, -1, 1.5f, 1.5f},
    {{-1, 0, 0, 0}, 266, 20, -1, -1, 1.5f, 1.5f}, {{0, -1, 0, 0}, 265, 22, -1, -1, 1.5f, 1.5f},
    {{0, 0, -1, 0}, 266, 21, -1, -1, 1.5f, 1.5f}, {{0, 0, 0, -1}, 265, 23, -1, -1, 1.5f, 1.5f},
    {{1, 0, 1, 0}, 264, 4, 264, 5, 1.5f, 1.5f},   {{-1, 0, -1, 0}, 266, 11, 266, 10, 1.5f, 1.5f},
    {{-1, 1, 0, 0}, 270, 0, -1, -1, 1.5f, 2.0f},  {{0, -1, -1, 0}, 272, 9, -1, -1, 1.0f, 2.0f},
    {{0, 0, 1, -1}, 268, 6, -1, -1, 1.0f, 1.5f},  {{1, 0, 0, 1}, 264, 3, -1, -1, 1.5f, 1.5f},
    {{1, -1, 0, 0}, 267, 12, -1, -1, 2.0f, 1.5f}, {{0, 1, 1, 0}, 264, 15, -1, -1, 1.5f, 1.5f},
    {{0, 0, -1, 1}, 269, 14, -1, -1, 1.5f, 1.0f}, {{-1, 0, 0, -1}, 271, 13, -1, -1, 2.0f, 1.0f},
    {{0, 1, 0, 1}, 264, 1, 264, 2, 1.5f, 1.5f},   {{0, -1, 0, -1}, 265, 8, 265, 7, 1.5f, 1.5f},
}};
const CliffPiece *cliffPiece(const std::array<int, 4> &e) {
    for (const CliffPiece &piece : kCliffPieces)
        if (piece.e == e) return &piece;
    return nullptr;
}
} // namespace

int Generator::findCliff(int bx, int by) const {
    if (bx * 3 + 2 > w_ || by * 3 + 2 > h_) return -1;
    const int tiles[4][2] = {{bx * 3 + 1, by * 3 + 1}, {bx * 3 + 1, by * 3 + 2},
                             {bx * 3 + 2, by * 3 + 1}, {bx * 3 + 2, by * 3 + 2}};
    for (const auto &tile : tiles) {
        if (!inMap(tile[0], tile[1])) continue;
        for (int index : tileObjects_[at(tile[0], tile[1])]) {
            const int id = objects_[(size_t)index].unitId;
            if (id >= 264 && id <= 273) return index;
        }
    }
    return -1;
}

std::array<int, 4> Generator::cliffEdgesOf(int object) const {
    if (object < 0) return {0, 0, 0, 0};
    const RmsObject &o = objects_[(size_t)object];
    for (const CliffPiece &piece : kCliffPieces)
        if ((piece.unitA == o.unitId && piece.tagA == o.facet) ||
            (piece.unitB == o.unitId && piece.tagB == o.facet))
            return piece.e;
    return {0, 0, 0, 0};
}

void Generator::removeOverlapping(int unitId, float x, float y) {
    const dat::Unit *t = unitFor(0, unitId);
    if (!t || !(t->collisionSize[0] > 0 || t->collisionSize[1] > 0)) return;
    const int x0 = std::max(0, (int)(x - 4)), x1 = std::min(w_ - 1, (int)(x + 4));
    const int y0 = std::max(0, (int)(y - 4)), y1 = std::min(h_ - 1, (int)(y + 4));
    for (int ty = y0; ty <= y1; ++ty)
        for (int tx = x0; tx <= x1; ++tx) {
            auto list = tileObjects_[at(tx, ty)];
            for (int index : list) {
                const RmsObject &o = objects_[(size_t)index];
                const dat::Unit *ot = unitFor(o.player, o.unitId);
                if (!ot || ot->id == unitId || ot->flyMode == 1) continue;
                const bool candidate = ot->canBeBuiltOn != 0 ||
                                       ((t->cls == 8 || t->cls == 9 || t->cls == 10) && ot->cls == 6);
                if (candidate && std::fabs(o.x - x) < ot->collisionSize[0] + t->collisionSize[0] &&
                    std::fabs(o.y - y) < ot->collisionSize[1] + t->collisionSize[1])
                    deleteObject(index);
            }
        }
}

void Generator::placeCliff(int bx, int by, std::array<int, 4> e) {
    const CliffPiece *piece = cliffPiece(e);
    if (!piece) return;
    int unit = piece->unitA, tag = piece->tagA;
    if (piece->unitB > -1 && rng_.next() * 2 > 0x7fff) {
        unit = piece->unitB;
        tag = piece->tagB;
    }
    if (unit < 0) return;
    const float x = (float)(bx * 3) + piece->fx, y = (float)(by * 3) + piece->fy;
    createObject(unit, 0, x, y, tag);
    removeOverlapping(unit, x, y);
}

void Generator::cliffFixNeighbour(int bx, int by, int j) {
    int nx = bx, ny = by;
    if (j == 0) {
        if (bx + 1 >= w_) return;
        nx = bx + 1;
    } else if (j == 1) {
        if (by + 1 >= h_) return;
        ny = by + 1;
    } else if (j == 2) {
        if (bx < 1) return;
        nx = bx - 1;
    } else {
        if (by < 1) return;
        ny = by - 1;
    }
    const int object = findCliff(nx, ny);
    if (object < 0) return;
    std::array<int, 4> e = cliffEdgesOf(object);
    deleteObject(object);
    e[(size_t)((j + 2) & 3)] = 0;
    placeCliff(nx, ny, e);
}

int Generator::cliffEdge(int bx, int by, int d, int val, int keep) {
    const int object = findCliff(bx, by);
    std::array<int, 4> e = cliffEdgesOf(object);
    const int cur = e[(size_t)d];
    int i1 = -1, i2 = -1, v1 = 0, v2 = 0;
    if (cur != 0) {
        if (val == 0) return cur;
        if (val == cur) return val;
    }
    if (object >= 0) deleteObject(object);
    auto valid = [&]() { return cliffPiece(e) != nullptr; };
    if (keep != d && keep >= 0) {
        for (int j = 0; j < 4; ++j)
            if (j != keep) {
                cliffFixNeighbour(bx, by, j);
                e[(size_t)j] = 0;
            }
    } else {
        int others = 0;
        for (int j = 0; j < 4; ++j)
            if (j != d && e[(size_t)j] != 0) others++;
        if (others == 2)
            for (int j = 0; j < 4; ++j)
                if (e[(size_t)j] != 0 && j != d) {
                    if (i1 < 0) {
                        i1 = j;
                        v1 = e[(size_t)j];
                    } else {
                        i2 = j;
                        v2 = e[(size_t)j];
                    }
                    e[(size_t)j] = 0;
                }
    }
    int ret = val;
    if (val != 0) {
        e[(size_t)d] = val;
        if (i1 > -1) {
            e[(size_t)i1] = v1;
            if (!valid()) {
                e[(size_t)i1] = 0;
                cliffFixNeighbour(bx, by, i1);
            }
        }
        if (i2 > -1) {
            e[(size_t)i2] = v2;
            if (!valid()) {
                e[(size_t)i2] = 0;
                cliffFixNeighbour(bx, by, i2);
            }
        }
        if (!valid())
            for (int j = 0; j < 4; ++j)
                if (j != d && e[(size_t)j] != 0) {
                    cliffFixNeighbour(bx, by, j);
                    e[(size_t)j] = 0;
                }
    } else {
        bool placed = false;
        for (int s : {1, -1}) {
            e[(size_t)d] = s;
            ret = s;
            if (i1 > -1) {
                e[(size_t)i1] = v1;
                if (valid()) {
                    if (v2) cliffFixNeighbour(bx, by, i2);
                    placed = true;
                    break;
                }
                e[(size_t)i1] = 0;
            }
            if (i2 > -1) {
                e[(size_t)i2] = v2;
                if (valid()) {
                    if (v1) cliffFixNeighbour(bx, by, i1);
                    placed = true;
                    break;
                }
                e[(size_t)i2] = 0;
            }
            if (valid()) {
                if (v1) cliffFixNeighbour(bx, by, i1);
                if (v2) cliffFixNeighbour(bx, by, i2);
                placed = true;
                break;
            }
        }
        if (!placed) {
            for (int j = 0; j < 4; ++j)
                if (j != d && e[(size_t)j] != 0) {
                    cliffFixNeighbour(bx, by, j);
                    e[(size_t)j] = 0;
                }
            if (v1) cliffFixNeighbour(bx, by, i1);
            if (v2) cliffFixNeighbour(bx, by, i2);
        }
    }
    placeCliff(bx, by, e);
    return ret;
}

void Generator::cliffPoint(int x, int y) {
    int bx = x / 3, by = y / 3;
    cliffCurBX_ = bx;
    cliffCurBY_ = by;
    if (cliffLastBX_ == bx && cliffLastBY_ == by) return;
    if (bx * 3 + 2 >= w_ || by * 3 + 2 >= h_ || bx < 0 || by < 0) return;
    if (cliffLastBX_ != -1 && cliffLastBY_ != -1) {
        const int dx = x - cliffLastBX_ * 3, dy = y - cliffLastBY_ * 3;
        if (0 <= dx && dx < 3) {
            if (0 <= dy && dy < 3) return;
            bx = cliffLastBX_;
        } else {
            by = cliffLastBY_;
        }
    }
    if (bx == cliffLastBX_ && by == cliffLastBY_) return;
    if (cliffLastBX_ == -1) {
        cliffLastBX_ = bx;
        cliffLastBY_ = by;
        cliffLastDir_ = -1;
        return;
    }
    const int d = cliffLastBX_ < bx ? 0 : cliffLastBX_ > bx ? 2 : (cliffLastBY_ < by ? 1 : 3);
    const int r = cliffEdge(cliffLastBX_, cliffLastBY_, d, 0, cliffLastDir_);
    cliffLastDir_ = (d + 2) & 3;
    cliffLastBX_ = bx;
    cliffLastBY_ = by;
    cliffEdge(bx, by, (d + 2) & 3, r, -1);
}

void Generator::cliffLine(int x0, int y0, int x1, int y1) {
    if (x0 / 3 != cliffCurBX_ || y0 / 3 != cliffCurBY_)
        cliffLastBX_ = cliffLastBY_ = cliffCurBX_ = cliffCurBY_ = cliffLastDir_ = -1;
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(w_ - 1, x1);
    y1 = std::min(h_ - 1, y1);
    const double dx = x1 - x0, dy = y1 - y0;
    const int n = (int)std::sqrt(dx * dx + dy * dy);
    cliffPoint(x0, y0);
    double cx = x0, cy = y0;
    if (n > 0) {
        const double sx = dx / n, sy = dy / n;
        for (int i = 0; i < n; ++i) {
            cx += sx;
            cy += sy;
            cliffPoint((int)cx, (int)cy);
        }
    }
    if (!(x1 == cx && y1 == cy)) cliffPoint(x1, y1);
}

void Generator::cliffModule() {
    const int W3 = w_ / 3, H3 = h_ / 3;
    if (W3 <= 0 || H3 <= 0) return;
    lists_.reset(W3, H3);
    std::vector<uint8_t> g((size_t)W3 * H3, 1);
    const int cand = lists_.newList();
    auto blockAt = [&](int bx, int by) -> uint8_t & { return g[(size_t)by * W3 + bx]; };
    auto clear = [&](int bx, int by, int r) {
        for (int y = std::max(0, by - r); y <= std::min(H3 - 1, by + r); ++y)
            for (int x = std::max(0, bx - r); x <= std::min(W3 - 1, bx + r); ++x) {
                blockAt(x, y) = 0;
                lists_.unlink(lists_.node(x, y));
            }
    };
    for (int by = 0; by < H3; ++by)
        for (int bx = 0; bx < W3; ++bx) {
            bool bad = false, uniform = true;
            const int e0 = elevation_[at(bx * 3, by * 3)];
            for (int y = by * 3; y < by * 3 + 3; ++y)
                for (int x = bx * 3; x < bx * 3 + 3; ++x) {
                    const int t = terrain_[at(x, y)];
                    bad = bad || t == 1 || t == 4 || t == 15 || t == 16 || t == 22 || t == 23 ||
                          t == 35;
                    uniform = uniform && elevation_[at(x, y)] == e0;
                }
            if (!bad && uniform) {
                if (blockAt(bx, by) != 0) {
                    blockAt(bx, by) = (uint8_t)(e0 + 1);
                    lists_.insert(cand, lists_.node(bx, by), 0.0f);
                }
            } else {
                if (bad) clear(bx, by, cliffTerrainDistance_);
                blockAt(bx, by) = 0;
            }
        }
    for (const auto &point : cliffAvoid_) clear(point[0] / 3, point[1] / 3, point[2] / 3 + 2);
    for (int by = 0; by < H3; ++by)
        for (int bx = 0; bx < W3; ++bx) {
            if (blockAt(bx, by) == 0) continue;
            bool isolated = true;
            const int nx[4] = {bx - 1, bx + 1, bx, bx}, ny[4] = {by, by, by - 1, by + 1};
            for (int k = 0; k < 4; ++k)
                if (nx[k] >= 0 && ny[k] >= 0 && nx[k] < W3 && ny[k] < H3 && blockAt(nx[k], ny[k]))
                    isolated = false;
            if (isolated) {
                blockAt(bx, by) = 0;
                lists_.unlink(lists_.node(bx, by));
            }
        }
    lists_.shuffle100(cand, rng_);
    const int cliffs = cliffMin_ + rng_.range(cliffMax_ - cliffMin_);
    for (int c = 0; c < cliffs; ++c) {
        const int length = cliffMinLength_ + rng_.range(cliffMaxLength_ - cliffMinLength_);
        if (lists_.empty(cand) || length < 3) break;
        int start = -1;
        while ((start = lists_.pop(cand)) >= 0 &&
               blockAt(lists_.x(start), lists_.y(start)) == 0) {
        }
        if (start < 0) break;
        int bx = lists_.x(start), by = lists_.y(start);
        const uint8_t code = blockAt(bx, by);
        blockAt(bx, by) = 0;
        std::vector<std::pair<int, int>> path{{bx, by}};
        int dir = rng_.range(4);
        auto normal = [](int d) {
            if (d > 3) return (d - 1) % 3 + 1;
            if (d < 0) return d + 3 * ((2 - d) / 3);
            return d;
        };
        for (int step = 0; step < length; ++step) {
            const int r = rng_.range(100);
            const int curl = cliffCurliness_;
            if (r < curl / 2) dir = dir <= 0 ? 3 : dir - 1;
            else if (r < curl) dir = dir < 3 ? dir + 1 : 0;
            std::vector<int> tries = {dir, dir + 1, dir - 1};
            if (step == 0) tries.push_back(dir - 2);
            bool moved = false;
            for (int t : tries) {
                const int d = normal(t);
                int nx = bx, ny = by;
                if (d == 0) ny--;
                else if (d == 1) nx++;
                else if (d == 2) ny++;
                else nx--;
                if (nx < 0 || ny < 0 || nx >= W3 || ny >= H3 || blockAt(nx, ny) != code) continue;
                bx = nx;
                by = ny;
                dir = d;
                blockAt(bx, by) = 0;
                path.push_back({bx, by});
                moved = true;
                break;
            }
            if (!moved) break;
        }
        int prevX = -1, prevY = -1;
        for (auto it = path.rbegin(); it != path.rend(); ++it) {
            const int cx = it->first * 3 + 1, cy = it->second * 3 + 1;
            if (prevX < 0) cliffLine(cx, cy, cx, cy);
            else cliffLine(prevX, prevY, cx, cy);
            prevX = cx;
            prevY = cy;
            clear(it->first, it->second, cliffDistance_);
        }
    }
}

// --- TERRAIN (0x4e5f70) -----------------------------------------------------

void Generator::terrainModule() {
    const std::vector<uint8_t> avoidBase = avoidGrid(w_, h_, playerAvoid_);
    for (const TerrainRec &rec : terrains_) {
        std::vector<uint8_t> avoid = avoidBase;
        lists_.reset(w_, h_);
        const int C = std::min(rec.clumps, 999);
        if (C <= 0) continue;
        const int cand = lists_.newList();
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_; ++x) lists_.append(cand, lists_.node(x, y));
        for (int i = 0; i < w_ * h_ / 8; ++i) {
            const int x = rng_.range(w_ - 1);
            const int y = rng_.range(h_ - 1);
            const int node = lists_.node(x, y);
            if (lists_.owner(node) == cand) lists_.pushFront(cand, node);
        }
        const int r = std::max(2, 2 * (int)std::sqrt((double)rec.tiles / C));
        const int T = rec.terrain, base = rec.base;
        auto ok = [&](int x, int y) {
            const int e = elevation_[at(x, y)];
            if (e < rec.minHeight || e > rec.maxHeight) return 0;
            if (rec.flat && slope_[at(x, y)] != 0) return 0;
            if (rec.spacing > 0)
                for (int py = y - rec.spacing; py <= y + rec.spacing; ++py)
                    for (int px = x - rec.spacing; px <= x + rec.spacing; ++px) {
                        if (!inMap(px, py)) continue;
                        const int t = terrain_[at(px, py)];
                        if (t != base && t != T) return 0;
                        if (rec.flat && slope_[at(px, py)] != 0) return 0;
                    }
            int n = 1;
            for (int py = y - 2; py <= y + 2; ++py)
                for (int px = x - 2; px <= x + 2; ++px)
                    if (inMap(px, py) && terrain_[at(px, py)] == T) n++;
            return n;
        };
        std::vector<int> clumps;
        for (int k = 0; k < C; ++k) clumps.push_back(lists_.newList());
        int count = 0, k = 0;
        while (k < C) {
            const int node = lists_.pop(cand);
            if (node < 0) break;
            const int x = lists_.x(node), y = lists_.y(node);
            if (terrain_[at(x, y)] != base || ok(x, y) <= 0) continue;
            if (rec.avoid && avoid[at(x, y)] != 0) continue;
            for (int py = y - r; py <= y + r; ++py)
                for (int px = x - r; px <= x + r; ++px)
                    if (inMap(px, py)) lists_.unlink(lists_.node(px, py));
            terrain_[at(x, y)] = (uint8_t)T;
            const int nx[4] = {x - 1, x, x + 1, x}, ny[4] = {y, y - 1, y, y + 1};
            for (int j = 0; j < 4; ++j)
                if (inMap(nx[j], ny[j])) lists_.insert(clumps[(size_t)k], lists_.node(nx[j], ny[j]), 0.0f);
            count++;
            k++;
        }
        bool popped = true;
        while (popped) {
            popped = false;
            for (int c = 0; c < C; ++c) {
                if (count >= rec.tiles) continue;
                const int node = lists_.pop(clumps[(size_t)c]);
                if (node < 0) continue;
                popped = true;
                const int x = lists_.x(node), y = lists_.y(node);
                if (rec.avoid && avoid[at(x, y)] > rng_.range(100)) continue;
                const int n = ok(x, y);
                if (terrain_[at(x, y)] != base || n <= 0) continue;
                float p = (float)(250 - rec.clumping * n);
                if (rec.avoid) p += avoid[at(x, y)];
                terrain_[at(x, y)] = (uint8_t)T;
                const int nx[4] = {x - 1, x + 1, x, x}, ny[4] = {y, y, y - 1, y + 1};
                for (int j = 0; j < 4; ++j)
                    if (inMap(nx[j], ny[j]) && terrain_[at(nx[j], ny[j])] == base) {
                        lists_.insert(clumps[(size_t)c], lists_.node(nx[j], ny[j]),
                                      rng_.range(100) + p);
                        count++;
                    }
            }
        }
        for (int c = 0; c < C; ++c)
            for (int node; (node = lists_.pop(clumps[(size_t)c])) >= 0;) {
                const int x = lists_.x(node), y = lists_.y(node);
                if (inMap(x - 1, y) && inMap(x + 1, y) && terrain_[at(x - 1, y)] == T &&
                    terrain_[at(x + 1, y)] == T)
                    terrain_[at(x, y)] = (uint8_t)T;
            }
    }
    cleanup(1);
    shorePass();
}

// 0x4e6040: land next to water becomes shore; ice and snow become ice shore.
void Generator::shorePass() {
    auto water = [](int t) { return t == 1 || t == 4 || t == 22 || t == 23; };
    for (int y = 0; y < h_; ++y)
        for (int x = 0; x < w_; ++x) {
            const int t = terrain_[at(x, y)];
            if (t == 2 || water(t)) continue;
            bool wet = false;
            for (int dy = -1; dy <= 1 && !wet; ++dy)
                for (int dx = -1; dx <= 1 && !wet; ++dx)
                    if ((dx || dy) && inMap(x + dx, y + dy) && water(terrain_[at(x + dx, y + dy)]))
                        wet = true;
            if (!wet) continue;
            if (t == 35 || t == 37 || t == 32 || t == 33 || t == 34 || t == 47)
                terrain_[at(x, y)] = 37;
            else
                terrain_[at(x, y)] = 2;
        }
}

// --- CONNECTIONS (0x4e42b0) -------------------------------------------------

void Generator::connectionModule() {
    for (const ConnectionRec &base : connections_) {
        std::vector<std::vector<std::pair<int, int>>> groups;
        if (base.kind == 1) {
            std::map<int, std::vector<std::pair<int, int>>> byTeam;
            for (const ExtLand &land : extLands_)
                if (land.player > 0 && land.player < (int)team_.size())
                    byTeam[team_[(size_t)land.player]].push_back({land.x, land.y});
            for (auto &entry : byTeam) groups.push_back(entry.second);
        } else {
            std::vector<std::pair<int, int>> points;
            for (const ExtLand &land : extLands_)
                if (base.kind != 0 || land.player > 0) points.push_back({land.x, land.y});
            groups.push_back(points);
        }
        for (const auto &points : groups) {
            for (size_t i = 0; i < points.size(); ++i)
                for (size_t j = i + 1; j < points.size(); ++j) {
                    // Best-first search (0x4d6db0) from i to j.
                    lists_.reset(w_, h_);
                    const int open = lists_.newList();
                    std::vector<uint8_t> g((size_t)w_ * h_, 0);
                    std::vector<float> cost((size_t)w_ * h_, 0.0f);
                    for (size_t t = 0; t < g.size(); ++t) {
                        const int terrain = terrain_[t];
                        if (terrain < 99 && base.cost[(size_t)terrain] <= 0) g[t] = 1;
                    }
                    const int sx = points[i].first, sy = points[i].second;
                    const int dx = points[j].first, dy = points[j].second;
                    if (!inMap(sx, sy) || !inMap(dx, dy)) continue;
                    g[at(dx, dy)] = 2;
                    g[at(sx, sy)] = 3;
                    lists_.insert(open, lists_.node(sx, sy), 0.0f);
                    bool found = false;
                    // Moves: code -> (dx, dy). The back-pointer is the code of
                    // the move that reached the tile.
                    static const int moveX[12] = {0, 0, 0, 0, -1, 1, 0, 0, -1, -1, 1, 1};
                    static const int moveY[12] = {0, 0, 0, 0, 0, 0, -1, 1, -1, 1, -1, 1};
                    while (!found) {
                        const int node = lists_.pop(open);
                        if (node < 0) break;
                        const int x = lists_.x(node), y = lists_.y(node);
                        const float gCost = cost[at(x, y)];
                        const float d = (float)std::sqrt((double)(dx - x) * (dx - x) +
                                                         (double)(dy - y) * (dy - y));
                        if (d == 0) {
                            found = true;
                            break;
                        }
                        float hx[2] = {0, 0}; // h of the x-1 / x+1 neighbours
                        auto ortho = [&](int nx, int ny, int code) {
                            if (!inMap(nx, ny)) return false;
                            const bool toward = (nx != x) ? std::abs(dx - nx) < std::abs(dx - x)
                                                          : std::abs(dy - ny) < std::abs(dy - y);
                            const float h = toward ? d - 1 : d + 1;
                            if (nx < x) hx[0] = h;
                            if (nx > x) hx[1] = h;
                            uint8_t &cell = g[at(nx, ny)];
                            if (cell == 1) return false;
                            if (cell == 2) {
                                cell = (uint8_t)code;
                                found = true;
                                return true;
                            }
                            if (cell != 0) return true;
                            const int terrain = terrain_[at(nx, ny)];
                            const float c = terrain < 99 ? base.cost[(size_t)terrain] : 1.0f;
                            cell = (uint8_t)code;
                            cost[at(nx, ny)] = gCost + c;
                            lists_.insert(open, lists_.node(nx, ny), gCost + c + h);
                            return true;
                        };
                        auto diagonal = [&](int nx, int ny, int code, bool openA, bool openB,
                                            float hX) {
                            if (found || !inMap(nx, ny) || !openA || !openB) return;
                            uint8_t &cell = g[at(nx, ny)];
                            if (cell == 1) return;
                            if (cell == 2) {
                                cell = (uint8_t)code;
                                found = true;
                                return;
                            }
                            if (cell != 0) return;
                            const bool towardY = std::abs(dy - ny) < std::abs(dy - y);
                            const float h = towardY ? hX - 0.41f : hX + 0.40f;
                            const int terrain = terrain_[at(nx, ny)];
                            const float c = terrain < 99 ? base.cost[(size_t)terrain] : 1.0f;
                            cell = (uint8_t)code;
                            cost[at(nx, ny)] = gCost + 1.36f * c;
                            lists_.insert(open, lists_.node(nx, ny), gCost + 1.36f * c + h);
                        };
                        const bool s = !found && ortho(x, y + 1, 7);
                        const bool n = !found && ortho(x, y - 1, 6);
                        const bool w = !found && ortho(x - 1, y, 4);
                        diagonal(x - 1, y - 1, 8, w, n, hx[0]);
                        diagonal(x - 1, y + 1, 9, w, s, hx[0]);
                        const bool e = !found && ortho(x + 1, y, 5);
                        diagonal(x + 1, y - 1, 10, e, n, hx[1]);
                        diagonal(x + 1, y + 1, 11, e, s, hx[1]);
                    }
                    if (!found) continue;
                    // Backtrack from j, painting the brush.
                    std::vector<int> painted;
                    int x = dx, y = dy;
                    for (int guard = 0; guard < w_ * h_; ++guard) {
                        const int t = terrain_[at(x, y)];
                        const int size = t < 99 ? base.size[(size_t)t] : 1;
                        const int var = t < 99 ? base.variance[(size_t)t] : 0;
                        const int x0 = std::max(0, x - rng_.range(2 * var) - (size - var));
                        const int y0 = std::max(0, y - rng_.range(2 * var) - (size - var));
                        const int x1 = std::min(w_ - 1, x + rng_.range(2 * var) + (size - var));
                        const int y1 = std::min(h_ - 1, y + rng_.range(2 * var) + (size - var));
                        for (int py = y0; py <= y1; ++py)
                            for (int px = x0; px <= x1; ++px) painted.push_back((int)at(px, py));
                        const int code = g[at(x, y)];
                        g[at(x, y)] = 0xff;
                        if (code < 4 || code > 11) break;
                        x -= moveX[code];
                        y -= moveY[code];
                        if (!inMap(x, y)) break;
                    }
                    for (int index : painted) {
                        const int t = terrain_[(size_t)index];
                        if (t < 99 && base.replace[(size_t)t] > -1)
                            terrain_[(size_t)index] = (uint8_t)base.replace[(size_t)t];
                    }
                }
        }
        cleanup(1);
        shorePass();
    }
}

// --- OBJECTS (0x4ddd40) -----------------------------------------------------

int Generator::buildCandidates(int list, int x, int y, int r, const std::vector<uint8_t> &g) {
    int x0 = 0, y0 = 0, x1 = w_ - 1, y1 = h_ - 1;
    if (r >= 0 && x >= 0 && y >= 0) {
        x0 = std::max(0, x - r);
        y0 = std::max(0, y - r);
        x1 = std::min(w_ - 1, x + r);
        y1 = std::min(h_ - 1, y + r);
    }
    for (int py = y0; py <= y1; ++py)
        for (int px = x0; px <= x1; ++px)
            if (g[at(px, py)] != 0) lists_.append(list, lists_.node(px, py));
    const int moves = ((x1 - x0 - 1) * (y1 - y0 - 1)) / 4;
    for (int i = 0; i < moves; ++i) {
        const int px = x0 + rng_.range(x1 - x0 - 1);
        const int py = y0 + rng_.range(y1 - y0 - 1);
        if (inMap(px, py) && g[at(px, py)] != 0) lists_.pushFront(list, lists_.node(px, py));
    }
    return list;
}

namespace {
float half(float size) { return size - std::floor(size) != 0.0f ? 0.5f : 0.0f; }
} // namespace

void Generator::placeGroup(const ObjectRec &rec, int type, int player, int gx, int gy,
                           std::vector<uint8_t> &g) {
    const dat::Unit *unit = unitFor(player, type);
    if (!unit) return;
    const float ox = half(unit->collisionSize[0]), oy = half(unit->collisionSize[1]);
    int n = std::max(1, rec.perGroup + rng_.range(2 * rec.variance) - rec.variance);
    if (rec.grouping == 1) {
        const int list = lists_.newList();
        buildCandidates(list, gx, gy, rec.radius, g);
        while (n > 0) {
            const int node = lists_.pop(list);
            if (node < 0) break;
            const int x = lists_.x(node), y = lists_.y(node);
            if (rec.terrainOn >= 0 && terrain_[at(x, y)] != rec.terrainOn) continue;
            if (canPlace(*unit, player, x + ox, y + oy, true, true, false, true, 1) == 0) {
                createObject(type, player, x + ox, y + oy);
                n--;
            }
        }
        while (lists_.pop(list) >= 0) {
        }
        return;
    }
    // Tight (0x4def30): grows from the centre.
    const int list = lists_.newList();
    const int x0 = std::max(0, gx - rec.radius), x1 = std::min(w_ - 1, gx + rec.radius);
    const int y0 = std::max(0, gy - rec.radius), y1 = std::min(h_ - 1, gy + rec.radius);
    lists_.insert(list, lists_.node(gx, gy), 0.0f);
    while (n > 0) {
        const int node = lists_.pop(list);
        if (node < 0) break;
        const int x = lists_.x(node), y = lists_.y(node);
        const float fx = x + ox, fy = y + oy;
        createObject(type, player, fx, fy);
        n--;
        g[at(x, y)] = 0;
        const int nx[4] = {x - 1, x + 1, x, x}, ny[4] = {y, y, y - 1, y + 1};
        const float px[4] = {fx - 1, fx + 1, fx, fx}, py[4] = {fy, fy, fy - 1, fy + 1};
        const bool inside[4] = {x > x0, x < x1, y > y0, y < y1};
        for (int k = 0; k < 4; ++k)
            if (inside[k] && canPlace(*unit, player, px[k], py[k], true, true, false, true, 1) == 0)
                lists_.insert(list, lists_.node(nx[k], ny[k]), (float)rng_.next());
    }
    while (lists_.pop(list) >= 0) {
    }
}

void Generator::objectModule() {
    // Terrain objects (forests) for the whole map (0x495f80 -> 0x495020).
    for (int y = 0; y < h_; ++y)
        for (int x = 0; x < w_; ++x) {
            removeTerrainObjects(x, y, terrain_[at(x, y)]);
            addTerrainObjects(x, y, true);
        }
    std::vector<uint8_t> g((size_t)w_ * h_, 1);
    const int64_t area = (int64_t)w_ * h_;
    for (const ObjectRec &rec : objectRecs_) {
        lists_.reset(w_, h_);
        int groups = rec.grouping == 0 && !rec.groupsSet ? rec.perGroup : rec.groups;
        if (rec.scaleMap) groups = std::max<int>(1, (int)(groups * area / 10000));
        if (rec.scalePlayers) groups = std::max(1, groups * players_);
        const dat::Unit *baseUnit = unitFor(0, rec.type);
        if (!baseUnit) continue;
        if (baseUnit->cls == 6) continue; // walls: 0x4df5b0 (not reproduced)
        auto spaceOut = [&](int list, int x, int y, const dat::Unit &unit) {
            auto unlinkSquare = [&](int r) {
                if (r <= 0) return;
                for (int py = std::max(0, y - r); py <= std::min(h_ - 1, y + r); ++py)
                    for (int px = std::max(0, x - r); px <= std::min(w_ - 1, x + r); ++px) {
                        const int node = lists_.node(px, py);
                        if (lists_.owner(node) == list) lists_.unlink(node);
                    }
            };
            unlinkSquare(rec.minGroupDistance);
            unlinkSquare(rec.tempMinGroupDistance);
            const std::vector<float> *costs =
                unit.terrainRestriction >= 0 &&
                        (size_t)unit.terrainRestriction < dat_.terrainRestrictions.size()
                    ? &dat_.terrainRestrictions[(size_t)unit.terrainRestriction]
                           .passableBuildableDmgMultiplier
                    : nullptr;
            const int r = std::max(0, rec.minGroupDistance);
            for (int py = std::max(0, y - r); py <= std::min(h_ - 1, y + r); ++py)
                for (int px = std::max(0, x - r); px <= std::min(w_ - 1, x + r); ++px) {
                    const int t = terrain_[at(px, py)];
                    if (!costs || (size_t)t >= costs->size() || (*costs)[(size_t)t] > 0)
                        g[at(px, py)] = 0;
                }
        };
        auto zoneOk = [&](const std::vector<uint8_t> &zones, int x, int y) {
            const int r = rec.maxZoneDistance, r2 = r * 10 / 14;
            if (r <= 0 || r2 < 0) return true;
            auto zoneAt = [&](int px, int py) {
                px = std::max(0, std::min(w_ - 1, px));
                py = std::max(0, std::min(h_ - 1, py));
                return zones[at(px, py)];
            };
            const uint8_t z = zoneAt(x, y);
            const int pts[8][2] = {{x, y - r},       {x + r2, y - r2}, {x + r, y},
                                   {x + r2, y + r2}, {x, y + r},       {x - r2, y + r2},
                                   {x - r, y},       {x - r2, y - r2}};
            for (const auto &p : pts)
                if (zoneAt(p[0], p[1]) != z) return false;
            return true;
        };
        auto placeOne = [&](int type, int player, int x, int y, bool ungroupedWorker) {
            const dat::Unit *unit = unitFor(player, type);
            if (!unit) return;
            if (rec.grouping == 0) {
                int id = type;
                if (ungroupedWorker && rng_.next() > 0x3fff) id = 293;
                createObject(id, player, x + half(unit->collisionSize[0]),
                             y + half(unit->collisionSize[1]));
            } else {
                placeGroup(rec, type, player, x, y, g);
            }
        };
        if (rec.place == -1 || rec.place == -2) {
            const int player = 0;
            const int type = rec.matchCiv ? resolveCiv(rec.type, player) : rec.type;
            const dat::Unit *unit = unitFor(player, type);
            if (!unit) continue;
            const std::vector<uint8_t> zones = zoneMap(*unit, -1);
            const int list = lists_.newList();
            buildCandidates(list, -1, -1, -1, g);
            int remaining = groups;
            while (remaining > 0) {
                const int node = lists_.pop(list);
                if (node < 0) break;
                const int x = lists_.x(node), y = lists_.y(node);
                if (!zoneOk(zones, x, y)) continue;
                if (rec.terrainOn >= 0 && terrain_[at(x, y)] != rec.terrainOn) continue;
                if (rec.place == -2 && rec.minDistance > 0) {
                    bool close = false;
                    for (const ExtLand &land : extLands_)
                        close = close || (std::abs(x - land.x) < rec.minDistance &&
                                          std::abs(y - land.y) < rec.minDistance);
                    if (close) continue;
                }
                if (canPlace(*unit, player, x + half(unit->collisionSize[0]),
                             y + half(unit->collisionSize[1]), true, true, false, true, 1) != 0)
                    continue;
                spaceOut(list, x, y, *unit);
                placeOne(type, player, x, y, false);
                remaining--;
            }
            continue;
        }
        const int wantedId = rec.place == 1 ? 1 : rec.place;
        for (const ExtLand &land : extLands_) {
            if (land.landId != wantedId) continue;
            const int player = rec.gaiaOnly ? 0 : land.player;
            const int type = rec.matchCiv ? resolveCiv(rec.type, land.player) : rec.type;
            const dat::Unit *unit = unitFor(player, type);
            if (!unit || !inMap(land.x, land.y)) continue;
            int remaining = groups;
            if (type == 83 && remaining == 1 && land.player > 0 && land.player <= players_)
                remaining = (int)s_.players[(size_t)land.player - 1].startingWorkers;
            const std::vector<uint8_t> zones = zoneMap(*unit, terrain_[at(land.x, land.y)]);
            const uint8_t originZone = zones[at(land.x, land.y)];
            const int list = lists_.newList();
            buildCandidates(list, land.x, land.y, rec.maxDistance, g);
            const int min = rec.minDistance;
            const int lox = std::max(land.x - min, 0), hix = std::min(land.x + min, w_ - 1);
            const int loy = std::max(land.y - min, 0), hiy = std::min(land.y + min, h_ - 1);
            while (remaining > 0) {
                const int node = lists_.pop(list);
                if (node < 0) break;
                const int x = lists_.x(node), y = lists_.y(node);
                if (lox < x && x < hix && loy < y && y < hiy) continue;
                if (min > -1) {
                    bool close = false;
                    for (const ExtLand &other : extLands_)
                        close = close || (std::abs(x - other.x) < min && std::abs(y - other.y) < min);
                    if (close) continue;
                }
                if (zones[at(x, y)] != originZone) continue;
                if (!zoneOk(zones, x, y)) continue;
                if (rec.terrainOn >= 0 && terrain_[at(x, y)] != rec.terrainOn) continue;
                if (canPlace(*unit, player, x + half(unit->collisionSize[0]),
                             y + half(unit->collisionSize[1]), true, true, true, true, 1) == 0) {
                    spaceOut(list, x, y, *unit);
                    placeOne(type, player, x, y, type == 83);
                    remaining--;
                } else if (rec.grouping == 0 && lists_.empty(list) && unit->id != 50) {
                    placeOne(type, player, x, y, type == 83);
                }
            }
            while (lists_.pop(list) >= 0) {
            }
        }
    }
}

// --- Driver (0x5c9df0 / 0x4df9b0) -------------------------------------------

bool Generator::run(const std::string &script, RmsResult &result) {
    if (w_ <= 8 || h_ <= 8 || w_ > 512 || h_ > 512) {
        result.error = "invalid map size";
        return false;
    }
    const size_t n = (size_t)w_ * h_;
    terrain_.assign(n, 0);
    elevation_.assign(n, 0);
    slope_.assign(n, 0);
    tileObjects_.assign(n, {});
    rng_.state = (uint32_t)s_.seed;
    predefine();
    parse(script, "script");
    postParse();
    const bool landSeen = seenLand_;
    const bool terrainSeen = seenTerrain_ || landSeen;
    const bool objectsSeen = seenObjects_ || terrainSeen;
    rng_.next(); // "map started with random"
    if (landSeen) landModule();
    else std::fill(terrain_.begin(), terrain_.end(), (uint8_t)baseTerrain_);
    rng_.next();
    if (seenElevation_) {
        elevationModule();
        rng_.next();
    } else {
        recomputeSlopes();
    }
    if (seenCliff_) {
        cliffModule();
        rng_.next();
    }
    if (terrainSeen) {
        terrainModule();
        rng_.next();
    }
    if (seenConnection_) {
        connectionModule();
        rng_.next();
    }
    if (objectsSeen) {
        objectModule();
        rng_.next();
    }
    result.width = w_;
    result.height = h_;
    result.terrain = terrain_;
    result.elevation = elevation_;
    result.objects.clear();
    for (const RmsObject &object : objects_)
        if (object.alive) result.objects.push_back(object);
    result.starts.assign((size_t)players_ + 1, {-1.0f, -1.0f});
    for (const ExtLand &land : extLands_)
        if (land.player > 0 && land.player <= players_ &&
            result.starts[(size_t)land.player][0] < 0)
            result.starts[(size_t)land.player] = {land.x + 0.5f, land.y + 0.5f};
    return true;
}

} // namespace

int rmsScriptForMapType(int mapType) {
    switch (mapType) {
    case 9: return 54201;
    case 10: return 54202;
    case 11: return 54203;
    case 12: return 54204;
    case 13: return 54205;
    case 14: return 54206;
    case 15: return 54214;
    case 16: return 54207;
    case 17: return 54208;
    case 18: return 54209;
    case 19: return 54210;
    case 20: return 54211;
    case 21: return 54215;
    case 22: return 54212;
    case 23: return 54213;
    case 24: return 54216;
    case 25: return 54217;
    case 26: return 54218;
    case 27: return 54228;
    case 29: return 54219;
    case 30: return 54220;
    case 31: return 54222;
    case 32: return 54221;
    case 33: return 54223;
    case 35: return 54225;
    case 36: return 54226;
    case 37: return 54227;
    case 61: return 54229;
    default:
        if (mapType >= 38 && mapType <= 50) return 55001 + (mapType - 38);
        if (mapType >= 55 && mapType <= 60) return 55014 + (mapType - 55);
        return 0;
    }
}

int rmsMapWidth(int mapType, int sizeIndex) {
    static const int normal[6] = {120, 144, 168, 200, 220, 240};
    static const int bumped[6] = {144, 168, 200, 220, 240, 255};
    sizeIndex = std::max(0, std::min(5, sizeIndex));
    switch (mapType) {
    case 10: case 17: case 20: case 21: case 23: case 24: case 26: case 27:
        return bumped[sizeIndex];
    default:
        return normal[sizeIndex];
    }
}

const char *rmsMapName(int mapType) {
    switch (mapType) {
    case 9: return "Desert";
    case 10: return "Water Mass";
    case 11: return "Sea";
    case 12: return "Forest";
    case 13: return "Shoreline";
    case 14: return "Land Mass";
    case 15: return "Space Mass";
    case 16: return "Nova Lake";
    case 17: return "Fortress";
    case 18: return "Nova Assault";
    case 19: return "Precipice";
    case 20: return "Islands";
    case 21: return "Space Satellites";
    case 22: return "Large Sea";
    case 23: return "Asteroids";
    case 24: return "Forced Deployment";
    case 25: return "Rivers";
    case 26: return "Team Islands";
    case 27: return "Team Space Satellites";
    case 29: return "Tundra";
    case 30: return "Flats";
    case 31: return "Savannah";
    case 32: return "Swamp";
    case 33: return "Arena";
    case 35: return "Motherlode";
    case 36: return "Ice Lake";
    case 37: return "Raiders";
    case 38: return "Kashyyyk";
    case 39: return "Endor's Moon";
    case 40: return "Yavin IV";
    case 41: return "Hoth";
    case 42: return "Krant";
    case 43: return "Hanoon";
    case 44: return "Geddes";
    case 45: return "Kessel";
    case 46: return "Tatooine";
    case 47: return "Reytha";
    case 48: return "Zaloriis";
    case 49: return "Dagobah";
    case 50: return "Naboo";
    case 55: return "Mos Espa";
    case 56: return "Sarapin";
    case 57: return "Aereen";
    case 58: return "Eredenn";
    case 59: return "Geonosis";
    case 60: return "Tatooine (New)";
    case 61: return "Blind Random";
    default: return "";
    }
}

bool generateRandomMap(const dat::DatFile &dat, const RmsSettings &settings,
                       const std::string &script, const RmsLoader &loader,
                       RmsResult &result) {
    Generator generator(dat, settings, loader);
    return generator.run(script, result);
}

} // namespace swgb
