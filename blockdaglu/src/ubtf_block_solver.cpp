#include "ubtf_block_solver.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cctype>
#include <iostream>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <stdexcept>
#include <cstdlib>
#include <string>
#include <limits>

#include "blockdaglu/env.h"
#include "btf_dynamic.h"

// ThreadPool creation is relatively expensive on Windows (thread creation + affinity/power policy).
// Reuse pools across calls/cases to improve cold-start and medium-case performance.
static ThreadPool& ReusedPool(std::size_t thread_count) {
  static std::mutex mtx;
  static std::unordered_map<std::size_t, std::unique_ptr<ThreadPool>> pools;
  std::lock_guard<std::mutex> lk(mtx);
  auto& p = pools[thread_count];
  if (!p) {
    p = std::make_unique<ThreadPool>(thread_count);
  }
  return *p;
}

static bool profile_enabled_local() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_PROFILE")) {
    return std::atoi(env) != 0;
  }
  return false;
}

static int pick_block_topk() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_BLOCK_TOPK")) {
    const int v = std::atoi(env);
    if (v >= 0) return v;
  }
  return 10;
}

static bool pick_superblock_subdecomp_enabled() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_SUPERBLOCK_SUBDECOMP")) {
    return std::atoi(env) != 0;
  }
  return false; // default: off (experimental)
}

static int pick_superblock_min_size() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_SUPERBLOCK_MIN")) {
    const int v = std::atoi(env);
    if (v > 0) return v;
  }
  return 8192;
}

static int pick_superblock_min_block_size() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_SUPERBLOCK_MIN_BLOCK_SIZE")) {
    const int v = std::atoi(env);
    if (v > 0) return v;
  }
  return 512;
}

static bool pick_superblock_stats_enabled() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_SUPERBLOCK_STATS")) {
    return std::atoi(env) != 0;
  }
  return false;
}

static bool pick_superblock_bbd_enabled() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_SUPERBLOCK_BBD")) {
    return std::atoi(env) != 0;
  }
  return false; // default: off (experimental)
}

static bool pick_gp_enabled() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_SPARSE_LU_KERNEL")) {
    std::string v(env);
    for (auto& ch : v) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return v == "gp" || v == "gplu";
  }
  return false; // default: KLU
}

static int pick_gp_max_n() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_GP_MAX_N")) {
    const int v = std::atoi(env);
    if (v > 0) return v;
  }
  return 4096; // safety default: only trial GP on moderate blocks
}

static bool pick_gp_force() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_GP_FORCE")) {
    return std::atoi(env) != 0;
  }
  return false;
}

static bool pick_gp_gate_log() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_GP_GATE_LOG")) {
    return std::atoi(env) != 0;
  }
  return false;
}

static bool pick_gp_require_no_swaps() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_GP_REQUIRE_NO_SWAPS")) {
    return std::atoi(env) != 0;
  }
  // Default: strict gating to avoid changing solutions on near-singular problems.
  return true;
}

static int pick_gp_swap_max_n() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_GP_SWAP_MAX_N")) {
    const int v = std::atoi(env);
    if (v >= 0) return v;
  }
  // Only allow swaps in relatively small blocks by default.
  return 256;
}

static int pick_gp_swap_max() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_GP_SWAP_MAX")) {
    const int v = std::atoi(env);
    if (v >= 0) return v;
  }
  // Default: strict means strict (no swaps). Opt-in relaxation via env.
  return 0;
}

static bool pick_gp_require_no_tiny_pivots() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_GP_REQUIRE_NO_TINY_PIVOTS")) {
    return std::atoi(env) != 0;
  }
  return true;
}

static double pick_gp_min_abs_diag() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_GP_MIN_ABS_DIAG")) {
    const double v = std::atof(env);
    if (v >= 0.0) return v;
  }
  return 1e-12;
}

static GpLuOptions pick_gp_options() {
  GpLuOptions opt;
  if (const char* env = blockdaglu_getenv_compat("USERALGO_GP_SCALE")) {
    const int v = std::atoi(env);
    if (v >= 0 && v <= 2) opt.scale = v;
  }
  if (const char* env = blockdaglu_getenv_compat("USERALGO_GP_DIAG_TOL")) {
    const double v = std::atof(env);
    if (v >= 0.0) opt.diag_prefer_tol = v;
  }
  if (const char* env = blockdaglu_getenv_compat("USERALGO_GP_PIVOT_TOL")) {
    const double v = std::atof(env);
    if (v >= 0.0) opt.pivot_tol = v;
  }
  if (const char* env = blockdaglu_getenv_compat("USERALGO_GP_PERTURB")) {
    const double v = std::atof(env);
    if (v >= 0.0) opt.perturb = v;
  }
  if (const char* env = blockdaglu_getenv_compat("USERALGO_GP_DROP_TOL")) {
    const double v = std::atof(env);
    if (v >= 0.0) opt.drop_tol = v;
  }
  return opt;
}

struct GpGateResult {
  bool accept = false;
  int32_t reject_reason = 0;
  double min_abs_diag = 0.0;
};

static double gp_min_abs_diag_of(const GpLuFactor& f) {
  double mn = std::numeric_limits<double>::infinity();
  for (double d : f.Udiag) mn = std::min(mn, std::abs(d));
  if (!std::isfinite(mn)) mn = 0.0;
  return mn;
}

static GpGateResult gp_gate_eval(const GpLuFactor& f, int32_t n) {
  GpGateResult r;
  if (!f.ok) {
    r.accept = false;
    r.reject_reason = 1;
    r.min_abs_diag = 0.0;
    return r;
  }

  r.min_abs_diag = gp_min_abs_diag_of(f);

  if (pick_gp_require_no_swaps()) {
    if (f.row_swap_count != 0) {
      const int swap_max_n = pick_gp_swap_max_n();
      const int swap_max = pick_gp_swap_max();
      if (swap_max_n > 0 && swap_max > 0 && n > 0 && n <= swap_max_n && f.row_swap_count <= swap_max) {
        // Allow a limited number of swaps in small blocks, but keep strict behaviour for larger blocks.
      } else {
        r.accept = false;
        r.reject_reason = 2;
        return r;
      }
    }
  }
  if (pick_gp_require_no_tiny_pivots() && f.tiny_pivot_count != 0) {
    r.accept = false;
    r.reject_reason = 3;
    return r;
  }
  const double min_abs_diag = pick_gp_min_abs_diag();
  if (min_abs_diag > 0.0 && !(r.min_abs_diag >= min_abs_diag)) {
    r.accept = false;
    r.reject_reason = 4;
    return r;
  }
  r.accept = true;
  r.reject_reason = 0;
  return r;
}

static int pick_superblock_sep_max() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_SUPERBLOCK_SEP_MAX")) {
    const int v = std::atoi(env);
    if (v > 0) return v;
  }
  return 2048;
}

static double pick_superblock_sep_pct_max() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_SUPERBLOCK_SEP_PCT_MAX")) {
    const double v = std::atof(env);
    if (v > 0.0) return v;
  }
  return 10.0;
}

static double pick_superblock_rcond_min() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_SUPERBLOCK_RCOND_MIN")) {
    const double v = std::atof(env);
    if (v > 0.0) return v;
  }
  // Default is conservative: super-block Schur can be unstable on ill-conditioned problems.
  return 1e-12;
}

enum class BlockSched {
  Reverse,
  Dag,
  Level,
};

enum class CsrMode {
  Offdiag,
  Full,
};

enum class FactorOrder {
  Id,
  Cost,
  Auto,
};

static FactorOrder pick_factor_order() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_FACTOR_ORDER")) {
    std::string v(env);
    for (auto& ch : v) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (v == "auto" || v == "adaptive") return FactorOrder::Auto;
    if (v == "cost" || v == "heavy" || v == "desc") return FactorOrder::Cost;
    if (v == "id" || v == "index" || v == "asc") return FactorOrder::Id;
  }
  return FactorOrder::Id; // default: stable & fastest on aggregate (auto is opt-in)
}

static BlockSched pick_block_sched() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_BLOCK_SCHED")) {
    const std::string v(env);
    if (v == "level") return BlockSched::Level;
    if (v == "dag") return BlockSched::Dag;
    if (v == "reverse") return BlockSched::Reverse;
  }
  // Default: reverse block order solves are correct for UBTF and avoid expensive DAG build.
  return BlockSched::Reverse;
}

static CsrMode pick_csr_mode() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_CSR_MODE")) {
    const std::string v(env);
    if (v == "full") return CsrMode::Full;
    if (v == "offdiag") return CsrMode::Offdiag;
  }
  return CsrMode::Offdiag;
}

static double pick_btf_maxwork() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_BTF_MAXWORK")) {
    const double v = std::atof(env);
    if (v > 0.0) return v;
    return 0.0;
  }
  return 0.0; // default: no limit (deterministic & safest)
}

static bool pick_btf_via_klu() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_BTF_IMPL")) {
    std::string v(env);
    for (auto& ch : v) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return v == "klu" || v == "klu_analyze" || v == "analyze";
  }
  return false; // default: use btf_order (btf.dll)
}

static int pick_btf_klu_ordering() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_BTF_KLU_ORDERING")) {
    const int v = std::atoi(env);
    if (v >= 0 && v <= 3) return v;
  }
  return 0; // default: AMD for global analysis
}

static double pick_drop_abs_lt() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_DROP_ABS_LT")) {
    const double v = std::atof(env);
    if (v >= 0.0) return v;
  }
  return -1.0; // default: auto (decide per matrix size)
}

static CscMatrixI32 coo_to_csc_i32_abs_filter(const std::vector<std::tuple<int,int,double>>& coo,
                                              int32_t n,
                                              double abs_lt) {
  CscMatrixI32 out;
  out.n = n;
  out.col_ptr.assign(n + 1, 0);

  // Pass 1: count columns after filtering.
  for (const auto& t : coo) {
    const int r = std::get<0>(t);
    const int c = std::get<1>(t);
    const double v = std::get<2>(t);
    if (r < 0 || c < 0 || r >= n || c >= n) continue;
    if (std::abs(v) < abs_lt) continue;
    out.col_ptr[c + 1]++;
  }
  for (int i = 0; i < n; ++i) {
    out.col_ptr[i + 1] += out.col_ptr[i];
  }
  out.nnz = out.col_ptr[n];

  out.row_ind.resize(static_cast<std::size_t>(out.nnz));
  out.values.resize(static_cast<std::size_t>(out.nnz));

  // Pass 2: fill.
  thread_local std::vector<int32_t> tls_next;
  tls_next.assign(out.col_ptr.begin(), out.col_ptr.end());
  for (const auto& t : coo) {
    const int r = std::get<0>(t);
    const int c = std::get<1>(t);
    const double v = std::get<2>(t);
    if (r < 0 || c < 0 || r >= n || c >= n) continue;
    if (std::abs(v) < abs_lt) continue;
    const int32_t pos = tls_next[c]++;
    out.row_ind[static_cast<std::size_t>(pos)] = static_cast<int32_t>(r);
    out.values[static_cast<std::size_t>(pos)] = v;
  }

  return out;
}

static std::vector<int32_t> invert_perm(const std::vector<int32_t>& p) {
  std::vector<int32_t> inv(p.size(), -1);
  for (int32_t i = 0; i < static_cast<int32_t>(p.size()); ++i) {
    inv[p[i]] = i;
  }
  return inv;
}

static std::vector<int32_t> merge_block_boundaries(const std::vector<int32_t>& R,
                                                   int32_t n,
                                                   int32_t target_blocks,
                                                   int32_t min_block_size) {
  if (R.size() < 2) return {0, n};
  const int32_t nblocks = static_cast<int32_t>(R.size() - 1);
  if (nblocks <= 1) return {0, n};
  if (target_blocks <= 0) target_blocks = 1;

  // Safe merge policy:
  // Merging SCC/BTF blocks can increase fill and cause huge slowdowns, so we only merge
  // consecutive small blocks into a bounded-size super-block (preferably handled by the
  // dense micro-kernel).
  int32_t merge_max = 0;
  if (const char* env = blockdaglu_getenv_compat("USERALGO_BLOCK_MERGE_MAX_SIZE")) {
    const int v = std::atoi(env);
    if (v > 0) merge_max = v;
  }
  if (merge_max <= 0) {
    // Default: aim to keep merged blocks within dense-kernel territory.
    if (const char* env = blockdaglu_getenv_compat("USERALGO_DENSE_BLOCK_MAX")) {
      const int v = std::atoi(env);
      if (v > 0) merge_max = v;
    }
    if (merge_max <= 0) merge_max = 64;
  }

  const int32_t small_block_max = std::max<int32_t>(1, std::min<int32_t>(merge_max, 32));

  std::vector<int32_t> newR;
  newR.reserve(R.size());
  newR.push_back(0);

  int32_t acc = 0;
  int32_t run_start = 0;
  int32_t run_end = 0;

  auto flush_small_run = [&]() {
    if (acc <= 0) return;
    if (newR.back() != run_start) newR.push_back(run_start);
    if (newR.back() != run_end) newR.push_back(run_end);
    acc = 0;
    run_start = 0;
    run_end = 0;
  };

  for (int32_t b = 0; b < nblocks; ++b) {
    const int32_t start = R[static_cast<std::size_t>(b)];
    const int32_t end = R[static_cast<std::size_t>(b + 1)];
    const int32_t sz = end - start;

    if (sz <= small_block_max) {
      if (acc == 0) {
        run_start = start;
        run_end = end;
        acc = sz;
        continue;
      }
      if (acc + sz <= merge_max) {
        acc += sz;
        run_end = end;
        continue;
      }

      // Would overflow the merge cap: flush and start a new run.
      flush_small_run();
      run_start = start;
      run_end = end;
      acc = sz;
      continue;
    }

    // Large block: flush any pending small run, then emit this block intact.
    flush_small_run();
    if (newR.back() != start) newR.push_back(start);
    if (newR.back() != end) newR.push_back(end);
  }

  flush_small_run();
  if (newR.back() != n) newR.push_back(n);
  newR.erase(std::unique(newR.begin(), newR.end()), newR.end());
  return newR;
}

