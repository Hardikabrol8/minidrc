// minidrc - a small layout design-rule checker.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "drc.hpp"
#include "gen.hpp"
#include "layout.hpp"
#include "svg.hpp"

using namespace drc;

namespace {

void usage() {
    std::puts(
        "usage:\n"
        "  minidrc check <layout.txt> <rules.txt> [--algo grid|sweep|naive] [--threads T] [--cell C]\n"
        "                [--show K] [--svg out.svg] [--svg-at I | --svg-full] [--svg-radius R]\n"
        "  minidrc bench <layout.txt> <rules.txt> [--threads T] [--naive-limit N] [--markdown]\n"
        "  minidrc gen   --rects N [--seed S] [--defects P] [--out layout.txt]\n");
}

struct Args {
    std::vector<std::string> pos;
    std::map<std::string, std::string> opt;
    bool has(const std::string& k) const { return opt.count(k) > 0; }
    double num(const std::string& k, double def) const {
        auto it = opt.find(k);
        return it == opt.end() ? def : std::stod(it->second);
    }
    std::string str(const std::string& k, const std::string& def) const {
        auto it = opt.find(k);
        return it == opt.end() ? def : it->second;
    }
};

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 2; i < argc; ++i) {
        std::string s = argv[i];
        if (s.rfind("--", 0) == 0) {
            if (s == "--markdown" || s == "--svg-full") a.opt[s] = "1";
            else if (i + 1 < argc) a.opt[s] = argv[++i];
            else throw std::invalid_argument("missing value for " + s);
        } else {
            a.pos.push_back(s);
        }
    }
    return a;
}

Algo parse_algo(const std::string& s) {
    if (s == "grid") return Algo::Grid;
    if (s == "sweep") return Algo::Sweep;
    if (s == "naive") return Algo::Naive;
    throw std::invalid_argument("unknown --algo '" + s + "' (grid, sweep, naive)");
}

double now_s() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

int cmd_check(const Args& a) {
    double t0 = now_s();
    Layout L = load_layout(a.pos.at(0));
    Rules R = load_rules(a.pos.at(1), L);
    double t_load = now_s() - t0;

    Options opt;
    opt.algo = parse_algo(a.str("--algo", "grid"));
    opt.threads = static_cast<int>(a.num("--threads", max_threads()));
    opt.cell = static_cast<Coord>(a.num("--cell", 0));
    Stats st;
    auto v = run_drc(L, R, opt, &st);

    std::map<std::string, size_t> count;
    for (const auto& x : v) count[std::string(to_string(x.rule)) + " " + L.layer_names[x.layer]]++;
    std::printf("layout: %zu shapes on %zu layers (loaded in %.2f s)\n", L.rects.size(), L.layer_names.size(), t_load);
    std::printf("algorithm: %s, threads: %d\n", to_string(opt.algo), opt.threads);
    std::printf("time: width %.3f s, spacing %.3f s (%llu exact distance checks), enclosure %.3f s\n", st.width_s,
                st.spacing_s, static_cast<unsigned long long>(st.candidate_pairs), st.enclosure_s);
    std::printf("violations: %zu\n", v.size());
    for (auto& [k, c] : count) std::printf("  %-16s %zu\n", k.c_str(), c);

    size_t show = static_cast<size_t>(a.num("--show", 10));
    for (size_t i = 0; i < v.size() && i < show; ++i) std::printf("  %s\n", describe(L, v[i]).c_str());
    if (v.size() > show) std::printf("  ... (%zu more, use --show)\n", v.size() - show);

    if (a.has("--svg")) {
        // --svg-full draws the whole layout (capped at 2*radius per side), otherwise
        // a window centred on violation number --svg-at.
        size_t at = a.has("--svg-full") ? v.size() : static_cast<size_t>(a.num("--svg-at", 0));
        Coord radius = static_cast<Coord>(a.num("--svg-radius", 300));
        std::ofstream f(a.str("--svg", ""));
        write_svg(f, L, v, window_around(L, v, at, radius));
        std::printf("wrote %s\n", a.str("--svg", "").c_str());
    }
    return v.empty() ? 0 : 2;
}

