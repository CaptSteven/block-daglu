#include "gp_lu.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace {
inline double absd(double x) { return x < 0.0 ? -x : x; }

using Entry = std::pair<int32_t, double>; // (col, val)

static void sort_and_compress_row(std::vector<Entry>& row) {
  std::sort(row.begin(), row.end(), [](const Entry& a, const Entry& b) { return a.first < b.first; });
  std::size_t w = 0;
  for (std::size_t i = 0; i < row.size(); ++i) {
    const int32_t c = row[i].first;
    const double v = row[i].second;
    if (w == 0 || row[w - 1].first != c) {
      row[w++] = row[i];
    } else {
      row[w - 1].second += v;
    }
  }
  row.resize(w);
}

static int32_t find_col(const std::vector<Entry>& row, int32_t col) {
  auto it = std::lower_bound(row.begin(), row.end(), col, [](const Entry& e, int32_t c) { return e.first < c; });
  if (it == row.end() || it->first != col) return -1;
  return static_cast<int32_t>(it - row.begin());
}

static void saxpy_inplace(std::vector<Entry>& row,
                          const std::vector<Entry>& pivot,
                          double alpha,
                          int32_t k,
                          double drop_tol,
                          std::vector<Entry>& tmp) {
  // row := row - alpha * pivot
  // Both 'row' and 'pivot' are sorted by col and contain only cols >= k.
  tmp.clear();
  tmp.reserve(row.size() + pivot.size());

  std::size_t i = 0;
  std::size_t j = 0;
  while (i < row.size() || j < pivot.size()) {
    int32_t c = 0;
    double v = 0.0;
    if (j >= pivot.size() || (i < row.size() && row[i].first < pivot[j].first)) {
      c = row[i].first;
      v = row[i].second;
      ++i;
    } else if (i >= row.size() || pivot[j].first < row[i].first) {
      c = pivot[j].first;
      v = -alpha * pivot[j].second;
      ++j;
    } else {
      c = row[i].first; // equal cols
      v = row[i].second - alpha * pivot[j].second;
      ++i;
      ++j;
    }

    if (c < k) continue;
    if (c == k) {
      // Eliminated by construction. Keep it dropped even if rounding leaves a tiny value.
      continue;
    }
    if (drop_tol > 0.0 && absd(v) <= drop_tol) continue;
    tmp.emplace_back(c, v);
  }

  row.swap(tmp);
}

static bool keep_val(double v, double drop_tol) {
  return drop_tol <= 0.0 || absd(v) > drop_tol;
}

} // namespace

