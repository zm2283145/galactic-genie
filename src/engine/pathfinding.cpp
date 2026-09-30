// SPDX-License-Identifier: GPL-3.0-or-later
#include "pathfinding.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace swgb {

void TilePathfinder::build(int size, const std::function<bool(int, int)> &passable,
                           const std::function<bool(int, int, int)> &edgeClear) {
    size_ = std::max(0, size);
    passable_.assign((size_t)size_ * size_, 0);
    edges_.assign((size_t)size_ * size_, 0);
    for (int y = 0; y < size_; y++)
        for (int x = 0; x < size_; x++)
            passable_[(size_t)y * size_ + x] = passable(x, y) ? 1 : 0;
    for (int y = 0; y < size_; y++)
        for (int x = 0; x < size_; x++) {
            if (!passable_[(size_t)y * size_ + x]) continue;
            uint8_t bits = 0;
            if (this->passable(x + 1, y) && (!edgeClear || edgeClear(x, y, 0)))
                bits |= 1;
            if (this->passable(x, y + 1) && (!edgeClear || edgeClear(x, y, 1)))
                bits |= 2;
            edges_[(size_t)y * size_ + x] = bits;
        }
}

// Cardinal step (dx, dy) out of tile (x, y).
bool TilePathfinder::edge(int x, int y, int dx, int dy) const {
    if (dx == 1) return passable(x, y) && (edges_[(size_t)y * size_ + x] & 1);
    if (dx == -1) return passable(x - 1, y) && (edges_[(size_t)y * size_ + x - 1] & 1);
    if (dy == 1) return passable(x, y) && (edges_[(size_t)y * size_ + x] & 2);
    if (dy == -1) return passable(x, y - 1) && (edges_[(size_t)(y - 1) * size_ + x] & 2);
    return false;
}

static float footprintDistance(float cx, float cy, const PathGoal &goal) {
    const float dx = std::max(0.0f, std::abs(cx - goal.x) - goal.halfX);
    const float dy = std::max(0.0f, std::abs(cy - goal.y) - goal.halfY);
    return std::sqrt(dx * dx + dy * dy);
}

PathResult TilePathfinder::find(float sx, float sy, const PathGoal &goal,
                                std::vector<std::array<float, 2>> &out,
                                int maxExpansions,
                                const std::function<bool(int, int)> &startClear) {
    out.clear();
    if (size_ <= 0) return PathResult::Failed;
    const size_t count = (size_t)size_ * size_;
    if (cost_.size() != count) {
        cost_.assign(count, 0.0f);
        parent_.assign(count, -1);
        stamp_.assign(count, 0);
        closed_.assign(count, 0);
        generation_ = 0;
    }
    if (++generation_ == 0) {
        std::fill(stamp_.begin(), stamp_.end(), 0);
        generation_ = 1;
    }
    auto clampTile = [this](float v) {
        return std::max(0, std::min(size_ - 1, (int)std::floor(v)));
    };
    const int startX = clampTile(sx), startY = clampTile(sy);
    const int goalTileX = clampTile(goal.x), goalTileY = clampTile(goal.y);
    const int start = startY * size_ + startX;
    // Tiles whose centre is within range + half a tile of the footprint are
    // goal tiles (the original adds 0.5 to the range for the same reason).
    const float goalReach = goal.range + 0.5f;
    auto isGoal = [&](int x, int y) {
        return footprintDistance(x + 0.5f, y + 0.5f, goal) <= goalReach;
    };
    auto heuristic = [&](int x, int y) {
        const float dx = (float)(goalTileX - x), dy = (float)(goalTileY - y);
        return std::sqrt(dx * dx + dy * dy);
    };
    struct Open {
        float f;
        int index;
        bool operator<(const Open &o) const { return f > o.f; }
    };
    std::priority_queue<Open> open;
    auto visit = [&](int index, float g, int parent) {
        stamp_[(size_t)index] = generation_;
        cost_[(size_t)index] = g;
        parent_[(size_t)index] = parent;
        closed_[(size_t)index] = 0;
    };
    const bool startCentreClear =
        startClear && passable(startX, startY) && startClear(startX, startY);
    visit(start, 0.0f, -1);
    open.push({heuristic(startX, startY), start});
    int best = start;
    float bestH = heuristic(startX, startY);
    int found = -1;
    int expansions = 0;
    static const int dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1},
                                   {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
    while (!open.empty() && expansions < maxExpansions) {
        const Open current = open.top();
        open.pop();
        const int index = current.index;
        if (stamp_[(size_t)index] != generation_ || closed_[(size_t)index]) continue;
        closed_[(size_t)index] = 1;
        expansions++;
        const int cx = index % size_, cy = index / size_;
        const float h = heuristic(cx, cy);
        if (h < bestH) {
            bestH = h;
            best = index;
        }
        if ((index == start || passable(cx, cy)) && isGoal(cx, cy)) {
            found = index;
            break;
        }
        const float g = cost_[(size_t)index];
        for (const auto &d : dirs) {
            const int nx = cx + d[0], ny = cy + d[1];
            if (!passable(nx, ny)) continue;
            const bool diagonal = d[0] != 0 && d[1] != 0;
            if (index == start && startClear) {
                // The unit is somewhere inside its tile: test the real step,
                // or a step via its own tile centre (the caller inserts that
                // centre as a waypoint when the direct step is blocked).
                const bool viaCentre =
                    startCentreClear &&
                    (diagonal ? (passable(cx + d[0], cy) && passable(cx, cy + d[1]) &&
                                 edge(cx, cy, d[0], 0) && edge(cx + d[0], cy, 0, d[1]) &&
                                 edge(cx, cy, 0, d[1]) && edge(cx, cy + d[1], d[0], 0))
                              : edge(cx, cy, d[0], d[1]));
                if (!viaCentre && !startClear(nx, ny)) continue;
            } else if (!diagonal) {
                if (!edge(cx, cy, d[0], d[1])) continue;
            } else if (!passable(cx + d[0], cy) || !passable(cx, cy + d[1]) ||
                       !edge(cx, cy, d[0], 0) || !edge(cx + d[0], cy, 0, d[1]) ||
                       !edge(cx, cy, 0, d[1]) || !edge(cx, cy + d[1], d[0], 0)) {
                continue; // no corner cutting
            }
            const int next = ny * size_ + nx;
            const float ng = g + (diagonal ? 1.41421356f : 1.0f);
            if (stamp_[(size_t)next] == generation_ &&
                (closed_[(size_t)next] || ng >= cost_[(size_t)next]))
                continue;
            visit(next, ng, index);
            open.push({ng + heuristic(nx, ny), next});
        }
    }
    const int end = found >= 0 ? found : best;
    if (end == start) return found >= 0 ? PathResult::Complete : PathResult::Failed;
    std::vector<int> reverse;
    for (int node = end; node != start && node >= 0; node = parent_[(size_t)node])
        reverse.push_back(node);
    for (auto it = reverse.rbegin(); it != reverse.rend(); ++it)
        out.push_back({(*it % size_) + 0.5f, (*it / size_) + 0.5f});
    return found >= 0 ? PathResult::Complete : PathResult::Partial;
}

} // namespace swgb