static bool block_merge_enabled() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_BLOCK_MERGE")) {
    return std::atoi(env) != 0;
  }
  return true; // default: enabled (bounded safe merges only)
}

static int pick_block_klu_btf() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_BLOCK_KLU_BTF")) {
    const int v = std::atoi(env);
    if (v == 0 || v == 1) return v;
  }
  return 0; // default: match SuiteSparseWrapper defaults
}

static int pick_block_klu_ordering() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_BLOCK_KLU_ORDERING")) {
    const int v = std::atoi(env);
    if (v >= 0 && v <= 3) return v;
  }
  return 1; // default: COLAMD
}

static int pick_block_klu_scale() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_BLOCK_KLU_SCALE")) {
    const int v = std::atoi(env);
    if (v >= -1 && v <= 2) return v;
  }
  return 0; // default: none
}

static int pick_block_klu_big_n() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_BLOCK_KLU_BIG_N")) {
    const int v = std::atoi(env);
    if (v > 0) return v;
  }
  return 4096;
}

static double clamp01(double x) {
  if (x < 0.0) return 0.0;
  if (x > 1.0) return 1.0;
  return x;
}

static int pick_block_klu_btf_for(int32_t n) {
  const int big_n = pick_block_klu_big_n();
  if (n >= big_n) {
    if (const char* env = blockdaglu_getenv_compat("USERALGO_BLOCK_KLU_BTF_BIG")) {
      const int v = std::atoi(env);
      if (v == 0 || v == 1) return v;
    }
    // Heuristic: keep BTF disabled inside diagonal blocks by default (it can add significant
    // symbolic overhead and often duplicates the global DM/BTF work we already did).
    return 0;
  }
  return pick_block_klu_btf();
}

static double pick_block_klu_tol() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_BLOCK_KLU_TOL")) {
    const double v = std::atof(env);
    if (v >= 0.0) return clamp01(v);
  }
  return -1.0; // default: keep KLU defaults
}

static double pick_block_klu_tol_for(int32_t n) {
  const int big_n = pick_block_klu_big_n();
  if (n >= big_n) {
    if (const char* env = blockdaglu_getenv_compat("USERALGO_BLOCK_KLU_TOL_BIG")) {
      const double v = std::atof(env);
      if (v >= 0.0) return clamp01(v);
    }
  }
  return pick_block_klu_tol();
}

static int pick_block_klu_scale_for(int32_t n) {
  const int big_n = pick_block_klu_big_n();
  if (n >= big_n) {
    if (const char* env = blockdaglu_getenv_compat("USERALGO_BLOCK_KLU_SCALE_BIG")) {
      const int v = std::atoi(env);
      if (v >= -1 && v <= 2) return v;
    }
    // Heuristic: enable scaling inside large diagonal blocks; often improves stability and
    // can *reduce* total factor time on ill-conditioned instances.
    return 1;
  }
  return pick_block_klu_scale();
}

UbftfDecomposition build_decomposition_btf(const std::vector<std::tuple<int,int,double>>& A_coo,
                                          int32_t n,
                                          int32_t target_blocks,
                                          int32_t min_block_size) {
  UbftfDecomposition dec;
  dec.n = n;

  using Clock = std::chrono::high_resolution_clock;
  const bool prof = profile_enabled_local();
  const auto t0 = prof ? Clock::now() : Clock::time_point{};

  // Convert to CSC for DM/BTF ordering (pattern-only).
  double drop_abs_lt = pick_drop_abs_lt();
  if (drop_abs_lt < 0.0) {
    // Safe default: only drop denorm-level values on very large matrices.
    // This reduces work on some instances while keeping the numerical behaviour effectively unchanged.
    drop_abs_lt = (n >= 50000) ? 1e-20 : 0.0;
  }
  CscMatrixI32 csc = (drop_abs_lt > 0.0) ? coo_to_csc_i32_abs_filter(A_coo, n, drop_abs_lt)
                                         : coo_to_csc_i32(A_coo, n);
  const auto t1 = prof ? Clock::now() : Clock::time_point{};

  // DM/BTF decomposition. Two interchangeable implementations:
  // - Default: call btf_order (btf.dll): max matching -> SCC -> block triangular form.
  // - Optional: call klu_analyze(btf=1) (klu.dll) and reuse its P/Q/R/nblocks.
  const bool use_klu_impl = pick_btf_via_klu();

  std::vector<int32_t> P(static_cast<std::size_t>(n), 0);
  std::vector<int32_t> Q(static_cast<std::size_t>(n), 0);
  std::vector<int32_t> R(static_cast<std::size_t>(n) + 1, 0);
  int32_t nblocks = 0;

  double btf_ms_first = 0.0;
  double btf_ms_retry = 0.0;

  if (use_klu_impl) {
    auto& klu = KluDynamic::instance();
    klu_common common{};
    if (!klu.defaults(&common)) {
      throw std::runtime_error("klu_defaults failed");
    }
    common.btf = 1;
    common.ordering = pick_btf_klu_ordering();
    common.scale = 0;
    common.halt_if_singular = 0;

    const auto t = prof ? Clock::now() : Clock::time_point{};
    klu_symbolic* sym = klu.analyze(n, csc.col_ptr.data(), csc.row_ind.data(), &common);
    if (prof) {
      btf_ms_first = std::chrono::duration<double, std::milli>(Clock::now() - t).count();
    }
    if (!sym) {
      throw std::runtime_error("klu_analyze(btf=1) failed");
    }

    nblocks = static_cast<int32_t>(sym->nblocks);
    if (nblocks <= 0) {
      klu.free_symbolic(&sym, &common);
      throw std::runtime_error("klu_analyze returned nblocks<=0");
    }

    for (int32_t i = 0; i < n; ++i) {
      P[static_cast<std::size_t>(i)] = static_cast<int32_t>(sym->P[i]);
      Q[static_cast<std::size_t>(i)] = static_cast<int32_t>(sym->Q[i]);
    }
    for (int32_t k = 0; k <= nblocks; ++k) {
      R[static_cast<std::size_t>(k)] = static_cast<int32_t>(sym->R[k]);
    }

    dec.nblocks_raw = nblocks;
    dec.nmatch = n;
    dec.match_work = 0.0;
    dec.match_maxwork_requested = 0.0;
    dec.match_maxwork_used = 0.0;

    klu.free_symbolic(&sym, &common);
  } else {
    // btf_order returns P/Q/R such that P*A*Q is in block upper triangular form.
    std::vector<int32_t> Work(static_cast<std::size_t>(5) * static_cast<std::size_t>(n), 0);
    double work = 0.0;
    int32_t nmatch = 0;

    const double maxwork = pick_btf_maxwork();
    dec.match_maxwork_requested = maxwork;
    dec.match_maxwork_used = maxwork;

    auto call_btf_order = [&](double mw) -> int32_t {
      std::fill(Work.begin(), Work.end(), 0);
      work = 0.0;
      nmatch = 0;
      const auto t = prof ? Clock::now() : Clock::time_point{};
      const int32_t nb = BtfDynamic::instance().order(n,
                                                     csc.col_ptr.data(),
                                                     csc.row_ind.data(),
                                                     mw,
                                                     &work,
                                                     P.data(),
                                                     Q.data(),
                                                     R.data(),
                                                     &nmatch,
                                                     Work.data());
      if (prof) {
        const auto dt = std::chrono::duration<double, std::milli>(Clock::now() - t).count();
        if (mw == maxwork) btf_ms_first += dt;
        else btf_ms_retry += dt;
      }
      return nb;
    };

    nblocks = call_btf_order(maxwork);
    if (nblocks <= 0) {
      throw std::runtime_error("btf_order failed");
    }
    dec.nblocks_raw = nblocks;
    dec.nmatch = nmatch;
    dec.match_work = work;

    // Safety: if user limited maxwork and matching is incomplete, re-run with no limit.
    if (maxwork > 0.0 && nmatch < n) {
      const int32_t nblocks2 = call_btf_order(0.0 /*no limit*/);
      if (nblocks2 <= 0) {
        throw std::runtime_error("btf_order retry failed");
      }
      nblocks = nblocks2;
      dec.nblocks_raw = nblocks2;
      dec.nmatch = nmatch;
      dec.match_work = work;
      dec.match_maxwork_used = 0.0;
    }
  }

  const auto t2 = prof ? Clock::now() : Clock::time_point{};

  if (!use_klu_impl) {
    // Unflip Q so it can be used as a permutation vector. (Flags indicate structural zeros on the diagonal.)
    for (int32_t k = 0; k < n; ++k) {
      Q[static_cast<std::size_t>(k)] = BTF_UNFLIP(Q[static_cast<std::size_t>(k)]);
    }
  }

  // Merge consecutive blocks to reduce overhead.
  std::vector<int32_t> Rused(R.begin(), R.begin() + static_cast<std::size_t>(nblocks + 1));
  std::vector<int32_t> Rm = block_merge_enabled() ? merge_block_boundaries(Rused, n, target_blocks, min_block_size) : std::move(Rused);
  dec.nblocks = static_cast<int32_t>(Rm.size() - 1);
  dec.P = std::move(P);
  dec.Q = std::move(Q);
  dec.invP = invert_perm(dec.P);
  dec.invQ = invert_perm(dec.Q);
  dec.R = std::move(Rm);
  const auto t3 = prof ? Clock::now() : Clock::time_point{};

  // Precompute block id for each perm index.
  thread_local std::vector<int32_t> tls_block_of_index;
  tls_block_of_index.assign(static_cast<std::size_t>(n), 0);
  std::vector<int32_t>& block_of_index = tls_block_of_index;
  for (int32_t b = 0; b < dec.nblocks; ++b) {
    for (int32_t k = dec.R[b]; k < dec.R[b + 1]; ++k) {
      block_of_index[k] = b;
    }
  }

  const bool csr_full = (pick_csr_mode() == CsrMode::Full);

  // Build permuted CSR (2-pass).
  // - Default: store inter-block strictly-upper entries only (bc > br), for fast RHS update.
  // - AB mode: store full UBTF matrix (bc >= br), including diagonal blocks (slower, but useful for attribution).
  // Diagonal blocks are always stored separately for factorization.
  const auto t_csr0 = prof ? Clock::now() : Clock::time_point{};

  thread_local std::vector<int32_t> tls_row_counts;
  thread_local std::vector<int32_t> tls_diag_counts;
  tls_row_counts.assign(static_cast<std::size_t>(n), 0);
  tls_diag_counts.assign(static_cast<std::size_t>(dec.nblocks), 0);
  std::vector<int32_t>& row_counts = tls_row_counts;
  std::vector<int32_t>& diag_counts = tls_diag_counts;
  int32_t csr_nnz = 0;
  bool violates_ubtf = false;
  if (drop_abs_lt > 0.0) {
    for (const auto& t : A_coo) {
      const int32_t r0 = static_cast<int32_t>(std::get<0>(t));
      const int32_t c0 = static_cast<int32_t>(std::get<1>(t));
      const double v = std::get<2>(t);
      if (r0 < 0 || c0 < 0 || r0 >= n || c0 >= n) continue;
      if (std::abs(v) < drop_abs_lt) continue;
      const int32_t rp = dec.invP[static_cast<std::size_t>(r0)];
      const int32_t cp = dec.invQ[static_cast<std::size_t>(c0)];
      if (rp < 0 || cp < 0) continue;
      const int32_t br = block_of_index[rp];
      const int32_t bc = block_of_index[cp];
      if (bc < br) {
        violates_ubtf = true;
        continue;
      }
      if (bc == br) {
        diag_counts[static_cast<std::size_t>(br)]++;
        if (csr_full) {
          row_counts[rp]++;
          csr_nnz++;
        }
        continue;
      }
      // bc > br
      row_counts[rp]++;
      csr_nnz++;
    }
  } else {
    for (const auto& t : A_coo) {
      const int32_t r0 = static_cast<int32_t>(std::get<0>(t));
      const int32_t c0 = static_cast<int32_t>(std::get<1>(t));
      if (r0 < 0 || c0 < 0 || r0 >= n || c0 >= n) continue;
      const int32_t rp = dec.invP[static_cast<std::size_t>(r0)];
      const int32_t cp = dec.invQ[static_cast<std::size_t>(c0)];
      if (rp < 0 || cp < 0) continue;
      const int32_t br = block_of_index[rp];
      const int32_t bc = block_of_index[cp];
      if (bc < br) {
        violates_ubtf = true;
        continue;
      }
      if (bc == br) {
        diag_counts[static_cast<std::size_t>(br)]++;
        if (csr_full) {
          row_counts[rp]++;
          csr_nnz++;
        }
        continue;
      }
      // bc > br
      row_counts[rp]++;
      csr_nnz++;
    }
  }
  if (violates_ubtf) {
    throw std::runtime_error("permutation is not UBTF (found lower-block entries)");
  }

  dec.Aperm_csr.n = n;
  dec.Aperm_csr.nnz = csr_nnz;
  dec.Aperm_csr.row_ptr.assign(n + 1, 0);
  for (int32_t i = 0; i < n; ++i) {
    dec.Aperm_csr.row_ptr[i + 1] = dec.Aperm_csr.row_ptr[i] + row_counts[i];
  }
  dec.Aperm_csr.col_ind.resize(static_cast<std::size_t>(csr_nnz));
  dec.Aperm_csr.values.resize(static_cast<std::size_t>(csr_nnz));
  thread_local std::vector<int32_t> tls_csr_next;
  tls_csr_next.assign(dec.Aperm_csr.row_ptr.begin(), dec.Aperm_csr.row_ptr.end());
  std::vector<int32_t>& next = tls_csr_next;
  dec.diag_blocks_coo.assign(dec.nblocks, {});
  for (int32_t b = 0; b < dec.nblocks; ++b) {
    dec.diag_blocks_coo[static_cast<std::size_t>(b)].reserve(static_cast<std::size_t>(diag_counts[static_cast<std::size_t>(b)]));
  }
  dec.csr_contains_diag = csr_full;

  for (const auto& t : A_coo) {
    const int32_t r0 = static_cast<int32_t>(std::get<0>(t));
    const int32_t c0 = static_cast<int32_t>(std::get<1>(t));
    const double v = std::get<2>(t);
    if (r0 < 0 || c0 < 0 || r0 >= n || c0 >= n) continue;
    if (drop_abs_lt > 0.0 && std::abs(v) < drop_abs_lt) continue;

    const int32_t rp = dec.invP[static_cast<std::size_t>(r0)];
    const int32_t cp = dec.invQ[static_cast<std::size_t>(c0)];
    if (rp < 0 || cp < 0) continue;

    const int32_t br = block_of_index[rp];
    const int32_t bc = block_of_index[cp];
    if (bc < br) {
      continue;
    }
    if (bc == br) {
      if (csr_full) {
        const int32_t pos = next[rp]++;
        dec.Aperm_csr.col_ind[pos] = cp;
        dec.Aperm_csr.values[pos] = v;
      }
      const int32_t start = dec.R[br];
      dec.diag_blocks_coo[br].emplace_back(static_cast<int>(rp - start), static_cast<int>(cp - start), v);
      continue;
    }
    // bc > br
    const int32_t pos = next[rp]++;
    dec.Aperm_csr.col_ind[pos] = cp;
    dec.Aperm_csr.values[pos] = v;
  }

  const auto t4 = prof ? Clock::now() : Clock::time_point{};

  const bool build_dag = (pick_block_sched() != BlockSched::Reverse);
  const auto t_dag0 = prof ? Clock::now() : Clock::time_point{};
  if (build_dag) {
    // Build dependency levels from block DAG derived from permuted CSR.
    // In UBTF solve, if there is any entry in block (i,j) with j>i, then i depends on j.
    std::vector<std::vector<int32_t>> deps(dec.nblocks);
    std::vector<int32_t> seen(dec.nblocks, -1);
    int32_t stamp = 0;
    for (int32_t b = 0; b < dec.nblocks; ++b) {
      const int32_t start = dec.R[b];
      const int32_t end = dec.R[b + 1];
      deps[b].clear();
      for (int32_t r = start; r < end; ++r) {
        for (int32_t p = dec.Aperm_csr.row_ptr[r]; p < dec.Aperm_csr.row_ptr[r + 1]; ++p) {
          const int32_t c = dec.Aperm_csr.col_ind[p];
          if (c >= end) {
            const int32_t bc = block_of_index[c];
            if (bc > b && seen[bc] != stamp) {
              seen[bc] = stamp;
              deps[b].push_back(bc);
            }
          }
        }
      }
      stamp++;
    }

    dec.block_deps = deps;
    dec.block_preds.assign(dec.nblocks, {});
    for (int32_t b = 0; b < dec.nblocks; ++b) {
      for (int32_t d : deps[b]) {
        if (d >= 0 && d < dec.nblocks) {
          dec.block_preds[d].push_back(b);
        }
      }
    }

    dec.block_level.assign(dec.nblocks, 0);
    dec.max_level = 0;
    for (int32_t b = dec.nblocks - 1; b >= 0; --b) {
      int32_t lev = 0;
      for (int32_t d : deps[b]) {
        lev = std::max<int32_t>(lev, dec.block_level[d] + 1);
      }
      dec.block_level[b] = lev;
      dec.max_level = std::max<int32_t>(dec.max_level, lev);
    }

    dec.blocks_by_level.assign(dec.max_level + 1, {});
    for (int32_t b = 0; b < dec.nblocks; ++b) {
      dec.blocks_by_level[dec.block_level[b]].push_back(b);
    }
  }

  if (prof) {
    const auto ms = [](Clock::time_point a, Clock::time_point b) {
      return std::chrono::duration<double, std::milli>(b - a).count();
    };
    const auto t5 = Clock::now();
    std::cerr << "USERALGO_PROFILE,btf_build_ms"
              << ",csc=" << ms(t0, t1)
              << ",btf_order=" << btf_ms_first
              << ",btf_retry=" << btf_ms_retry
              << ",btf_total=" << (btf_ms_first + btf_ms_retry)
              << ",merge_perm=" << ms(t2, t3)
              << ",csr_diag=" << ms(t_csr0, t4)
              << ",dag_levels=" << ms(t_dag0, t5)
              << ",sched=" << (build_dag ? "dag_or_level" : "reverse")
              << ",csr=" << (csr_full ? "full" : "offdiag")
              << ",n=" << n
              << ",nnz_total=" << static_cast<int32_t>(A_coo.size())
              << ",nnz_off=" << dec.Aperm_csr.nnz
              << ",nblocks_raw=" << dec.nblocks_raw
              << ",nblocks=" << dec.nblocks
              << ",nmatch=" << dec.nmatch
              << ",maxwork_req=" << dec.match_maxwork_requested
              << ",maxwork_used=" << dec.match_maxwork_used
              << "\n";
  }

  return dec;
}

