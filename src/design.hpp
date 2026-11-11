// design.hpp — Read-only description of a benchmark: FPGA grid, cells and nets.
//
// Everything is stored with integer ids and flat (CSR) arrays so that the
// annealer's inner loop touches contiguous memory only.
#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct TypeSites {
    // Columns (x) that contain at least one site of this type, sorted ascending,
    // and for each column the sorted list of y coordinates of the sites.
    std::vector<int> xs;
    std::vector<std::vector<int>> ys;
    std::vector<int> colOf;   // x -> index into xs, or -1 (size W)
    int colGap = 1;           // typical distance between neighbouring columns
    int rowGap = 1;           // typical distance between neighbouring sites in a column
    long long count = 0;
};

struct Design {
    int W = 0, H = 0;
    std::vector<std::string> typeNames;      // type id -> name
    std::vector<int8_t> siteType;            // x*H + y -> type id, -1 = INVALID / absent
    std::vector<TypeSites> sites;            // per type id

    // Cells, in the order they appear in the input .pl (output keeps that order).
    std::vector<std::string> cellName;
    std::vector<int> cellType;
    std::vector<char> cellFixed;
    std::vector<int> initX, initY;           // coordinates from the input .pl
    std::vector<int> movable;                // ids of movable cells

    // Nets in CSR form. Duplicate pins of the same cell inside a net are removed,
    // and nets with fewer than two distinct cells are dropped (their HPWL is 0).
    std::vector<int> netStart, netPins;      // pins of net n: netPins[netStart[n] .. netStart[n+1])
    std::vector<int> cellNetStart, cellNets; // nets of cell c: cellNets[cellNetStart[c] .. +1)

    int numCells() const { return (int)cellName.size(); }
    int numNets() const { return (int)netStart.size() - 1; }
    int netDegree(int n) const { return netStart[n + 1] - netStart[n]; }
    int typeId(const std::string& name);     // creates the id if new

    // Parses the three benchmark files. Throws std::runtime_error on malformed input.
    void load(const std::string& sitemap, const std::string& nets, const std::string& pl);
};
