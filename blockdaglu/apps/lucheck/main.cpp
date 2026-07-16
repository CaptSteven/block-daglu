#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "UserAlgorithmWrapper.h"

#include "AuxiliaryFunction.h"
#include "SuiteSparseWrapper.h"

static std::vector<double> load_vector_csv_singlecol(const std::string& path) {
  std::vector<double> v;
  std::ifstream f(path);
  if (!f.is_open()) return v;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty()) continue;
    v.push_back(std::stod(line));
  }
  return v;
}

static std::filesystem::path default_cases_dir() {
  std::filesystem::path p = std::filesystem::current_path();
  for (int i = 0; i < 8; ++i) {
    auto candidate = p / "data" / "FinalTestCases";
    if (std::filesystem::exists(candidate)) return candidate;
    p = p.parent_path();
  }
  return {};
}

static std::size_t nnz(const COOMatrix& m) { return m.size(); }

static COOMatrix permute_coo_old_to_new(const COOMatrix& A,
                                        const std::vector<int>& row_old_to_new,
                                        const std::vector<int>& col_old_to_new) {
  COOMatrix out;
  out.reserve(A.size());
  for (const auto& t : A) {
    const int ro = std::get<0>(t);
    const int co = std::get<1>(t);
    if (ro < 0 || co < 0 || ro >= static_cast<int>(row_old_to_new.size()) || co >= static_cast<int>(col_old_to_new.size())) continue;
    const int rn = row_old_to_new[ro];
    const int cn = col_old_to_new[co];
    if (rn < 0 || cn < 0) continue;
    out.emplace_back(rn, cn, std::get<2>(t));
  }
  return out;
}

