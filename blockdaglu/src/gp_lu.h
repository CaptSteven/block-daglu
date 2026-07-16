#pragma once

#include <cstdint>
#include <vector>

#include "sparse_formats.h"

// A lightweight single-thread sparse LU factorization (Gilbert–Peierls style),
// intended as a self-developed replacement candidate for block KLU.
//
// Notes / trade-offs:
// - Implements row partial pivoting (row swaps) for numerical stability.
// - Designed for correctness/stability first (vA). Later versions can add parallelism (vB)
//   and structured BBD/Schur (vC).
struct GpLuFactor {
  int32_t n = 0;
  bool ok = false;

  // Optional row scaling: A_scaled = diag(row_scale) * A.
  // If empty, scaling is disabled.
  std::vector<double> row_scale; // size n

  // Row permutation P such that P*(diag(row_scale)*A) = L*U.
  // perm[i] gives the original row index placed at row i after pivoting.
  std::vector<int32_t> perm; // size n

  // L multipliers stored by row (strictly lower triangular): for each row i, entries (k, L(i,k)) with k<i.
  std::vector<std::vector<std::pair<int32_t, double>>> L_rows; // size n

  // U stored by row (upper triangular): for each row i, entries (j, U(i,j)) with j>=i.
  std::vector<std::vector<std::pair<int32_t, double>>> U_rows; // size n
  std::vector<double> Udiag; // size n, diagonal pivots

  // Diagnostics
  int32_t tiny_pivot_count = 0;
  int32_t row_swap_count = 0;
  int32_t zero_pivot_count = 0;
  double pivot_tol = 0.0;
  double perturb = 0.0;
};

struct GpLuOptions {
  int scale = 0; // 0 none, 1 sum, 2 max
  double diag_prefer_tol = 1e-3; // prefer diagonal if |a_kk| >= tol * max_{i>=k} |a_ik|
  double pivot_tol = 1e-12;
  double perturb = 1e-12;
  double drop_tol = 0.0; // 0 => keep all entries
};

bool gp_lu_factor(const CscMatrixI32& A, GpLuFactor& out, const GpLuOptions& opt);

// Solves in-place: A x = b, using factorization from gp_lu_factor.
// B is column-major with leading dimension ldim (same convention as KLU solve wrapper).
bool gp_lu_solve_inplace_strided(const GpLuFactor& f, double* B, int32_t ldim, int32_t nrhs);
