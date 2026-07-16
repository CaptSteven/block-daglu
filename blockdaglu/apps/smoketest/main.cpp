#include <filesystem>
#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "UserAlgorithmWrapper.h"

#if __has_include("tool/include/Tester.h")
  #include "tool/include/Tester.h"
#elif __has_include("Tester.h")
  #include "Tester.h"
#else
  #error "Cannot find Tester.h. Add tool/include to your include path."
#endif

static std::filesystem::path default_cases_dir() {
  // Prefer the open-source layout:
  //   BlockDAGLU/data/FinalTestCases/...
  std::filesystem::path p = std::filesystem::current_path();
  for (int i = 0; i < 6; ++i) {
    auto candidate = p / "data" / "FinalTestCases";
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
    p = p.parent_path();
  }
  return {};
}

int main(int argc, char** argv) {
  std::filesystem::path cases = (argc >= 2) ? std::filesystem::path(argv[1]) : default_cases_dir();
  if (cases.empty() || !std::filesystem::exists(cases)) {
    std::cerr << "Usage: blockdaglu_smoketest <FinalTestCases path>\n";
    std::cerr << "Could not locate data/FinalTestCases automatically.\n";
    return 2;
  }

  std::cout << "Cases: " << cases.string() << "\n";

  PerformanceTester tester;
  auto alg = std::make_unique<UserAlgorithmWrapper>();

  // Run all numeric subfolders (e.g., 1..15).
  std::vector<int> ids;
  for (const auto& e : std::filesystem::directory_iterator(cases)) {
    if (!e.is_directory()) continue;
    const std::string name = e.path().filename().string();
    try {
      std::size_t pos = 0;
      const int id = std::stoi(name, &pos);
      if (pos == name.size() && id > 0) ids.push_back(id);
    } catch (...) {
      // ignore
    }
  }
  std::sort(ids.begin(), ids.end());
  if (ids.empty()) {
    std::cerr << "No case folders found under: " << cases.string() << "\n";
    return 2;
  }

  for (int id : ids) {
    const std::string folder = std::to_string(id);
    const auto case_path = cases / folder;

    const std::string path1 = (case_path / (folder + "_JacobiMatrixInfo.txt")).string();
    const std::string path2 = (case_path / (folder + "_VarInfo.csv")).string();
    const std::string path3 = (case_path / (folder + "_ResidualValue.csv")).string();
    const std::string path4 = (case_path / (folder + "_PerturbVarValue.csv")).string();

    tester.testAlgorithm(alg.get(), path1, path2, path3, path4, id, false);
  }

  tester.generateFinalReport();
  return 0;
}
