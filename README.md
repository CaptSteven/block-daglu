# BlockDAGLU

BlockDAGLU 是一套面向稀疏线性方程组 `Ax=b` 的工程化求解框架：通过 **DM/BTF → UBTF → Block-DAG** 把稀疏结构显式化，并用 **work-stealing** 调度并行块内 LU；同时提供稳健回退与可插拔后端边界。

## 本仓库提供什么

- 稀疏求解框架：结构分解、块构建、Block-DAG 调度、稳健回退、可插拔后端接口
- 可复现实验：基准脚本 + 图表生成脚本（复现口径写清楚）
- 可公开数据集（竞赛矩阵）与复现命令（用于复现实验图/表）

## 核心思路（1 分钟读懂）

- **DM/BTF → UBTF**：只看 pattern 的结构分解，把矩阵置换成“对角块 + 跨块上三角”的形式
- **Block-DAG 调度**：把对角块视为任务（task），按依赖就绪执行；用 work-stealing 缓解负载不均与长尾
- **AUTO 决策**：基于可观测画像特征选择路线（单体/分块）与线程甜点区（避免“线程越多越慢”）
- **稳健性**：对病态/近奇异信号做检测与回退，保证满足精度门槛

## 配置方式（环境变量）

为了兼容历史脚本，本项目同时支持两套环境变量前缀：

- 推荐：`BLOCKDAGLU_*`
- 兼容：`USERALGO_*`

当同时设置时，`BLOCKDAGLU_*` 优先生效。

## 快速开始（占位）

前置：Windows + Visual Studio 2022（或 Build Tools）+ CMake 3.20+。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\\scripts\\build.ps1 -Config Release
powershell -NoProfile -ExecutionPolicy Bypass -File .\\scripts\\run_benchmark.ps1
```

`run_benchmark.ps1` 会把结果写入 `bench/out/<timestamp>/`（并在控制台打印 `summary.txt`）。

如果你只想构建“库本体”（不需要竞赛工具链/基准工具），可以：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\\scripts\\build.ps1 -Config Release -NoToolchain
```

如果你只想集成对外 C++ API（不需要竞赛工具链/基准工具），可以只构建核心库：

```powershell
cmake -S .\\blockdaglu -B .\\build\\blockdaglu -DCMAKE_BUILD_TYPE=Release `
  -DBLOCKDAGLU_BUILD_COMPAT_SPARSEMATRIXLUTEST=OFF `
  -DBLOCKDAGLU_BUILD_SMOKETEST=OFF
cmake --build .\\build\\blockdaglu --config Release -j
```

更详细说明见：`docs/BUILD.md`。

## 对外 API（通用接口）

核心算法已从“竞赛接口（ALInterface）”中解耦，作为独立的 C++ 类对外提供：

- 头文件：`blockdaglu/include/blockdaglu/solver.h`
- 示例程序：`blockdaglu/apps/minimal/main.cpp`（构建目标：`blockdaglu_minimal`）

最小用法示意：

```cpp
#include "blockdaglu/solver.h"
blockdaglu::Solver s;
blockdaglu::CooMatrix A = { {0,0,4.0}, {0,1,1.0}, {1,0,1.0}, {1,1,3.0}, {1,2,1.0}, {2,1,1.0}, {2,2,2.0} };
std::vector<double> b = {1.0, 2.0, 3.0};
s.set_matrix(std::move(A));
s.analyze();
s.factor();
s.solve_in_place(b); // b 被覆盖为解 x
```

竞赛接口适配仍保留在：

- `blockdaglu/include/UserAlgorithmWrapper.h`（仅用于兼容竞赛工具链）

## 可复现与数据

- 基准脚本：`bench/`
- 画图脚本：`scripts/`
- 数据集目录：`data/FinalTestCases/`（1–15 用例，保留原始文件名与目录结构）
- 数据发布：由于部分文件 >100MB，发布到 GitHub 需使用 Git LFS（见 `docs/DATASET.md`）

## 代码位置

当前版本的核心实现代码已导入到：

- `blockdaglu/`（原参赛实现目录的对外仓库化迁移；后续会继续把“竞赛接口/工具链”与“通用库接口”解耦）

## 比赛资料（可选）

为了方便外部读者理解项目来源，本仓库提供了一个**不参与构建**的比赛资料备份目录：

- `contest/`（比赛工具/提交压缩包/技术报告/项目报告等，仅供参考，发布前请确认再分发许可）

## 许可证

MIT，见 `LICENSE`。

## 第三方组件与许可

见 `third_party/THIRD_PARTY_NOTICES.md`。

## 发布清单（给维护者）

见 `RELEASE_CHECKLIST.md`。
