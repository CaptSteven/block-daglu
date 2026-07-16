#pragma once

#include <memory>
#include <vector>

#if __has_include("tool/include/ALInterface.h")
  #include "tool/include/ALInterface.h"
#elif __has_include("ALInterface.h")
  #include "ALInterface.h"
#else
  #error "Cannot find organizer headers. Add tool/include to your include path."
#endif

#include "blockdaglu/solver.h"

// Compatibility adapter for the competition interface (ALInterface).
// This keeps the original class name used by the competition harness, while the
// core algorithm lives in blockdaglu::Solver (tool-independent).
class UserAlgorithmWrapper final : public ALInterface {
 public:
  UserAlgorithmWrapper();
  // NOTE: ALInterface::~ALInterface() is not virtual in organizer headers.
  // Do not mark this as 'override'.
  ~UserAlgorithmWrapper();

  bool prepareData(COOMatrix m) override;
  bool rearrangeMatrix() override;
  bool decompositionLU() override;
  bool solveLinearSystem(std::vector<double>& b) override;

  COOMatrix getRearrangedMatrix() override;
  COOMatrix getQ() override;
  COOMatrix getP() override;
  COOMatrix getL() override;
  COOMatrix getU() override;

  const char* selectedMode() const;

 private:
  std::unique_ptr<blockdaglu::Solver> solver_;
};
