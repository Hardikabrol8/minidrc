// svg.hpp - render part of a layout and its violations as an SVG picture.
#pragma once

#include <iosfwd>
#include <vector>

#include "drc.hpp"

namespace drc {

struct Box {
    Coord x1, y1, x2, y2;
};

// A window around violation `index` (or the whole layout if there are none).
Box window_around(const Layout& L, const std::vector<Violation>& v, size_t index, Coord radius);

// Shapes intersecting the window are drawn semi-transparent, one colour per
// layer; violations in the window are outlined in red.
void write_svg(std::ostream& out, const Layout& L, const std::vector<Violation>& v, const Box& window);

}  // namespace drc
