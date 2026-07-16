#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>

#include "AuxiliaryFunction.h"

// NOTE: SuiteSparseWrapper hides its KLU common struct as private. For local
// verification we temporarily expose it. This executable is for diagnostics only.
#define private public
#include "SuiteSparseWrapper.h"
#undef private

static std::filesystem::path default_cases_dir() {
  std::filesystem::path p = std::filesystem::current_path();
  for (int i = 0; i < 8; ++i) {
    auto candidate = p / "data" / "FinalTestCases";
    if (std::filesystem::exists(candidate)) return candidate;
    p = p.parent_path();
  }
  return {};
}

static void print_common(const char* tag, const klu_common& c) {
  std::cout << tag << ":\n";
  std::cout << "  btf=" << c.btf << "\n";
  std::cout << "  ordering=" << c.ordering << "\n";
  std::cout << "  scale=" << c.scale << "\n";
  std::cout << "  tol=" << c.tol << "\n";
  std::cout << "  halt_if_singular=" << c.halt_if_singular << "\n";
  std::cout << "  status=" << c.status << "\n";
}

int main(int argc, char** argv) {
  std::filesystem::path cases = (argc >= 2) ? std::filesystem::path(argv[1]) : default_cases_dir();
  const int case_id = (argc >= 3) ? std::atoi(argv[2]) : 1;
  if (cases.empty() || !std::filesystem::exists(cases)) {
    std::cerr << "Usage: blockdaglu_commoncheck <FinalTestCases path> [case_id]\n";
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

  SuiteSparseWrapper ss;
  print_common("after ctor", ss.m_kluCommon);

  if (!ss.prepareData(A)) {
    std::cerr << "prepareData failed\n";
    return 4;
  }
  print_common("after prepareData", ss.m_kluCommon);

  if (!ss.rearrangeMatrix()) {
    std::cerr << "rearrangeMatrix failed\n";
    return 5;
  }
  print_common("after rearrangeMatrix", ss.m_kluCommon);

  if (!ss.decompositionLU()) {
    std::cerr << "decompositionLU failed\n";
    return 6;
  }
  print_common("after decompositionLU", ss.m_kluCommon);

  return 0;
}
