// layout.hpp - layout geometry (axis-aligned rectangles on layers) and rule decks.
#pragma once

#include <algorithm>
#include <cstdint>
#include <iosfwd>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace drc {

using Coord = int64_t;  // integer database units (think nanometres)

struct Rect {
    Coord x1, y1, x2, y2;  // x1 < x2, y1 < y2
    int layer;

    Coord width() const { return x2 - x1; }
    Coord height() const { return y2 - y1; }
    Coord min_dim() const { return std::min(width(), height()); }
};

// Gap between two rectangles along each axis (0 if they overlap on that axis).
inline Coord gap_x(const Rect& a, const Rect& b) { return std::max<Coord>({0, b.x1 - a.x2, a.x1 - b.x2}); }
inline Coord gap_y(const Rect& a, const Rect& b) { return std::max<Coord>({0, b.y1 - a.y2, a.y1 - b.y2}); }

// Squared Euclidean distance between the closest points of two rectangles.
// Integer arithmetic only: comparing dist2 < S*S avoids sqrt and rounding.
inline Coord dist2(const Rect& a, const Rect& b) {
    Coord dx = gap_x(a, b), dy = gap_y(a, b);
    return dx * dx + dy * dy;
}

// Rectangles that overlap or touch (even at a corner) are treated as one
// connected shape, so the spacing rule does not apply between them.
inline bool touching(const Rect& a, const Rect& b) { return gap_x(a, b) == 0 && gap_y(a, b) == 0; }

struct Layout {
    std::vector<std::string> layer_names;
    std::unordered_map<std::string, int> layer_index;
    std::vector<Rect> rects;
    std::vector<std::vector<int>> by_layer;  // layer -> rect ids

    int layer(const std::string& name);       // get or create
    int find_layer(const std::string& name) const;  // -1 if absent
    void add(const std::string& layer_name, Coord x1, Coord y1, Coord x2, Coord y2);
};

struct Rules {
    struct Width { int layer; Coord min; };
    struct Spacing { int layer; Coord min; };
    struct Enclosure { int inner; int outer; Coord min; };  // inner must sit inside outer by >= min
    std::vector<Width> width;
    std::vector<Spacing> spacing;
    std::vector<Enclosure> enclosure;
};

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Layout file: one rectangle per line, "LAYER x1 y1 x2 y2", '#' starts a comment.
Layout parse_layout(std::istream& in, const std::string& name = "<layout>");
Layout load_layout(const std::string& path);

// Rule file lines:  width M1 20 | spacing M1 30 | enclosure V1 M1 5
// Layers are created in the layout if the layout doesn't use them yet.
Rules parse_rules(std::istream& in, Layout& layout, const std::string& name = "<rules>");
Rules load_rules(const std::string& path, Layout& layout);

}  // namespace drc
