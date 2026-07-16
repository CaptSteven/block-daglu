# 贡献指南（CONTRIBUTING）

欢迎为 BlockDAGLU 提交 Issue / PR。为了让讨论与合并更高效，请参考以下约定。

## 1. 仓库结构约定

- `blockdaglu/`：库本体与可执行工具（CMake 工程在此目录）
- `data/`：数据安装说明；Final15 不进入公开仓库
- `docs/`：架构、构建、配置、数据和技术报告
- `showcase/`：静态展示页和公开展示素材
- `third_party/`：本地依赖安装位置，默认被 Git 忽略

## 2. 开发环境

当前主要验证环境：
- Windows 10/11
- Visual Studio 2022（或 Build Tools）+ CMake

## 3. 提交内容建议

### Bug / Correctness
- 给出最小复现：用例路径、环境变量、命令行
- 如果涉及数值稳定性，请同时提供：
  - `rel_residual` 与 `rel_err` 的对比（SuiteSparse vs BlockDAGLU）
  - 是否触发了回退/精炼（如相关开关已开启）

### 性能优化
- 请用 `blockdaglu_benchmark` 的输出文件（`bench/out/.../summary.txt`）提供加权总耗时与 speedup
- 如涉及线程/调度，请补充：机器信息（CPU 核数/频率/内存）与线程数设置

## 4. 编码约定（简要）

- C++17
- 优先保持可读性与可调试性（性能关键路径例外）
- 避免引入新的“强绑定”依赖；对外 API 以 `blockdaglu::Solver` 为中心

## 5. 私有数据与大文件

不要提交以下内容：

- `data/FinalTestCases/**`
- `third_party/SparseMatrixLUTestTool/**`
- 竞赛原始材料、访问令牌、私有下载地址和未知二进制文件

需要复现完整基准时，请使用带 SHA-256 校验的安装脚本，并只提交脱敏后的结果文件。
