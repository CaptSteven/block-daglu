#pragma once

#include <cstdint>
#include <tuple>
#include <vector>

struct CscMatrixI32 {
  int32_t n = 0;
  int32_t nnz = 0;
  std::vector<int32_t> col_ptr;   // n+1
  std::vector<int32_t> row_ind;   // nnz
  std::vector<double> values;     // nnz
};

struct CsrMatrixI32 {
  int32_t n = 0;
  int32_t nnz = 0;
  std::vector<int32_t> row_ptr;   // n+1
  std::vector<int32_t> col_ind;   // nnz
  std::vector<double> values;     // nnz
};

inline CscMatrixI32 coo_to_csc_i32(const std::vector<std::tuple<int,int,double>>& coo, int32_t n) {
  CscMatrixI32 out;
  out.n = n;
  out.nnz = static_cast<int32_t>(coo.size());
  out.col_ptr.assign(n + 1, 0);
  out.row_ind.resize(out.nnz);
  out.values.resize(out.nnz);

  for (const auto& t : coo) {
    const int c = std::get<1>(t);
    if (c >= 0 && c < n) {
      out.col_ptr[c + 1]++;
    }
  }
  for (int i = 0; i < n; ++i) {
    out.col_ptr[i + 1] += out.col_ptr[i];
  }
  thread_local std::vector<int32_t> tls_next;
  tls_next.assign(out.col_ptr.begin(), out.col_ptr.end());
  for (const auto& t : coo) {
    const int r = std::get<0>(t);
    const int c = std::get<1>(t);
    const double v = std::get<2>(t);
    const int32_t pos = tls_next[c]++;
    out.row_ind[pos] = static_cast<int32_t>(r);
    out.values[pos] = v;
  }
  return out;
}

inline CsrMatrixI32 coo_to_csr_i32(const std::vector<std::tuple<int,int,double>>& coo, int32_t n) {
  CsrMatrixI32 out;
  out.n = n;
  out.nnz = static_cast<int32_t>(coo.size());
  out.row_ptr.assign(n + 1, 0);
  out.col_ind.resize(out.nnz);
  out.values.resize(out.nnz);

  for (const auto& t : coo) {
    const int r = std::get<0>(t);
    if (r >= 0 && r < n) {
      out.row_ptr[r + 1]++;
    }
  }
  for (int i = 0; i < n; ++i) {
    out.row_ptr[i + 1] += out.row_ptr[i];
  }
  thread_local std::vector<int32_t> tls_next;
  tls_next.assign(out.row_ptr.begin(), out.row_ptr.end());
  for (const auto& t : coo) {
    const int r = std::get<0>(t);
    const int c = std::get<1>(t);
    const double v = std::get<2>(t);
    const int32_t pos = tls_next[r]++;
    out.col_ind[pos] = static_cast<int32_t>(c);
    out.values[pos] = v;
  }
  return out;
}

// Pattern-only CSC build (no values array).
// Useful for symbolic routines (e.g., DM/BTF) that depend only on sparsity structure.
inline CscMatrixI32 coo_to_csc_pattern_i32(const std::vector<std::tuple<int,int,double>>& coo, int32_t n) {
  CscMatrixI32 out;
  out.n = n;
  out.col_ptr.assign(n + 1, 0);
  out.nnz = static_cast<int32_t>(coo.size());
  out.row_ind.resize(static_cast<std::size_t>(out.nnz));
  out.values.clear();

  // Count columns (treat input as mostly-valid; bounds checks only on column to avoid O(nnz) extra branching).
  for (const auto& t : coo) {
    const int c = std::get<1>(t);
    if (c >= 0 && c < n) out.col_ptr[c + 1]++;
  }
  for (int i = 0; i < n; ++i) {
    out.col_ptr[i + 1] += out.col_ptr[i];
  }
  out.nnz = out.col_ptr[n];

  thread_local std::vector<int32_t> tls_next;
  tls_next.assign(out.col_ptr.begin(), out.col_ptr.end());
  for (const auto& t : coo) {
    const int r = std::get<0>(t);
    const int c = std::get<1>(t);
    if (c < 0 || c >= n) continue;
    const int32_t pos = tls_next[c]++;
    out.row_ind[static_cast<std::size_t>(pos)] = static_cast<int32_t>(r);
  }

  return out;
}
