#pragma once

#include <cstdlib>
#include <cstring>
#include <string>

// Environment variable compatibility layer.
//
// The project was originally developed with USERALGO_* env vars.
// For open-source packaging we prefer BLOCKDAGLU_*.
//
// Rule:
// - If the code asks for USERALGO_FOO, we first try BLOCKDAGLU_FOO.
// - Otherwise, fall back to the original name.
inline const char* blockdaglu_getenv_compat(const char* name) {
  if (!name) return nullptr;
  constexpr const char* kOld = "USERALGO_";
  constexpr const char* kNew = "BLOCKDAGLU_";
  constexpr std::size_t kOldLen = 8;
  if (std::strncmp(name, kOld, kOldLen) == 0) {
    std::string alt = std::string(kNew) + (name + kOldLen);
    if (const char* v = std::getenv(alt.c_str())) return v;
  }
  return std::getenv(name);
}

