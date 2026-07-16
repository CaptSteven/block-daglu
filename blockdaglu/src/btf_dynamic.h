#pragma once

#include <cstdint>
#include <stdexcept>

#if __has_include("suitesparse/btf.h")
  #include "suitesparse/btf.h"
#elif __has_include("tool/include/suitesparse/btf.h")
  #include "tool/include/suitesparse/btf.h"
#else
  #error "Cannot find suitesparse/btf.h. Add tool/include to your include path."
#endif

// Runtime loader for btf.dll (avoids needing an import .lib).
class BtfDynamic {
public:
  static BtfDynamic& instance();

  int32_t order(int32_t n,
                int32_t* Ap,
                int32_t* Ai,
                double maxwork,
                double* work,
                int32_t* P,
                int32_t* Q,
                int32_t* R,
                int32_t* nmatch,
                int32_t* Work) const;

private:
  BtfDynamic();
  ~BtfDynamic();

  BtfDynamic(const BtfDynamic&) = delete;
  BtfDynamic& operator=(const BtfDynamic&) = delete;

  void load();

  using FnOrder = int32_t (*)(int32_t,
                              int32_t*,
                              int32_t*,
                              double,
                              double*,
                              int32_t*,
                              int32_t*,
                              int32_t*,
                              int32_t*,
                              int32_t*);

  void* dll_{nullptr};
  FnOrder order_{nullptr};
};

