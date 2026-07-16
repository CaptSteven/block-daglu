#include "UserAlgorithmWrapper.h"

#if defined(_WIN32)
  #if defined(USERALGO_EXPORTS)
    #define USERALGO_API __declspec(dllexport)
  #else
    #define USERALGO_API __declspec(dllimport)
  #endif
#else
  #define USERALGO_API
#endif

extern "C" {

// Optional factory exports (useful if the evaluator loads your DLL dynamically).
USERALGO_API ALInterface* CreateAlgorithm() {
  return new UserAlgorithmWrapper();
}

USERALGO_API void DestroyAlgorithm(ALInterface* algorithm) {
  // ALInterface::~ALInterface() is not virtual in the organizer headers; deleting via base pointer is UB.
  delete static_cast<UserAlgorithmWrapper*>(algorithm);
}

}  // extern "C"
