#include "klu_dynamic.h"

#include <mutex>

#ifdef _WIN32
  #include <windows.h>
#endif

#if !defined(BLOCKDAGLU_USE_LINKED_SUITESPARSE)
static void* load_sym(void* dll, const char* name) {
#ifdef _WIN32
  auto p = reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(dll), name));
  if (!p) {
    throw std::runtime_error(std::string("Missing symbol in klu.dll: ") + name);
  }
  return p;
#else
  (void)dll;
  (void)name;
  throw std::runtime_error("load_sym not supported on this platform");
#endif
}
#endif

KluDynamic& KluDynamic::instance() {
  // Intentionally "leak" the singleton to avoid shutdown-order issues.
  // We keep klu.dll loaded until process exit because global caches may
  // still reference KLU symbolic objects in their destructors.
  static KluDynamic* inst = new KluDynamic();
  return *inst;
}

KluDynamic::KluDynamic() {
  load();
}

KluDynamic::~KluDynamic() {
#ifdef _WIN32
  if (dll_) {
    FreeLibrary(reinterpret_cast<HMODULE>(dll_));
    dll_ = nullptr;
  }
#endif
}

void KluDynamic::load() {
#if defined(BLOCKDAGLU_USE_LINKED_SUITESPARSE)
  defaults_ = &klu_defaults;
  analyze_ = &klu_analyze;
  analyze_given_ = &klu_analyze_given;
  factor_ = &klu_factor;
  solve_ = &klu_solve;
  extract_ = &klu_extract;
  free_symbolic_ = &klu_free_symbolic;
  free_numeric_ = &klu_free_numeric;
#elif defined(_WIN32)
  // Rely on PATH / current directory (typical evaluator setup places klu.dll next to the runner).
  dll_ = reinterpret_cast<void*>(LoadLibraryA("klu.dll"));
  if (!dll_) {
    throw std::runtime_error("Failed to load klu.dll. Ensure it is on PATH or next to the executable.");
  }

  defaults_ = reinterpret_cast<FnDefaults>(load_sym(dll_, "klu_defaults"));
  analyze_ = reinterpret_cast<FnAnalyze>(load_sym(dll_, "klu_analyze"));
  analyze_given_ = reinterpret_cast<FnAnalyzeGiven>(load_sym(dll_, "klu_analyze_given"));
  factor_ = reinterpret_cast<FnFactor>(load_sym(dll_, "klu_factor"));
  solve_ = reinterpret_cast<FnSolve>(load_sym(dll_, "klu_solve"));
  extract_ = reinterpret_cast<FnExtract>(load_sym(dll_, "klu_extract"));
  free_symbolic_ = reinterpret_cast<FnFreeSymbolic>(load_sym(dll_, "klu_free_symbolic"));
  free_numeric_ = reinterpret_cast<FnFreeNumeric>(load_sym(dll_, "klu_free_numeric"));
#else
  throw std::runtime_error("KluDynamic is only implemented for Windows in this project.");
#endif
}

int KluDynamic::defaults(klu_common* c) const {
  return defaults_(c);
}

klu_symbolic* KluDynamic::analyze(int32_t n, int32_t* Ap, int32_t* Ai, klu_common* c) const {
  return analyze_(n, Ap, Ai, c);
}

klu_symbolic* KluDynamic::analyze_given(int32_t n, int32_t* Ap, int32_t* Ai, int32_t* P, int32_t* Q, klu_common* c) const {
  return analyze_given_(n, Ap, Ai, P, Q, c);
}

klu_numeric* KluDynamic::factor(int32_t* Ap, int32_t* Ai, double* Ax, klu_symbolic* s, klu_common* c) const {
  return factor_(Ap, Ai, Ax, s, c);
}

int KluDynamic::solve(klu_symbolic* s, klu_numeric* n, int32_t ldim, int32_t nrhs, double* B, klu_common* c) const {
  return solve_(s, n, ldim, nrhs, B, c);
}

int KluDynamic::extract(klu_numeric* numeric,
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
                        klu_common* c) const {
  return extract_(numeric, symbolic, Lp, Li, Lx, Up, Ui, Ux, Fp, Fi, Fx, P, Q, Rs, R, c);
}

void KluDynamic::free_symbolic(klu_symbolic** s, klu_common* c) const {
  free_symbolic_(s, c);
}

void KluDynamic::free_numeric(klu_numeric** n, klu_common* c) const {
  free_numeric_(n, c);
}