bool gp_lu_factor(const CscMatrixI32& A, GpLuFactor& out, const GpLuOptions& opt) {
  out = GpLuFactor{};
  out.n = A.n;
  out.ok = false;
  if (opt.scale < 0 || opt.scale > 2) return false;
  if (opt.diag_prefer_tol < 0.0) return false;
  out.pivot_tol = opt.pivot_tol;
  out.perturb = opt.perturb;

  const int32_t n = A.n;
  if (n <= 0) return false;
  if (static_cast<int32_t>(A.col_ptr.size()) != n + 1) return false;
  if (A.row_ind.size() != A.values.size()) return false;

  // Row scaling in original row order.
  if (opt.scale != 0) {
    out.row_scale.assign(static_cast<std::size_t>(n), 1.0);
    std::vector<double> acc(static_cast<std::size_t>(n), 0.0);
    for (std::size_t p = 0; p < A.row_ind.size(); ++p) {
      const int32_t r = A.row_ind[p];
      if (r < 0 || r >= n) continue;
      const double a = std::abs(A.values[p]);
      if (opt.scale == 1) acc[static_cast<std::size_t>(r)] += a;
      else acc[static_cast<std::size_t>(r)] = std::max(acc[static_cast<std::size_t>(r)], a);
    }
    for (int32_t r = 0; r < n; ++r) {
      const double s = acc[static_cast<std::size_t>(r)];
      out.row_scale[static_cast<std::size_t>(r)] = (s > 0.0 ? (1.0 / s) : 1.0);
    }
  }

  // Build row-wise active matrix rows (cols 0..n-1), then keep them sorted.
  std::vector<std::vector<Entry>> active(static_cast<std::size_t>(n));
  for (int32_t c = 0; c < n; ++c) {
    const int32_t p0 = A.col_ptr[static_cast<std::size_t>(c)];
    const int32_t p1 = A.col_ptr[static_cast<std::size_t>(c + 1)];
    if (p0 < 0 || p1 < p0 || static_cast<std::size_t>(p1) > A.row_ind.size()) return false;
    for (int32_t p = p0; p < p1; ++p) {
      const int32_t r = A.row_ind[static_cast<std::size_t>(p)];
      if (r < 0 || r >= n) continue;
      double v = A.values[static_cast<std::size_t>(p)];
      if (!out.row_scale.empty()) v *= out.row_scale[static_cast<std::size_t>(r)];
      active[static_cast<std::size_t>(r)].emplace_back(c, v);
    }
  }
  for (int32_t r = 0; r < n; ++r) {
    auto& row = active[static_cast<std::size_t>(r)];
    sort_and_compress_row(row);
    // Optional initial drop.
    if (opt.drop_tol > 0.0) {
      std::size_t w = 0;
      for (std::size_t i = 0; i < row.size(); ++i) {
        if (keep_val(row[i].second, opt.drop_tol)) row[w++] = row[i];
      }
      row.resize(w);
    }
  }

  out.perm.resize(static_cast<std::size_t>(n));
  for (int32_t i = 0; i < n; ++i) out.perm[static_cast<std::size_t>(i)] = i;

  out.L_rows.assign(static_cast<std::size_t>(n), {});
  out.U_rows.assign(static_cast<std::size_t>(n), {});
  out.Udiag.assign(static_cast<std::size_t>(n), 0.0);

  std::vector<Entry> tmp;
  tmp.reserve(256);

  const double pivot_tol = (opt.pivot_tol > 0.0 ? opt.pivot_tol : 0.0);
  const double perturb = (opt.perturb > 0.0 ? opt.perturb : 0.0);

  for (int32_t k = 0; k < n; ++k) {
    // Pivot selection: scan rows k..n-1 for max |A(i,k)|.
    int32_t piv = -1;
    double best = 0.0;
    double diagabs = 0.0;
    bool has_diag_in_rowk = false;
    for (int32_t i = k; i < n; ++i) {
      const auto& row = active[static_cast<std::size_t>(i)];
      const int32_t pos = find_col(row, k);
      if (pos < 0) continue;
      const double a = absd(row[static_cast<std::size_t>(pos)].second);
      if (a > best) {
        best = a;
        piv = i;
      }
      if (i == k) {
        has_diag_in_rowk = true;
        diagabs = a;
      }
    }

    if (piv < 0) {
      out.zero_pivot_count++;
      return false;
    }

    // Diagonal preference (closer to KLU behaviour): keep the diagonal if it's not "too small".
    // This avoids unnecessary row swaps that can change the chosen solution on near-singular problems.
    if (has_diag_in_rowk && best > 0.0 && diagabs >= opt.diag_prefer_tol * best) {
      piv = k;
    }

    if (piv != k) {
      std::swap(active[static_cast<std::size_t>(k)], active[static_cast<std::size_t>(piv)]);
      std::swap(out.L_rows[static_cast<std::size_t>(k)], out.L_rows[static_cast<std::size_t>(piv)]);
      std::swap(out.perm[static_cast<std::size_t>(k)], out.perm[static_cast<std::size_t>(piv)]);
      out.row_swap_count++;
    }

    auto& prow = active[static_cast<std::size_t>(k)];
    const int32_t dpos = find_col(prow, k);
    if (dpos < 0) {
      out.zero_pivot_count++;
      return false;
    }
    double diag = prow[static_cast<std::size_t>(dpos)].second;
    if (pivot_tol > 0.0 && absd(diag) < pivot_tol) {
      out.tiny_pivot_count++;
      if (diag == 0.0) diag = (perturb != 0.0 ? perturb : pivot_tol);
      else diag += (diag > 0.0 ? (perturb != 0.0 ? perturb : pivot_tol) : -(perturb != 0.0 ? perturb : pivot_tol));
      prow[static_cast<std::size_t>(dpos)].second = diag;
    }
    if (diag == 0.0) {
      out.zero_pivot_count++;
      return false;
    }

    // Store U row k (cols >= k), keep sorted.
    out.U_rows[static_cast<std::size_t>(k)] = prow;
    out.Udiag[static_cast<std::size_t>(k)] = diag;

    // Eliminate rows below.
    for (int32_t i = k + 1; i < n; ++i) {
      auto& row = active[static_cast<std::size_t>(i)];
      const int32_t pos = find_col(row, k);
      if (pos < 0) continue;
      const double aik = row[static_cast<std::size_t>(pos)].second;
      const double l = aik / diag;
      if (l != 0.0) out.L_rows[static_cast<std::size_t>(i)].emplace_back(k, l);
      // row := row - l * prow, dropping col k
      saxpy_inplace(row, prow, l, k, opt.drop_tol, tmp);
    }

    // This row will not be used again in the active matrix; clear to save memory.
    prow.clear();
  }

  // Ensure L rows are sorted by column.
  for (int32_t i = 0; i < n; ++i) {
    auto& lr = out.L_rows[static_cast<std::size_t>(i)];
    std::sort(lr.begin(), lr.end(), [](const Entry& a, const Entry& b) { return a.first < b.first; });
  }

  out.ok = true;
  return true;
}

