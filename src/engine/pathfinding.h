// SPDX-License-Identifier: GPL-3.0-or-later
// Tile pathfinder modelled on the original Genie long-range pathfinder
// (battlegrounds_x1.exe 0x4982f0 / 0x498820 / 0x4989f0 / 0x499010):
//
//  * A* over map tiles. Step costs are the Euclidean distance between tile
//    centres and the heuristic is the Euclidean distance to the target tile.
//  * The goal is a *region*: every passable tile whose centre lies within
//    `range` of the target footprint (a rectangle of half-size halfX/halfY).
//    This is what lets units attack, gather or build from wherever they can
//    get close enough, instead of fighting for one exact point.
//  * If no goal tile is reachable the search returns the path to the explored
//    tile that got closest to the target (the original keeps that node in
//    its "best" slot and reports a partial path).
//
// The original searches a 4-level quadtree of 1/2/4/8-tile blocks and emits
// waypoints where the straight line to the next block crosses a block edge.
// Here the same result is produced with a flat tile search followed by
// line-of-sight smoothing, which is simpler and gives identical routes on
// open ground.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace swgb {

struct PathGoal {
    float x = 0, y = 0;         // target centre (world tiles)
    float halfX = 0, halfY = 0; // target footprint half size (0 for a point)
    float range = 0;            // accepted distance from the footprint
};

enum class PathResult : uint8_t { Failed, Complete, Partial };

class TilePathfinder {
public:
    // passable(x, y) for 0 <= x,y < size. edgeClear(x, y, dir) says whether
    // a unit can travel between the centres of (x, y) and its east (dir 0)
    // or south (dir 1) neighbour; this is the original's per-cell
    // edge-passable bits, which catch footprints that are not tile aligned.
    void build(int size, const std::function<bool(int, int)> &passable,
               const std::function<bool(int, int, int)> &edgeClear = {});
    int size() const { return size_; }
    bool passable(int x, int y) const {
        return x >= 0 && y >= 0 && x < size_ && y < size_ &&
               passable_[(size_t)y * size_ + x] != 0;
    }

    // Finds a tile route from (sx, sy) to the goal region. Waypoints are tile
    // centres, start excluded. maxExpansions bounds the work per search.
    // startClear(x, y) validates the first step from the unit's actual
    // position (which is rarely a tile centre) to a neighbouring tile.
    PathResult find(float sx, float sy, const PathGoal &goal,
                    std::vector<std::array<float, 2>> &out,
                    int maxExpansions = 20000,
                    const std::function<bool(int, int)> &startClear = {});

private:
    int size_ = 0;
    std::vector<uint8_t> passable_;
    std::vector<uint8_t> edges_; // bit0: east clear, bit1: south clear
    bool edge(int x, int y, int dx, int dy) const;
    // Search scratch, reused between searches.
    std::vector<float> cost_;
    std::vector<int32_t> parent_;
    std::vector<uint32_t> stamp_;
    std::vector<uint8_t> closed_;
    uint32_t generation_ = 0;
};

} // namespace swgb