static void print_topk_blocks(const char* tag,
                             const UbftfDecomposition& dec,
                             const UbftfFactors& fac,
                             const std::vector<double>& ms,
                             int k) {
  if (k <= 0) return;
  if (ms.empty()) return;

  std::vector<int32_t> idx(static_cast<std::size_t>(dec.nblocks), 0);
  for (int32_t b = 0; b < dec.nblocks; ++b) idx[static_cast<std::size_t>(b)] = b;

  std::stable_sort(idx.begin(), idx.end(), [&](int32_t a, int32_t b) {
    return ms[static_cast<std::size_t>(a)] > ms[static_cast<std::size_t>(b)];
  });

  double sum = 0.0;
  for (double x : ms) sum += x;
  double top = 0.0;
  const int kk = std::min<int>(k, dec.nblocks);
  for (int i = 0; i < kk; ++i) top += ms[static_cast<std::size_t>(idx[static_cast<std::size_t>(i)])];
  const int32_t max_bid = idx.empty() ? -1 : idx[0];
  const double max_ms = (max_bid >= 0) ? ms[static_cast<std::size_t>(max_bid)] : 0.0;

  std::cerr << "USERALGO_PROFILE,block_topk_" << tag
            << ",k=" << kk
            << ",sum_ms=" << sum
            << ",topk_ms=" << top
            << ",topk_pct=" << (sum > 0.0 ? (100.0 * top / sum) : 0.0)
            << ",max_bid=" << max_bid
            << ",max_ms=" << max_ms
            << "\n";

  for (int rank = 1; rank <= kk; ++rank) {
    const int32_t bid = idx[static_cast<std::size_t>(rank - 1)];
    const auto& bf = fac.blocks[static_cast<std::size_t>(bid)];
    const double t = ms[static_cast<std::size_t>(bid)];
    const char* kernel = nullptr;
    if (bf.bbd_enabled) kernel = "bbd";
    else if (bf.sub_fac) kernel = "subdecomp";
    else if (bf.gp) kernel = "gp";
    else if (!bf.dense_lu.empty()) kernel = "dense";
    else kernel = "klu";
    std::cerr << "USERALGO_PROFILE,block_topk_" << tag << "_row"
              << ",rank=" << rank
              << ",bid=" << bid
              << ",start=" << bf.start
              << ",size=" << bf.size
              << ",kernel=" << kernel
              << ",ms=" << t;
    if (bf.sub_fac) {
      std::cerr << ",sub_nblocks=" << bf.sub_nblocks
                << ",sub_max_block=" << bf.sub_max_block;
    }
    std::cerr << "\n";
  }
}

static UbftfFactors factor_blocks_parallel_depth(const UbftfDecomposition& dec,
                                                 std::size_t thread_count,
                                                 const klu_common* base_common,
                                                 int depth,
                                                 std::vector<double>* factor_ms);

struct SuperBlockSepStats {
  int32_t n1 = 0;
  int32_t n2 = 0;
  int32_t ns = 0;
  int32_t max_level = 0;
  int32_t components = 0;
};