static void run_one(ALInterface* alg,
                    const std::string& name,
                    const COOMatrix& A,
                    const std::string& jac,
                    const std::string& var) {
  COOMatrix A_copy = A;
  if (!alg->prepareData(std::move(A_copy))) {
    std::cout << name << ": prepareData failed\n";
    return;
  }
  if (!alg->rearrangeMatrix()) {
    std::cout << name << ": rearrangeMatrix failed\n";
    return;
  }
  if (!alg->decompositionLU()) {
    std::cout << name << ": decompositionLU failed\n";
    return;
  }

  COOMatrix Are = alg->getRearrangedMatrix();
  COOMatrix L = alg->getL();
  COOMatrix U = alg->getU();
  COOMatrix Are2 = alg->getRearrangedMatrix();
  COOMatrix Pm = alg->getP();
  COOMatrix Qm = alg->getQ();

  const double res_re = calculateResidual(Are, L, U);
  const double res_re2 = calculateResidual(Are2, L, U);
  const double res_orig = calculateResidual(A, L, U);

  int n = 0;
  for (const auto& t : A) n = std::max(n, std::max(std::get<0>(t), std::get<1>(t)));
  n++;

  // Extract permutation vectors from P/Q matrices if they look like permutation matrices.
  std::vector<int> Pvec(n, -1);
  std::vector<int> Qvec(n, -1);
  for (const auto& t : Pm) {
    const int r = std::get<0>(t);
    const int c = std::get<1>(t);
    const double v = std::get<2>(t);
    if (r >= 0 && r < n && std::abs(v - 1.0) < 1e-12) Pvec[r] = c;
  }
  for (const auto& t : Qm) {
    const int r = std::get<0>(t);
    const int c = std::get<1>(t);
    const double v = std::get<2>(t);
    if (c >= 0 && c < n && std::abs(v - 1.0) < 1e-12) Qvec[c] = r;  // Q has 1 at (Q[i], i)
  }

  int P_fixed = 0;
  int Q_fixed = 0;
  for (int i = 0; i < n; ++i) {
    if (Pvec[i] == i) P_fixed++;
    if (Qvec[i] == i) Q_fixed++;
  }

  // Candidate permutations:
  // - Assume Pvec/Qvec are new->old (MATLAB A(P,Q) semantics): old->new maps are Pinv/Qinv.
  // - Alternative: assume Pvec/Qvec are old->new directly.
  std::vector<int> Pinv(n, -1);
  std::vector<int> Qinv(n, -1);
  for (int i = 0; i < n; ++i) {
    if (Pvec[i] >= 0 && Pvec[i] < n) Pinv[Pvec[i]] = i;
    if (Qvec[i] >= 0 && Qvec[i] < n) Qinv[Qvec[i]] = i;
  }
  COOMatrix A_cand1 = permute_coo_old_to_new(A, Pinv, Qinv);  // current getRearrangedMatrix semantics
  COOMatrix A_cand2 = permute_coo_old_to_new(A, Pvec, Qvec);  // alternative semantics
  COOMatrix A_cand3 = permute_coo_old_to_new(A, Pinv, Qvec);
  COOMatrix A_cand4 = permute_coo_old_to_new(A, Pvec, Qinv);
  const double res_cand1 = calculateResidual(A_cand1, L, U);
  const double res_cand2 = calculateResidual(A_cand2, L, U);
  const double res_cand3 = calculateResidual(A_cand3, L, U);
  const double res_cand4 = calculateResidual(A_cand4, L, U);

  auto minmax = [](const COOMatrix& M) {
    int rmin = std::numeric_limits<int>::max();
    int rmax = std::numeric_limits<int>::min();
    int cmin = std::numeric_limits<int>::max();
    int cmax = std::numeric_limits<int>::min();
    for (const auto& t : M) {
      rmin = std::min(rmin, std::get<0>(t));
      rmax = std::max(rmax, std::get<0>(t));
      cmin = std::min(cmin, std::get<1>(t));
      cmax = std::max(cmax, std::get<1>(t));
    }
    if (M.empty()) {
      rmin = rmax = cmin = cmax = -1;
    }
    return std::tuple<int, int, int, int>{rmin, rmax, cmin, cmax};
  };
  const auto [Lrmin, Lrmax, Lcmin, Lcmax] = minmax(L);
  const auto [Urmin, Urmax, Ucmin, Ucmax] = minmax(U);
  auto diag_count = [](const COOMatrix& M) {
    std::size_t c = 0;
    for (const auto& t : M) {
      if (std::get<0>(t) == std::get<1>(t)) c++;
    }
    return c;
  };
  const std::size_t Ldiag = diag_count(L);
  const std::size_t Udiag = diag_count(U);
  auto max_abs = [](const COOMatrix& M) {
    double m = 0.0;
    for (const auto& t : M) m = std::max(m, std::abs(std::get<2>(t)));
    return m;
  };
  const double Lmax = max_abs(L);
  const double Umax = max_abs(U);

  std::cout << name << ":\n";
  std::cout << "  A.nnz=" << nnz(A) << " Are.nnz=" << nnz(Are) << " Are2.nnz=" << nnz(Are2) << "\n";
  std::cout << "  P_fixed=" << P_fixed << "/" << n << " Q_fixed=" << Q_fixed << "/" << n << "\n";
  std::cout << "  L.nnz=" << nnz(L) << " U.nnz=" << nnz(U) << "\n";
  std::cout << "  L.diag=" << Ldiag << " U.diag=" << Udiag << "\n";
  std::cout << "  max|L|=" << std::scientific << Lmax << " max|U|=" << std::scientific << Umax << "\n";
  std::cout << "  L.row=[" << Lrmin << "," << Lrmax << "] L.col=[" << Lcmin << "," << Lcmax << "]\n";
  std::cout << "  U.row=[" << Urmin << "," << Urmax << "] U.col=[" << Ucmin << "," << Ucmax << "]\n";
  std::cout << "  residual(Are, L, U)=" << std::scientific << res_re << "\n";
  std::cout << "  residual(Are2,L, U)=" << std::scientific << res_re2 << "\n";
  std::cout << "  residual(A,   L, U)=" << std::scientific << res_orig << "\n";
  std::cout << "  residual(A_cand1(Pinv,Qinv),L,U)=" << std::scientific << res_cand1 << "\n";
  std::cout << "  residual(A_cand2(Pvec,Qvec), L,U)=" << std::scientific << res_cand2 << "\n";
  std::cout << "  residual(A_cand3(Pinv,Qvec), L,U)=" << std::scientific << res_cand3 << "\n";
  std::cout << "  residual(A_cand4(Pvec,Qinv), L,U)=" << std::scientific << res_cand4 << "\n";
}

int main(int argc, char** argv) {
  std::filesystem::path cases = (argc >= 2) ? std::filesystem::path(argv[1]) : default_cases_dir();
  const int case_id = (argc >= 3) ? std::atoi(argv[2]) : 1;
  if (cases.empty() || !std::filesystem::exists(cases)) {
    std::cerr << "Usage: blockdaglu_lucheck <FinalTestCases path> [case_id]\n";
    return 2;
  }

  const std::string folder = std::to_string(case_id);
  const auto case_path = cases / folder;
  const std::string jac = (case_path / (folder + "_JacobiMatrixInfo.txt")).string();
  const std::string var = (case_path / (folder + "_VarInfo.csv")).string();

  COOMatrix A = parseScaledValues(jac, var);
  if (A.empty()) {
    std::cerr << "Failed to load matrix for case " << case_id << "\n";
    return 3;
  }

  std::cout << "Case " << case_id << "\n";
  SuiteSparseWrapper ss;
  UserAlgorithmWrapper ua;
  run_one(&ss, "SuiteSparse", A, jac, var);
  run_one(&ua, "BlockDAGLU", A, jac, var);
  return 0;
}
