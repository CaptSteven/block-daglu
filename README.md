# BlockDAGLU

> 稀疏线性方程组求解框架：把矩阵结构转成 Block-DAG，在全局 KLU 与块级并行路线间选择。

**技术栈：** C++ · SuiteSparse / KLU · CMake · Windows / MSVC

**使用边界：** 历史 1.99× 是修复前单轮结果，不代表当前版本的正式复现。

---

BlockDAGLU 是面向稀疏线性方程组 `Ax=b` 的工程化求解框架。它通过 **DM/BTF → UBTF → Block-DAG** 显式化稀疏结构，并由 AUTO 在全局 KLU 与块级并行路线之间选择。

## 主要能力

- 完整 COO 输入验证、重复项合并和可诊断错误状态
- 全局 KLU 与 UBTF/BLOCK 求解路线
- 有界、线程安全、配置隔离的 symbolic/decomposition 缓存
- BLOCK 数值失败时的 AUTO 全局 KLU 回退
- 与竞赛 `ALInterface` 兼容的 P/Q/A/L/U 懒加载导出
- Windows/MSVC CI、数值测试和可审计基准产物

## 快速构建

前置环境：Visual Studio 2022、CMake 3.20+、Git 和 vcpkg。

```powershell
git clone --branch 2024.05.24 https://github.com/microsoft/vcpkg.git .\vcpkg
.\vcpkg\bootstrap-vcpkg.bat -disableMetrics
.\vcpkg\vcpkg.exe install suitesparse:x64-windows
$env:VCPKG_ROOT = "$PWD\vcpkg"

.\scripts\build.ps1 -Config Release
ctest --test-dir .\build\blockdaglu -C Release --output-on-failure
```

公开构建默认只生成核心库、最小示例和测试，不需要私有竞赛工具链。原竞赛 DLL 加载模式仍可通过 `-DBLOCKDAGLU_USE_LINKED_SUITESPARSE=OFF` 使用。

## C++ API

```cpp
#include "blockdaglu/solver.h"

blockdaglu::Solver solver;
blockdaglu::CooMatrix matrix = {
  {0, 0, 4.0}, {0, 1, 1.0},
  {1, 0, 1.0}, {1, 1, 3.0}, {1, 2, 1.0},
  {2, 1, 1.0}, {2, 2, 2.0},
};
std::vector<double> rhs{1.0, 2.0, 3.0};

if (!solver.set_matrix(std::move(matrix), 3) ||
    !solver.analyze() ||
    !solver.factor() ||
    !solver.solve_in_place(rhs)) {
  throw std::runtime_error(solver.last_error_message());
}
```

旧的 `set_matrix(CooMatrix)` 保持兼容并从最大索引推断维度。新重载允许显式维度，从而表示尾部为空的行或列。矩阵扫描与规范化发生在 `analyze()`，保持竞赛 `prepareData()` 的计时规则。

## Final15 数据与基准

Final15 和竞赛工具链不进入公开仓库。维护者可通过私有 URL 或已有压缩包安装：

```powershell
.\scripts\install_final15.ps1 `
  -ArchivePath C:\secure\FinalTestCases.zip `
  -Sha256 <sha256>

.\scripts\install_competition_toolchain.ps1 `
  -ArchivePath C:\secure\SparseMatrixLUTestTool.zip `
  -Sha256 <sha256>

.\scripts\build.ps1 -CompetitionToolchain
.\scripts\run_benchmark.ps1 -Warmup 1 -Repetitions 10
```

基准输出位于 `bench/out/<timestamp>/`，包含逐次运行、median/P95、精度 margin、环境元数据和汇总结果。任何求解失败或 `err_user > ε` 都会使命令返回非零状态。

展示页中的 `1.99x` 是修复前的历史单轮结果，不代表当前公开版本的正式复现结论。正式数字将在私有 Final15 工作流复跑后更新。

## 文档

- 架构说明：`docs/ARCHITECTURE.md`
- 构建说明：`docs/BUILD.md`
- 配置说明：`docs/CONFIGURATION.md`
- 数据与私有 CI：`docs/DATASET.md`
- 作品技术报告：`docs/REPORT.md`

## 许可证

自有代码采用 MIT 许可证。SuiteSparse 通过包管理器获取并遵循其各组件许可证。竞赛工具和 Final15 数据由使用者按其原始授权单独取得。

<!-- 更新备注：YANG KAISEN，2026-11-11；改写与核对依据见工作区变更记录。 -->
