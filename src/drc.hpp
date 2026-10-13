// drc.hpp - design-rule checks: minimum width, minimum spacing, via enclosure.
//
// Spacing is the expensive check: naively every pair of shapes on a layer is a
// candidate, O(n^2). Two ways to only look at nearby pairs:
//   * Grid  - bucket rectangles into uniform cells (a spatial hash); a shape only
//             needs to be compared with shapes in cells within S of it.
//   * Sweep - sweep a vertical line left to right, keeping only shapes within S
//             of the line in an "active" set ordered by y.
// Both find candidates with a cheap box test ("bloat by S and intersect") and
// then confirm with the exact Euclidean distance.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "layout.hpp"

namespace drc {

enum class RuleKind : uint8_t { Width, Spacing, Enclosure };
const char* to_string(RuleKind k);

struct Violation {
    RuleKind rule;
    int layer;        // layer the rule is about (inner layer for enclosure)
    int a;            // offending rectangle
    int b;            // spacing: the other rectangle; enclosure: best partial cover (-1 = none)
    double measured;  // width / distance / best enclosure margin
    Coord required;

    bool operator<(const Violation& o) const {
        if (rule != o.rule) return rule < o.rule;
        if (a != o.a) return a < o.a;
        return b < o.b;
    }
    bool operator==(const Violation& o) const { return rule == o.rule && a == o.a && b == o.b; }
};

// Uniform grid over a subset of rectangles. Each rectangle is listed in every
// cell it overlaps; cell contents are stored CSR-style (one flat array plus
// offsets) so a query touches contiguous memory.
class Grid {
public:
    Grid(const std::vector<Rect>& rects, const std::vector<int>& ids, Coord cell);

    // Calls f(local_index) for every rectangle listed in a cell that overlaps the
    // query box. A rectangle spanning several cells can be reported more than once.
    template <class F>
    void query(Coord x1, Coord y1, Coord x2, Coord y2, F&& f) const {
        if (items_.empty()) return;
        const int cx1 = cx(x1), cx2 = cx(x2), cy1 = cy(y1), cy2 = cy(y2);
        for (int y = cy1; y <= cy2; ++y)
            for (int x = cx1; x <= cx2; ++x) {
                const size_t c = static_cast<size_t>(y) * nx_ + x;
                for (uint32_t k = start_[c]; k < start_[c + 1]; ++k) f(items_[k]);
            }
    }

    Coord cell() const { return cell_; }
    size_t num_cells() const { return static_cast<size_t>(nx_) * ny_; }
    size_t num_entries() const { return items_.size(); }

private:
    int cx(Coord x) const { return clampi((x - x0_) / cell_, nx_); }
    int cy(Coord y) const { return clampi((y - y0_) / cell_, ny_); }
    static int clampi(Coord v, int n) { return static_cast<int>(v < 0 ? 0 : (v >= n ? n - 1 : v)); }

    Coord x0_ = 0, y0_ = 0, cell_ = 1;
    int nx_ = 1, ny_ = 1;
    std::vector<uint32_t> start_;  // cell c holds items_[start_[c] .. start_[c+1])
    std::vector<int> items_;       // local indices into the ids vector
};

// Default cell size: sqrt(mean width * mean height) of the shapes, but never
// smaller than the rule distance.
Coord auto_cell_size(const std::vector<Rect>& rects, const std::vector<int>& ids, Coord rule);

enum class Algo { Naive, Grid, Sweep };
const char* to_string(Algo a);

struct Options {
    Algo algo = Algo::Grid;
    int threads = 1;
    Coord cell = 0;  // grid cell size, 0 = auto
};

struct Stats {
    double width_s = 0, spacing_s = 0, enclosure_s = 0;
    uint64_t candidate_pairs = 0;  // exact distance checks performed by the spacing check
};

std::vector<Violation> check_width(const Layout& L, const Rules::Width& r);
std::vector<Violation> check_spacing(const Layout& L, const Rules::Spacing& r, const Options& opt,
                                     uint64_t* candidate_pairs = nullptr);
std::vector<Violation> check_enclosure(const Layout& L, const Rules::Enclosure& r, const Options& opt);

// All rules; result sorted (rule, a, b) so different algorithms can be compared.
std::vector<Violation> run_drc(const Layout& L, const Rules& R, const Options& opt, Stats* stats = nullptr);

std::string describe(const Layout& L, const Violation& v);

int max_threads();

}  // namespace drc
