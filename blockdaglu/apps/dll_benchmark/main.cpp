#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "blockdaglu/env.h"

#if defined(_WIN32)
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
#endif

#include "AuxiliaryFunction.h"
#include "SuiteSparseWrapper.h"

using Clock = std::chrono::high_resolution_clock;

static const char* arg_value(int argc, char** argv, const char* key) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (argv[i] && std::string(argv[i]) == key) return argv[i + 1];
  }
  return nullptr;
}

static bool profile_enabled() {
  if (const char* env = blockdaglu_getenv_compat("USERALGO_PROFILE")) {
    return std::atoi(env) != 0;
  }
  return false;
}

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
  auto t0 = Clock::now();
  if (!alg->prepareData(std::move(A_copy))) {
    r.ok = false;
    return r;
  }
  auto t1 = Clock::now();
  if (!alg->rearrangeMatrix()) {
    r.ok = false;
    return r;
  }
  auto t2 = Clock::now();
  if (!alg->decompositionLU()) {
    r.ok = false;
    return r;
  }
  auto t3 = Clock::now();

  std::vector<double> x = b;
  if (!alg->solveLinearSystem(x)) {
    r.ok = false;
    return r;
  }
  auto t4 = Clock::now();

  r.t_reorder_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
  r.t_lu_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();
  r.t_solve_ms = std::chrono::duration<double, std::milli>(t4 - t3).count();
  r.t_total_ms = std::chrono::duration<double, std::milli>(t4 - t1).count();
  r.rel_err = rel_err_l2(x, xstar);
  r.rel_residual = rel_residual_l2(A, r.n, x, b0);
  r.ok = true;
  return r;
}

static double weight_for_n(int n) {
  if (n <= 10000) return 0.05;
  if (n <= 20000) return 0.1;
  if (n <= 40000) return 0.15;
  if (n <= 55000) return 0.25;
  return 0.5;
}

static std::filesystem::path default_cases_dir() {
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
    std::cerr << "Usage: blockdaglu_dll_benchmark <FinalTestCases path> [case_id] --dll <algo.dll>\n";
    return 2;
  }

  const char* dll_arg = arg_value(argc, argv, "--dll");
  if (!dll_arg) {
    std::cerr << "Missing required argument: --dll <useralgo.dll>\n";
    return 3;
  }

  const std::filesystem::path dll_path = std::filesystem::path(dll_arg);
  if (!std::filesystem::exists(dll_path)) {
    std::cerr << "DLL not found: " << dll_path.string() << "\n";
    return 4;
  }

#if !defined(_WIN32)
  std::cerr << "This tool currently supports Windows DLL loading only.\n";
  return 5;
#else
  HMODULE user_dll = LoadLibraryA(dll_path.string().c_str());
  if (!user_dll) {
    std::cerr << "LoadLibrary failed for: " << dll_path.string() << "\n";
    return 6;
  }
  using CreateFn = ALInterface* (*)();
  auto create = reinterpret_cast<CreateFn>(GetProcAddress(user_dll, "CreateAlgorithm"));
  if (!create) {
    std::cerr << "GetProcAddress(CreateAlgorithm) failed for: " << dll_path.string() << "\n";
    return 7;
  }
#endif

  // Note: ALInterface's destructor is not virtual (per provided header). We thus
  // keep the user algorithm instance alive for the whole process and let the OS
  // reclaim memory on exit (avoids per-case leaks affecting later timings).
  ALInterface* ua = create();
  if (!ua) {
    std::cerr << "CreateAlgorithm returned null for: " << dll_path.string() << "\n";
    return 8;
  }

  std::vector<RunResult> results;
  constexpr int kCases = 7;
  const int only_case = (argc >= 3) ? std::atoi(argv[2]) : 0;
  const int from_case = (only_case >= 1 && only_case <= kCases) ? only_case : 1;
  const int to_case = (only_case >= 1 && only_case <= kCases) ? only_case : kCases;

  for (int i = from_case; i <= to_case; ++i) {
    const std::string folder = std::to_string(i);
    const auto case_path = cases / folder;
    const std::string jac = (case_path / (folder + "_JacobiMatrixInfo.txt")).string();
    const std::string var = (case_path / (folder + "_VarInfo.csv")).string();
    const std::string b = (case_path / (folder + "_ResidualValue.csv")).string();
    const std::string xs = (case_path / (folder + "_PerturbVarValue.csv")).string();

    SuiteSparseWrapper ss;

    results.push_back(run_one(&ss, "SuiteSparse", i, jac, var, b, xs));
    results.push_back(run_one(ua, "BlockDAGLU", i, jac, var, b, xs));
  }

  std::cout << "alg,case,n,nnz,reorder_ms,lu_ms,solve_ms,total_ms,rel_err,rel_residual,ok\n";
  for (const auto& r : results) {
    std::cout << r.alg << "," << r.case_id << "," << r.n << "," << r.nnz << ","
              << std::fixed << std::setprecision(3) << r.t_reorder_ms << "," << r.t_lu_ms << "," << r.t_solve_ms << ","
              << r.t_total_ms << "," << std::scientific << std::setprecision(6) << r.rel_err << ","
              << std::scientific << std::setprecision(6) << r.rel_residual << "," << (r.ok ? "1" : "0") << "\n";
  }

  std::cout << "case,n,nnz,xstar_rel_residual,ss_rel_residual,ua_rel_residual,eps_suitesparse,err_user,margin_eps_minus_user,pass_user_le_eps\n";
  for (int i = from_case; i <= to_case; ++i) {
    const RunResult* ss = nullptr;
    const RunResult* ua = nullptr;
    for (const auto& r : results) {
      if (r.case_id != i) continue;
      if (r.alg == "SuiteSparse") ss = &r;
      if (r.alg == "BlockDAGLU") ua = &r;
    }
    if (!ss || !ua) continue;

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

    if (profile_enabled() && ua->ok && ss->ok) {
      const bool tiny_residual = std::isfinite(ua->rel_residual) && ua->rel_residual < 1e-10;
      const bool large_err_vs_eps = (std::isfinite(eps) && eps > 0.0 && std::isfinite(err) && err > eps * 5.0);
      if (tiny_residual && large_err_vs_eps) {
        std::cerr << "USERALGO_PROFILE,solution_selection"
                  << ",case=" << i << ",eps=" << std::scientific << std::setprecision(6) << eps
                  << ",err=" << std::scientific << std::setprecision(6) << err
                  << ",margin=" << std::scientific << std::setprecision(6) << margin
                  << ",ua_rel_residual=" << std::scientific << std::setprecision(6) << ua->rel_residual
                  << ",ss_rel_residual=" << std::scientific << std::setprecision(6) << ss->rel_residual
                  << ",xstar_rel_residual=" << std::scientific << std::setprecision(6) << xstar_rel_res << "\n";
      }
    }

    std::cout << i << "," << ua->n << "," << ua->nnz << "," << std::scientific << std::setprecision(6) << xstar_rel_res
              << "," << ss->rel_residual << "," << ua->rel_residual << "," << std::scientific << std::setprecision(6)
              << eps << "," << err << "," << margin << "," << (pass ? "1" : "0") << "\n";
  }

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
    std::cout << alg << "_weighted_total_ms," << std::fixed << std::setprecision(3) << wsum << "\n";
    std::cout << alg << "_max_rel_err," << std::scientific << std::setprecision(6) << max_err << "\n";
    std::cout << alg << "_all_ok," << (all_ok ? "1" : "0") << "\n";
  }

  return 0;
}
