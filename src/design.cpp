// design.cpp — Benchmark file parsing (.sitemap, .pl, .nets) and site indexing.
#include "design.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

int Design::typeId(const std::string& name) {
    for (size_t i = 0; i < typeNames.size(); i++)
        if (typeNames[i] == name) return (int)i;
    typeNames.push_back(name);
    return (int)typeNames.size() - 1;
}

static int medianGap(std::vector<int> v) {
    if (v.size() < 2) return 1;
    std::vector<int> g;
    for (size_t i = 1; i < v.size(); i++) g.push_back(v[i] - v[i - 1]);
    std::nth_element(g.begin(), g.begin() + g.size() / 2, g.end());
    return std::max(1, g[g.size() / 2]);
}

void Design::load(const std::string& fSitemap, const std::string& fNets, const std::string& fPl) {
    // ---- .sitemap ---------------------------------------------------------
    {
        std::ifstream f(fSitemap);
        if (!f) throw std::runtime_error("cannot open " + fSitemap);
        if (!(f >> W >> H) || W <= 0 || H <= 0) throw std::runtime_error("bad sitemap header");
        siteType.assign((size_t)W * H, -1);
        int x, y;
        std::string t;
        while (f >> x >> y >> t) {
            if (x < 0 || y < 0 || x >= W || y >= H) throw std::runtime_error("site out of grid");
            if (t == "INVALID") continue;
            siteType[(size_t)x * H + y] = (int8_t)typeId(t);
        }
    }
    // ---- .pl ----------------------------------------------------------------
    std::unordered_map<std::string, int> cellId;
    {
        std::ifstream f(fPl);
        if (!f) throw std::runtime_error("cannot open " + fPl);
        std::string name, t, st;
        int x, y;
        while (f >> name >> t >> x >> y >> st) {
            int id = (int)cellName.size();
            if (!cellId.emplace(name, id).second) throw std::runtime_error("duplicate cell " + name);
            cellName.push_back(name);
            cellType.push_back(typeId(t));
            cellFixed.push_back(st == "F");
            initX.push_back(x);
            initY.push_back(y);
            if (st != "F") movable.push_back(id);
        }
    }
    // ---- per-type site lists ----------------------------------------------
    sites.assign(typeNames.size(), TypeSites());
    for (auto& ts : sites) ts.colOf.assign(W, -1);
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++) {
            int t = siteType[(size_t)x * H + y];
            if (t < 0) continue;
            TypeSites& ts = sites[t];
            if (ts.colOf[x] < 0) {
                ts.colOf[x] = (int)ts.xs.size();
                ts.xs.push_back(x);
                ts.ys.emplace_back();
            }
            ts.ys[ts.colOf[x]].push_back(y);
            ts.count++;
        }
    for (auto& ts : sites) {
        ts.colGap = medianGap(ts.xs);
        std::vector<int> gaps;
        for (auto& col : ts.ys) gaps.push_back(medianGap(col));
        if (!gaps.empty()) {
            std::nth_element(gaps.begin(), gaps.begin() + gaps.size() / 2, gaps.end());
            ts.rowGap = gaps[gaps.size() / 2];
        }
    }
    // ---- .nets ------------------------------------------------------------
    {
        std::ifstream f(fNets);
        if (!f) throw std::runtime_error("cannot open " + fNets);
        std::string line, tok;
        std::vector<int> pins;
        std::vector<int> seen(numCells(), -1);
        netStart.push_back(0);
        int lineNo = 0;
        while (std::getline(f, line)) {
            std::istringstream ss(line);
            if (!(ss >> tok)) continue;  // empty line; first token is the net name
            pins.clear();
            while (ss >> tok) {
                auto it = cellId.find(tok);
                if (it == cellId.end()) throw std::runtime_error("unknown cell in nets: " + tok);
                if (seen[it->second] != lineNo) {
                    seen[it->second] = lineNo;
                    pins.push_back(it->second);
                }
            }
            lineNo++;
            if (pins.size() < 2) continue;
            netPins.insert(netPins.end(), pins.begin(), pins.end());
            netStart.push_back((int)netPins.size());
        }
    }
    // ---- cell -> nets (CSR) -------------------------------------------------
    cellNetStart.assign(numCells() + 1, 0);
    for (int n = 0; n < numNets(); n++)
        for (int i = netStart[n]; i < netStart[n + 1]; i++) cellNetStart[netPins[i] + 1]++;
    for (int c = 0; c < numCells(); c++) cellNetStart[c + 1] += cellNetStart[c];
    cellNets.assign(netPins.size(), 0);
    std::vector<int> fill(cellNetStart.begin(), cellNetStart.end() - 1);
    for (int n = 0; n < numNets(); n++)
        for (int i = netStart[n]; i < netStart[n + 1]; i++) cellNets[fill[netPins[i]]++] = n;
}
