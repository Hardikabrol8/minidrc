#include "layout.hpp"

#include <cctype>
#include <fstream>
#include <sstream>

namespace drc {

int Layout::layer(const std::string& name) {
    auto [it, inserted] = layer_index.try_emplace(name, static_cast<int>(layer_names.size()));
    if (inserted) {
        layer_names.push_back(name);
        by_layer.emplace_back();
    }
    return it->second;
}

int Layout::find_layer(const std::string& name) const {
    auto it = layer_index.find(name);
    return it == layer_index.end() ? -1 : it->second;
}

void Layout::add(const std::string& layer_name, Coord x1, Coord y1, Coord x2, Coord y2) {
    int l = layer(layer_name);
    by_layer[l].push_back(static_cast<int>(rects.size()));
    rects.push_back(Rect{x1, y1, x2, y2, l});
}

namespace {
std::string strip_comment(const std::string& s) { return s.substr(0, s.find('#')); }

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

Layout parse_layout(std::istream& in, const std::string& name) {
    Layout L;
    std::string raw;
    int line = 0;
    while (std::getline(in, raw)) {
        ++line;
        std::istringstream ss(strip_comment(raw));
        std::string layer;
        if (!(ss >> layer)) continue;  // blank line
        Coord x1, y1, x2, y2;
        std::string extra;
        if (!(ss >> x1 >> y1 >> x2 >> y2) || (ss >> extra))
            throw ParseError(name + ":" + std::to_string(line) + ": expected 'LAYER x1 y1 x2 y2'");
        if (x1 >= x2 || y1 >= y2)
            throw ParseError(name + ":" + std::to_string(line) + ": empty rectangle (need x1 < x2 and y1 < y2)");
        L.add(layer, x1, y1, x2, y2);
    }
    return L;
}

Layout load_layout(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw ParseError("cannot open '" + path + "'");
    return parse_layout(f, path);
}

Rules parse_rules(std::istream& in, Layout& L, const std::string& name) {
    Rules R;
    std::string raw;
    int line = 0;
    while (std::getline(in, raw)) {
        ++line;
        std::istringstream ss(strip_comment(raw));
        std::string kind;
        if (!(ss >> kind)) continue;
        auto fail = [&](const std::string& msg) {
            return ParseError(name + ":" + std::to_string(line) + ": " + msg);
        };
        kind = lower(kind);
        std::string a, b, extra;
        Coord v;
        if (kind == "width" || kind == "spacing") {
            if (!(ss >> a >> v) || (ss >> extra) || v <= 0)
                throw fail("expected '" + kind + " LAYER value' with value > 0");
            if (kind == "width") R.width.push_back({L.layer(a), v});
            else R.spacing.push_back({L.layer(a), v});
        } else if (kind == "enclosure") {
            if (!(ss >> a >> b >> v) || (ss >> extra) || v < 0)
                throw fail("expected 'enclosure INNER OUTER value' with value >= 0");
            R.enclosure.push_back({L.layer(a), L.layer(b), v});
        } else {
            throw fail("unknown rule '" + kind + "' (use width, spacing or enclosure)");
        }
    }
    return R;
}

Rules load_rules(const std::string& path, Layout& L) {
    std::ifstream f(path);
    if (!f) throw ParseError("cannot open '" + path + "'");
    return parse_rules(f, L, path);
}

}  // namespace drc
