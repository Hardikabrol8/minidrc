#include "drc.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <queue>
#include <set>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace drc {

int max_threads() {
#ifdef _OPENMP
    return omp_get_max_threads();
#else
    return 1;
#endif
}

const char* to_string(RuleKind k) {
    switch (k) {
        case RuleKind::Width: return "width";
        case RuleKind::Spacing: return "spacing";
        case RuleKind::Enclosure: return "enclosure";
    }
    return "?";
}

const char* to_string(Algo a) {
    switch (a) {
        case Algo::Naive: return "naive";
        case Algo::Grid: return "grid";
        case Algo::Sweep: return "sweep";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Grid

Grid::Grid(const std::vector<Rect>& rects, const std::vector<int>& ids, Coord cell) : cell_(std::max<Coord>(cell, 1)) {
    if (ids.empty()) {
        start_.assign(2, 0);
        return;
    }
    Coord X1 = std::numeric_limits<Coord>::max(), Y1 = X1, X2 = std::numeric_limits<Coord>::min(), Y2 = X2;
    for (int id : ids) {
        const Rect& r = rects[id];
        X1 = std::min(X1, r.x1); Y1 = std::min(Y1, r.y1);
        X2 = std::max(X2, r.x2); Y2 = std::max(Y2, r.y2);
    }
    x0_ = X1;
    y0_ = Y1;
    // Keep the number of cells proportional to the number of shapes so the
    // grid's memory stays O(n) even for sparse layouts.
    const size_t max_cells = std::max<size_t>(4 * ids.size(), 1024);
    for (;;) {
        Coord nx = (X2 - X1) / cell_ + 1, ny = (Y2 - Y1) / cell_ + 1;
        if (static_cast<double>(nx) * static_cast<double>(ny) <= static_cast<double>(max_cells)) {
            nx_ = static_cast<int>(nx);
            ny_ = static_cast<int>(ny);
            break;
        }
        cell_ *= 2;
    }

    // Two-pass CSR build: count entries per cell, prefix-sum, then fill.
    const size_t ncell = num_cells();
    start_.assign(ncell + 1, 0);
    auto for_cells = [&](const Rect& r, auto&& f) {
        for (int y = cy(r.y1); y <= cy(r.y2); ++y)
            for (int x = cx(r.x1); x <= cx(r.x2); ++x) f(static_cast<size_t>(y) * nx_ + x);
    };
    for (int id : ids) for_cells(rects[id], [&](size_t c) { ++start_[c + 1]; });
    for (size_t c = 0; c < ncell; ++c) start_[c + 1] += start_[c];
    items_.resize(start_[ncell]);
    std::vector<uint32_t> fill(start_.begin(), start_.end() - 1);
    for (size_t i = 0; i < ids.size(); ++i)
        for_cells(rects[ids[i]], [&](size_t c) { items_[fill[c]++] = static_cast<int>(i); });
}

Coord auto_cell_size(const std::vector<Rect>& rects, const std::vector<int>& ids, Coord rule) {
    if (ids.empty()) return std::max<Coord>(rule, 1);
    // Geometric mean of the average width and height. For square-ish shapes this
    // is just their size; for long thin wires (20 x 1000) it lands in between,
    // balancing "cells per shape" against "shapes per cell" (see README).
    double sw = 0, sh = 0;
    for (int id : ids) { sw += static_cast<double>(rects[id].width()); sh += static_cast<double>(rects[id].height()); }
    const double n = static_cast<double>(ids.size());
    const auto cell = static_cast<Coord>(std::sqrt((sw / n) * (sh / n)));
    return std::max<Coord>(cell, std::max<Coord>(rule, 1));
}

// ---------------------------------------------------------------------------
// Width

std::vector<Violation> check_width(const Layout& L, const Rules::Width& r) {
    std::vector<Violation> out;
    for (int id : L.by_layer[r.layer]) {
        Coord w = L.rects[id].min_dim();
        if (w < r.min) out.push_back({RuleKind::Width, r.layer, id, -1, static_cast<double>(w), r.min});
    }
    return out;
}

// ---------------------------------------------------------------------------
// Spacing

namespace {

// Exact test for one candidate pair. Returns true and fills v on a violation.
inline bool spacing_pair(const Layout& L, int ia, int ib, Coord S, int layer, Violation& v) {
    const Rect &a = L.rects[ia], &b = L.rects[ib];
    const Coord dx = gap_x(a, b), dy = gap_y(a, b);
    if (dx >= S || dy >= S) return false;    // outside the bloated box: can't violate
    if (dx == 0 && dy == 0) return false;    // touching/overlapping: same shape
    const Coord d2 = dx * dx + dy * dy;      // corner-to-corner uses true Euclidean distance
    if (d2 >= S * S) return false;
    v = {RuleKind::Spacing, layer, std::min(ia, ib), std::max(ia, ib), std::sqrt(static_cast<double>(d2)), S};
    return true;
}

std::vector<Violation> spacing_naive(const Layout& L, const std::vector<int>& ids, Coord S, int layer,
                                     uint64_t& checks) {
    std::vector<Violation> out;
    Violation v;
    for (size_t i = 0; i < ids.size(); ++i)
        for (size_t j = i + 1; j < ids.size(); ++j) {
            ++checks;
            if (spacing_pair(L, ids[i], ids[j], S, layer, v)) out.push_back(v);
        }
    return out;
}

std::vector<Violation> spacing_grid(const Layout& L, const std::vector<int>& ids, Coord S, int layer,
                                    const Options& opt, uint64_t& checks) {
    const Coord cell = opt.cell > 0 ? opt.cell : auto_cell_size(L.rects, ids, S);
    Grid grid(L.rects, ids, cell);
    const int64_t n = static_cast<int64_t>(ids.size());
    std::vector<Violation> out;
    uint64_t total_checks = 0;
    (void)opt;

#pragma omp parallel num_threads(opt.threads) reduction(+ : total_checks)
    {
        std::vector<Violation> local;
        std::vector<int64_t> seen(ids.size(), -1);  // per-thread "already checked against i" stamps
        Violation v;
#pragma omp for schedule(dynamic, 256)
        for (int64_t i = 0; i < n; ++i) {
            const Rect& a = L.rects[ids[i]];
            // Bloat by S: anything closer than S must be listed in one of these cells.
            grid.query(a.x1 - S, a.y1 - S, a.x2 + S, a.y2 + S, [&](int j) {
                if (j <= i || seen[j] == i) return;  // each pair once, each candidate once
                seen[j] = i;
                ++total_checks;
                if (spacing_pair(L, ids[i], ids[j], S, layer, v)) local.push_back(v);
            });
        }
#pragma omp critical
        out.insert(out.end(), local.begin(), local.end());
    }
    checks += total_checks;
    return out;
}

std::vector<Violation> spacing_sweep(const Layout& L, const std::vector<int>& ids, Coord S, int layer,
                                     uint64_t& checks) {
    // Sweep along the long axis of the shapes: for a layer of mostly vertical
    // wires, swap x and y so the bounded-height query below stays narrow.
    double sum_w = 0, sum_h = 0;
    for (int id : ids) { sum_w += static_cast<double>(L.rects[id].width()); sum_h += static_cast<double>(L.rects[id].height()); }
    const bool transpose = sum_h > sum_w;
    std::vector<Rect> R;
    R.reserve(ids.size());
    Coord hmax = 0;
    for (int id : ids) {
        Rect r = L.rects[id];
        if (transpose) r = Rect{r.y1, r.x1, r.y2, r.x2, r.layer};
        hmax = std::max(hmax, r.height());
        R.push_back(r);
    }
    std::vector<int> order(ids.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return R[a].x1 < R[b].x1; });

    // active: shapes whose right edge is within S of the sweep line, keyed by y1.
    std::set<std::pair<Coord, int>> active;
    std::vector<std::set<std::pair<Coord, int>>::iterator> where(ids.size());
    using Exit = std::pair<Coord, int>;  // (x2, index), min-heap
    std::priority_queue<Exit, std::vector<Exit>, std::greater<Exit>> exits;

    std::vector<Violation> out;
    Violation v;
    for (int i : order) {
        const Rect& a = R[i];
        while (!exits.empty() && exits.top().first + S <= a.x1) {  // gap in x >= S from now on
            active.erase(where[exits.top().second]);
            exits.pop();
        }
        // A candidate b needs |gap in y| < S. Since no shape is taller than hmax,
        // b.y1 must lie in [a.y1 - hmax - S + 1, a.y2 + S - 1].
        auto it = active.lower_bound({a.y1 - hmax - S + 1, std::numeric_limits<int>::min()});
        for (; it != active.end() && it->first <= a.y2 + S - 1; ++it) {
            ++checks;
            if (spacing_pair(L, ids[i], ids[it->second], S, layer, v)) out.push_back(v);
        }
        where[i] = active.insert({a.y1, i}).first;
        exits.push({a.x2, i});
    }
    return out;
}

}  // namespace

std::vector<Violation> check_spacing(const Layout& L, const Rules::Spacing& r, const Options& opt,
                                     uint64_t* candidate_pairs) {
    const auto& ids = L.by_layer[r.layer];
    uint64_t checks = 0;
    std::vector<Violation> out;
    switch (opt.algo) {
        case Algo::Naive: out = spacing_naive(L, ids, r.min, r.layer, checks); break;
        case Algo::Grid: out = spacing_grid(L, ids, r.min, r.layer, opt, checks); break;
        case Algo::Sweep: out = spacing_sweep(L, ids, r.min, r.layer, checks); break;
    }
    if (candidate_pairs) *candidate_pairs += checks;
    return out;
}

// ---------------------------------------------------------------------------
// Enclosure: every inner shape (via) must sit inside one outer shape (metal)
// with at least `min` margin on all four sides.

std::vector<Violation> check_enclosure(const Layout& L, const Rules::Enclosure& r, const Options& opt) {
    const auto& inner = L.by_layer[r.inner];
    const auto& outer = L.by_layer[r.outer];
    std::vector<Violation> out;
    if (inner.empty()) return out;

    const bool naive = opt.algo == Algo::Naive;
    Grid grid(L.rects, naive ? std::vector<int>{} : outer, auto_cell_size(L.rects, outer, r.min + 1));
    const int64_t n = static_cast<int64_t>(inner.size());

#pragma omp parallel num_threads(opt.threads)
    {
        std::vector<Violation> local;
#pragma omp for schedule(dynamic, 256)
        for (int64_t i = 0; i < n; ++i) {
            const Rect& via = L.rects[inner[i]];
            Coord best = std::numeric_limits<Coord>::min();
            int best_id = -1;
            auto consider = [&](int k) {
                const Rect& m = L.rects[outer[k]];
                if (gap_x(via, m) > 0 || gap_y(via, m) > 0) return;  // no contact at all
                const Coord margin = std::min({via.x1 - m.x1, via.y1 - m.y1, m.x2 - via.x2, m.y2 - via.y2});
                if (margin > best) { best = margin; best_id = outer[k]; }
            };
            if (naive) for (size_t k = 0; k < outer.size(); ++k) consider(static_cast<int>(k));
            else grid.query(via.x1, via.y1, via.x2, via.y2, consider);
            if (best < r.min)
                local.push_back({RuleKind::Enclosure, r.inner, inner[i], best_id,
                                 best_id < 0 ? 0.0 : static_cast<double>(best), r.min});
        }
#pragma omp critical
        out.insert(out.end(), local.begin(), local.end());
    }
    return out;
}

// ---------------------------------------------------------------------------

std::vector<Violation> run_drc(const Layout& L, const Rules& R, const Options& opt, Stats* stats) {
    using clk = std::chrono::steady_clock;
    auto secs = [](clk::time_point t0) { return std::chrono::duration<double>(clk::now() - t0).count(); };
    std::vector<Violation> all;
    Stats s;

    auto t0 = clk::now();
    for (const auto& r : R.width) {
        auto v = check_width(L, r);
        all.insert(all.end(), v.begin(), v.end());
    }
    s.width_s = secs(t0);

    t0 = clk::now();
    for (const auto& r : R.spacing) {
        auto v = check_spacing(L, r, opt, &s.candidate_pairs);
        all.insert(all.end(), v.begin(), v.end());
    }
    s.spacing_s = secs(t0);

    t0 = clk::now();
    for (const auto& r : R.enclosure) {
        auto v = check_enclosure(L, r, opt);
        all.insert(all.end(), v.begin(), v.end());
    }
    s.enclosure_s = secs(t0);

    std::sort(all.begin(), all.end());
    if (stats) *stats = s;
    return all;
}

std::string describe(const Layout& L, const Violation& v) {
    char buf[256];
    const Rect& a = L.rects[v.a];
    const std::string& layer = L.layer_names[v.layer];
    switch (v.rule) {
        case RuleKind::Width:
            std::snprintf(buf, sizeof buf, "width     %-4s rect #%d (%lld,%lld)-(%lld,%lld): %.0f < %lld",
                          layer.c_str(), v.a, (long long)a.x1, (long long)a.y1, (long long)a.x2, (long long)a.y2,
                          v.measured, (long long)v.required);
            break;
        case RuleKind::Spacing:
            std::snprintf(buf, sizeof buf, "spacing   %-4s rects #%d and #%d: %.2f < %lld", layer.c_str(), v.a, v.b,
                          v.measured, (long long)v.required);
            break;
        case RuleKind::Enclosure:
            if (v.b < 0)
                std::snprintf(buf, sizeof buf, "enclosure %-4s rect #%d (%lld,%lld): not covered by any shape",
                              layer.c_str(), v.a, (long long)a.x1, (long long)a.y1);
            else
                std::snprintf(buf, sizeof buf, "enclosure %-4s rect #%d in %s #%d: margin %.0f < %lld", layer.c_str(),
                              v.a, L.layer_names[L.rects[v.b].layer].c_str(), v.b, v.measured, (long long)v.required);
            break;
    }
    return buf;
}

}  // namespace drc
