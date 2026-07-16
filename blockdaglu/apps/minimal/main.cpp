#include <cstdint>
#include <iostream>
#include <cstdlib>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#endif

#include "blockdaglu/solver.h"

// A minimal, tool-independent example.
// NOTE: KLU/BTF are loaded dynamically (klu.dll / btf.dll). On Windows, ensure they are on PATH
// (or in the working directory) before running.
int main() {
  const bool trace = (std::getenv("BLOCKDAGLU_TRACE") != nullptr);
  auto tlog = [&](const char* s) {
    if (!trace) return;
    std::cerr << "[trace] " << s << "\n";
    std::cerr.flush();
  };

  tlog("start");
  blockdaglu::Solver s;
  tlog("solver_ctor_ok");

  // 3x3 example:
  // [4 1 0] [x0] = [1]
  // [1 3 1] [x1] = [2]
  // [0 1 2] [x2] = [3]
  blockdaglu::CooMatrix A;
  A.emplace_back(0, 0, 4.0);
  A.emplace_back(0, 1, 1.0);
  A.emplace_back(1, 0, 1.0);
  A.emplace_back(1, 1, 3.0);
  A.emplace_back(1, 2, 1.0);
  A.emplace_back(2, 1, 1.0);
  A.emplace_back(2, 2, 2.0);

  std::vector<double> b{1.0, 2.0, 3.0};

  tlog("set_matrix");
  if (!s.set_matrix(std::move(A))) {
    std::cerr << "set_matrix failed\n";
    return 1;
  }
  tlog("analyze");
  if (!s.analyze()) {
    std::cerr << "analyze failed (check klu.dll / btf.dll availability)\n";
    return 1;
  }
  tlog("factor");
  if (!s.factor()) {
    std::cerr << "factor failed\n";
    return 1;
  }
  tlog("solve");
  if (!s.solve_in_place(b)) {
    std::cerr << "solve failed\n";
    return 1;
  }
  tlog("done");

  std::cout << "x = [";
  for (std::size_t i = 0; i < b.size(); ++i) {
    std::cout << b[i] << (i + 1 == b.size() ? "" : ", ");
  }
  std::cout << "]\n";
  return 0;
}
