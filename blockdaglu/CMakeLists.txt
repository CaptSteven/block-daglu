cmake_minimum_required(VERSION 3.20)

# NOTE: This repository started from a competition implementation. We keep the
# original target names for now, but the external-facing project name is BlockDAGLU.
project(BlockDAGLU LANGUAGES C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BLOCKDAGLU_BUILD_COMPAT_SPARSEMATRIXLUTEST "Build competition-compatible adapter + benchmark tools" OFF)
option(BLOCKDAGLU_BUILD_SMOKETEST "Build local smoketest executable (requires tool bundle)" OFF)
option(BLOCKDAGLU_USE_LINKED_SUITESPARSE "Link SuiteSparse through CMake instead of loading DLLs by name" ON)

# SuiteSparse dependency. Public builds use a linked package (vcpkg/config package).
# The legacy header + runtime DLL mode remains available for the original evaluator bundle.
set(SUITESPARSE_INCLUDE_DIR "" CACHE PATH "Path to SuiteSparse headers root (contains suitesparse/klu.h)")
set(SUITESPARSE_LIBRARY_DIR "" CACHE PATH "Optional directory containing SuiteSparse libraries")
set(BLOCKDAGLU_SUITESPARSE_TARGETS "")

if(BLOCKDAGLU_USE_LINKED_SUITESPARSE)
  find_package(SuiteSparse CONFIG QUIET)
  if(NOT TARGET SuiteSparse::KLU AND NOT TARGET SuiteSparse::klu)
    # SuiteSparse 7 installs per-component config packages, while older vcpkg
    # ports expose one aggregate SuiteSparse package.
    find_package(SuiteSparse_config CONFIG QUIET)
    find_package(BTF CONFIG QUIET)
    find_package(AMD CONFIG QUIET)
    find_package(COLAMD CONFIG QUIET)
    find_package(KLU CONFIG QUIET)
  endif()
  foreach(component KLU BTF AMD COLAMD SuiteSparseConfig)
    string(TOLOWER "${component}" component_lower)
    if(TARGET "SuiteSparse::${component}")
      list(APPEND BLOCKDAGLU_SUITESPARSE_TARGETS "SuiteSparse::${component}")
    elseif(TARGET "SuiteSparse::${component_lower}")
      list(APPEND BLOCKDAGLU_SUITESPARSE_TARGETS "SuiteSparse::${component_lower}")
    elseif(TARGET "${component}::${component}")
      list(APPEND BLOCKDAGLU_SUITESPARSE_TARGETS "${component}::${component}")
    endif()
  endforeach()

  if(NOT BLOCKDAGLU_SUITESPARSE_TARGETS)
    if(NOT SUITESPARSE_INCLUDE_DIR OR NOT SUITESPARSE_LIBRARY_DIR)
      message(FATAL_ERROR
        "Linked SuiteSparse was requested, but no SuiteSparse CMake package was found. "
        "Install the vcpkg suitesparse package or set SUITESPARSE_INCLUDE_DIR and SUITESPARSE_LIBRARY_DIR.")
    endif()
    find_library(BLOCKDAGLU_KLU_LIBRARY NAMES klu PATHS "${SUITESPARSE_LIBRARY_DIR}" REQUIRED NO_DEFAULT_PATH)
    find_library(BLOCKDAGLU_BTF_LIBRARY NAMES btf PATHS "${SUITESPARSE_LIBRARY_DIR}" REQUIRED NO_DEFAULT_PATH)
    find_library(BLOCKDAGLU_AMD_LIBRARY NAMES amd PATHS "${SUITESPARSE_LIBRARY_DIR}" REQUIRED NO_DEFAULT_PATH)
    find_library(BLOCKDAGLU_COLAMD_LIBRARY NAMES colamd PATHS "${SUITESPARSE_LIBRARY_DIR}" REQUIRED NO_DEFAULT_PATH)
    find_library(BLOCKDAGLU_CONFIG_LIBRARY NAMES suitesparseconfig SuiteSparseConfig PATHS "${SUITESPARSE_LIBRARY_DIR}" REQUIRED NO_DEFAULT_PATH)
    set(BLOCKDAGLU_SUITESPARSE_TARGETS
      ${BLOCKDAGLU_KLU_LIBRARY}
      ${BLOCKDAGLU_BTF_LIBRARY}
      ${BLOCKDAGLU_AMD_LIBRARY}
      ${BLOCKDAGLU_COLAMD_LIBRARY}
      ${BLOCKDAGLU_CONFIG_LIBRARY}
    )
  endif()