static SuperBlockSepStats estimate_separator_bfs(const std::vector<std::tuple<int,int,double>>& coo, int32_t n) {
  SuperBlockSepStats st;
  if (n <= 0) return st;

  // Build an undirected adjacency (pattern-only, duplicates allowed).
  std::vector<int32_t> deg(static_cast<std::size_t>(n), 0);
  std::size_t edges = 0;
  for (const auto& t : coo) {
    const int r = std::get<0>(t);
    const int c = std::get<1>(t);
    if (r < 0 || c < 0 || r >= n || c >= n) continue;
    if (r == c) continue;
    deg[static_cast<std::size_t>(r)]++;
    deg[static_cast<std::size_t>(c)]++;
    edges += 2;
  }

  int32_t root = 0;
  for (int32_t i = 1; i < n; ++i) {
    if (deg[static_cast<std::size_t>(i)] > deg[static_cast<std::size_t>(root)]) root = i;
  }

  std::vector<int32_t> off(static_cast<std::size_t>(n) + 1, 0);
  for (int32_t i = 0; i < n; ++i) {
    off[static_cast<std::size_t>(i + 1)] = off[static_cast<std::size_t>(i)] + deg[static_cast<std::size_t>(i)];
  }
  std::vector<int32_t> adj(static_cast<std::size_t>(off.back()), 0);
  auto next = off;
  for (const auto& t : coo) {
    const int r = std::get<0>(t);
    const int c = std::get<1>(t);
    if (r < 0 || c < 0 || r >= n || c >= n) continue;
    if (r == c) continue;
    adj[static_cast<std::size_t>(next[static_cast<std::size_t>(r)]++)] = static_cast<int32_t>(c);
    adj[static_cast<std::size_t>(next[static_cast<std::size_t>(c)]++)] = static_cast<int32_t>(r);
  }

  // BFS levels from root (only within its component).
  std::vector<int32_t> level(static_cast<std::size_t>(n), -1);
  std::vector<int32_t> q;
  q.reserve(static_cast<std::size_t>(n));
  level[static_cast<std::size_t>(root)] = 0;
  q.push_back(root);
  std::size_t qi = 0;
  int32_t max_level = 0;
  while (qi < q.size()) {
    const int32_t u = q[qi++];
    const int32_t lu = level[static_cast<std::size_t>(u)];
    max_level = std::max<int32_t>(max_level, lu);
    for (int32_t p = off[static_cast<std::size_t>(u)]; p < off[static_cast<std::size_t>(u + 1)]; ++p) {
      const int32_t v = adj[static_cast<std::size_t>(p)];
      if (level[static_cast<std::size_t>(v)] != -1) continue;
      level[static_cast<std::size_t>(v)] = lu + 1;
      q.push_back(v);
    }
  }
  st.max_level = max_level;

  const int32_t sep_level = max_level / 2;
  std::vector<uint8_t> is_sep(static_cast<std::size_t>(n), 0);
  for (int32_t i = 0; i < n; ++i) {
    if (level[static_cast<std::size_t>(i)] == sep_level) {
      is_sep[static_cast<std::size_t>(i)] = 1;
      st.ns++;
    }
  }

  // Components in remaining graph (excluding separator vertices).
  std::vector<int32_t> comp(static_cast<std::size_t>(n), -1);
  std::vector<int32_t> comp_sizes;
  comp_sizes.reserve(16);

  for (int32_t i = 0; i < n; ++i) {
    if (is_sep[static_cast<std::size_t>(i)]) continue;
    if (comp[static_cast<std::size_t>(i)] != -1) continue;
    const int32_t cid = static_cast<int32_t>(comp_sizes.size());
    int32_t sz = 0;
    q.clear();
    q.push_back(i);
    comp[static_cast<std::size_t>(i)] = cid;
    qi = 0;
    while (qi < q.size()) {
      const int32_t u = q[qi++];
      sz++;
      for (int32_t p = off[static_cast<std::size_t>(u)]; p < off[static_cast<std::size_t>(u + 1)]; ++p) {
        const int32_t v = adj[static_cast<std::size_t>(p)];
        if (is_sep[static_cast<std::size_t>(v)]) continue;
        if (comp[static_cast<std::size_t>(v)] != -1) continue;
        comp[static_cast<std::size_t>(v)] = cid;
        q.push_back(v);
      }
    }
    comp_sizes.push_back(sz);
  }
  st.components = static_cast<int32_t>(comp_sizes.size());

  if (comp_sizes.empty()) {
    st.n1 = n - st.ns;
    st.n2 = 0;
    return st;
  }

  // Greedy balance components into two partitions (largest-first).
  std::vector<int32_t> order(static_cast<std::size_t>(comp_sizes.size()), 0);
  for (int32_t i = 0; i < static_cast<int32_t>(comp_sizes.size()); ++i) order[static_cast<std::size_t>(i)] = i;
  std::stable_sort(order.begin(), order.end(), [&](int32_t a, int32_t b) {
    return comp_sizes[static_cast<std::size_t>(a)] > comp_sizes[static_cast<std::size_t>(b)];
  });

  std::vector<uint8_t> comp_side(static_cast<std::size_t>(comp_sizes.size()), 0);
  int32_t s1 = 0;
  int32_t s2 = 0;
  for (int32_t cid : order) {
    if (s1 <= s2) {
      comp_side[static_cast<std::size_t>(cid)] = 0;
      s1 += comp_sizes[static_cast<std::size_t>(cid)];
    } else {
      comp_side[static_cast<std::size_t>(cid)] = 1;
      s2 += comp_sizes[static_cast<std::size_t>(cid)];
    }
  }

  st.n1 = s1;
  st.n2 = s2;
  return st;
}

struct BbdPartition {
  int32_t n = 0;
  int32_t n1 = 0;
  int32_t n2 = 0;
  int32_t ns = 0;
  std::vector<int32_t> perm;    // new -> old
  std::vector<int32_t> invperm; // old -> new
};

static bool build_bbd_partition_bfs(const std::vector<std::tuple<int,int,double>>& coo, int32_t n, BbdPartition& out) {
  out = BbdPartition{};
  out.n = n;
  if (n <= 0) return false;

  // Build undirected adjacency (pattern-only).
  std::vector<int32_t> deg(static_cast<std::size_t>(n), 0);
  for (const auto& t : coo) {
    const int r = std::get<0>(t);
    const int c = std::get<1>(t);
    if (r < 0 || c < 0 || r >= n || c >= n) continue;
    if (r == c) continue;
    deg[static_cast<std::size_t>(r)]++;
    deg[static_cast<std::size_t>(c)]++;
  }

  int32_t root = 0;
  for (int32_t i = 1; i < n; ++i) {
    if (deg[static_cast<std::size_t>(i)] > deg[static_cast<std::size_t>(root)]) root = i;
  }

  std::vector<int32_t> off(static_cast<std::size_t>(n) + 1, 0);
  for (int32_t i = 0; i < n; ++i) {
    off[static_cast<std::size_t>(i + 1)] = off[static_cast<std::size_t>(i)] + deg[static_cast<std::size_t>(i)];
  }
  std::vector<int32_t> adj(static_cast<std::size_t>(off.back()), 0);
  auto next = off;
  for (const auto& t : coo) {
    const int r = std::get<0>(t);
    const int c = std::get<1>(t);
    if (r < 0 || c < 0 || r >= n || c >= n) continue;
    if (r == c) continue;
    adj[static_cast<std::size_t>(next[static_cast<std::size_t>(r)]++)] = static_cast<int32_t>(c);
    adj[static_cast<std::size_t>(next[static_cast<std::size_t>(c)]++)] = static_cast<int32_t>(r);
  }

  // BFS levels.
  std::vector<int32_t> level(static_cast<std::size_t>(n), -1);
  std::vector<int32_t> q;
  q.reserve(static_cast<std::size_t>(n));
  level[static_cast<std::size_t>(root)] = 0;
  q.push_back(root);
  std::size_t qi = 0;
  int32_t max_level = 0;
  while (qi < q.size()) {
    const int32_t u = q[qi++];
    const int32_t lu = level[static_cast<std::size_t>(u)];
    max_level = std::max<int32_t>(max_level, lu);
    for (int32_t p = off[static_cast<std::size_t>(u)]; p < off[static_cast<std::size_t>(u + 1)]; ++p) {
      const int32_t v = adj[static_cast<std::size_t>(p)];
      if (level[static_cast<std::size_t>(v)] != -1) continue;
      level[static_cast<std::size_t>(v)] = lu + 1;
      q.push_back(v);
    }
  }

  const int32_t sep_level = max_level / 2;
  std::vector<uint8_t> is_sep(static_cast<std::size_t>(n), 0);
  int32_t ns = 0;
  for (int32_t i = 0; i < n; ++i) {
    if (level[static_cast<std::size_t>(i)] == sep_level) {
      is_sep[static_cast<std::size_t>(i)] = 1;
      ns++;
    }
  }
  if (ns <= 0 || ns >= n) return false;

  // Components in remaining graph.
  std::vector<int32_t> comp(static_cast<std::size_t>(n), -1);
  std::vector<int32_t> comp_sizes;
  comp_sizes.reserve(16);

  for (int32_t i = 0; i < n; ++i) {
    if (is_sep[static_cast<std::size_t>(i)]) continue;
    if (comp[static_cast<std::size_t>(i)] != -1) continue;
    const int32_t cid = static_cast<int32_t>(comp_sizes.size());
    int32_t sz = 0;
    q.clear();
    q.push_back(i);
    comp[static_cast<std::size_t>(i)] = cid;
    qi = 0;
    while (qi < q.size()) {
      const int32_t u = q[qi++];
      sz++;
      for (int32_t p = off[static_cast<std::size_t>(u)]; p < off[static_cast<std::size_t>(u + 1)]; ++p) {
        const int32_t v = adj[static_cast<std::size_t>(p)];
        if (is_sep[static_cast<std::size_t>(v)]) continue;
        if (comp[static_cast<std::size_t>(v)] != -1) continue;
        comp[static_cast<std::size_t>(v)] = cid;
        q.push_back(v);
      }
    }
    comp_sizes.push_back(sz);
  }
  if (comp_sizes.empty()) return false;

  // Greedy balance components into two partitions.
  std::vector<int32_t> order(static_cast<std::size_t>(comp_sizes.size()), 0);
  for (int32_t i = 0; i < static_cast<int32_t>(comp_sizes.size()); ++i) order[static_cast<std::size_t>(i)] = i;
  std::stable_sort(order.begin(), order.end(), [&](int32_t a, int32_t b) {
    return comp_sizes[static_cast<std::size_t>(a)] > comp_sizes[static_cast<std::size_t>(b)];
  });
  std::vector<uint8_t> comp_side(static_cast<std::size_t>(comp_sizes.size()), 0);
  int32_t s1 = 0;
  int32_t s2 = 0;
  for (int32_t cid : order) {
    if (s1 <= s2) {
      comp_side[static_cast<std::size_t>(cid)] = 0;
      s1 += comp_sizes[static_cast<std::size_t>(cid)];
    } else {
      comp_side[static_cast<std::size_t>(cid)] = 1;
      s2 += comp_sizes[static_cast<std::size_t>(cid)];
    }
  }

  const int32_t n1 = s1;
  const int32_t n2 = s2;
  if (n1 <= 0 || n2 <= 0) return false;

  out.n1 = n1;
  out.n2 = n2;
  out.ns = ns;
  out.perm.assign(static_cast<std::size_t>(n), 0);
  out.invperm.assign(static_cast<std::size_t>(n), -1);

  int32_t p1 = 0;
  int32_t p2 = n1;
  int32_t ps = n1 + n2;
  for (int32_t i = 0; i < n; ++i) {
    int32_t ni = -1;
    if (is_sep[static_cast<std::size_t>(i)]) {
      ni = ps++;
    } else {
      const int32_t cid = comp[static_cast<std::size_t>(i)];
      if (cid < 0) continue;
      if (comp_side[static_cast<std::size_t>(cid)] == 0) ni = p1++;
      else ni = p2++;
    }
    out.invperm[static_cast<std::size_t>(i)] = ni;
    out.perm[static_cast<std::size_t>(ni)] = i;
  }

  if (p1 != n1 || p2 != n1 + n2 || ps != n) return false;
  return true;
}

// Dense helpers (defined later in this file).
static bool dense_lu_factor_inplace(std::vector<double>& a, std::vector<int32_t>& piv, int32_t n);
static bool dense_lu_solve_inplace(const std::vector<double>& lu,
                                  const std::vector<int32_t>& piv,
                                  int32_t n,
                                  std::vector<double>& rhs);
static bool dense_lu_solve_inplace_strided(const std::vector<double>& lu,
                                          const std::vector<int32_t>& piv,
                                          int32_t n,
                                          double* B,
                                          int32_t ldim,
                                          int32_t nrhs);

static UbftfFactors::LocalFactor factor_local(const std::vector<std::tuple<int,int,double>>& coo,
                                              int32_t n,
                                              const klu_common* base_common,
                                              int dense_max,
                                              int klu_btf,
                                              int klu_ordering,
                                              int klu_scale) {
  UbftfFactors::LocalFactor out;
  out.n = n;
  out.ok = false;

  if (n <= 0) return out;
  if (n <= dense_max) {
    out.dense = true;
    out.dense_lu.assign(static_cast<std::size_t>(n) * static_cast<std::size_t>(n), 0.0);
    for (const auto& t : coo) {
      const int r = std::get<0>(t);
      const int c = std::get<1>(t);
      const double v = std::get<2>(t);
      if (r >= 0 && c >= 0 && r < n && c < n) {
        out.dense_lu[static_cast<std::size_t>(r) * n + c] += v;
      }
    }
    if (!dense_lu_factor_inplace(out.dense_lu, out.piv, n)) return out;
    out.ok = true;
    return out;
  }

  out.dense = false;
  out.A = coo_to_csc_i32(coo, n);

  const bool gp_enabled = pick_gp_enabled();
  const int gp_max_n = pick_gp_max_n();
  const bool gp_force = pick_gp_force();
  if (gp_enabled && (gp_force || n <= gp_max_n)) {
    out.gp = std::make_unique<GpLuFactor>();
    const GpLuOptions opt = pick_gp_options();
    if (gp_lu_factor(out.A, *out.gp, opt) && gp_gate_eval(*out.gp, n).accept) {
      out.ok = true;
      return out;
    }
    // Reject GP on stability/solution-selection grounds; fall back to KLU below.
    out.gp.reset();
  }

  auto& klu = KluDynamic::instance();
  if (base_common) out.common = *base_common;
  else if (!klu.defaults(&out.common)) return out;
  out.common.btf = klu_btf;
  out.common.ordering = klu_ordering;
  out.common.scale = klu_scale;

  out.sym = klu.analyze(n, out.A.col_ptr.data(), out.A.row_ind.data(), &out.common);
  if (!out.sym) return out;
  out.num = klu.factor(out.A.col_ptr.data(), out.A.row_ind.data(), out.A.values.data(), out.sym, &out.common);
  if (!out.num) return out;
  out.ok = true;
  return out;
}

static void free_local(UbftfFactors::LocalFactor& f) {
  auto& klu = KluDynamic::instance();
  f.gp.reset();
  if (f.num) {
    klu.free_numeric(&f.num, &f.common);
    f.num = nullptr;
  }
  if (f.sym) {
    klu.free_symbolic(&f.sym, &f.common);
    f.sym = nullptr;
  }
  f.A = CscMatrixI32{};
  f.dense_lu.clear();
  f.piv.clear();
  f.ok = false;
}

