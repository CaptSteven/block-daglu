#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "blockdaglu/types.h"

// Internal block-decomposition structures live in the implementation headers (global namespace).
struct UbftfDecomposition;
struct UbftfFactors;

namespace blockdaglu {

struct GlobalKluState;

// Core solver (tool-independent).
//
// API shape:
// - set_matrix: provide sparse matrix (COO).
// - analyze: route decision + reordering / structure analysis.
// - factor: numeric factorization.
// - solve_in_place: solve Ax=b (b overwritten by x).
//
// NOTE: This class intentionally mirrors the competition pipeline stages, but
// does NOT depend on the competition ALInterface.
class Solver final {
 public:
  Solver();
  ~Solver();

  bool set_matrix(CooMatrix m);
  bool analyze();
  bool factor();
  bool solve_in_place(std::vector<double>& b);

  // Optional: export internals for debugging/visualization/compat adapters.
  CooMatrix get_rearranged_matrix();
  CooMatrix getQ();
  CooMatrix getP();
  CooMatrix getL();
  CooMatrix getU();

  int32_t n() const { return n_; }

 private:
  struct DiagStats {
    int32_t n = 0;
    int32_t present = 0;
    int32_t missing = 0;
    int32_t zeros = 0;
    int32_t tiny = 0;
    double min_abs = 0.0;
    double max_abs = 0.0;
    double median_abs = 0.0;
  };

  enum class Mode {
    Auto,
    Klu,
    Block,
  };

  bool prepared_ = false;
  bool decomposed_ = false;
  bool factored_ = false;
  bool factors_extracted_ = false;

  int32_t n_ = 0;
  CooMatrix original_matrix_;
  std::uint64_t matrix_sig_ = 0;
  CooMatrix L_;
  CooMatrix U_;
  std::vector<double> Rs_;
  std::vector<int32_t> P_perm_;
  std::vector<int32_t> Q_perm_;

  Mode mode_ = Mode::Auto;
  bool use_global_klu_ = true;

  std::unique_ptr<GlobalKluState> global_;
  std::shared_ptr<const ::UbftfDecomposition> decomposition_;
  std::unique_ptr<::UbftfFactors> factors_;

  std::size_t build_threads_ = 0;
  std::size_t work_threads_ = 0;

  DiagStats diag_stats_{};
  bool diag_stats_valid_ = false;
  double rcond_global_ = -1.0;
  double rcond_blocks_min_ = -1.0;
};

} // namespace blockdaglu
