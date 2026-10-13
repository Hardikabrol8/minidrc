#include "svg.hpp"

#include <algorithm>
#include <limits>
#include <ostream>

namespace drc {

namespace {

// The region that explains a violation: the gap between two shapes for spacing,
// the shape itself otherwise.
Box marker(const Layout& L, const Violation& v) {
    const Rect& a = L.rects[v.a];
    if (v.rule != RuleKind::Spacing) return {a.x1, a.y1, a.x2, a.y2};
    const Rect& b = L.rects[v.b];
    auto span = [](Coord a1, Coord a2, Coord b1, Coord b2) {
        Coord lo = std::max(a1, b1), hi = std::min(a2, b2);  // overlap of the projections...
        if (lo > hi) std::swap(lo, hi);                      // ...or the gap between them
        return std::pair<Coord, Coord>{lo, hi};
    };
    auto [x1, x2] = span(a.x1, a.x2, b.x1, b.x2);
    auto [y1, y2] = span(a.y1, a.y2, b.y1, b.y2);
    return {x1, y1, x2, y2};
}

bool intersects(const Box& w, Coord x1, Coord y1, Coord x2, Coord y2) {
    return x1 <= w.x2 && x2 >= w.x1 && y1 <= w.y2 && y2 >= w.y1;
}

}  // namespace

Box window_around(const Layout& L, const std::vector<Violation>& v, size_t index, Coord radius) {
    if (v.empty() || index >= v.size()) {
        Box b{std::numeric_limits<Coord>::max(), std::numeric_limits<Coord>::max(),
              std::numeric_limits<Coord>::min(), std::numeric_limits<Coord>::min()};
        for (const Rect& r : L.rects) {
            b.x1 = std::min(b.x1, r.x1); b.y1 = std::min(b.y1, r.y1);
            b.x2 = std::max(b.x2, r.x2); b.y2 = std::max(b.y2, r.y2);
        }
        if (L.rects.empty()) b = {0, 0, 100, 100};
        const Coord pad = std::max<Coord>(10, std::max(b.x2 - b.x1, b.y2 - b.y1) / 40);
        b = {b.x1 - pad, b.y1 - pad, b.x2 + pad, b.y2 + pad};
        b.x2 = std::min(b.x2, b.x1 + 2 * radius);  // keep huge layouts readable
        b.y2 = std::min(b.y2, b.y1 + 2 * radius);
        return b;
    }
    Box m = marker(L, v[index]);
    Coord cx = (m.x1 + m.x2) / 2, cy = (m.y1 + m.y2) / 2;
    return {cx - radius, cy - radius, cx + radius, cy + radius};
}

void write_svg(std::ostream& out, const Layout& L, const std::vector<Violation>& v, const Box& w) {
    static const char* colors[] = {"#1f77b4", "#ff7f0e", "#2ca02c", "#9467bd", "#8c564b", "#17becf"};
    const double W = 900.0;
    const double scale = W / static_cast<double>(std::max(w.x2 - w.x1, w.y2 - w.y1));
    auto X = [&](Coord x) { return (static_cast<double>(x - w.x1)) * scale; };
    auto Y = [&](Coord y) { return (static_cast<double>(w.y2 - y)) * scale; };  // SVG y points down
    const double h = static_cast<double>(w.y2 - w.y1) * scale;

    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << W << "\" height=\"" << h + 30
        << "\" font-family=\"monospace\" font-size=\"12\">\n<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n";
    for (size_t l = 0; l < L.layer_names.size(); ++l) {
        out << "<g fill=\"" << colors[l % 6] << "\" fill-opacity=\"0.45\">\n";
        for (int id : L.by_layer[l]) {
            const Rect& r = L.rects[id];
            if (!intersects(w, r.x1, r.y1, r.x2, r.y2)) continue;
            out << "<rect x=\"" << X(r.x1) << "\" y=\"" << Y(r.y2) << "\" width=\"" << (r.x2 - r.x1) * scale
                << "\" height=\"" << (r.y2 - r.y1) * scale << "\"/>\n";
        }
        out << "</g>\n";
    }
    out << "<g fill=\"none\" stroke=\"red\" stroke-width=\"2\">\n";
    for (const Violation& x : v) {
        Box m = marker(L, x);
        if (!intersects(w, m.x1, m.y1, m.x2, m.y2)) continue;
        const double pad = 3;
        out << "<rect x=\"" << X(m.x1) - pad << "\" y=\"" << Y(m.y2) - pad << "\" width=\""
            << (m.x2 - m.x1) * scale + 2 * pad << "\" height=\"" << (m.y2 - m.y1) * scale + 2 * pad
            << "\"><title>" << to_string(x.rule) << "</title></rect>\n";
    }
    out << "</g>\n";
    // Legend
    double lx = 8;
    for (size_t l = 0; l < L.layer_names.size(); ++l) {
        out << "<rect x=\"" << lx << "\" y=\"" << h + 10 << "\" width=\"12\" height=\"12\" fill=\"" << colors[l % 6]
            << "\" fill-opacity=\"0.45\"/><text x=\"" << lx + 16 << "\" y=\"" << h + 21 << "\">"
            << L.layer_names[l] << "</text>\n";
        lx += 70;
    }
    out << "<rect x=\"" << lx << "\" y=\"" << h + 10 << "\" width=\"12\" height=\"12\" fill=\"none\" stroke=\"red\" "
        << "stroke-width=\"2\"/><text x=\"" << lx + 16 << "\" y=\"" << h + 21 << "\">violation</text>\n</svg>\n";
}

}  // namespace drc