bool gp_lu_solve_inplace_strided(const GpLuFactor& f, double* B, int32_t ldim, int32_t nrhs) {
  if (!f.ok) return false;
  if (!B) return false;
  const int32_t n = f.n;
  if (n <= 0) return false;
  if (ldim < n || nrhs <= 0) return false;
  if (static_cast<int32_t>(f.perm.size()) != n) return false;
  if (static_cast<int32_t>(f.L_rows.size()) != n) return false;
  if (static_cast<int32_t>(f.U_rows.size()) != n) return false;
  if (static_cast<int32_t>(f.Udiag.size()) != n) return false;

  thread_local std::vector<double> y;
  thread_local std::vector<double> x;
  y.assign(static_cast<std::size_t>(n), 0.0);
  x.assign(static_cast<std::size_t>(n), 0.0);

  for (int32_t rhs = 0; rhs < nrhs; ++rhs) {
    const double* b = B + static_cast<std::size_t>(rhs) * static_cast<std::size_t>(ldim);

    // y := P * (D*b)
    for (int32_t i = 0; i < n; ++i) {
      const int32_t orig = f.perm[static_cast<std::size_t>(i)];
      double v = b[static_cast<std::size_t>(orig)];
      if (!f.row_scale.empty()) {
        if (static_cast<int32_t>(f.row_scale.size()) != n) return false;
        v *= f.row_scale[static_cast<std::size_t>(orig)];
      }
      y[static_cast<std::size_t>(i)] = v;
    }

    // Forward solve with unit-lower L (stored by row).
    for (int32_t i = 0; i < n; ++i) {
      double sum = y[static_cast<std::size_t>(i)];
      const auto& lr = f.L_rows[static_cast<std::size_t>(i)];
      for (const auto& e : lr) {
        const int32_t k = e.first;
        if (k < 0 || k >= i) continue;
        sum -= e.second * y[static_cast<std::size_t>(k)];
      }
      y[static_cast<std::size_t>(i)] = sum;
    }

    // Back solve with U (stored by row).
    for (int32_t i = n - 1; i >= 0; --i) {
      double sum = y[static_cast<std::size_t>(i)];
      const auto& ur = f.U_rows[static_cast<std::size_t>(i)];
      for (const auto& e : ur) {
        const int32_t j = e.first;
        if (j <= i) continue;
        sum -= e.second * x[static_cast<std::size_t>(j)];
      }
      const double diag = f.Udiag[static_cast<std::size_t>(i)];
      if (diag == 0.0) return false;
      x[static_cast<std::size_t>(i)] = sum / diag;
    }

    // Write x back (variables are not permuted).
    double* out = B + static_cast<std::size_t>(rhs) * static_cast<std::size_t>(ldim);
    for (int32_t i = 0; i < n; ++i) {
      out[static_cast<std::size_t>(i)] = x[static_cast<std::size_t>(i)];
    }
  }

  return true;
}
