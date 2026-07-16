#include "btf_dynamic.h"

#include <string>

#ifdef _WIN32
  #include <windows.h>
#endif

static void* load_sym(void* dll, const char* name) {
#ifdef _WIN32
  auto p = reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(dll), name));
  if (!p) {
    throw std::runtime_error(std::string("Missing symbol in btf.dll: ") + name);
  }
  return p;
#else
  (void)dll;
  (void)name;
  throw std::runtime_error("load_sym not supported on this platform");
#endif
}

BtfDynamic& BtfDynamic::instance() {
  // Intentionally "leak" the singleton to avoid shutdown-order issues.
  // We keep btf.dll loaded until process exit.
  static BtfDynamic* inst = new BtfDynamic();
  return *inst;
}

BtfDynamic::BtfDynamic() {
  load();
}

BtfDynamic::~BtfDynamic() {
#ifdef _WIN32
  if (dll_) {
    FreeLibrary(reinterpret_cast<HMODULE>(dll_));
    dll_ = nullptr;
  }
#endif
}

void BtfDynamic::load() {
#ifdef _WIN32
  dll_ = reinterpret_cast<void*>(LoadLibraryA("btf.dll"));
  if (!dll_) {
    throw std::runtime_error("Failed to load btf.dll. Ensure it is on PATH or next to the executable.");
  }

  order_ = reinterpret_cast<FnOrder>(load_sym(dll_, "btf_order"));
#else
  throw std::runtime_error("BtfDynamic is only implemented for Windows in this project.");
#endif
}

int32_t BtfDynamic::order(int32_t n,
                          int32_t* Ap,
                          int32_t* Ai,
                          double maxwork,
                          double* work,
                          int32_t* P,
                          int32_t* Q,
                          int32_t* R,
                          int32_t* nmatch,
                          int32_t* Work) const {
  if (!order_) throw std::runtime_error("BtfDynamic: order not loaded");
  return order_(n, Ap, Ai, maxwork, work, P, Q, R, nmatch, Work);
}
