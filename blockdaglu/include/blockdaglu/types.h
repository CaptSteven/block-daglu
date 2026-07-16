#pragma once

#include <tuple>
#include <vector>

namespace blockdaglu {

// Minimal COO matrix type (row, col, value).
// Matches the competition tool's COOMatrix instantiation so adapters can move without copying.
using CooMatrix = std::vector<std::tuple<int, int, double>>;

} // namespace blockdaglu

