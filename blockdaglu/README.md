# BlockDAGLU 核心代码目录

本目录包含 BlockDAGLU 的核心实现与若干工具程序。

## 目录结构

- `include/blockdaglu/`：对外 C++ API（核心入口：`blockdaglu/solver.h`）
- `src/`：核心实现（DM/BTF→UBTF、Block-DAG 调度、线程池、数值后端动态加载等）
- `include/UserAlgorithmWrapper.h` + `src/UserAlgorithmWrapper.cpp`：竞赛接口（ALInterface）兼容适配层（仅用于兼容工具链）
- `apps/`：可执行程序
  - `minimal/`：最小示例（不依赖竞赛工具链）
  - `benchmark/`：读取竞赛数据集做对比/输出 CSV（依赖竞赛工具链）
  - `tests/`：输入验证、缓存、并发、数值求解和因子导出回归测试

核心 API 保持 `set_matrix → analyze → factor → solve_in_place` 流程，并提供 `SolverError`、`last_error_message()`、显式维度重载及一致的 P/Q/A/L/U 兼容导出。

## 构建

构建与运行说明见仓库根目录：

- `../docs/BUILD.md`
