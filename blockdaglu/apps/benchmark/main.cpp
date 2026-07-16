#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#include <cstdlib>

#include "blockdaglu/env.h"
#include "UserAlgorithmWrapper.h"

#include "AuxiliaryFunction.h"
#include "SuiteSparseWrapper.h"

using Clock = std::chrono::high_resolution_clock;

static bool profile_enabled() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_PROFILE")) {
    return std::atoi(env) != 0;
  }
  return false;
}

static std::vector<double> load_vector_csv_singlecol(const std::string& path) {
  std::vector<double> v;
  std::ifstream f(path);
  if (!f.is_open()) {
    return v;
  }
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty()) continue;
    v.push_back(std::stod(line));
  }
  return v;
}

static double rel_err_l2(const std::vector<double>& x, const std::vector<double>& x_star) {
  double num = 0.0;
  double den = 0.0;
  const std::size_t n = std::min(x.size(), x_star.size());
  for (std::size_t i = 0; i < n; ++i) {
    const double d = x[i] - x_star[i];
    num += d * d;
    den += x_star[i] * x_star[i];
  }
  if (den == 0.0) return std::numeric_limits<double>::infinity();
  return std::sqrt(num / den);
}

static double rel_residual_l2(const COOMatrix& A,
                              int n,
                              const std::vector<double>& x,
                              const std::vector<double>& b0) {
  if (static_cast<int>(x.size()) != n || static_cast<int>(b0.size()) != n) {
    return std::numeric_limits<double>::infinity();
  }
  std::vector<double> y(static_cast<std::size_t>(n), 0.0);
  for (const auto& t : A) {
    const int i = std::get<0>(t);
    const int j = std::get<1>(t);
    const double v = std::get<2>(t);
    if (i < 0 || j < 0 || i >= n || j >= n) continue;
    y[static_cast<std::size_t>(i)] += v * x[static_cast<std::size_t>(j)];
  }
  double num = 0.0;
  double den = 0.0;
  for (int i = 0; i < n; ++i) {
    const double r = b0[static_cast<std::size_t>(i)] - y[static_cast<std::size_t>(i)];
    num += r * r;
    const double bi = b0[static_cast<std::size_t>(i)];
    den += bi * bi;
  }
  if (den == 0.0) return std::numeric_limits<double>::infinity();
  return std::sqrt(num / den);
}

struct RunResult {
  std::string alg;
  int case_id = 0;
  int n = 0;
  std::size_t nnz = 0;
  double t_reorder_ms = 0.0;
  double t_lu_ms = 0.0;
  double t_solve_ms = 0.0;
  double t_total_ms = 0.0;
  double rel_err = 0.0;
  double rel_residual = 0.0;
  bool ok = false;
};

static RunResult run_one(ALInterface* alg,
                         const std::string& alg_name,
                         int case_id,
                          const std::string& jacobi_txt,
                          const std::string& varinfo_csv,
                          const std::string& b_csv,
                          const std::string& xstar_csv) {
  RunResult r;
  r.alg = alg_name;
  r.case_id = case_id;

  COOMatrix A = parseScaledValues(jacobi_txt, varinfo_csv);
  r.nnz = A.size();
  int n = 0;
  for (const auto& t : A) n = std::max(n, std::max(std::get<0>(t), std::get<1>(t)));
  r.n = n + 1;

  std::vector<double> b = load_vector_csv_singlecol(b_csv);
  const std::vector<double> b0 = b;
  std::vector<double> xstar = load_vector_csv_singlecol(xstar_csv);

  if (static_cast<int>(b.size()) != r.n) {
    r.ok = false;
    return r;
  }

  COOMatrix A_copy = A;
  if (!alg->prepareData(std::move(A_copy))) {
    r.ok = false;
    return r;
  }

  const auto t0 = Clock::now();
  const bool ok_re = alg->rearrangeMatrix();
  const auto t1 = Clock::now();
  const bool ok_lu = ok_re && alg->decompositionLU();
  const auto t2 = Clock::now();
  const bool ok_sol = ok_lu && alg->solveLinearSystem(b);
  const auto t3 = Clock::now();

  r.t_reorder_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  r.t_lu_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
  r.t_solve_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();
  r.t_total_ms = r.t_reorder_ms + r.t_lu_ms + r.t_solve_ms;
  r.ok = ok_sol;

  if (ok_sol && xstar.size() == b.size()) {
    r.rel_err = rel_err_l2(b, xstar);
    r.rel_residual = rel_residual_l2(A, r.n, b, b0);
  } else {
    r.rel_err = std::numeric_limits<double>::infinity();
    r.rel_residual = std::numeric_limits<double>::infinity();
  }
  return r;
}