static bool solve_local_inplace(const UbftfFactors::LocalFactor& f, std::vector<double>& rhs) {
  if (!f.ok) return false;
  if (static_cast<int32_t>(rhs.size()) != f.n) return false;
  if (f.dense) {
    return dense_lu_solve_inplace(f.dense_lu, f.piv, f.n, rhs);
  }
  if (f.gp) {
    return gp_lu_solve_inplace_strided(*f.gp, rhs.data(), f.n, 1);
  }
  if (!f.sym || !f.num) return false;
  auto& klu = KluDynamic::instance();
  klu_common common = f.common;
  const int ok = klu.solve(f.sym, f.num, f.n, 1, rhs.data(), &common);
  return ok != 0;
}

static bool solve_local_inplace_strided(const UbftfFactors::LocalFactor& f, double* B, int32_t ldim, int32_t nrhs) {
  if (!f.ok) return false;
  if (!B) return false;
  if (ldim < f.n || nrhs <= 0) return false;
  if (f.dense) {
    return dense_lu_solve_inplace_strided(f.dense_lu, f.piv, f.n, B, ldim, nrhs);
  }
  if (f.gp) {
    return gp_lu_solve_inplace_strided(*f.gp, B, ldim, nrhs);
  }
  if (!f.sym || !f.num) return false;
  auto& klu = KluDynamic::instance();
  klu_common common = f.common;
  const int ok = klu.solve(f.sym, f.num, f.n, nrhs, B, &common);
  return ok != 0;
}

static bool solve_local_inplace_ptr(const UbftfFactors::LocalFactor& f, double* x) {
  return solve_local_inplace_strided(f, x, f.n, 1);
}

static bool dense_lu_solve_inplace_ptr(const std::vector<double>& lu,
                                      const std::vector<int32_t>& piv,
                                      int32_t n,
                                      double* x) {
  return dense_lu_solve_inplace_strided(lu, piv, n, x, n, 1);
}

static UbftfFactors::BlockFactor factor_one_block(const UbftfDecomposition& dec,
                                                  int32_t block_id,
                                                  const klu_common* base_common,
                                                  std::size_t thread_count,
                                                  int depth) {
  UbftfFactors::BlockFactor bf;
  bf.start = dec.R[block_id];
  bf.size = dec.R[block_id + 1] - dec.R[block_id];
  bf.ok = false;

  if (bf.size <= 0) {
    return bf;
  }

  const int dense_max = []() -> int {
    if (const char* env = blockdaglu_getenv_compat("USERALGO_DENSE_BLOCK_MAX")) {
      const int v = std::atoi(env);
      if (v >= 1) return v;
    }
    return 64;
  }();

  // Dense micro-kernel for small blocks (avoids KLU overhead when nblocks is large).
  if (bf.size <= dense_max) {
    const int m = static_cast<int>(bf.size);
    bf.dense_lu.assign(static_cast<std::size_t>(m) * static_cast<std::size_t>(m), 0.0);
    bf.piv.resize(static_cast<std::size_t>(m), 0);

    for (const auto& t : dec.diag_blocks_coo[block_id]) {
      const int r = std::get<0>(t);
      const int c = std::get<1>(t);
      const double v = std::get<2>(t);
      if (r >= 0 && c >= 0 && r < m && c < m) {
        bf.dense_lu[static_cast<std::size_t>(r) * static_cast<std::size_t>(m) + static_cast<std::size_t>(c)] += v;
      }
    }

    // LU with partial pivoting (in-place).
    for (int k = 0; k < m; ++k) {
      int piv = k;
      double best = std::abs(bf.dense_lu[static_cast<std::size_t>(k) * m + k]);
      for (int i = k + 1; i < m; ++i) {
        const double a = std::abs(bf.dense_lu[static_cast<std::size_t>(i) * m + k]);
        if (a > best) {
          best = a;
          piv = i;
        }
      }
      if (best == 0.0) {
        return bf;
      }
      bf.piv[static_cast<std::size_t>(k)] = piv;
      if (piv != k) {
        for (int j = 0; j < m; ++j) {
          std::swap(bf.dense_lu[static_cast<std::size_t>(k) * m + j],
                    bf.dense_lu[static_cast<std::size_t>(piv) * m + j]);
        }
      }
      const double akk = bf.dense_lu[static_cast<std::size_t>(k) * m + k];
      for (int i = k + 1; i < m; ++i) {
        double& aik = bf.dense_lu[static_cast<std::size_t>(i) * m + k];
        aik /= akk;
        const double lik = aik;
        for (int j = k + 1; j < m; ++j) {
          bf.dense_lu[static_cast<std::size_t>(i) * m + j] -= lik * bf.dense_lu[static_cast<std::size_t>(k) * m + j];
        }
      }
    }

    bf.ok = true;
    return bf;
  }

  // Profile-only: estimate how hard it would be to split this super-block with a BFS separator.
  if (profile_enabled_local() && pick_superblock_stats_enabled() && depth == 0 && bf.size >= pick_superblock_min_size()) {
    const auto st = estimate_separator_bfs(dec.diag_blocks_coo[block_id], bf.size);
    std::cerr << "USERALGO_PROFILE,superblock_sep_stats"
              << ",bid=" << block_id
              << ",size=" << bf.size
              << ",n1=" << st.n1
              << ",n2=" << st.n2
              << ",ns=" << st.ns
              << ",sep_pct=" << (bf.size > 0 ? (100.0 * static_cast<double>(st.ns) / static_cast<double>(bf.size)) : 0.0)
              << ",max_level=" << st.max_level
              << ",components=" << st.components
              << "\n";
  }

  // Experimental: split super-block into BBD (2-way) + Schur complement, to enable block-in-block parallelism.
  if (depth == 0 && pick_superblock_bbd_enabled() && bf.size >= pick_superblock_min_size()) {
    try {
      const int32_t m = bf.size;
      const auto st = estimate_separator_bfs(dec.diag_blocks_coo[block_id], m);
      const double sep_pct = (m > 0) ? (100.0 * static_cast<double>(st.ns) / static_cast<double>(m)) : 100.0;
      if (st.ns > 0 && st.ns <= pick_superblock_sep_max() && sep_pct <= pick_superblock_sep_pct_max()) {
        BbdPartition part;
        if (build_bbd_partition_bfs(dec.diag_blocks_coo[block_id], m, part) &&
            part.ns == st.ns &&
            part.n1 > 0 && part.n2 > 0 && part.ns > 0) {

          // Gather sub-block COO in new ordering.
          std::vector<std::tuple<int,int,double>> A11;
          std::vector<std::tuple<int,int,double>> A22;
          std::vector<std::tuple<int,int,double>> A1s;
          std::vector<std::tuple<int,int,double>> A2s;
          std::vector<std::tuple<int,int,double>> As1;
          std::vector<std::tuple<int,int,double>> As2;
          std::vector<std::tuple<int,int,double>> Ass_coo;

          A11.reserve(dec.diag_blocks_coo[block_id].size() / 4);
          A22.reserve(dec.diag_blocks_coo[block_id].size() / 4);

          bool has_cross = false;
          for (const auto& t : dec.diag_blocks_coo[block_id]) {
            const int r0 = std::get<0>(t);
            const int c0 = std::get<1>(t);
            const double v = std::get<2>(t);
            if (r0 < 0 || c0 < 0 || r0 >= m || c0 >= m) continue;
            const int r = part.invperm[static_cast<std::size_t>(r0)];
            const int c = part.invperm[static_cast<std::size_t>(c0)];
            if (r < 0 || c < 0) continue;

            const bool r1 = r < part.n1;
            const bool r2 = (r >= part.n1 && r < part.n1 + part.n2);
            const bool rs = r >= part.n1 + part.n2;
            const bool c1 = c < part.n1;
            const bool c2 = (c >= part.n1 && c < part.n1 + part.n2);
            const bool cs = c >= part.n1 + part.n2;

            if ((r1 && c2) || (r2 && c1)) {
              has_cross = true;
              break;
            }

            if (r1 && c1) {
              A11.emplace_back(r, c, v);
            } else if (r2 && c2) {
              A22.emplace_back(r - part.n1, c - part.n1, v);
            } else if (rs && cs) {
              Ass_coo.emplace_back(r - (part.n1 + part.n2), c - (part.n1 + part.n2), v);
            } else if (r1 && cs) {
              A1s.emplace_back(r, c - (part.n1 + part.n2), v);
            } else if (r2 && cs) {
              A2s.emplace_back(r - part.n1, c - (part.n1 + part.n2), v);
            } else if (rs && c1) {
              As1.emplace_back(r - (part.n1 + part.n2), c, v);
            } else if (rs && c2) {
              As2.emplace_back(r - (part.n1 + part.n2), c - part.n1, v);
            } else {
              // entries inside separator/partitions handled above; no other blocks should exist
            }
          }

          if (!has_cross) {
            const int dense_max = []() -> int {
              if (const char* env = blockdaglu_getenv_compat("USERALGO_DENSE_BLOCK_MAX")) {
                const int v = std::atoi(env);
                if (v >= 1) return v;
              }
              return 64;
            }();

            // Factor A11 and A22 independently.
            auto f1 = std::make_unique<UbftfFactors::LocalFactor>(
              factor_local(A11, part.n1, base_common, dense_max,
                           pick_block_klu_btf(), pick_block_klu_ordering(), pick_block_klu_scale()));
            auto f2 = std::make_unique<UbftfFactors::LocalFactor>(
              factor_local(A22, part.n2, base_common, dense_max,
                           pick_block_klu_btf(), pick_block_klu_ordering(), pick_block_klu_scale()));

            const double rcond_min = pick_superblock_rcond_min();
            const auto rcond_ok = [&](const UbftfFactors::LocalFactor& f) {
              if (!f.ok) return false;
              if (f.dense) return true;
              if (f.common.status != KLU_OK) return false;
              if (f.common.numerical_rank != f.n) return false;
              if (f.common.rcond > 0.0 && f.common.rcond < rcond_min) return false;
              return true;
            };

            if (rcond_ok(*f1) && rcond_ok(*f2)) {
              // Build Ass dense.
              std::vector<double> Ass(static_cast<std::size_t>(part.ns) * static_cast<std::size_t>(part.ns), 0.0);
              for (const auto& t : Ass_coo) {
                const int r = std::get<0>(t);
                const int c = std::get<1>(t);
                const double v = std::get<2>(t);
                if (r >= 0 && c >= 0 && r < part.ns && c < part.ns) {
                  Ass[static_cast<std::size_t>(r) * part.ns + c] += v;
                }
              }

              // Build A1s/A2s dense (column-major) and solve for X = inv(Aii)*Ais.
              std::vector<double> X1(static_cast<std::size_t>(part.n1) * static_cast<std::size_t>(part.ns), 0.0);
              std::vector<double> X2(static_cast<std::size_t>(part.n2) * static_cast<std::size_t>(part.ns), 0.0);
              for (const auto& t : A1s) {
                const int r = std::get<0>(t);
                const int c = std::get<1>(t);
                const double v = std::get<2>(t);
                if (r >= 0 && c >= 0 && r < part.n1 && c < part.ns) {
                  X1[static_cast<std::size_t>(r) + static_cast<std::size_t>(part.n1) * static_cast<std::size_t>(c)] += v;
                }
              }
              for (const auto& t : A2s) {
                const int r = std::get<0>(t);
                const int c = std::get<1>(t);
                const double v = std::get<2>(t);
                if (r >= 0 && c >= 0 && r < part.n2 && c < part.ns) {
                  X2[static_cast<std::size_t>(r) + static_cast<std::size_t>(part.n2) * static_cast<std::size_t>(c)] += v;
                }
              }

              if (solve_local_inplace_strided(*f1, X1.data(), part.n1, part.ns) &&
                  solve_local_inplace_strided(*f2, X2.data(), part.n2, part.ns)) {

                // Compute Schur: S = Ass - As1*X1 - As2*X2.
                std::vector<double> S = Ass; // row-major

                // Build As1/As2 CSR for later use (and for Schur update).
                const CsrMatrixI32 As1_csr = coo_to_csr_i32(As1, part.ns);
                const CsrMatrixI32 As2_csr = coo_to_csr_i32(As2, part.ns);

                auto schur_update = [&](const CsrMatrixI32& As, const std::vector<double>& X, int32_t ni) {
                  // X is ni x ns, column-major.
                  for (int32_t r = 0; r < part.ns; ++r) {
                    for (int32_t p = As.row_ptr[static_cast<std::size_t>(r)]; p < As.row_ptr[static_cast<std::size_t>(r + 1)]; ++p) {
                      const int32_t k = As.col_ind[static_cast<std::size_t>(p)];
                      const double a = As.values[static_cast<std::size_t>(p)];
                      if (k < 0 || k >= ni) continue;
                      const double* xk = &X[static_cast<std::size_t>(k)];
                      for (int32_t j = 0; j < part.ns; ++j) {
                        S[static_cast<std::size_t>(r) * part.ns + j] -= a * xk[static_cast<std::size_t>(ni) * static_cast<std::size_t>(j)];
                      }
                    }
                  }
                };

                schur_update(As1_csr, X1, part.n1);
                schur_update(As2_csr, X2, part.n2);

                std::vector<int32_t> Spiv;
                std::vector<double> Slu = S;
                if (dense_lu_factor_inplace(Slu, Spiv, part.ns)) {
                  // Build A1s/A2s CSR too (for solve stage).
                  const CsrMatrixI32 A1s_csr = coo_to_csr_i32(A1s, part.n1);
                  const CsrMatrixI32 A2s_csr = coo_to_csr_i32(A2s, part.n2);

                  bf.bbd_enabled = true;
                  bf.bbd_n1 = part.n1;
                  bf.bbd_n2 = part.n2;
                  bf.bbd_ns = part.ns;
                  bf.bbd_perm = std::move(part.perm);
                  bf.bbd_invperm = std::move(part.invperm);
                  bf.bbd_f1 = std::move(f1);
                  bf.bbd_f2 = std::move(f2);
                  bf.bbd_A1s = A1s_csr;
                  bf.bbd_A2s = A2s_csr;
                  bf.bbd_As1 = As1_csr;
                  bf.bbd_As2 = As2_csr;
                  bf.bbd_Ass = std::move(Ass);
                  bf.bbd_S_lu = std::move(Slu);
                  bf.bbd_S_piv = std::move(Spiv);

                  bf.ok = true;
                  return bf;
                }
              }
            }

            if (f1) free_local(*f1);
            if (f2) free_local(*f2);
          }
        }
      }
    } catch (...) {
      // Fall back to baseline KLU below.
    }
  }

  // Optional nested decomposition for very large blocks (experimental).
  // This reduces the Amdahl bottleneck when one giant diagonal block dominates LU time.
  if (depth == 0 && pick_superblock_subdecomp_enabled() && bf.size >= pick_superblock_min_size()) {
    try {
      const int32_t m = bf.size;
      const int32_t target_blocks =
        std::max<int32_t>(1, std::min<int32_t>(m, static_cast<int32_t>(std::max<std::size_t>(1, thread_count) * 4)));
      const int32_t min_block = static_cast<int32_t>(pick_superblock_min_block_size());

      bf.sub_dec = std::make_unique<UbftfDecomposition>(
        build_decomposition_btf(dec.diag_blocks_coo[block_id], m, target_blocks, min_block));
      UbftfFactors sub = factor_blocks_parallel_depth(*bf.sub_dec, thread_count, base_common, depth + 1, nullptr);

      int32_t max_sz = 0;
      for (const auto& b : sub.blocks) max_sz = std::max<int32_t>(max_sz, b.size);
      bf.sub_nblocks = bf.sub_dec ? bf.sub_dec->nblocks : 0;
      bf.sub_max_block = max_sz;
      bf.sub_fac = std::make_unique<UbftfFactors>(std::move(sub));

      // If any nested block fails, fall back to KLU on the full block.
      for (const auto& b : bf.sub_fac->blocks) {
        if (!b.ok) {
          bf.sub_fac.reset();
          bf.sub_dec.reset();
          bf.sub_nblocks = 0;
          bf.sub_max_block = 0;
          break;
        }
      }

      if (bf.sub_fac && bf.sub_dec) {
        bf.ok = true;
        return bf;
      }
    } catch (...) {
      // Fall back to KLU path below.
      bf.sub_fac.reset();
      bf.sub_dec.reset();
      bf.sub_nblocks = 0;
      bf.sub_max_block = 0;
    }
  }

  bf.A = coo_to_csc_i32(dec.diag_blocks_coo[block_id], bf.size);

  const bool gp_enabled = pick_gp_enabled();
  const int gp_max_n = pick_gp_max_n();
  const bool gp_force = pick_gp_force();
  if (gp_enabled && (gp_force || bf.size <= gp_max_n)) {
    bf.gp_tried = true;
    bf.gp = std::make_unique<GpLuFactor>();
    const GpLuOptions opt = pick_gp_options();
    if (gp_lu_factor(bf.A, *bf.gp, opt)) {
      const GpGateResult gr = gp_gate_eval(*bf.gp, bf.size);
      bf.gp_row_swaps = bf.gp->row_swap_count;
      bf.gp_tiny_pivots = bf.gp->tiny_pivot_count;
      bf.gp_min_abs_diag = gr.min_abs_diag;
      bf.gp_reject_reason = gr.reject_reason;
      if (gr.accept) {
        bf.ok = true;
        return bf;
      }
    } else {
      bf.gp_reject_reason = 1;
    }
    // Reject and fall back to KLU below (keep diag stats for profiling).
    bf.gp.reset();
  }

  auto& klu = KluDynamic::instance();
  if (base_common) {
    bf.common = *base_common;
  } else if (!klu.defaults(&bf.common)) {
    return bf;
  }
  // Second-level ordering within the (possibly merged) block.
  // Default to organizer baseline defaults to keep solve behaviour stable.
  bf.common.ordering = pick_block_klu_ordering();
  const double tol = pick_block_klu_tol_for(bf.size);
  if (tol >= 0.0) {
    bf.common.tol = tol;
  }

  auto cleanup_klu = [&]() {
    if (bf.num) {
      klu.free_numeric(&bf.num, &bf.common);
      bf.num = nullptr;
    }
    if (bf.sym) {
      klu.free_symbolic(&bf.sym, &bf.common);
      bf.sym = nullptr;
    }
  };

  auto try_klu = [&](int btf, int scale) -> bool {
    cleanup_klu();
    bf.common.btf = btf;
    bf.common.scale = scale;
    bf.sym = klu.analyze(bf.size, bf.A.col_ptr.data(), bf.A.row_ind.data(), &bf.common);
    if (!bf.sym) return false;
    bf.num = klu.factor(bf.A.col_ptr.data(), bf.A.row_ind.data(), bf.A.values.data(), bf.sym, &bf.common);
    return bf.num != nullptr;
  };

  // Attempt 1: fast defaults for blocks.
  const int btf0 = pick_block_klu_btf_for(bf.size);
  const int scale0 = pick_block_klu_scale_for(bf.size);
  if (!try_klu(btf0, scale0)) {
    // Attempt 2: enable scaling only (often fixes near-singular pivot issues with modest overhead).
    if (!try_klu(btf0, 1)) {
      // Attempt 3: allow internal BTF + scaling as a last resort.
      if (!try_klu(1, 1)) {
        cleanup_klu();
        return bf;
      }
    }
  }

  bf.ok = true;
  return bf;
}

