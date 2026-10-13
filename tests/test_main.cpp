// Self-contained tests (no framework dependency). Run: ./minidrc_tests
#include <cstdio>
#include <functional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "drc.hpp"
#include "gen.hpp"
#include "layout.hpp"

using namespace drc;

static int g_failed = 0, g_checks = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_checks;                                                       \
        if (!(cond)) {                                                    \
            ++g_failed;                                                   \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                 \
    } while (0)

template <class E>
static bool throws(const std::function<void()>& f) {
    try { f(); } catch (const E&) { return true; } catch (...) { return false; }
    return false;
}

static Layout layout_of(const std::string& text) {
    std::istringstream in(text);
    return parse_layout(in);
}
static Rules rules_of(const std::string& text, Layout& L) {
    std::istringstream in(text);
    return parse_rules(in, L);
}
static size_t count(const std::vector<Violation>& v, RuleKind k) {
    size_t c = 0;
    for (const auto& x : v) c += x.rule == k;
    return c;
}

static const Algo kAlgos[] = {Algo::Naive, Algo::Grid, Algo::Sweep};

static std::vector<Violation> run(const Layout& L, const Rules& R, Algo a, int threads = 1) {
    Options o;
    o.algo = a;
    o.threads = threads;
    return run_drc(L, R, o);
}

static void test_geometry() {
    Rect a{0, 0, 10, 10, 0}, b{13, 14, 20, 20, 0};
    CHECK(gap_x(a, b) == 3 && gap_y(a, b) == 4 && dist2(a, b) == 25);  // 3-4-5 triangle
    Rect c{10, 0, 20, 10, 0};
    CHECK(touching(a, c));  // shared edge
    Rect d{10, 10, 20, 20, 0};
    CHECK(touching(a, d));  // shared corner only
}

static void test_spacing_edges() {
    // Exactly at the rule is legal; one unit closer is a violation.
    for (Algo algo : kAlgos) {
        Layout L = layout_of("M1 0 0 100 20\nM1 0 50 100 70\n");  // gap 30
        Rules R = rules_of("spacing M1 30\n", L);
        CHECK(run(L, R, algo).empty());
        Layout L2 = layout_of("M1 0 0 100 20\nM1 0 49 100 69\n");  // gap 29
        Rules R2 = rules_of("spacing M1 30\n", L2);
        auto v = run(L2, R2, algo);
        CHECK(v.size() == 1 && v[0].measured == 29.0);
    }
}

static void test_euclidean_corner() {
    for (Algo algo : kAlgos) {
        // dx = dy = 25: Chebyshev distance 25 < 30 but Euclidean 35.36 >= 30 -> legal.
        Layout L = layout_of("M1 0 0 100 20\nM1 125 45 225 65\n");
        Rules R = rules_of("spacing M1 30\n", L);
        CHECK(run(L, R, algo).empty());
        // dx = dy = 20: Euclidean 28.28 < 30 -> violation.
        Layout L2 = layout_of("M1 0 0 100 20\nM1 120 40 220 60\n");
        Rules R2 = rules_of("spacing M1 30\n", L2);
        CHECK(run(L2, R2, algo).size() == 1);
    }
}

static void test_touching_and_layers() {
    for (Algo algo : kAlgos) {
        // Overlapping, edge-touching and corner-touching shapes form one polygon.
        Layout L = layout_of("M1 0 0 100 20\nM1 50 0 150 20\nM1 150 0 200 50\nM1 200 50 260 70\n");
        Rules R = rules_of("spacing M1 30\n", L);
        CHECK(run(L, R, algo).empty());
        // Close shapes on different layers don't interact.
        Layout L2 = layout_of("M1 0 0 100 20\nM2 0 25 100 45\n");
        Rules R2 = rules_of("spacing M1 30\nspacing M2 30\n", L2);
        CHECK(run(L2, R2, algo).empty());
    }
}

static void test_width_and_enclosure() {
    Layout L = layout_of(
        "M1 0 0 400 20\n"        // 0: ok
        "M1 0 100 400 119\n"     // 1: width 19 < 20
        "M2 100 -50 120 120\n"   // 2
        "V1 105 5 115 15\n"      // 3: enclosed by 5 in both metals -> ok
        "V1 306 5 316 15\n"      // 4: no M2 there at all -> not covered (M2 rule)
        "V1 10 4 20 14\n");      // 5: M1 margin 4 at the bottom -> violation (M1 rule)
    Rules R = rules_of("width M1 20\nenclosure V1 M1 5\nenclosure V1 M2 5\n", L);
    for (Algo algo : {Algo::Naive, Algo::Grid}) {
        auto v = run(L, R, algo);
        CHECK(count(v, RuleKind::Width) == 1);
        CHECK(count(v, RuleKind::Enclosure) == 3);  // via 4: no M2; via 5: M1 margin 4, and no M2
        bool not_covered = false, margin4 = false;
        for (const auto& x : v) {
            if (x.rule == RuleKind::Enclosure && x.a == 4 && x.b < 0) not_covered = true;
            if (x.rule == RuleKind::Enclosure && x.a == 5 && x.b == 0 && x.measured == 4.0) margin4 = true;
        }
        CHECK(not_covered && margin4);
    }
}