static double weight_for_n(int n) {
  if (n <= 20000) return 0.1;
  if (n <= 40000) return 0.15;
  if (n <= 55000) return 0.25;
  return 0.5;
}

static std::filesystem::path default_cases_dir() {
  // Prefer the open-source layout:
  //   BlockDAGLU/data/FinalTestCases/...
  std::filesystem::path p = std::filesystem::current_path();
  for (int i = 0; i < 6; ++i) {
    auto candidate = p / "data" / "FinalTestCases";
    if (std::filesystem::exists(candidate)) return candidate;
    p = p.parent_path();
  }
  return {};
}

int main(int argc, char** argv) {
  std::filesystem::path cases = (argc >= 2) ? std::filesystem::path(argv[1]) : default_cases_dir();
  if (!std::filesystem::exists(cases)) {
    std::cerr << "Usage: blockdaglu_benchmark <cases_root path> [case_id]\n";
    std::cerr << "Could not locate data/FinalTestCases automatically.\n";
    return 2;
  }

  // Optional: output directory (write clean CSV files; avoids mixing with tool logs on stdout).
  // If unset, the program prints CSV to stdout (may be noisy because the tool parser writes logs).
  std::filesystem::path out_dir;
  if (const char* env = std::getenv("BLOCKDAGLU_BENCH_OUT")) {
    out_dir = std::filesystem::path(env);
  }
  const bool file_out = !out_dir.empty();
  if (file_out) {
    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    if (ec) {
      std::cerr << "Failed to create out dir: " << out_dir.string() << "\n";
      return 2;
    }
  }

  std::vector<RunResult> results;
  int max_case = 0;
  for (const auto& entry : std::filesystem::directory_iterator(cases)) {
    if (!entry.is_directory()) continue;
    const std::string name = entry.path().filename().string();
    if (name.empty()) continue;
    bool ok = true;
    for (char ch : name) {
      if (ch < '0' || ch > '9') {
        ok = false;
        break;
      }
    }
    if (!ok) continue;
    const int id = std::atoi(name.c_str());
    if (id > max_case) max_case = id;
  }
  if (max_case <= 0) {
    std::cerr << "No numeric case folders found under: " << cases.string() << "\n";
    return 2;
  }

  const int only_case = (argc >= 3) ? std::atoi(argv[2]) : 0;
  const int from_case = (only_case >= 1 && only_case <= max_case) ? only_case : 1;
  const int to_case = (only_case >= 1 && only_case <= max_case) ? only_case : max_case;

  for (int i = from_case; i <= to_case; ++i) {
    const std::string folder = std::to_string(i);
    const auto case_path = cases / folder;
    const std::string jac = (case_path / (folder + "_JacobiMatrixInfo.txt")).string();
    const std::string var = (case_path / (folder + "_VarInfo.csv")).string();
    const std::string b = (case_path / (folder + "_ResidualValue.csv")).string();
    const std::string xs = (case_path / (folder + "_PerturbVarValue.csv")).string();

    SuiteSparseWrapper ss;
    UserAlgorithmWrapper ua;

    results.push_back(run_one(&ss, "SuiteSparse", i, jac, var, b, xs));
    results.push_back(run_one(&ua, "BlockDAGLU", i, jac, var, b, xs));
  }

  auto open_or_stdout = [&](const std::filesystem::path& p) -> std::unique_ptr<std::ostream> {
    if (!file_out) return {};
    auto f = std::make_unique<std::ofstream>(p.string());
    if (!f->is_open()) {
      std::cerr << "Failed to open: " << p.string() << "\n";
      std::exit(2);
    }
    return f;
  };

  std::unique_ptr<std::ostream> runs_file = open_or_stdout(out_dir / "runs.csv");
  std::ostream& runs_out = runs_file ? *runs_file : std::cout;

  // Algorithm-level table.
  runs_out << "alg,case,n,nnz,reorder_ms,lu_ms,solve_ms,total_ms,rel_err,rel_residual,ok\n";
  for (const auto& r : results) {
    runs_out << r.alg << "," << r.case_id << "," << r.n << "," << r.nnz << ","
             << std::fixed << std::setprecision(3)
             << r.t_reorder_ms << "," << r.t_lu_ms << "," << r.t_solve_ms << "," << r.t_total_ms << ","
             << std::scientific << std::setprecision(6) << r.rel_err << ","
             << std::scientific << std::setprecision(6) << r.rel_residual << ","
             << (r.ok ? "1" : "0") << "\n";
  }

  // Per-case ε margin (ε := SuiteSparse error, margin := ε - err_user).
  std::unique_ptr<std::ostream> margin_file = open_or_stdout(out_dir / "margin.csv");
  std::ostream& margin_out = margin_file ? *margin_file : std::cout;

  margin_out << "case,n,nnz,xstar_rel_residual,ss_rel_residual,ua_rel_residual,eps_suitesparse,err_user,margin_eps_minus_user,pass_user_le_eps\n";
  for (int i = from_case; i <= to_case; ++i) {
    const RunResult* ss = nullptr;
    const RunResult* ua = nullptr;
    for (const auto& r : results) {
      if (r.case_id != i) continue;
      if (r.alg == "SuiteSparse") ss = &r;
      if (r.alg == "BlockDAGLU") ua = &r;
    }
    if (!ss || !ua) continue;

    // Recompute A, b0, xstar residual for evidence (not timed).
    const std::string folder = std::to_string(i);
    const auto case_path = cases / folder;
    const std::string jac = (case_path / (folder + "_JacobiMatrixInfo.txt")).string();
    const std::string var = (case_path / (folder + "_VarInfo.csv")).string();
    const std::string bpath = (case_path / (folder + "_ResidualValue.csv")).string();
    const std::string xspath = (case_path / (folder + "_PerturbVarValue.csv")).string();
    COOMatrix A = parseScaledValues(jac, var);
    int n = 0;
    for (const auto& t : A) n = std::max(n, std::max(std::get<0>(t), std::get<1>(t)));
    const int nn = n + 1;
    const std::vector<double> b0 = load_vector_csv_singlecol(bpath);
    const std::vector<double> xstar = load_vector_csv_singlecol(xspath);
    const double xstar_rel_res = rel_residual_l2(A, nn, xstar, b0);

    const double eps = ss->rel_err;
    const double err = ua->rel_err;
    const double margin = eps - err;
    const bool pass = ua->ok && ss->ok && (err <= eps);

    // Diagnostic: for near-singular systems, multiple solutions may exist.
    // In that case, a solver can achieve a tiny residual but still be far from x* (large rel_err).
    if (profile_enabled() && ua->ok && ss->ok) {
      const bool tiny_residual = std::isfinite(ua->rel_residual) && ua->rel_residual < 1e-10;
      const bool large_err_vs_eps = (std::isfinite(eps) && eps > 0.0 && std::isfinite(err) && err > eps * 5.0);
      if (tiny_residual && large_err_vs_eps) {
        std::cerr << "USERALGO_PROFILE,solution_selection"
                  << ",case=" << i
                  << ",eps=" << std::scientific << std::setprecision(6) << eps
                  << ",err=" << std::scientific << std::setprecision(6) << err
                  << ",margin=" << std::scientific << std::setprecision(6) << margin
                  << ",ua_rel_residual=" << std::scientific << std::setprecision(6) << ua->rel_residual
                  << ",ss_rel_residual=" << std::scientific << std::setprecision(6) << ss->rel_residual
                  << ",xstar_rel_residual=" << std::scientific << std::setprecision(6) << xstar_rel_res
                  << "\n";
      }
    }
    margin_out << i << "," << ua->n << "," << ua->nnz << ","
               << std::scientific << std::setprecision(6)
               << xstar_rel_res << "," << ss->rel_residual << "," << ua->rel_residual << ","
               << std::scientific << std::setprecision(6)
               << eps << "," << err << "," << margin << ","
               << (pass ? "1" : "0") << "\n";
  }

  // Weighted total time summary for each algorithm.
  std::unique_ptr<std::ostream> summary_file = open_or_stdout(out_dir / "summary.txt");
  std::ostream& summary_out = summary_file ? *summary_file : std::cout;

  double ss_w = 0.0, ua_w = 0.0;
  for (const std::string& alg : {"SuiteSparse", "BlockDAGLU"}) {
    double wsum = 0.0;
    double max_err = 0.0;
    bool all_ok = true;
    for (const auto& r : results) {
      if (r.alg != alg) continue;
      all_ok = all_ok && r.ok;
      max_err = std::max(max_err, r.rel_err);
      wsum += weight_for_n(r.n) * r.t_total_ms;
    }
    if (alg == "SuiteSparse") ss_w = wsum;
    if (alg == "BlockDAGLU") ua_w = wsum;
    summary_out << alg << "_weighted_total_ms," << std::fixed << std::setprecision(3) << wsum << "\n";
    summary_out << alg << "_max_rel_err," << std::scientific << std::setprecision(6) << max_err << "\n";
    summary_out << alg << "_all_ok," << (all_ok ? "1" : "0") << "\n";
  }
  if (ss_w > 0.0 && ua_w > 0.0) {
    summary_out << "speedup_suitesparse_over_blockdaglu," << std::fixed << std::setprecision(3) << (ss_w / ua_w) << "\n";
  }

  if (file_out) {
    std::cerr << "Wrote benchmark outputs to: " << out_dir.string() << "\n";
  }

  return 0;
}
