#pragma once

#include <cstdint>
#include <memory>
#include <tuple>
#include <vector>

#include "gp_lu.h"
#include "klu_dynamic.h"
#include "sparse_formats.h"
#include "thread_pool.h"

struct UbftfDecomposition {
  int32_t n = 0;
  int32_t nblocks_raw = 0;  // blocks from DM/BTF before merging
  int32_t nmatch = 0;       // size of maximum matching (n if structurally full rank)
  double match_work = 0.0;  // returned work estimate from btf_maxtrans
  double match_maxwork_requested = 0.0; // env requested maxwork
  double match_maxwork_used = 0.0;      // maxwork actually used (0 if retried with no limit)
  std::vector<int32_t> P;      // perm-index -> original row
  std::vector<int32_t> Q;      // perm-index -> original col
  std::vector<int32_t> invP;   // original row -> perm-index
  std::vector<int32_t> invQ;   // original col -> perm-index
  std::vector<int32_t> R;      // block boundaries in perm index, size nblocks+1
  int32_t nblocks = 0;

  // For fast solve: A in permuted coordinates, row-major
  // Note: by default stores only inter-block strictly-upper entries (bc > br). Diagonal blocks are stored separately.
  // For AB experiments, can be forced to store the full UBTF matrix (including diagonal blocks) via env.
  CsrMatrixI32 Aperm_csr;
  bool csr_contains_diag = false; // true if Aperm_csr also includes diagonal-block entries

  // Diagonal block entries in local coordinates (row/col 0..block_size-1)
  std::vector<std::vector<std::tuple<int,int,double>>> diag_blocks_coo;

  // Block DAG dependencies for solve (bid -> deps to the right / higher index).
  std::vector<std::vector<int32_t>> block_deps;
  std::vector<std::vector<int32_t>> block_preds;

  // Level scheduling for solve (0 solved first; increasing levels depend on previous ones).
  std::vector<int32_t> block_level;
  int32_t max_level = 0;
  std::vector<std::vector<int32_t>> blocks_by_level;
};

struct UbftfFactors {
  struct LocalFactor {
    int32_t n = 0;

    // Dense LU (row-major) for small factors.
    std::vector<double> dense_lu; // n*n
    std::vector<int32_t> piv;     // n

    // Sparse LU factor for larger factors (either KLU or self-developed GP LU).
    CscMatrixI32 A;               // local CSC
    klu_common common{};
    klu_symbolic* sym = nullptr;
    klu_numeric* num = nullptr;
    std::unique_ptr<GpLuFactor> gp;

    bool dense = false;
    bool ok = false;
  };

  struct BlockFactor {
    int32_t start = 0;     // perm start
    int32_t size = 0;

    // Dense micro-kernel (for small blocks).
    std::vector<double> dense_lu;   // size*size, row-major
    std::vector<int32_t> piv;       // size

    // KLU kernel (for larger blocks).
    CscMatrixI32 A;                // local CSC
    klu_common common{};
    klu_symbolic* sym = nullptr;
    klu_numeric* num = nullptr;
    std::unique_ptr<GpLuFactor> gp;

    // GP attempt diagnostics (for profiling / gate reasoning).
    bool gp_tried = false;
    int32_t gp_row_swaps = 0;
    int32_t gp_tiny_pivots = 0;
    double gp_min_abs_diag = 0.0;
    // 0: not rejected (either not tried, accepted, or later fallback)
    // 1: factor failed
    // 2: rejected: swaps
    // 3: rejected: tiny pivots
    // 4: rejected: min_abs_diag
    int32_t gp_reject_reason = 0;

    // Optional nested decomposition for super-blocks (block-in-block).
    // Used to reduce Amdahl bottleneck by splitting the largest diagonal block further.
    std::unique_ptr<UbftfDecomposition> sub_dec;
    std::unique_ptr<UbftfFactors> sub_fac;
    int32_t sub_nblocks = 0;
    int32_t sub_max_block = 0;

    // Optional BBD (2-way) split + Schur complement for super-blocks.
    bool bbd_enabled = false;
    int32_t bbd_n1 = 0;
    int32_t bbd_n2 = 0;
    int32_t bbd_ns = 0;
    std::vector<int32_t> bbd_perm;    // new-index -> old-index (local 0..size-1)
    std::vector<int32_t> bbd_invperm; // old-index -> new-index

    std::unique_ptr<LocalFactor> bbd_f1;
    std::unique_ptr<LocalFactor> bbd_f2;

    // Coupling matrices in new ordering.
    // A1s: n1 x ns, A2s: n2 x ns, As1: ns x n1, As2: ns x n2 (CSR, row-major).
    CsrMatrixI32 bbd_A1s;
    CsrMatrixI32 bbd_A2s;
    CsrMatrixI32 bbd_As1;
    CsrMatrixI32 bbd_As2;

    // Ass and Schur factor (dense, row-major, ns*ns).
    std::vector<double> bbd_Ass;
    std::vector<double> bbd_S_lu;
    std::vector<int32_t> bbd_S_piv;

    bool ok = false;
  };

  int32_t n = 0;
  std::vector<BlockFactor> blocks;
};

// Builds a DM/BTF block triangular decomposition via btf_order (max matching + SCC),
// and optionally merges consecutive blocks to reduce scheduling overhead.
UbftfDecomposition build_decomposition_btf(const std::vector<std::tuple<int,int,double>>& A_coo,
                                           int32_t n,
                                           int32_t target_blocks,
                                           int32_t min_block_size);

// Faster variant when CSC is already available (e.g., AUTO mode precomputes CSC for cheap fallback).
// Only the pattern (col_ptr/row_ind) is used for BTF ordering; values are ignored.
UbftfDecomposition build_decomposition_btf_from_csc(const CscMatrixI32& A_csc,
                                                    const std::vector<std::tuple<int,int,double>>& A_coo,
                                                    int32_t target_blocks,
                                                    int32_t min_block_size);

// Factors each diagonal block (in parallel) and stores KLU factors.
UbftfFactors factor_blocks_parallel(const UbftfDecomposition& dec, std::size_t thread_count);

// Releases KLU resources inside factors.
void free_factors(UbftfFactors& fac);

// Solves A x = b using block back substitution on the permuted UBTF form.
// Input b is in original row order; output x is returned in original column order.
// Returns false on failure.
bool solve_ubtf(const UbftfDecomposition& dec, const UbftfFactors& fac, std::vector<double>& b, std::size_t thread_count);
