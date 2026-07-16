# 构建与运行（Windows / CMake）

本仓库当前以 Windows + MSVC 为主要验证环境，使用 CMake 构建。

## 1) 构建

在仓库根目录执行：

```powershell
cmake -S .\\blockdaglu -B .\\build\\blockdaglu -DCMAKE_BUILD_TYPE=Release
cmake --build .\\build\\blockdaglu --config Release -j
```

说明：默认配置下，CMake 只会构建 **核心库**（`blockdaglu_core`）与最小示例（`blockdaglu_minimal`），不依赖竞赛工具链。

### 构建完整工具链（包含 benchmark / 竞赛适配 DLL）

你可以显式打开竞赛适配与本地基准工具：

```powershell
cmake -S .\\blockdaglu -B .\\build\\blockdaglu -DCMAKE_BUILD_TYPE=Release `
  -DBLOCKDAGLU_BUILD_COMPAT_SPARSEMATRIXLUTEST=ON `
  -DBLOCKDAGLU_BUILD_SMOKETEST=ON
cmake --build .\\build\\blockdaglu --config Release -j
```

也可以使用脚本一键构建（默认会构建完整工具链）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\\scripts\\build.ps1 -Config Release
```

如果你只希望构建核心库（不需要竞赛工具链/基准工具），可以：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\\scripts\\build.ps1 -Config Release -NoToolchain
```

产物（示例）：

- core-only：
  - `build\\blockdaglu\\Release\\blockdaglu_core.lib`
  - `build\\blockdaglu\\Release\\blockdaglu_minimal.exe`
- 完整工具链（打开 `BLOCKDAGLU_BUILD_COMPAT_SPARSEMATRIXLUTEST` 后）：
  - `build\\blockdaglu\\Release\\blockdaglu_smlt.dll`（竞赛接口适配 DLL：CreateAlgorithm/DestroyAlgorithm）
  - 以及若干本地工具（smoketest/benchmark 等）

常用可执行文件：

- `blockdaglu_benchmark.exe`：读取 `data/FinalTestCases`，对比 SuiteSparse vs BlockDAGLU 并输出 CSV
- `blockdaglu_minimal.exe`：不依赖竞赛工具链的最小示例（直接用 C++ API 构建 COO 并求解）

## 2) 运行（需要依赖 DLL 在 PATH 中）

BlockDAGLU 的默认后端在该实现中采用动态加载方式；运行时需要将对应 DLL 放入 `PATH`。

本仓库已随 `third_party/` 一起打包运行所需 DLL：

- `third_party/SparseMatrixLUTestTool/tool/lib/x64/Release/SparseMatrixLUTestTool.dll`
- `third_party/SuiteSparse/bin/x64/Release/klu.dll`
- `third_party/SuiteSparse/bin/x64/Release/btf.dll`
- `third_party/SuiteSparse/bin/x64/Release/amd.dll`
- `third_party/SuiteSparse/bin/x64/Release/colamd.dll`
- `third_party/SuiteSparse/bin/x64/Release/suitesparseconfig.dll`

你可以直接使用脚本一键跑基准：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\\scripts\\run_benchmark.ps1
```

## 2.1) 作为依赖集成到你的 CMake 工程（core-only）

如果你希望把 BlockDAGLU 当作一个“库”集成进自己的工程，建议使用 core-only 配置（不依赖竞赛工具链）：

```cmake
add_subdirectory(path/to/BlockDAGLU/blockdaglu blockdaglu_build)
target_link_libraries(your_target PRIVATE blockdaglu_core)
```

对应的 CMake 配置参数：

- `-DBLOCKDAGLU_BUILD_COMPAT_SPARSEMATRIXLUTEST=OFF`
- `-DBLOCKDAGLU_BUILD_SMOKETEST=OFF`

脚本会将结果写入：

- `bench/out/<timestamp>/runs.csv`（逐算法逐用例耗时/误差）
- `bench/out/<timestamp>/margin.csv`（逐用例 ε 对齐校验与 margin）
- `bench/out/<timestamp>/summary.txt`（加权总耗时与 speedup）

或手动将目录加入 PATH：

```powershell
$env:PATH = \".\\third_party\\SparseMatrixLUTestTool\\tool\\lib\\x64\\Release;.\\third_party\\SuiteSparse\\bin\\x64\\Release;$env:PATH\"
```

## 3) 数据集路径

公开测试用例数据位于：

- `data\\FinalTestCases\\1` … `data\\FinalTestCases\\15`

说明见：`data\\README.md`。