else()
  if(NOT SUITESPARSE_INCLUDE_DIR)
    if(EXISTS "${CMAKE_SOURCE_DIR}/../third_party/SuiteSparse/include/suitesparse/klu.h")
      set(SUITESPARSE_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../third_party/SuiteSparse/include")
    endif()
  endif()
  if(NOT EXISTS "${SUITESPARSE_INCLUDE_DIR}/suitesparse/klu.h")
    message(FATAL_ERROR "SUITESPARSE_INCLUDE_DIR is not set or invalid. It must contain suitesparse/klu.h.")
  endif()
endif()

# Tool bundle (headers + import libs) used by the benchmark/dataset loader.
# Only required for the competition-compatible build and local benchmark tools.
#
# For open-source packaging, we vendor a copy under:
#   BlockDAGLU/third_party/SparseMatrixLUTestTool/tool
set(SPARSEMATRIXLUTEST_TOOL_ROOT "" CACHE PATH "Path to SparseMatrixLUTest tool root (contains tool/include)")
if(BLOCKDAGLU_BUILD_COMPAT_SPARSEMATRIXLUTEST OR BLOCKDAGLU_BUILD_SMOKETEST)
  if(NOT SPARSEMATRIXLUTEST_TOOL_ROOT)
    if(EXISTS "${CMAKE_SOURCE_DIR}/../third_party/SparseMatrixLUTestTool/tool/include/ALInterface.h")
      set(SPARSEMATRIXLUTEST_TOOL_ROOT "${CMAKE_SOURCE_DIR}/../third_party/SparseMatrixLUTestTool")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/../SparseMatrixLUTest/SparseMatrixLUTest/tool/include/ALInterface.h")
      set(SPARSEMATRIXLUTEST_TOOL_ROOT "${CMAKE_SOURCE_DIR}/../SparseMatrixLUTest/SparseMatrixLUTest")
    endif()
  endif()

  if(NOT EXISTS "${SPARSEMATRIXLUTEST_TOOL_ROOT}/tool/include/ALInterface.h")
    message(FATAL_ERROR "SPARSEMATRIXLUTEST_TOOL_ROOT is not set or invalid. It must contain tool/include/ALInterface.h. Suggested: ../third_party/SparseMatrixLUTestTool")
  endif()
endif()

set(SPARSEMATRIXLUTEST_LIB_DIR "" CACHE PATH "Path to directory containing SparseMatrixLUTestTool.lib")
if(BLOCKDAGLU_BUILD_COMPAT_SPARSEMATRIXLUTEST)
  if(NOT SPARSEMATRIXLUTEST_LIB_DIR)
    if(EXISTS "${SPARSEMATRIXLUTEST_TOOL_ROOT}/tool/lib/x64/Release/SparseMatrixLUTestTool.lib")
      set(SPARSEMATRIXLUTEST_LIB_DIR "${SPARSEMATRIXLUTEST_TOOL_ROOT}/tool/lib/x64/Release")
    elseif(EXISTS "${SPARSEMATRIXLUTEST_TOOL_ROOT}/tool/lib/x64/Debug/SparseMatrixLUTestTool.lib")
      set(SPARSEMATRIXLUTEST_LIB_DIR "${SPARSEMATRIXLUTEST_TOOL_ROOT}/tool/lib/x64/Debug")
    endif()
  endif()
endif()

# Core library (tool-independent). Still requires SuiteSparse headers for types.
add_library(blockdaglu_core STATIC
  src/solver.cpp
  src/thread_pool.cpp
  src/klu_dynamic.cpp
  src/btf_dynamic.cpp
  src/gp_lu.cpp
  src/ubtf_block_solver.cpp
)

target_include_directories(blockdaglu_core PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/include
)

if(SUITESPARSE_INCLUDE_DIR)
  target_include_directories(blockdaglu_core PRIVATE ${SUITESPARSE_INCLUDE_DIR})
endif()

target_compile_definitions(blockdaglu_core PRIVATE
  BLOCKDAGLU_CORE=1
)

if(BLOCKDAGLU_USE_LINKED_SUITESPARSE)
  target_compile_definitions(blockdaglu_core PRIVATE BLOCKDAGLU_USE_LINKED_SUITESPARSE=1)
  target_link_libraries(blockdaglu_core PUBLIC ${BLOCKDAGLU_SUITESPARSE_TARGETS})
endif()

# Minimal, tool-independent example.
add_executable(blockdaglu_minimal
  apps/minimal/main.cpp
)

target_include_directories(blockdaglu_minimal PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(blockdaglu_minimal PRIVATE
  blockdaglu_core
)

if(BUILD_TESTING)
  add_executable(blockdaglu_tests
    apps/tests/main.cpp
  )
  target_link_libraries(blockdaglu_tests PRIVATE blockdaglu_core)
  add_test(NAME blockdaglu_tests COMMAND blockdaglu_tests)
endif()

if(BLOCKDAGLU_BUILD_COMPAT_SPARSEMATRIXLUTEST)
  if(NOT EXISTS "${SPARSEMATRIXLUTEST_TOOL_ROOT}/tool/include/ALInterface.h")
    message(FATAL_ERROR "SPARSEMATRIXLUTEST_TOOL_ROOT is invalid for compat build. It must contain tool/include/ALInterface.h")
  endif()
  if(NOT EXISTS "${SPARSEMATRIXLUTEST_LIB_DIR}/SparseMatrixLUTestTool.lib")
    message(FATAL_ERROR "SPARSEMATRIXLUTEST_LIB_DIR is not set or invalid for compat build. It must contain SparseMatrixLUTestTool.lib")
  endif()
endif()

if(BLOCKDAGLU_BUILD_COMPAT_SPARSEMATRIXLUTEST)
  # Competition-compatible DLL (ALInterface + CreateAlgorithm exports).
  add_library(blockdaglu_smlt_adapter_obj OBJECT
    src/UserAlgorithmWrapper.cpp
  )

  target_include_directories(blockdaglu_smlt_adapter_obj PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${SPARSEMATRIXLUTEST_TOOL_ROOT}/tool/include
  )

  add_library(blockdaglu_smlt SHARED
    $<TARGET_OBJECTS:blockdaglu_smlt_adapter_obj>
    src/exports.cpp
  )

  target_include_directories(blockdaglu_smlt PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${SPARSEMATRIXLUTEST_TOOL_ROOT}/tool/include
  )

  target_compile_definitions(blockdaglu_smlt PRIVATE
    USERALGO_EXPORTS=1
  )

  target_link_directories(blockdaglu_smlt PRIVATE
    ${SPARSEMATRIXLUTEST_LIB_DIR}
  )

  target_link_libraries(blockdaglu_smlt PRIVATE
    blockdaglu_core
    SparseMatrixLUTestTool.lib
  )

  # Tools / demos (dataset parsing uses the tool bundle).
  if(BLOCKDAGLU_BUILD_SMOKETEST)
    add_executable(blockdaglu_smoketest
      apps/smoketest/main.cpp
      $<TARGET_OBJECTS:blockdaglu_smlt_adapter_obj>
    )

    target_include_directories(blockdaglu_smoketest PRIVATE
      ${CMAKE_CURRENT_LIST_DIR}/include
      ${SPARSEMATRIXLUTEST_TOOL_ROOT}/tool/include
    )

    target_link_directories(blockdaglu_smoketest PRIVATE
      ${SPARSEMATRIXLUTEST_LIB_DIR}
    )

    target_link_libraries(blockdaglu_smoketest PRIVATE
      blockdaglu_core
      SparseMatrixLUTestTool.lib
    )
  endif()

  add_executable(blockdaglu_benchmark
    apps/benchmark/main.cpp
    $<TARGET_OBJECTS:blockdaglu_smlt_adapter_obj>
  )

  target_include_directories(blockdaglu_benchmark PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${SPARSEMATRIXLUTEST_TOOL_ROOT}/tool/include
  )

  target_link_directories(blockdaglu_benchmark PRIVATE
    ${SPARSEMATRIXLUTEST_LIB_DIR}
  )

  target_link_libraries(blockdaglu_benchmark PRIVATE
    blockdaglu_core
    SparseMatrixLUTestTool.lib
  )

  add_executable(blockdaglu_dll_benchmark
    apps/dll_benchmark/main.cpp
  )

  target_include_directories(blockdaglu_dll_benchmark PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${SPARSEMATRIXLUTEST_TOOL_ROOT}/tool/include
  )

  target_link_directories(blockdaglu_dll_benchmark PRIVATE
    ${SPARSEMATRIXLUTEST_LIB_DIR}
  )

  target_link_libraries(blockdaglu_dll_benchmark PRIVATE
    SparseMatrixLUTestTool.lib
  )

  add_executable(blockdaglu_lucheck
    apps/lucheck/main.cpp
    $<TARGET_OBJECTS:blockdaglu_smlt_adapter_obj>
  )

  target_include_directories(blockdaglu_lucheck PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${SPARSEMATRIXLUTEST_TOOL_ROOT}/tool/include
  )

  target_link_directories(blockdaglu_lucheck PRIVATE
    ${SPARSEMATRIXLUTEST_LIB_DIR}
  )

  target_link_libraries(blockdaglu_lucheck PRIVATE
    blockdaglu_core
    SparseMatrixLUTestTool.lib
  )

  add_executable(blockdaglu_commoncheck
    apps/commoncheck/main.cpp
  )

  target_include_directories(blockdaglu_commoncheck PRIVATE
    ${SPARSEMATRIXLUTEST_TOOL_ROOT}/tool/include
  )

  target_link_directories(blockdaglu_commoncheck PRIVATE
    ${SPARSEMATRIXLUTEST_LIB_DIR}
  )

  target_link_libraries(blockdaglu_commoncheck PRIVATE
    SparseMatrixLUTestTool.lib
  )
endif()