static UbftfFactors factor_blocks_parallel_depth(const UbftfDecomposition& dec,
                                                 std::size_t thread_count,
                                                 const klu_common* base_common,
                                                 int depth,
                                                 std::vector<double>* factor_ms) {
  using Clock = std::chrono::high_resolution_clock;
  const bool prof = profile_enabled_local();

  UbftfFactors fac;
  fac.n = dec.n;
  fac.blocks.resize(dec.nblocks);

  std::vector<int32_t> order(static_cast<std::size_t>(dec.nblocks), 0);
  for (int32_t i = 0; i < dec.nblocks; ++i) order[static_cast<std::size_t>(i)] = i;

  const FactorOrder fo = pick_factor_order();
  auto decide_factor_order = [&](FactorOrder requested) -> FactorOrder {
    if (requested != FactorOrder::Auto) return requested;
    if (thread_count <= 1 || dec.nblocks <= 1) return FactorOrder::Id;

    // Heuristic: cost-ordering helps when there is a large tail (one/few giant diagonal blocks)
    // competing with many small blocks. For smaller cases, keep stable id ordering to reduce overhead.
    if (dec.n < 50000) return FactorOrder::Id;
    if (dec.nblocks < static_cast<int32_t>(thread_count) * 2) return FactorOrder::Id;

    double sum_cost = 0.0;
    double max_cost = 0.0;
    int32_t max_sz = 0;
    for (int32_t b = 0; b < dec.nblocks; ++b) {
      const int32_t sz = dec.R[static_cast<std::size_t>(b + 1)] - dec.R[static_cast<std::size_t>(b)];
      max_sz = std::max<int32_t>(max_sz, sz);
      const double nnz = static_cast<double>(dec.diag_blocks_coo[static_cast<std::size_t>(b)].size());
      const double cost = nnz * static_cast<double>(std::max<int32_t>(1, sz));
      sum_cost += cost;
      max_cost = std::max(max_cost, cost);
    }
    const double mean_cost = (dec.nblocks > 0) ? (sum_cost / static_cast<double>(dec.nblocks)) : 0.0;
    const double skew = (mean_cost > 0.0) ? (max_cost / mean_cost) : 0.0;
    const double top1_share = (sum_cost > 0.0) ? (max_cost / sum_cost) : 0.0;

    if (max_sz >= 4096 && (top1_share >= 0.18 || skew >= 7.0)) {
      return FactorOrder::Cost;
    }
    return FactorOrder::Id;
  };

  const FactorOrder decided_fo = decide_factor_order(fo);
  if (decided_fo == FactorOrder::Cost) {
    // Schedule heavy blocks first to reduce tail latency (Amdahl critical path).
    // Diagonal blocks are independent to factor, so this does not affect correctness.
    std::stable_sort(order.begin(), order.end(), [&](int32_t a, int32_t b) {
      const int32_t sa = dec.R[static_cast<std::size_t>(a + 1)] - dec.R[static_cast<std::size_t>(a)];
      const int32_t sb = dec.R[static_cast<std::size_t>(b + 1)] - dec.R[static_cast<std::size_t>(b)];
      const double na = static_cast<double>(dec.diag_blocks_coo[static_cast<std::size_t>(a)].size());
      const double nb = static_cast<double>(dec.diag_blocks_coo[static_cast<std::size_t>(b)].size());
      const double ca = na * static_cast<double>(std::max<int32_t>(1, sa));
      const double cb = nb * static_cast<double>(std::max<int32_t>(1, sb));
      if (ca != cb) return ca > cb;
      return sa > sb;
    });
  }

  // Avoid nested use of the same pool inside a pool worker (subdecomp can recurse).
  // Depth==0 is the common path (top-level factor) where reuse yields most benefit.
  ThreadPool local_pool(thread_count);
  ThreadPool& pool = (depth == 0) ? ReusedPool(thread_count) : local_pool;
  // Chunk to amortize overhead when nblocks is large.
  const std::size_t chunk = (decided_fo == FactorOrder::Cost) ? 1 : ((dec.nblocks > 2048) ? 16 : 1);
  pool.parallel_for(0, static_cast<std::size_t>(dec.nblocks), [&](std::size_t oi) {
    const int32_t bid = order[oi];
    const auto t0 = (prof && factor_ms) ? Clock::now() : Clock::time_point{};
    fac.blocks[static_cast<std::size_t>(bid)] =
      factor_one_block(dec, bid, base_common, thread_count, depth);
    if (prof && factor_ms) {
      const auto t1 = Clock::now();
      (*factor_ms)[static_cast<std::size_t>(bid)] =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    }
  }, chunk);

  return fac;
}

