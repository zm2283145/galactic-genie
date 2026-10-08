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
    // Templated so the per-tile callbacks inline (a grid build calls them
    // about 3 * size^2 times).
    template <class Passable, class EdgeClear>
    void build(int size, const Passable &passable, const EdgeClear &edgeClear) {
        size_ = size > 0 ? size : 0;
        passable_.assign((size_t)size_ * size_, 0);
        edges_.assign((size_t)size_ * size_, 0);
        for (int y = 0; y < size_; y++)
            for (int x = 0; x < size_; x++)
                passable_[(size_t)y * size_ + x] = passable(x, y) ? 1 : 0;
        for (int y = 0; y < size_; y++)
            for (int x = 0; x < size_; x++) {
                if (!passable_[(size_t)y * size_ + x]) continue;
                uint8_t bits = 0;
                if (this->passable(x + 1, y) && edgeClear(x, y, 0)) bits |= 1;
                if (this->passable(x, y + 1) && edgeClear(x, y, 1)) bits |= 2;
                edges_[(size_t)y * size_ + x] = bits;
            }
        buildMoves();
        components_.clear();
    }
    // Recomputes passable_ for the tiles in [minX, maxX] x [minY, maxY], the
    // edges into and inside that rectangle, and the moves around it, with the
    // same callbacks build() takes. The rest of the grid is kept: callers pass
    // every tile whose answers can have changed.
    template <class Passable, class EdgeClear>
    void rebuildRegion(int minX, int minY, int maxX, int maxY, const Passable &passable,
                       const EdgeClear &edgeClear) {
        if (size_ <= 0) return;
        minX = minX < 0 ? 0 : minX;
        minY = minY < 0 ? 0 : minY;
        maxX = maxX >= size_ ? size_ - 1 : maxX;
        maxY = maxY >= size_ ? size_ - 1 : maxY;
        if (minX > maxX || minY > maxY) return;
        for (int y = minY; y <= maxY; y++)
            for (int x = minX; x <= maxX; x++)
                passable_[(size_t)y * size_ + x] = passable(x, y) ? 1 : 0;
        // A tile's east/south edge depends on it and that neighbour, so the
        // tiles left of and above the rectangle change too.
        for (int y = minY > 0 ? minY - 1 : 0; y <= maxY; y++)
            for (int x = minX > 0 ? minX - 1 : 0; x <= maxX; x++) {
                uint8_t bits = 0;
                if (passable_[(size_t)y * size_ + x]) {
                    if (this->passable(x + 1, y) && edgeClear(x, y, 0)) bits |= 1;
                    if (this->passable(x, y + 1) && edgeClear(x, y, 1)) bits |= 2;
                }
                edges_[(size_t)y * size_ + x] = bits;
            }
        buildMoves(minX - 2, minY - 2, maxX + 2, maxY + 2);
        components_.clear();
    }
    bool sameGridForTesting(const TilePathfinder &other) const {
        return size_ == other.size_ && passable_ == other.passable_ && edges_ == other.edges_ &&
               moves_ == other.moves_;
    }
    void build(int size, const std::function<bool(int, int)> &passable) {
        build(size, passable, [](int, int, int) { return true; });
    }
    int size() const { return size_; }
    bool passable(int x, int y) const {
        return x >= 0 && y >= 0 && x < size_ && y < size_ &&
               passable_[(size_t)y * size_ + x] != 0;
    }

    // Connected area of a tile (tiles joined by find()'s steps share it), or
    // -1 for a blocked or outside tile. Labelled on first use per grid: a
    // cheap "can a unit at A ever walk to B" test before a path search.
    int component(int x, int y) const;

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
    // Per tile, bit d set when find() may step in direction kDirs[d] from it
    // (target passable, edge clear, no corner cutting); precomputed by
    // build() from passable_/edges_ exactly as find() used to test per step.
    std::vector<uint8_t> moves_;
    mutable std::vector<int32_t> components_;
    bool edge(int x, int y, int dx, int dy) const;
    void buildMoves();
    void buildMoves(int minX, int minY, int maxX, int maxY);
    void buildMovesReference(std::vector<uint8_t> &moves) const;
    // Search scratch, reused between searches.
    // Per-tile search state in one record (one cache line per visited tile).
    struct NodeState {
        float cost;
        int32_t parent;
        uint32_t stamp;
        uint8_t closed;
    };
    std::vector<NodeState> nodes_;
    struct OpenNode {
        float f;
        float h;
        int index;
        int16_t x, y; // index % size_, index / size_ (no divide per pop)
    };
    std::vector<OpenNode> open_;
    uint32_t generation_ = 0;
};

} // namespace swgb
