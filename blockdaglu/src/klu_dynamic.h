#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

#if __has_include("suitesparse/klu.h")
  #include "suitesparse/klu.h"
#elif __has_include("tool/include/suitesparse/klu.h")
  #include "tool/include/suitesparse/klu.h"
#else
  #error "Cannot find suitesparse/klu.h. Add tool/include to your include path."
#endif

// Runtime loader for klu.dll (avoids needing an import .lib).
class KluDynamic {
public:
  static KluDynamic& instance();

  klu_symbolic* analyze(int32_t n, int32_t* Ap, int32_t* Ai, klu_common* c) const;
  klu_symbolic* analyze_given(int32_t n, int32_t* Ap, int32_t* Ai, int32_t* P, int32_t* Q, klu_common* c) const;
  klu_numeric* factor(int32_t* Ap, int32_t* Ai, double* Ax, klu_symbolic* s, klu_common* c) const;
  int solve(klu_symbolic* s, klu_numeric* n, int32_t ldim, int32_t nrhs, double* B, klu_common* c) const;
  int extract(klu_numeric* numeric,
              klu_symbolic* symbolic,
              int32_t* Lp,
              int32_t* Li,
              double* Lx,
              int32_t* Up,
              int32_t* Ui,
              double* Ux,
              int32_t* Fp,
              int32_t* Fi,
              double* Fx,
              int32_t* P,
              int32_t* Q,
              double* Rs,
              int32_t* R,
              klu_common* c) const;

  int defaults(klu_common* c) const;
  void free_symbolic(klu_symbolic** s, klu_common* c) const;
  void free_numeric(klu_numeric** n, klu_common* c) const;

private:
  KluDynamic();
  ~KluDynamic();

  KluDynamic(const KluDynamic&) = delete;
  KluDynamic& operator=(const KluDynamic&) = delete;

  void load();

  using FnDefaults = int (*)(klu_common*);
  using FnAnalyze = klu_symbolic* (*)(int32_t, int32_t*, int32_t*, klu_common*);
  using FnAnalyzeGiven = klu_symbolic* (*)(int32_t, int32_t*, int32_t*, int32_t*, int32_t*, klu_common*);
  using FnFactor = klu_numeric* (*)(int32_t*, int32_t*, double*, klu_symbolic*, klu_common*);
  using FnSolve = int (*)(klu_symbolic*, klu_numeric*, int32_t, int32_t, double*, klu_common*);
  using FnExtract = int (*)(klu_numeric*,
                            klu_symbolic*,
                            int32_t*,
                            int32_t*,
                            double*,
                            int32_t*,
                            int32_t*,
                            double*,
                            int32_t*,
                            int32_t*,
                            double*,
                            int32_t*,
                            int32_t*,
                            double*,
                            int32_t*,
                            klu_common*);
  using FnFreeSymbolic = int (*)(klu_symbolic**, klu_common*);
  using FnFreeNumeric = int (*)(klu_numeric**, klu_common*);

  void* dll_{nullptr};

  FnDefaults defaults_{nullptr};
  FnAnalyze analyze_{nullptr};
  FnAnalyzeGiven analyze_given_{nullptr};
  FnFactor factor_{nullptr};
  FnSolve solve_{nullptr};
  FnExtract extract_{nullptr};
  FnFreeSymbolic free_symbolic_{nullptr};
  FnFreeNumeric free_numeric_{nullptr};
};