UbftfFactors factor_blocks_parallel(const UbftfDecomposition& dec, std::size_t thread_count) {
  klu_common base_common{};
  const klu_common* base = nullptr;
  {
    auto& klu = KluDynamic::instance();
    if (klu.defaults(&base_common)) {
      base = &base_common;
    }
  }

  const bool prof = profile_enabled_local();
  const int topk = pick_block_topk();
  std::vector<double> factor_ms;
  std::vector<double>* out_ms = nullptr;
  if (prof && topk > 0) {
    factor_ms.assign(static_cast<std::size_t>(dec.nblocks), 0.0);
    out_ms = &factor_ms;
  }

  UbftfFactors fac = factor_blocks_parallel_depth(dec, thread_count, base, /*depth=*/0, out_ms);

  if (prof) {
    std::size_t dense = 0;
    std::size_t klu = 0;
    std::size_t gp = 0;
    std::size_t sub = 0;
    std::size_t bbd = 0;
    std::size_t bad = 0;
    int32_t max_sz = 0;
    for (const auto& b : fac.blocks) {
      if (!b.ok) bad++;
      max_sz = std::max<int32_t>(max_sz, b.size);
      if (b.bbd_enabled) bbd++;
      else if (b.sub_fac) sub++;
      else if (b.gp) gp++;
      else if (!b.dense_lu.empty()) dense++;
      else klu++;
    }
    std::cerr << "USERALGO_PROFILE,factor_blocks"
              << ",nblocks=" << dec.nblocks
              << ",dense_blocks=" << dense
              << ",gp_blocks=" << gp
              << ",klu_blocks=" << klu
              << ",bbd_blocks=" << bbd
              << ",subdecomp_blocks=" << sub
              << ",bad_blocks=" << bad
              << ",max_block=" << max_sz
              << ",threads=" << thread_count
              << "\n";

    if (topk > 0 && !factor_ms.empty()) {
      print_topk_blocks("factor", dec, fac, factor_ms, topk);
    }
  }

  // GP gate summary (low overhead). Can be emitted even with USERALGO_PROFILE=0 for clean timing
  // while still collecting coverage statistics.
  if (pick_gp_gate_log()) {
    std::size_t gp_tried = 0;
    std::size_t gp_used = 0;
    std::size_t gp_rej_fail = 0;
    std::size_t gp_rej_swaps = 0;
    std::size_t gp_rej_tiny = 0;
    std::size_t gp_rej_mindiag = 0;
    int32_t gp_max_used_n = 0;
    int32_t gp_max_used_bid = -1;
    for (std::size_t i = 0; i < fac.blocks.size(); ++i) {
      const auto& b = fac.blocks[i];
      if (b.gp_tried) gp_tried++;
      if (b.gp) {
        gp_used++;
        if (b.size > gp_max_used_n) {
          gp_max_used_n = b.size;
          gp_max_used_bid = static_cast<int32_t>(i);
        }
      }
      switch (b.gp_reject_reason) {
        case 1: gp_rej_fail++; break;
        case 2: gp_rej_swaps++; break;
        case 3: gp_rej_tiny++; break;
        case 4: gp_rej_mindiag++; break;
        default: break;
      }
    }
    std::cerr << "USERALGO_PROFILE,gp_gate"
              << ",tried=" << gp_tried
              << ",used=" << gp_used
              << ",rej_fail=" << gp_rej_fail
              << ",rej_swaps=" << gp_rej_swaps
              << ",rej_tiny=" << gp_rej_tiny
              << ",rej_mindiag=" << gp_rej_mindiag
              << ",swap_policy=" << (pick_gp_require_no_swaps() ? "strict" : "open")
              << ",swap_max_n=" << pick_gp_swap_max_n()
              << ",swap_max=" << pick_gp_swap_max()
              << ",min_abs_diag=" << pick_gp_min_abs_diag()
              << ",max_used_n=" << gp_max_used_n
              << ",max_used_bid=" << gp_max_used_bid
              << "\n";
  }

  return fac;
}

void free_factors(UbftfFactors& fac) {
  auto& klu = KluDynamic::instance();
  for (auto& b : fac.blocks) {
    if (b.bbd_f1) {
      free_local(*b.bbd_f1);
      b.bbd_f1.reset();
    }
    if (b.bbd_f2) {
      free_local(*b.bbd_f2);
      b.bbd_f2.reset();
    }
    b.bbd_perm.clear();
    b.bbd_invperm.clear();
    b.bbd_A1s = CsrMatrixI32{};
    b.bbd_A2s = CsrMatrixI32{};
    b.bbd_As1 = CsrMatrixI32{};
    b.bbd_As2 = CsrMatrixI32{};
    b.bbd_Ass.clear();
    b.bbd_S_lu.clear();
    b.bbd_S_piv.clear();
    b.bbd_enabled = false;
    b.bbd_n1 = b.bbd_n2 = b.bbd_ns = 0;

    if (b.sub_fac) {
      free_factors(*b.sub_fac);
      b.sub_fac.reset();
      b.sub_dec.reset();
      b.sub_nblocks = 0;
      b.sub_max_block = 0;
    }
    b.gp.reset();
    if (b.num) {
      klu.free_numeric(&b.num, &b.common);
      b.num = nullptr;
    }
    if (b.sym) {
      klu.free_symbolic(&b.sym, &b.common);
      b.sym = nullptr;
    }
    b.dense_lu.clear();
    b.piv.clear();
  }
}

static double vec_norm2(const std::vector<double>& v) {
  double s = 0.0;
  for (double x : v) s += x * x;
  return std::sqrt(s);
}

static bool dense_lu_solve_inplace(const UbftfFactors::BlockFactor& bf, std::vector<double>& rhs) {
  const int m = static_cast<int>(bf.size);
  if (m <= 0) return false;
  if (rhs.size() != static_cast<std::size_t>(m)) return false;
  if (bf.dense_lu.size() != static_cast<std::size_t>(m) * static_cast<std::size_t>(m)) return false;
  if (bf.piv.size() != static_cast<std::size_t>(m)) return false;

  // Apply row pivots (same swaps as during factorization).
  for (int k = 0; k < m; ++k) {
    const int piv = static_cast<int>(bf.piv[static_cast<std::size_t>(k)]);
    if (piv != k) {
      std::swap(rhs[static_cast<std::size_t>(k)], rhs[static_cast<std::size_t>(piv)]);
    }
  }

  // Forward substitution for L (unit diagonal).
  for (int i = 0; i < m; ++i) {
    double sum = rhs[static_cast<std::size_t>(i)];
    for (int j = 0; j < i; ++j) {
      sum -= bf.dense_lu[static_cast<std::size_t>(i) * m + j] * rhs[static_cast<std::size_t>(j)];
    }
    rhs[static_cast<std::size_t>(i)] = sum;
  }

  // Backward substitution for U.
  for (int i = m - 1; i >= 0; --i) {
    double sum = rhs[static_cast<std::size_t>(i)];
    for (int j = i + 1; j < m; ++j) {
      sum -= bf.dense_lu[static_cast<std::size_t>(i) * m + j] * rhs[static_cast<std::size_t>(j)];
    }
    const double diag = bf.dense_lu[static_cast<std::size_t>(i) * m + i];
    if (diag == 0.0) return false;
    rhs[static_cast<std::size_t>(i)] = sum / diag;
  }

  return true;
}

static bool dense_lu_factor_inplace(std::vector<double>& a, std::vector<int32_t>& piv, int32_t n) {
  if (n <= 0) return false;
  if (a.size() != static_cast<std::size_t>(n) * static_cast<std::size_t>(n)) return false;
  piv.assign(static_cast<std::size_t>(n), 0);

  for (int32_t k = 0; k < n; ++k) {
    int32_t p = k;
    double best = std::abs(a[static_cast<std::size_t>(k) * n + k]);
    for (int32_t i = k + 1; i < n; ++i) {
      const double v = std::abs(a[static_cast<std::size_t>(i) * n + k]);
      if (v > best) {
        best = v;
        p = i;
      }
    }
    if (best == 0.0) return false;
    piv[static_cast<std::size_t>(k)] = p;
    if (p != k) {
      for (int32_t j = 0; j < n; ++j) {
        std::swap(a[static_cast<std::size_t>(k) * n + j], a[static_cast<std::size_t>(p) * n + j]);
      }
    }
    const double akk = a[static_cast<std::size_t>(k) * n + k];
    for (int32_t i = k + 1; i < n; ++i) {
      double& aik = a[static_cast<std::size_t>(i) * n + k];
      aik /= akk;
      const double lik = aik;
      for (int32_t j = k + 1; j < n; ++j) {
        a[static_cast<std::size_t>(i) * n + j] -= lik * a[static_cast<std::size_t>(k) * n + j];
      }
    }
  }
  return true;
}

static bool dense_lu_solve_inplace(const std::vector<double>& lu,
                                  const std::vector<int32_t>& piv,
                                  int32_t n,
                                  std::vector<double>& rhs) {
  if (n <= 0) return false;
  if (rhs.size() != static_cast<std::size_t>(n)) return false;
  if (lu.size() != static_cast<std::size_t>(n) * static_cast<std::size_t>(n)) return false;
  if (piv.size() != static_cast<std::size_t>(n)) return false;

  for (int32_t k = 0; k < n; ++k) {
    const int32_t p = piv[static_cast<std::size_t>(k)];
    if (p != k) std::swap(rhs[static_cast<std::size_t>(k)], rhs[static_cast<std::size_t>(p)]);
  }

  for (int32_t i = 0; i < n; ++i) {
    double sum = rhs[static_cast<std::size_t>(i)];
    for (int32_t j = 0; j < i; ++j) {
      sum -= lu[static_cast<std::size_t>(i) * n + j] * rhs[static_cast<std::size_t>(j)];
    }
    rhs[static_cast<std::size_t>(i)] = sum;
  }

  for (int32_t i = n - 1; i >= 0; --i) {
    double sum = rhs[static_cast<std::size_t>(i)];
    for (int32_t j = i + 1; j < n; ++j) {
      sum -= lu[static_cast<std::size_t>(i) * n + j] * rhs[static_cast<std::size_t>(j)];
    }
    const double diag = lu[static_cast<std::size_t>(i) * n + i];
    if (diag == 0.0) return false;
    rhs[static_cast<std::size_t>(i)] = sum / diag;
  }
  return true;
}

static bool dense_lu_solve_inplace_strided(const std::vector<double>& lu,
                                          const std::vector<int32_t>& piv,
                                          int32_t n,
                                          double* B,
                                          int32_t ldim,
                                          int32_t nrhs) {
  if (!B) return false;
  if (n <= 0 || ldim < n || nrhs <= 0) return false;
  if (lu.size() != static_cast<std::size_t>(n) * static_cast<std::size_t>(n)) return false;
  if (piv.size() != static_cast<std::size_t>(n)) return false;

  for (int32_t k = 0; k < n; ++k) {
    const int32_t p = piv[static_cast<std::size_t>(k)];
    if (p == k) continue;
    for (int32_t j = 0; j < nrhs; ++j) {
      std::swap(B[static_cast<std::size_t>(k) + static_cast<std::size_t>(ldim) * j],
                B[static_cast<std::size_t>(p) + static_cast<std::size_t>(ldim) * j]);
    }
  }

  // Forward solve L (unit diag).
  for (int32_t i = 0; i < n; ++i) {
    for (int32_t j = 0; j < nrhs; ++j) {
      double sum = B[static_cast<std::size_t>(i) + static_cast<std::size_t>(ldim) * j];
      for (int32_t k = 0; k < i; ++k) {
        sum -= lu[static_cast<std::size_t>(i) * n + k] *
               B[static_cast<std::size_t>(k) + static_cast<std::size_t>(ldim) * j];
      }
      B[static_cast<std::size_t>(i) + static_cast<std::size_t>(ldim) * j] = sum;
    }
  }

  // Backward solve U.
  for (int32_t i = n - 1; i >= 0; --i) {
    for (int32_t j = 0; j < nrhs; ++j) {
      double sum = B[static_cast<std::size_t>(i) + static_cast<std::size_t>(ldim) * j];
      for (int32_t k = i + 1; k < n; ++k) {
        sum -= lu[static_cast<std::size_t>(i) * n + k] *
               B[static_cast<std::size_t>(k) + static_cast<std::size_t>(ldim) * j];
      }
      const double diag = lu[static_cast<std::size_t>(i) * n + i];
      if (diag == 0.0) return false;
      B[static_cast<std::size_t>(i) + static_cast<std::size_t>(ldim) * j] = sum / diag;
    }
  }

  return true;
}