int cmd_bench(const Args& a) {
    Layout L = load_layout(a.pos.at(0));
    Rules R = load_rules(a.pos.at(1), L);
    const int T = static_cast<int>(a.num("--threads", max_threads()));
    const size_t naive_limit = static_cast<size_t>(a.num("--naive-limit", 50000));

    auto run = [&](Algo algo, int threads, Stats& st) {
        Options o;
        o.algo = algo;
        o.threads = threads;
        return run_drc(L, R, o, &st);
    };
    Stats sg1, sgT, ssw, snv;
    auto vg1 = run(Algo::Grid, 1, sg1);
    auto vgT = run(Algo::Grid, T, sgT);
    auto vsw = run(Algo::Sweep, 1, ssw);
    bool ok = vg1 == vgT && vg1 == vsw;

    // The naive check is O(n^2): run it for real on small layouts, otherwise
    // time it on a random sample of each layer and scale by (n / m)^2.
    size_t largest = 0;
    for (const auto& r : R.spacing) largest = std::max(largest, L.by_layer[r.layer].size());
    bool naive_real = largest <= naive_limit;
    double naive_s = 0;
    uint64_t naive_pairs = 0;
    if (naive_real) {
        auto vn = run(Algo::Naive, 1, snv);
        ok = ok && vn == vg1;
        naive_s = snv.spacing_s;
        naive_pairs = snv.candidate_pairs;
    } else {
        for (const auto& r : R.spacing) {
            const auto& ids = L.by_layer[r.layer];
            const size_t n = ids.size(), m = std::min(n, naive_limit);
            Layout sub;  // first m shapes of this layer
            for (size_t i = 0; i < m; ++i) {
                const Rect& x = L.rects[ids[i]];
                sub.add(L.layer_names[r.layer], x.x1, x.y1, x.x2, x.y2);
            }
            Options o;
            o.algo = Algo::Naive;
            double t0 = now_s();
            uint64_t pairs = 0;
            check_spacing(sub, {0, r.min}, o, &pairs);
            const double f = static_cast<double>(n) / static_cast<double>(m);
            naive_s += (now_s() - t0) * f * f;
            naive_pairs += static_cast<uint64_t>(static_cast<double>(n) * static_cast<double>(n - 1) / 2);
        }
    }

    const double g1 = sg1.spacing_s, gT = sgT.spacing_s, sw = ssw.spacing_s;
    if (a.has("--markdown")) {
        std::printf("| %zu | %zu | %.2f s%s | %.3f s | %.3f s | %.3f s | %.0fx |\n", L.rects.size(), vg1.size(),
                    naive_s, naive_real ? "" : " (est.)", sw, g1, gT, naive_s / std::min({g1, gT, sw}));
        return ok ? 0 : 1;
    }
    std::printf("layout: %zu shapes, %zu violations\n", L.rects.size(), vg1.size());
    std::printf("spacing check (the O(n^2) part):\n");
    std::printf("  %-22s %10s %18s %10s\n", "algorithm", "time", "distance checks", "speedup");
    auto row = [&](const char* name, double s, uint64_t pairs, const char* note) {
        std::printf("  %-22s %9.3fs %18llu %9.0fx %s\n", name, s, static_cast<unsigned long long>(pairs),
                    naive_s / s, note);
    };
    row("naive all-pairs", naive_s, naive_pairs, naive_real ? "" : "(extrapolated from a sample)");
    row("sweep line", sw, ssw.candidate_pairs, "");
    row("grid, 1 thread", g1, sg1.candidate_pairs, "");
    if (T > 1) {
        std::string gname = "grid, " + std::to_string(T) + " threads";
        row(gname.c_str(), gT, sgT.candidate_pairs, "");
    }
    std::printf("enclosure check: %.3f s (grid, %d thread%s)\n", sgT.enclosure_s, T, T > 1 ? "s" : "");
    std::printf("all algorithms report identical violations: %s\n", ok ? "yes" : "NO - BUG");
    return ok ? 0 : 1;
}

int cmd_gen(const Args& a) {
    const size_t n = static_cast<size_t>(a.num("--rects", 100000));
    const uint64_t seed = static_cast<uint64_t>(a.num("--seed", 1));
    const double p = a.num("--defects", 0.001);
    GenStats st;
    if (a.has("--out")) {
        std::ofstream f(a.str("--out", ""));
        generate(f, n, seed, p, st);
    } else {
        generate(std::cout, n, seed, p, st);
    }
    std::fprintf(stderr,
                 "generated %zu shapes (M1 %zu, M2 %zu, V1 %zu); injected: %zu narrow wires, %zu shifted wires, "
                 "%zu short gaps, %zu offset vias\n",
                 st.total(), st.m1, st.m2, st.vias, st.injected_narrow, st.injected_shift, st.injected_end_to_end,
                 st.injected_offset_via);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }
    std::string cmd = argv[1];
    try {
        Args a = parse_args(argc, argv);
        if (cmd == "check" && a.pos.size() == 2) return cmd_check(a);
        if (cmd == "bench" && a.pos.size() == 2) return cmd_bench(a);
        if (cmd == "gen" && a.pos.empty()) return cmd_gen(a);
        usage();
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
