cmake_minimum_required(VERSION 3.20)

# NOTE: This repository started from a competition implementation. We keep the
# original target names for now, but the external-facing project name is BlockDAGLU.
project(BlockDAGLU LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BLOCKDAGLU_BUILD_COMPAT_SPARSEMATRIXLUTEST "Build competition-compatible adapter + benchmark tools" OFF)
option(BLOCKDAGLU_BUILD_SMOKETEST "Build local smoketest executable (requires tool bundle)" OFF)

# SuiteSparse headers (needed by the core library).
# Prefer a clean dependency, independent from the competition tool bundle.
set(SUITESPARSE_INCLUDE_DIR "" CACHE PATH "Path to SuiteSparse headers root (contains suitesparse/klu.h)")
if(NOT SUITESPARSE_INCLUDE_DIR)
  if(EXISTS "${CMAKE_SOURCE_DIR}/../third_party/SuiteSparse/include/suitesparse/klu.h")
    set(SUITESPARSE_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../third_party/SuiteSparse/include")
  endif()
endif()
if(NOT EXISTS "${SUITESPARSE_INCLUDE_DIR}/suitesparse/klu.h")
  message(FATAL_ERROR "SUITESPARSE_INCLUDE_DIR is not set or invalid. It must contain suitesparse/klu.h. Suggested: ../third_party/SuiteSparse/include")
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
  ${SUITESPARSE_INCLUDE_DIR}
)

target_compile_definitions(blockdaglu_core PRIVATE
  BLOCKDAGLU_CORE=1
)

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
