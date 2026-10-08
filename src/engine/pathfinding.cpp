// SPDX-License-Identifier: GPL-3.0-or-later
#include "pathfinding.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace swgb {

static const int kDirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1},
                                {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};

int TilePathfinder::component(int x, int y) const {
    if (x < 0 || y < 0 || x >= size_ || y >= size_) return -1;
    const size_t cells = (size_t)size_ * size_;
    if (components_.size() != cells) {
        // Orthogonal steps suffice: a diagonal step needs both of its
        // orthogonal routes clear, so it never joins two areas on its own.
        components_.assign(cells, -1);
        std::vector<int32_t> queue;
        int32_t label = 0;
        for (size_t start = 0; start < cells; ++start) {
            if (components_[start] >= 0 || !passable_[start]) continue;
            queue.clear();
            queue.push_back((int32_t)start);
            components_[start] = label;
            for (size_t head = 0; head < queue.size(); ++head) {
                const int32_t index = queue[head];
                const int cx = index % size_, cy = index / size_;
                const uint8_t bits = moves_[(size_t)index];
                for (int d = 0; d < 4; ++d) {
                    if (!(bits & (1u << d))) continue;
                    const int32_t next = (cy + kDirs[d][1]) * size_ + cx + kDirs[d][0];
                    if (components_[(size_t)next] >= 0) continue;
                    components_[(size_t)next] = label;
                    queue.push_back(next);
                }
            }
            ++label;
        }
    }
    return components_[(size_t)y * size_ + x];
}

// Same bits as buildMovesReference(), read straight from edges_: an edge
// bit is only ever set between two passable tiles, so edge() reduces to the
// bit and passable() of a step's end tiles is implied by the edges tested.
void TilePathfinder::buildMoves() {
    moves_.assign((size_t)size_ * size_, 0);
    const int n = size_;
    auto east = [&](int x, int y) { // edge (x, y) -> (x + 1, y)
        return x >= 0 && y >= 0 && x < n && y < n && (edges_[(size_t)y * n + x] & 1);
    };
    auto south = [&](int x, int y) { // edge (x, y) -> (x, y + 1)
        return x >= 0 && y >= 0 && x < n && y < n && (edges_[(size_t)y * n + x] & 2);
    };
    auto horizontal = [&](int x, int y, int dx) { return dx > 0 ? east(x, y) : east(x - 1, y); };
    auto vertical = [&](int x, int y, int dy) { return dy > 0 ? south(x, y) : south(x, y - 1); };
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            uint8_t bits = 0;
            if (east(x, y)) bits |= 1;
            if (east(x - 1, y)) bits |= 2;
            if (south(x, y)) bits |= 4;
            if (south(x, y - 1)) bits |= 8;
            for (int d = 4; d < 8; d++) {
                const int dx = kDirs[d][0], dy = kDirs[d][1];
                if (horizontal(x, y, dx) && vertical(x + dx, y, dy) && vertical(x, y, dy) &&
                    horizontal(x, y + dy, dx))
                    bits |= (uint8_t)(1u << d);
            }
            moves_[(size_t)y * n + x] = bits;
        }
#ifdef SWGB_CHECK_MOVES
    std::vector<uint8_t> reference;
    buildMovesReference(reference);
    if (reference != moves_) __builtin_trap();
#endif
}