static void test_small_example() {
    Layout L = load_layout(std::string(EXAMPLES_DIR) + "/small.txt");
    Rules R = load_rules(std::string(EXAMPLES_DIR) + "/rules.txt", L);
    for (Algo algo : kAlgos) {
        auto v = run(L, R, algo);
        CHECK(v.size() == 3);
        CHECK(count(v, RuleKind::Width) == 1);
        CHECK(count(v, RuleKind::Spacing) == 1);
        CHECK(count(v, RuleKind::Enclosure) == 1);
    }
}

static void test_parse_errors() {
    CHECK(throws<ParseError>([] { layout_of("M1 0 0 10\n"); }));        // missing coordinate
    CHECK(throws<ParseError>([] { layout_of("M1 10 0 0 10\n"); }));     // x1 > x2
    CHECK(throws<ParseError>([] { layout_of("M1 0 0 10 10 7\n"); }));   // extra field
    Layout L = layout_of("M1 0 0 10 10 # comment\n\n");
    CHECK(L.rects.size() == 1);
    CHECK(throws<ParseError>([&] { rules_of("spacing M1\n", L); }));
    CHECK(throws<ParseError>([&] { rules_of("area M1 5\n", L); }));
    CHECK(throws<ParseError>([&] { rules_of("width M1 -3\n", L); }));
}

// Random layouts: the three spacing algorithms (and 1 vs many threads) must
// report exactly the same violations. Mixes thin wires, tall wires, big blocks
// and clusters so all code paths (including the sweep transpose) get exercised.
static void test_random_agreement() {
    std::mt19937_64 rng(12345);
    auto uni = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    for (int trial = 0; trial < 40; ++trial) {
        std::ostringstream os;
        const int n = uni(10, 1500), side = uni(200, 6000);
        for (int i = 0; i < n; ++i) {
            int kind = uni(0, 3), x = uni(0, side), y = uni(0, side), w, h;
            if (kind == 0) { w = uni(20, 400); h = uni(10, 30); }        // horizontal wire
            else if (kind == 1) { w = uni(10, 30); h = uni(20, 400); }   // vertical wire
            else if (kind == 2) { w = uni(1, 60); h = uni(1, 60); }      // small shape
            else { w = uni(100, 900); h = uni(100, 900); }                // big block
            os << (uni(0, 4) == 0 ? "M2 " : "M1 ") << x << ' ' << y << ' ' << x + w << ' ' << y + h << '\n';
        }
        Layout L = layout_of(os.str());
        Rules R = rules_of("width M1 12\nspacing M1 " + std::to_string(uni(5, 60)) + "\nspacing M2 25\n", L);
        auto vn = run(L, R, Algo::Naive);
        auto vg = run(L, R, Algo::Grid, 1);
        auto vgt = run(L, R, Algo::Grid, 4);
        auto vs = run(L, R, Algo::Sweep);
        CHECK(vn == vg);
        CHECK(vn == vgt);
        CHECK(vn == vs);
        // Grid result must not depend on the cell size, even a silly one.
        Options o;
        o.cell = uni(1, 2000);
        CHECK(run_drc(L, R, o) == vn);
    }
}

static void test_generated_layout() {
    std::ostringstream os;
    GenStats st;
    generate(os, 20000, 7, 0.0, st);  // no injected defects
    std::istringstream in(os.str());
    Layout clean = parse_layout(in);
    Rules R = load_rules(std::string(EXAMPLES_DIR) + "/rules.txt", clean);
    CHECK(clean.rects.size() == st.total());
    CHECK(st.vias > 0);
    CHECK(run(clean, R, Algo::Grid).empty());  // a defect-free layout is clean

    std::ostringstream os2;
    generate(os2, 20000, 7, 0.01, st);
    std::istringstream in2(os2.str());
    Layout dirty = parse_layout(in2);
    Rules R2 = load_rules(std::string(EXAMPLES_DIR) + "/rules.txt", dirty);
    auto v = run(dirty, R2, Algo::Grid);
    CHECK(count(v, RuleKind::Width) == st.injected_narrow);  // each thinned wire is one width error
    CHECK(count(v, RuleKind::Spacing) >= st.injected_end_to_end);
    CHECK(count(v, RuleKind::Enclosure) >= st.injected_offset_via);
    CHECK(run(dirty, R2, Algo::Sweep) == v);
}

int main() {
    struct T { const char* name; void (*fn)(); };
    const T tests[] = {
        {"geometry helpers", test_geometry},
        {"spacing at the rule boundary", test_spacing_edges},
        {"euclidean corner-to-corner", test_euclidean_corner},
        {"touching shapes and layer isolation", test_touching_and_layers},
        {"width and enclosure", test_width_and_enclosure},
        {"examples/small.txt", test_small_example},
        {"parse errors", test_parse_errors},
        {"naive == grid == sweep on random layouts", test_random_agreement},
        {"generated layouts", test_generated_layout},
    };
    for (const auto& t : tests) {
        int before = g_failed;
        t.fn();
        std::printf("%s %s\n", g_failed == before ? "[pass]" : "[FAIL]", t.name);
    }
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
