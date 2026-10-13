// gen.hpp - synthetic layout generator (see gen.cpp for the structure).
#pragma once

#include <cstdint>
#include <iosfwd>

#include "layout.hpp"

namespace drc {

struct GenStats {
    size_t m1 = 0, m2 = 0, vias = 0;
    size_t injected_narrow = 0;      // wires thinner than the width rule
    size_t injected_shift = 0;       // M1 wires pushed 15 toward the next track
    size_t injected_end_to_end = 0;  // wire ends closer than the spacing rule
    size_t injected_offset_via = 0;  // vias shifted so one side's margin < 5
    size_t total() const { return m1 + m2 + vias; }
};

// Writes "LAYER x1 y1 x2 y2" lines for roughly target_rects shapes.
void generate(std::ostream& out, size_t target_rects, uint64_t seed, double defect_rate, GenStats& st);

}  // namespace drc