void TilePathfinder::buildMovesReference(std::vector<uint8_t> &moves) const {
    moves.assign((size_t)size_ * size_, 0);
    for (int y = 0; y < size_; y++)
        for (int x = 0; x < size_; x++) {
            uint8_t bits = 0;
            for (int d = 0; d < 8; d++) {
                const int dx = kDirs[d][0], dy = kDirs[d][1];
                if (!passable(x + dx, y + dy)) continue;
                const bool diagonal = dx != 0 && dy != 0;
                if (!diagonal) {
                    if (!edge(x, y, dx, dy)) continue;
                } else if (!passable(x + dx, y) || !passable(x, y + dy) ||
                           !edge(x, y, dx, 0) || !edge(x + dx, y, 0, dy) ||
                           !edge(x, y, 0, dy) || !edge(x, y + dy, dx, 0)) {
                    continue;
                }
                bits |= (uint8_t)(1u << d);
            }
            moves[(size_t)y * size_ + x] = bits;
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
    if (nodes_.size() != count) {
        nodes_.assign(count, NodeState{0.0f, -1, 0, 0});
        generation_ = 0;
    }
    if (++generation_ == 0) {
        for (NodeState &node : nodes_) node.stamp = 0;
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
    // Min-heap on f in a reused buffer (same push/pop order as a
    // std::priority_queue: both use std::push_heap/pop_heap). h is kept with
    // the entry so the pop does not recompute it.
    std::vector<OpenNode> &heap = open_;
    heap.clear();
    const auto heapLess = [](const OpenNode &a, const OpenNode &b) { return a.f > b.f; };
    struct OpenQueue {
        std::vector<OpenNode> &v;
        decltype(heapLess) less;
        bool empty() const { return v.empty(); }
        const OpenNode &top() const { return v.front(); }
        void push(OpenNode node) {
            v.push_back(node);
            std::push_heap(v.begin(), v.end(), less);
        }
        void pop() {
            std::pop_heap(v.begin(), v.end(), less);
            v.pop_back();
        }
    } open{heap, heapLess};
    auto visit = [&](int index, float g, int parent) {
        NodeState &node = nodes_[(size_t)index];
        node.stamp = generation_;
        node.cost = g;
        node.parent = parent;
        node.closed = 0;
    };
    const bool startCentreClear =
        startClear && passable(startX, startY) && startClear(startX, startY);
    visit(start, 0.0f, -1);
    open.push({heuristic(startX, startY), heuristic(startX, startY), start, (int16_t)startX,
               (int16_t)startY});
    int best = start;
    float bestH = heuristic(startX, startY);
    int found = -1;
    int expansions = 0;
    while (!open.empty() && expansions < maxExpansions) {
        const OpenNode current = open.top();
        open.pop();
        const int index = current.index;
        NodeState &state = nodes_[(size_t)index];
        if (state.stamp != generation_ || state.closed) continue;
        state.closed = 1;
        expansions++;
        const int cx = current.x, cy = current.y;
        const float h = current.h;
        if (h < bestH) {
            bestH = h;
            best = index;
        }
        if ((index == start || passable(cx, cy)) && isGoal(cx, cy)) {
            found = index;
            break;
        }
        const float g = state.cost;
        // Steps to try, in direction order: the precomputed moves, or for the
        // start tile the start rules (pure tests, so evaluating them before
        // visiting any neighbour changes nothing).
        unsigned candidates = moves_[(size_t)index];
        if (index == start && startClear) {
            candidates = 0;
            for (int dirIndex = 0; dirIndex < 8; dirIndex++) {
                const int *d = kDirs[dirIndex];
                const int nx = cx + d[0], ny = cy + d[1];
                const bool diagonal = d[0] != 0 && d[1] != 0;
                if (!passable(nx, ny)) continue;
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
                candidates |= 1u << dirIndex;
            }
        }
        for (; candidates; candidates &= candidates - 1) {
            const int dirIndex = __builtin_ctz(candidates);
            const int nx = cx + kDirs[dirIndex][0], ny = cy + kDirs[dirIndex][1];
            const int next = ny * size_ + nx;
            const float ng = g + (dirIndex >= 4 ? 1.41421356f : 1.0f);
            const NodeState &nextState = nodes_[(size_t)next];
            if (nextState.stamp == generation_ && (nextState.closed || ng >= nextState.cost))
                continue;
            visit(next, ng, index);
            const float nh = heuristic(nx, ny);
            open.push({ng + nh, nh, next, (int16_t)nx, (int16_t)ny});
        }
    }
    const int end = found >= 0 ? found : best;
    if (end == start) return found >= 0 ? PathResult::Complete : PathResult::Failed;
    std::vector<int> reverse;
    for (int node = end; node != start && node >= 0; node = nodes_[(size_t)node].parent)
        reverse.push_back(node);
    for (auto it = reverse.rbegin(); it != reverse.rend(); ++it)
        out.push_back({(*it % size_) + 0.5f, (*it / size_) + 0.5f});
    return found >= 0 ? PathResult::Complete : PathResult::Partial;
}

} // namespace swgb