bool solve_ubtf(const UbftfDecomposition& dec, const UbftfFactors& fac, std::vector<double>& b, std::size_t thread_count) {
  using Clock = std::chrono::high_resolution_clock;
  struct DepthGuard {
    int& depth;
    explicit DepthGuard(int& d) : depth(d) { ++depth; }
    ~DepthGuard() { --depth; }
  };
  static thread_local int solve_depth = 0;
  DepthGuard dg(solve_depth);

  if (static_cast<int32_t>(b.size()) != dec.n) {
    return false;
  }
  if (fac.blocks.size() != static_cast<std::size_t>(dec.nblocks)) {
    return false;
  }

  // Permute RHS to permuted row order.
  std::vector<double> bperm(dec.n);
  for (int32_t i = 0; i < dec.n; ++i) {
    bperm[i] = b[dec.P[i]];
  }

  std::vector<double> xperm(dec.n, 0.0);

  auto& klu = KluDynamic::instance();
  const bool use_threads = thread_count > 1 && dec.nblocks > 1;

  // Only emit per-block top-K at the outermost solve to avoid overwhelming logs
  // when nested decompositions are enabled.
  const bool prof = profile_enabled_local() && solve_depth == 1;
  const int topk = pick_block_topk();
  std::vector<double> solve_ms;
  if (prof && topk > 0) {
    solve_ms.assign(static_cast<std::size_t>(dec.nblocks), 0.0);
  }

  thread_local std::vector<double> tls_rhs;
  thread_local std::vector<double> tls_tmp;

  auto solve_block = [&](int32_t bid) -> bool {
    const auto& bf = fac.blocks[static_cast<std::size_t>(bid)];
    if (!bf.ok) return false;

    const int32_t start = bf.start;
    const int32_t end = start + bf.size;
    tls_rhs.resize(static_cast<std::size_t>(bf.size));
    std::vector<double>& rhs = tls_rhs;

    // rhs = bperm_block - sum_{col>=end} A(row,col)*xperm[col]
    for (int32_t r = start; r < end; ++r) {
      double v = bperm[r];
      if (!dec.csr_contains_diag) {
        for (int32_t p = dec.Aperm_csr.row_ptr[r]; p < dec.Aperm_csr.row_ptr[r + 1]; ++p) {
          const int32_t c = dec.Aperm_csr.col_ind[p];
          v -= dec.Aperm_csr.values[p] * xperm[c];
        }
      } else {
        for (int32_t p = dec.Aperm_csr.row_ptr[r]; p < dec.Aperm_csr.row_ptr[r + 1]; ++p) {
          const int32_t c = dec.Aperm_csr.col_ind[p];
          if (c >= end) {
            v -= dec.Aperm_csr.values[p] * xperm[c];
          }
        }
      }
      rhs[static_cast<std::size_t>(r - start)] = v;
    }

    if (!bf.dense_lu.empty()) {
      if (!dense_lu_solve_inplace(bf, rhs)) return false;
    } else if (bf.bbd_enabled) {
      const int32_t m = bf.size;
      const int32_t n1 = bf.bbd_n1;
      const int32_t n2 = bf.bbd_n2;
      const int32_t ns = bf.bbd_ns;
      if (n1 <= 0 || n2 <= 0 || ns <= 0 || n1 + n2 + ns != m) return false;
      if (!bf.bbd_f1 || !bf.bbd_f2) return false;
      if (!bf.bbd_f1->ok || !bf.bbd_f2->ok) return false;
      if (static_cast<int32_t>(bf.bbd_perm.size()) != m) return false;
      if (static_cast<int32_t>(bf.bbd_invperm.size()) != m) return false;
      if (static_cast<int32_t>(bf.bbd_S_piv.size()) != ns) return false;
      if (static_cast<int32_t>(bf.bbd_S_lu.size()) != ns * ns) return false;

      tls_tmp.resize(static_cast<std::size_t>(m) +
                     static_cast<std::size_t>(n1) + static_cast<std::size_t>(n2) +
                     static_cast<std::size_t>(ns) +
                     static_cast<std::size_t>(n1) + static_cast<std::size_t>(n2));
      double* work = tls_tmp.data();
      double* bnew = work;                   // m
      double* y1 = bnew + m;                 // n1
      double* y2 = y1 + n1;                  // n2
      double* xs = y2 + n2;                  // ns (rhs_s then xs)
      double* z1 = xs + ns;                  // n1
      double* z2 = z1 + n1;                  // n2

      // bnew = P * rhs (permute old->new using invperm).
      for (int32_t i = 0; i < m; ++i) {
        const int32_t ni = bf.bbd_invperm[static_cast<std::size_t>(i)];
        if (ni < 0 || ni >= m) return false;
        bnew[ni] = rhs[static_cast<std::size_t>(i)];
      }

      // Solve A11/A22.
      for (int32_t i = 0; i < n1; ++i) y1[i] = bnew[i];
      for (int32_t i = 0; i < n2; ++i) y2[i] = bnew[n1 + i];
      if (!solve_local_inplace_ptr(*bf.bbd_f1, y1)) return false;
      if (!solve_local_inplace_ptr(*bf.bbd_f2, y2)) return false;

      // rhs_s = bs - As1*y1 - As2*y2.
      for (int32_t i = 0; i < ns; ++i) {
        xs[i] = bnew[n1 + n2 + i];
      }

      auto spmv_sub = [&](const CsrMatrixI32& A, const double* x, int32_t x_n) {
        if (A.n != ns) return false;
        for (int32_t r = 0; r < ns; ++r) {
          double acc = 0.0;
          for (int32_t p = A.row_ptr[static_cast<std::size_t>(r)]; p < A.row_ptr[static_cast<std::size_t>(r + 1)]; ++p) {
            const int32_t c = A.col_ind[static_cast<std::size_t>(p)];
            if (c < 0 || c >= x_n) continue;
            acc += A.values[static_cast<std::size_t>(p)] * x[c];
          }
          xs[r] -= acc;
        }
        return true;
      };

      if (!spmv_sub(bf.bbd_As1, y1, n1)) return false;
      if (!spmv_sub(bf.bbd_As2, y2, n2)) return false;

      // Solve Schur for xs in-place.
      if (!dense_lu_solve_inplace_ptr(bf.bbd_S_lu, bf.bbd_S_piv, ns, xs)) return false;

      // z1 = A1s*xs, z2 = A2s*xs.
      auto spmv = [&](const CsrMatrixI32& A, double* out, int32_t nrows) {
        if (A.n != nrows) return false;
        for (int32_t r = 0; r < nrows; ++r) {
          double acc = 0.0;
          for (int32_t p = A.row_ptr[static_cast<std::size_t>(r)]; p < A.row_ptr[static_cast<std::size_t>(r + 1)]; ++p) {
            const int32_t c = A.col_ind[static_cast<std::size_t>(p)];
            if (c < 0 || c >= ns) continue;
            acc += A.values[static_cast<std::size_t>(p)] * xs[c];
          }
          out[r] = acc;
        }
        return true;
      };

      if (!spmv(bf.bbd_A1s, z1, n1)) return false;
      if (!spmv(bf.bbd_A2s, z2, n2)) return false;
      if (!solve_local_inplace_ptr(*bf.bbd_f1, z1)) return false;
      if (!solve_local_inplace_ptr(*bf.bbd_f2, z2)) return false;

      // x1 = y1 - z1, x2 = y2 - z2.
      for (int32_t i = 0; i < n1; ++i) bnew[i] = y1[i] - z1[i];
      for (int32_t i = 0; i < n2; ++i) bnew[n1 + i] = y2[i] - z2[i];
      for (int32_t i = 0; i < ns; ++i) bnew[n1 + n2 + i] = xs[i];

      // rhs(old) = invP * bnew (permute new->old).
      for (int32_t ni = 0; ni < m; ++ni) {
        const int32_t oi = bf.bbd_perm[static_cast<std::size_t>(ni)];
        if (oi < 0 || oi >= m) return false;
        rhs[static_cast<std::size_t>(oi)] = bnew[ni];
      }
    } else if (bf.sub_fac && bf.sub_dec) {
      if (!solve_ubtf(*bf.sub_dec, *bf.sub_fac, rhs, thread_count)) return false;
    } else {
      if (bf.gp) {
        if (!gp_lu_solve_inplace_strided(*bf.gp, rhs.data(), bf.size, 1)) return false;
      } else {
        if (!bf.sym || !bf.num) return false;
        klu_common common = bf.common; // KLU updates common stats; keep local copy
        const int ok = klu.solve(bf.sym, bf.num, bf.size, 1, rhs.data(), &common);
        if (!ok) return false;
      }
    }

    for (int32_t i = 0; i < bf.size; ++i) {
      xperm[start + i] = rhs[static_cast<std::size_t>(i)];
    }
    return true;
  };

  const BlockSched sched = pick_block_sched();

  if (sched == BlockSched::Reverse || dec.block_deps.empty()) {
    for (int32_t bid = dec.nblocks - 1; bid >= 0; --bid) {
      const auto t0 = (prof && topk > 0) ? Clock::now() : Clock::time_point{};
      if (!solve_block(bid)) return false;
      if (prof && topk > 0) {
        const auto t1 = Clock::now();
        solve_ms[static_cast<std::size_t>(bid)] =
          std::chrono::duration<double, std::milli>(t1 - t0).count();
      }
    }
  } else if (sched == BlockSched::Level) {
    // Level scheduler (deterministic). Each level can be solved in parallel.
    if (!use_threads) {
      for (int32_t lev = 0; lev <= dec.max_level; ++lev) {
        const auto& blocks = dec.blocks_by_level[lev];
        for (int32_t bid : blocks) {
          const auto t0 = (prof && topk > 0) ? Clock::now() : Clock::time_point{};
          if (!solve_block(bid)) return false;
          if (prof && topk > 0) {
            const auto t1 = Clock::now();
            solve_ms[static_cast<std::size_t>(bid)] =
              std::chrono::duration<double, std::milli>(t1 - t0).count();
          }
        }
      }
    } else {
      ThreadPool& pool = ReusedPool(thread_count);
      std::atomic<bool> ok{true};
      for (int32_t lev = 0; lev <= dec.max_level; ++lev) {
        const auto& blocks = dec.blocks_by_level[lev];
        ok.store(true, std::memory_order_release);
        pool.parallel_for(0, blocks.size(), [&](std::size_t i) {
          if (!ok.load(std::memory_order_acquire)) return;
          const int32_t bid = blocks[i];
          const auto t0 = (prof && topk > 0) ? Clock::now() : Clock::time_point{};
          if (!solve_block(bid)) {
            ok.store(false, std::memory_order_release);
          } else if (prof && topk > 0) {
            const auto t1 = Clock::now();
            solve_ms[static_cast<std::size_t>(bid)] =
              std::chrono::duration<double, std::milli>(t1 - t0).count();
          }
        }, 1);
        if (!ok.load(std::memory_order_acquire)) return false;
      }
    }
  } else if (!use_threads) {
    // Single-threaded fallback for DAG schedule: reverse order.
    for (int32_t bid = dec.nblocks - 1; bid >= 0; --bid) {
      const auto t0 = (prof && topk > 0) ? Clock::now() : Clock::time_point{};
      if (!solve_block(bid)) return false;
      if (prof && topk > 0) {
        const auto t1 = Clock::now();
        solve_ms[static_cast<std::size_t>(bid)] =
          std::chrono::duration<double, std::milli>(t1 - t0).count();
      }
    }
  } else {
    // DAG scheduler with a shared ready queue (work-stealing happens inside the thread pool).
    std::vector<std::atomic<int32_t>> remaining(dec.nblocks);
    for (int32_t b = 0; b < dec.nblocks; ++b) {
      remaining[b].store(static_cast<int32_t>(dec.block_deps[static_cast<std::size_t>(b)].size()), std::memory_order_relaxed);
    }

    std::mutex mtx;
    std::condition_variable cv;
    std::vector<int32_t> ready;
    ready.reserve(static_cast<std::size_t>(dec.nblocks));
    for (int32_t b = 0; b < dec.nblocks; ++b) {
      if (remaining[b].load(std::memory_order_relaxed) == 0) {
        ready.push_back(b);
      }
    }

    std::atomic<int32_t> done{0};
    std::atomic<bool> ok{true};
    ThreadPool& pool = ReusedPool(thread_count);

    const std::size_t workers = pool.thread_count();
    for (std::size_t t = 0; t < workers; ++t) {
      pool.submit([&]() {
        while (ok.load(std::memory_order_acquire)) {
          int32_t bid = -1;
          {
            std::unique_lock<std::mutex> lk(mtx);
            cv.wait(lk, [&]() {
              return !ok.load(std::memory_order_acquire) ||
                     !ready.empty() ||
                     done.load(std::memory_order_acquire) == dec.nblocks;
            });
            if (!ok.load(std::memory_order_acquire)) return;
            if (done.load(std::memory_order_acquire) == dec.nblocks) return;
            if (ready.empty()) continue;
            bid = ready.back();
            ready.pop_back();
          }

          const auto t0 = (prof && topk > 0) ? Clock::now() : Clock::time_point{};
          if (!solve_block(bid)) {
            ok.store(false, std::memory_order_release);
            cv.notify_all();
            return;
          }
          if (prof && topk > 0) {
            const auto t1 = Clock::now();
            solve_ms[static_cast<std::size_t>(bid)] =
              std::chrono::duration<double, std::milli>(t1 - t0).count();
          }

          done.fetch_add(1, std::memory_order_acq_rel);
          for (int32_t pred : dec.block_preds[static_cast<std::size_t>(bid)]) {
            if (remaining[pred].fetch_sub(1, std::memory_order_acq_rel) == 1) {
              {
                std::lock_guard<std::mutex> lk(mtx);
                ready.push_back(pred);
              }
              cv.notify_one();
            }
          }

          if (done.load(std::memory_order_acquire) == dec.nblocks) {
            cv.notify_all();
          }
        }
      });
    }

    pool.wait_idle();
    if (!ok.load(std::memory_order_acquire) || done.load(std::memory_order_acquire) != dec.nblocks) {
      return false;
    }
  }

  if (prof && topk > 0) {
    print_topk_blocks("solve", dec, fac, solve_ms, topk);
  }

  // Unpermute solution to original column order (overwrite b with x).
  for (int32_t i = 0; i < dec.n; ++i) {
    const int32_t orig_col = dec.Q[i];
    b[orig_col] = xperm[i];
  }

  return true;
}

