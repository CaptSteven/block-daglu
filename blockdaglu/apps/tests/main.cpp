#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "blockdaglu/solver.h"

namespace {

using blockdaglu::CooMatrix;
using blockdaglu::Solver;
using blockdaglu::SolverError;

std::atomic<int> failures{0};

void check(bool condition, const std::string& message) {
  if (condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << "\n";
}

void set_env(const char* name, const char* value) {
#if defined(_WIN32)
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}

std::vector<double> multiply(const CooMatrix& matrix, int n, const std::vector<double>& x) {
  std::vector<double> out(static_cast<std::size_t>(n), 0.0);
  for (const auto& entry : matrix) {
    out[static_cast<std::size_t>(std::get<0>(entry))] +=
      std::get<2>(entry) * x[static_cast<std::size_t>(std::get<1>(entry))];
  }
  return out;
}

std::vector<double> dense(const CooMatrix& matrix, int n) {
  std::vector<double> out(static_cast<std::size_t>(n) * static_cast<std::size_t>(n), 0.0);
  for (const auto& entry : matrix) {
    out[static_cast<std::size_t>(std::get<0>(entry)) * static_cast<std::size_t>(n) +
        static_cast<std::size_t>(std::get<1>(entry))] += std::get<2>(entry);
  }
  return out;
}

std::vector<double> dense_multiply(const std::vector<double>& a,
                                   const std::vector<double>& b,
                                   int n) {
  std::vector<double> out(static_cast<std::size_t>(n) * static_cast<std::size_t>(n), 0.0);
  for (int row = 0; row < n; ++row) {
    for (int k = 0; k < n; ++k) {
      const double av = a[static_cast<std::size_t>(row) * n + k];
      if (av == 0.0) continue;
      for (int col = 0; col < n; ++col) {
        out[static_cast<std::size_t>(row) * n + col] +=
          av * b[static_cast<std::size_t>(k) * n + col];
      }
    }
  }
  return out;
}

bool close(double a, double b, double tolerance = 1e-9) {
  return std::abs(a - b) <= tolerance * std::max({1.0, std::abs(a), std::abs(b)});
}

void check_vector(const std::vector<double>& actual,
                  const std::vector<double>& expected,
                  const std::string& message) {
  check(actual.size() == expected.size(), message + " size");
  if (actual.size() != expected.size()) return;
  for (std::size_t i = 0; i < actual.size(); ++i) {
    check(close(actual[i], expected[i]), message + " at index " + std::to_string(i));
  }
}

CooMatrix small_matrix() {
  return {
    {0, 0, 4.0}, {0, 1, 1.0},
    {1, 0, 1.0}, {1, 1, 3.0}, {1, 2, 1.0},
    {2, 1, 1.0}, {2, 2, 2.0},
  };
}

void test_invalid_inputs() {
  Solver negative;
  check(negative.set_matrix({{-1, 0, 1.0}}), "set_matrix remains storage-only for negative input");
  check(!negative.analyze(), "negative index is rejected during analyze");
  check(negative.last_error() == SolverError::InvalidIndex, "negative index error code");

  Solver non_finite;
  check(non_finite.set_matrix({{0, 0, std::numeric_limits<double>::infinity()}}),
        "set_matrix remains storage-only for non-finite input");
  check(!non_finite.analyze(), "non-finite value is rejected during analyze");
  check(non_finite.last_error() == SolverError::NonFiniteValue, "non-finite error code");

  Solver declared_dimension;
  check(declared_dimension.set_matrix({{2, 2, 1.0}}, 2), "explicit dimension accepted for deferred validation");
  check(!declared_dimension.analyze(), "entry outside explicit dimension is rejected");
  check(declared_dimension.last_error() == SolverError::InvalidIndex, "explicit dimension error code");
}

void test_solve_and_duplicates() {
  set_env("BLOCKDAGLU_MODE", "klu");
  Solver solver;
  CooMatrix matrix = small_matrix();
  matrix.emplace_back(0, 0, 1.0);
  matrix.emplace_back(0, 0, -1.0);
  const std::vector<double> expected{0.2222222222222222, 0.1111111111111111, 1.4444444444444444};
  std::vector<double> rhs{1.0, 2.0, 3.0};
  check(solver.set_matrix(std::move(matrix), 3), "set duplicate matrix");
  check(solver.analyze(), "analyze duplicate matrix: " + solver.last_error_message());
  check(solver.factor(), "factor duplicate matrix: " + solver.last_error_message());
  check(solver.solve_in_place(rhs), "solve duplicate matrix: " + solver.last_error_message());
  check_vector(rhs, expected, "duplicate matrix solution");

  std::vector<double> bad_rhs{1.0, 2.0};
  check(!solver.solve_in_place(bad_rhs), "wrong RHS size is rejected");
  check(solver.last_error() == SolverError::InvalidRightHandSide, "wrong RHS error code");
}

CooMatrix cache_matrix(int n, int changed_column) {
  CooMatrix matrix;
  matrix.reserve(static_cast<std::size_t>(n) * 2);
  for (int row = 0; row < n; ++row) {
    matrix.emplace_back(row, row, 4.0);
    if (row + 2 < n) {
      const int col = (row == n / 2) ? changed_column : row + 1;
      matrix.emplace_back(row, col, 0.25);
    } else {
      matrix.emplace_back(row, row, 0.5);
    }
  }
  return matrix;
}

void solve_known_ones(CooMatrix matrix, int n, const std::string& label) {
  const std::vector<double> expected(static_cast<std::size_t>(n), 1.0);
  std::vector<double> rhs = multiply(matrix, n, expected);
  Solver solver;
  check(solver.set_matrix(std::move(matrix), n), label + " set");
  check(solver.analyze(), label + " analyze: " + solver.last_error_message());
  check(solver.factor(), label + " factor: " + solver.last_error_message());
  check(solver.solve_in_place(rhs), label + " solve: " + solver.last_error_message());
  check_vector(rhs, expected, label + " solution");
}

void test_cache_isolation() {
  set_env("BLOCKDAGLU_MODE", "block");
  set_env("BLOCKDAGLU_CACHE", "1");
  constexpr int n = 1100;
  solve_known_ones(cache_matrix(n, n / 2 + 1), n, "cache pattern A");
  solve_known_ones(cache_matrix(n, n / 2 + 2), n, "cache pattern B");

  CooMatrix original_values = cache_matrix(n, n / 2 + 1);
  CooMatrix scaled_values = original_values;
  for (auto& entry : scaled_values) std::get<2>(entry) *= 2.0;
  solve_known_ones(std::move(original_values), n, "cache values A");
  solve_known_ones(std::move(scaled_values), n, "cache values B");
}

void test_compatibility_export() {
  set_env("BLOCKDAGLU_MODE", "block");
  Solver solver;
  // Upper block-triangular structure exercises the KLU BTF off-diagonal path,
  // and uneven row magnitudes exercise non-unit row scaling.
  CooMatrix matrix{
    {0, 0, 100.0}, {0, 1, 20.0}, {0, 2, 7.0},
    {1, 0, 30.0}, {1, 1, 80.0}, {1, 3, 5.0},
    {2, 2, 0.25}, {2, 3, 0.05},
    {3, 2, 0.10}, {3, 3, 0.50},
  };
  check(solver.set_matrix(matrix, 4), "export set");
  check(solver.analyze(), "export analyze: " + solver.last_error_message());
  check(solver.factor(), "export factor: " + solver.last_error_message());

  const CooMatrix rearranged = solver.get_rearranged_matrix();
  const CooMatrix l = solver.getL();
  const CooMatrix u = solver.getU();
  check(!rearranged.empty(), "rearranged matrix export is non-empty");
  check(!l.empty(), "L export is non-empty");
  check(!u.empty(), "U export is non-empty");

  const auto lhs = dense(rearranged, 4);
  const auto rhs = dense_multiply(dense(l, 4), dense(u, 4), 4);
  check_vector(lhs, rhs, "compatibility export P*A*Q equals L*U");
}

void test_concurrent_solvers() {
  set_env("BLOCKDAGLU_MODE", "klu");
  auto worker = [](const std::string& label) { solve_known_ones(cache_matrix(256, 129), 256, label); };
  std::thread first(worker, "concurrent A");
  std::thread second(worker, "concurrent B");
  first.join();
  second.join();
}

}  // namespace

int main() {
  test_invalid_inputs();
  test_solve_and_duplicates();
  test_cache_isolation();
  test_compatibility_export();
  test_concurrent_solvers();

  if (failures.load() != 0) {
    std::cerr << failures.load() << " test assertion(s) failed\n";
    return 1;
  }
  std::cout << "All BlockDAGLU tests passed\n";
  return 0;
}
