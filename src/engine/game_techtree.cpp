// SPDX-License-Identifier: GPL-3.0-or-later
// The Technology Tree screen's data (TribeTechHelpScreen 0x462060): the dat
// tech tree section filtered for one civilization and laid out by building
// columns (hardcoded order, 0x464f10) and Tech Level bands.
#include "game.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace swgb {
namespace {
constexpr int kBuildingIcons = 53241;
constexpr int kUnitIcons = 53251;
constexpr int kTechIcons = 53261;
// 0x464f10: left-to-right building columns.
constexpr int kBuildingOrder[] = {109, 621, 49,  317, 103, 209, 584, 323, 562, 84,  319, 68,
                                  50,  87,  12,  101, 276, 45,  199, 1576, 598, 79, 234, 235,
                                  236, 196, 72,  487, 117, 155, 195, 335, 104, 82,  70};

} // namespace

TechTreeData Game::techTreeData(int civilization) const {
    TechTreeData data;
    const dat::DatFile &dat = assets_.dat();
    const int player = localPlayer_ > 0 ? localPlayer_ : -1;
    data.playerCivilization = player > 0 ? civilizationForPlayer(player) : -1;
    if (civilization < 1) civilization = data.playerCivilization > 0 ? data.playerCivilization : 1;
    data.civilization = civilization;
    const bool own = civilization == data.playerCivilization && player > 0;
    for (size_t civ = 0; civ < dat.civs.size(); ++civ)
        data.civilizationNames.push_back(assets_.localizedString(10230 + (int)civ).empty()
                                             ? dat.civs[civ].name
                                             : assets_.localizedString(10230 + (int)civ));
    data.bonus = assets_.localizedString(20149 + civilization);
    auto playerResearched = [&](int p, int tech) {
        return p > 0 && (size_t)p < researchedTechs_.size() && researchedTechs_[(size_t)p].count(tech);
    };
    data.currentAge = 4;
    if (own) {
        // Tech Levels 2..4 are technologies 1..3.
        data.currentAge = 0;
        for (int level = 1; level <= 3; ++level)
            if (playerResearched(player, level)) data.currentAge = level;
    }
    if (civilization < 0 || (size_t)civilization >= dat.civs.size()) return data;
    const dat::Civ &civ = dat.civs[(size_t)civilization];
    const int iconSet = std::max<int>(1, civ.iconSet);
    // Technologies the civilization's tech tree effect disables.
    std::set<int> civDisabled;
    if (civ.techTreeId >= 0 && (size_t)civ.techTreeId < dat.effects.size())
        for (const dat::EffectCommand &command : dat.effects[(size_t)civ.techTreeId].commands)
            if (command.type == 102 && command.d >= 0) civDisabled.insert((int)std::lround(command.d));
    auto unitOf = [&](int id) -> const dat::Unit * {
        if (id < 0 || (size_t)id >= civ.units.size() || !civ.units[(size_t)id].exists) return nullptr;
        return &civ.units[(size_t)id];
    };
    // Units of another civilization are not shown (0x48baa0).
    auto civUnit = [&](int id) {
        const dat::Unit *unit = unitOf(id);
        return unit && (unit->civilization == 0 || unit->civilization == civilization);
    };
    auto techVisible = [&](int id) {
        if (id == 0 || id == 2 || id == 3 || id < 0 || (size_t)id >= dat.techs.size()) return false;
        const int techCiv = dat.techs[(size_t)id].civ;
        return techCiv <= 0 || techCiv == civilization;
    };
    auto unitStatus = [&](int id, int enabling) {
        if (enabling >= 0 && civDisabled.count(enabling)) return 3;
        if (!own) return 5;
        return unitAvailable(player, id) ? 5 : 2;
    };
    auto techStatus = [&](int id) {
        if (civDisabled.count(id)) return 3;
        if (!own) return 5;
        return playerResearched(player, id) ? 5 : 2;
    };
    auto unitName = [&](const dat::Unit &unit) {
        const std::string &name = assets_.localizedString(unit.languageDllName);
        return name.empty() ? unit.name : name;
    };
    auto helpText = [&](int nameId) {
        std::string text = assets_.localizedString(nameId + 21000);
        // Drop the "(<cost>" part of the help line.
        const size_t cost = text.find("(<cost>)");
        if (cost != std::string::npos) text.erase(cost, 8);
        else if (text.find("(<cost>") != std::string::npos) text.erase(text.find("(<cost>"), 7);
        // Drop the help markup (<b>, <i>, <br> ...).
        std::string plain;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '<') {
                const size_t close = text.find('>', i);
                if (close != std::string::npos && close - i <= 8) {
                    if (text.compare(i, 4, "<br>") == 0) plain += ' ';
                    i = close;
                    continue;
                }
            }
            plain += text[i];
        }
        return plain;
    };
    std::map<int, const dat::TechTreeBuilding *> buildings;
    for (const dat::TechTreeBuilding &building : dat.techTree.buildings)
        buildings[building.id] = &building;
    int column = 0;
    std::array<int, 4> rows{{1, 1, 1, 1}};
    for (int buildingId : kBuildingOrder) {
        const auto found = buildings.find(buildingId);
        if (found == buildings.end() || !civUnit(buildingId)) continue;
        const dat::TechTreeBuilding &building = *found->second;
        const dat::Unit *buildingUnit = unitOf(buildingId);
        // Items under this building, grouped into lines by vertical line.
        struct Item {
            int type, id, age, line, nameId, enabling;
        };
        std::vector<Item> items;
        for (const dat::TechTreeUnit &unit : dat.techTree.units)
            if (unit.upperBuilding == buildingId && civUnit(unit.id) && unitOf(unit.id))
                items.push_back({3, unit.id, std::max(1, unit.common.age()), unit.verticalLine,
                                 unitOf(unit.id)->languageDllName, unit.enablingResearch});
        for (const dat::TechTreeResearch &tech : dat.techTree.researches)
            if (tech.upperBuilding == buildingId && techVisible(tech.id))
                items.push_back({4, tech.id, std::max(1, tech.common.age()), tech.verticalLine,
                                 dat.techs[(size_t)tech.id].languageDllName, -1});
        std::map<int, std::vector<Item>> lines;
        for (const Item &item : items) lines[item.line].push_back(item);
        std::vector<std::vector<Item>> ordered;
        for (auto &entry : lines) {
            std::stable_sort(entry.second.begin(), entry.second.end(),
                             [](const Item &a, const Item &b) { return a.age < b.age; });
            ordered.push_back(entry.second);
        }
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const std::vector<Item> &a, const std::vector<Item> &b) {
                             return a.front().age < b.front().age;
                         });
        // Lines that start in later bands share columns with earlier ones
        // when they do not overlap.
        std::vector<std::array<int, 5>> columnUse; // rows used per band in each column
        const int buildingAge = std::max(1, std::min(4, building.common.age()));
        std::vector<int> lineColumn(ordered.size(), 0);
        for (size_t l = 0; l < ordered.size(); ++l) {
            std::array<int, 5> need{};
            for (const Item &item : ordered[l]) need[(size_t)item.age]++;
            size_t c = 0;
            for (; c < columnUse.size(); ++c) {
                bool fits = true;
                for (int age = 1; age <= 4 && fits; ++age)
                    if (need[(size_t)age] && columnUse[c][(size_t)age]) fits = false;
                if (fits) break;
            }
            if (c == columnUse.size()) {
                std::array<int, 5> fresh{};
                columnUse.push_back(fresh);
            }
            lineColumn[l] = (int)c;
            for (int age = 1; age <= 4; ++age) columnUse[c][(size_t)age] += need[(size_t)age];
        }
        const int width = std::max<int>(1, (int)columnUse.size());
        TechTreeNode node;
        node.type = 1;
        node.id = buildingId;
        node.column = column;
        node.width = width;
        node.age = buildingAge;
        node.row = 0;
        node.status = unitStatus(buildingId, building.enablingResearch);
        node.iconSlp = kBuildingIcons + iconSet - 1;
        node.iconFrame = buildingUnit ? buildingUnit->iconId : -1;
        node.name = buildingUnit ? unitName(*buildingUnit) : std::string();
        node.help = buildingUnit ? helpText(buildingUnit->languageDllName) : std::string();
        const int buildingIndex = (int)data.nodes.size();
        data.nodes.push_back(node);
        // The building heads its columns: their first row in its band is
        // taken.
        std::vector<std::array<int, 5>> nextRow(columnUse.size());
        for (size_t c = 0; c < nextRow.size(); ++c) {
            nextRow[c] = {};
            nextRow[c][(size_t)buildingAge] = 1;
        }
        for (size_t l = 0; l < ordered.size(); ++l) {
            int parent = buildingIndex;
            for (const Item &item : ordered[l]) {
                TechTreeNode child;
                child.type = item.type;
                child.id = item.id;
                child.column = column + lineColumn[l];
                child.age = std::max(1, std::min(4, item.age));
                child.row = nextRow[(size_t)lineColumn[l]][(size_t)child.age]++;
                rows[(size_t)child.age - 1] = std::max(rows[(size_t)child.age - 1], child.row + 1);
                child.parent = parent;
                if (item.type == 3) {
                    const dat::Unit *unit = unitOf(item.id);
                    child.status = unitStatus(item.id, item.enabling);
                    child.iconSlp = kUnitIcons + iconSet - 1;
                    child.iconFrame = unit ? unit->iconId : -1;
                    child.name = unit ? unitName(*unit) : std::string();
                } else {
                    const dat::Tech &tech = dat.techs[(size_t)item.id];
                    child.status = techStatus(item.id);
                    child.iconSlp = kTechIcons + iconSet - 1;
                    child.iconFrame = tech.iconId;
                    const std::string &name = assets_.localizedString(tech.languageDllName);
                    child.name = name.empty() ? tech.name : name;
                }
                child.help = helpText(item.nameId);
                parent = (int)data.nodes.size();
                data.nodes.push_back(child);
            }
        }
        column += width;
    }
    data.columns = column;
    data.rows = rows;
    return data;
}

} // namespace swgb
