// design.hpp — Read-only description of a benchmark: FPGA grid, cells and nets.
//
// Everything is stored with integer ids and flat (CSR) arrays so that the
// annealer's inner loop touches contiguous memory only.
#pragma once
#include <string>
#include <vector>
#include <cstdint>

/// Index of all sites of one resource type (CLB, RAM, DSP, IO), by column.
///
/// Used to draw random compatible targets quickly and to find the nearest
/// free site during legalization.
struct TypeSites {
    std::vector<int> xs;               ///< Columns (x) with at least one site of this type, ascending.
    std::vector<std::vector<int>> ys;  ///< For each entry of xs, the sorted y coordinates of its sites.
    std::vector<int> colOf;            ///< x -> index into xs, or -1 if the column has none (size W).
    int colGap = 1;  ///< Typical distance between neighbouring columns (widens the move window).
    int rowGap = 1;  ///< Typical distance between neighbouring sites in a column (e.g. 5 for RAM).
    long long count = 0;  ///< Total number of sites of this type.
};

/// Read-only benchmark: the FPGA site grid, the cells and the nets.
///
/// Cells and nets are referred to by integer ids. Cells keep the order of the
/// input .pl, and the output .pl is written in that same order.
struct Design {
    int W = 0;  ///< Grid width (number of columns).
    int H = 0;  ///< Grid height (number of rows).
    std::vector<std::string> typeNames;  ///< Type id -> name (e.g. "CLB").
    std::vector<int8_t> siteType;        ///< Site index x*H + y -> type id; -1 = INVALID or absent.
    std::vector<TypeSites> sites;        ///< Site index of each type, by type id.

    std::vector<std::string> cellName;  ///< Cell id -> name.
    std::vector<int> cellType;          ///< Cell id -> type id.
    std::vector<char> cellFixed;        ///< Cell id -> 1 if fixed (F in the .pl), 0 if movable.
    std::vector<int> initX, initY;      ///< Coordinates from the input .pl.
    std::vector<int> movable;           ///< Ids of the movable cells.

    /// Net -> pins in CSR form: the cells of net n are
    /// netPins[netStart[n] .. netStart[n+1]). Duplicate pins of a cell are
    /// removed, and nets with fewer than two distinct cells are dropped
    /// (their HPWL is always 0).
    std::vector<int> netStart, netPins;
    /// Cell -> nets in CSR form: the nets of cell c are
    /// cellNets[cellNetStart[c] .. cellNetStart[c+1]).
    std::vector<int> cellNetStart, cellNets;

    /// Number of cells, fixed and movable.
    int numCells() const { return (int)cellName.size(); }
    /// Number of nets kept after parsing.
    int numNets() const { return (int)netStart.size() - 1; }
    /// Number of distinct cells on net n.
    int netDegree(int n) const { return netStart[n + 1] - netStart[n]; }
    /// Returns the id of type `name`, creating a new id if it is not known yet.
    int typeId(const std::string& name);

    /// Parses the three benchmark files and builds all indexes.
    /// @param sitemap Path to the .sitemap file (grid and site types).
    /// @param nets    Path to the .nets file.
    /// @param pl      Path to the input .pl file (cell types, coordinates, M/F).
    /// @throws std::runtime_error on missing files or malformed input.
    void load(const std::string& sitemap, const std::string& nets, const std::string& pl);

    /// Builds `sites` from `siteType` and `typeNames`.
    /// Also used to build the coarse design in cluster.cpp.
    void buildSiteIndex();
    /// Builds `cellNetStart` / `cellNets` from the net CSR arrays.
    /// Also used to build the coarse design in cluster.cpp.
    void buildCellNets();
};
