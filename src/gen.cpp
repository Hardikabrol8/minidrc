// gen.cpp - synthetic two-metal routing layout with injected rule violations.
//
// Structure (all numbers in database units):
//   M1: horizontal wires, width 20, on tracks every 50 (so 30 of space) in y
//   M2: vertical wires, same pitch, in x
//   V1: 10x10 vias where an M1 and an M2 wire cross, centred so both metals
//       enclose the via by exactly 5 on the narrow sides
// A clean layout satisfies examples/rules.txt (width 20, spacing 30, enclosure 5).
// A small fraction of shapes is then damaged on purpose: thinned wires, wires
// pushed toward the next track, wire ends pulled too close, and off-centre vias.
#include "gen.hpp"

#include <algorithm>
#include <cmath>
#include <ostream>
#include <random>

namespace drc {

namespace {
constexpr Coord kWidth = 20, kPitch = 50, kVia = 10, kEnc = 5, kSpace = 30;
}

void generate(std::ostream& out, size_t target_rects, uint64_t seed, double defect_rate, GenStats& st) {
    st = GenStats{};
    std::mt19937_64 rng(seed);
    auto uni = [&](Coord lo, Coord hi) { return std::uniform_int_distribution<Coord>(lo, hi)(rng); };
    auto chance = [&](double p) { return std::uniform_real_distribution<double>(0, 1)(rng) < p; };

    // About 1.25 shapes per wire (vias included), wires average ~1215 units of
    // track each (length + gap) -> choose the square side for the target count.
    const Coord side = std::max<Coord>(
        2000, static_cast<Coord>(std::sqrt(static_cast<double>(target_rects) * 24300.0)));
    const int tracks = static_cast<int>(side / kPitch);

    struct Seg { Coord lo, hi; };
    // Lays wires along one track: random lengths, gaps >= spacing (sometimes not).
    auto lay_track = [&](std::vector<Seg>& segs) {
        Coord pos = uni(0, 300);
        while (pos < side) {
            Coord len = uni(100, 2000);
            segs.push_back({pos, std::min(pos + len, side + 100)});
            Coord gap = uni(kSpace, 300);
            if (chance(defect_rate)) { gap = uni(5, kSpace - 5); ++st.injected_end_to_end; }
            pos = segs.back().hi + gap;
        }
    };

    // M2 first (vertical), kept per track so vias can find a crossing wire.
    std::vector<std::vector<Seg>> m2(tracks);
    for (int j = 0; j < tracks; ++j) {
        lay_track(m2[j]);
        for (const Seg& s : m2[j]) {
            const Coord x = j * kPitch;
            out << "M2 " << x << ' ' << s.lo << ' ' << x + kWidth << ' ' << s.hi << '\n';
            ++st.m2;
        }
    }

    // M2 wire on track j that covers [y, y + kWidth] with enclosure margin, if any.
    auto m2_covers = [&](int j, Coord y) {
        const auto& segs = m2[j];
        auto it = std::upper_bound(segs.begin(), segs.end(), y, [](Coord v, const Seg& s) { return v < s.lo; });
        if (it == segs.begin()) return false;
        --it;
        return it->lo <= y && it->hi >= y + kWidth;
    };

    for (int i = 0; i < tracks; ++i) {
        std::vector<Seg> segs;
        lay_track(segs);
        const Coord y = i * kPitch;
        for (const Seg& s : segs) {
            Coord y1 = y, y2 = y + kWidth;
            bool damaged = false;
            if (chance(defect_rate)) { y2 -= uni(3, 8); damaged = true; ++st.injected_narrow; }
            else if (chance(defect_rate)) { y1 += 15; y2 += 15; damaged = true; ++st.injected_shift; }
            out << "M1 " << s.lo << ' ' << y1 << ' ' << s.hi << ' ' << y2 << '\n';
            ++st.m1;
            if (damaged) continue;  // keep the damage isolated: no vias on damaged wires

            // Up to two vias at crossings with an M2 wire.
            for (int k = 0; k < 2; ++k) {
                if (!chance(0.35)) continue;
                const Coord lo = (s.lo + kPitch - 1) / kPitch, hi = (s.hi - kWidth) / kPitch;
                if (lo > hi) break;
                const int j = static_cast<int>(uni(lo, hi));
                if (j >= tracks || !m2_covers(j, y)) continue;
                Coord vx = j * kPitch + kEnc, vy = y + kEnc;
                if (chance(defect_rate * 5)) { vx += uni(2, 4); ++st.injected_offset_via; }
                out << "V1 " << vx << ' ' << vy << ' ' << vx + kVia << ' ' << vy + kVia << '\n';
                ++st.vias;
            }
        }
    }
}

}  // namespace drc
