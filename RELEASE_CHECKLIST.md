# BlockDAGLU 开源发布清单（Release Checklist）

这个清单用于把仓库从“能跑”提升到“对外可用、可维护、可再发布”。

## A. 法务/合规（发布前必须确认）

1) 许可
- `LICENSE` 为 MIT（库本体）。
- 确认 README/文档中不出现与 MIT 冲突的表述。

2) 第三方组件再分发
- 核对 `third_party/THIRD_PARTY_NOTICES.md` 中列出的组件、版本/来源、许可文本是否齐全。
- 若发布二进制（Release assets）：
  - 必须把 `third_party/licenses/` 中的许可文本与 notices 一并附带。
  - 公开仓库不提交预编译 SuiteSparse DLL；二进制发布物需附 vcpkg 安装的组件许可证。

3) 数据集与比赛资料
- 确认公开提交不包含 `data/FinalTestCases/`、`contest/` 或 `SparseMatrixLUTestTool`。
- 私有数据和工具必须通过 URL/本地压缩包 + SHA-256 安装脚本部署。

## B. 版本与发布物（建议）

- 版本号：建议从 `v0.1.0` 起（语义化版本）。
- Release Note（建议包含）：
  - 本版本的主要特性/修复点
  - 已验证的平台与编译器（Windows/MSVC 版本）
  - 复现实验口径（weighted_total_ms 的定义）

## C. 构建验证（发布前必须跑）

1) 仅核心库（不需要竞赛工具链）
```powershell
cmake -S .\blockdaglu -B .\build\blockdaglu_core -DCMAKE_BUILD_TYPE=Release `
  -DBLOCKDAGLU_BUILD_COMPAT_SPARSEMATRIXLUTEST=OFF `
  -DBLOCKDAGLU_BUILD_SMOKETEST=OFF
cmake --build .\build\blockdaglu_core --config Release -j
```

2) 完整构建（包含 benchmark 工具）
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Config Release
```

3) 最小示例运行（链接模式由 vcpkg 复制运行依赖）
```powershell
$env:PATH=".\third_party\SuiteSparse\bin\x64\Release;$env:PATH"
.\build\blockdaglu\Release\blockdaglu_minimal.exe
```

4) 基准跑通（建议先跑单用例）
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\run_benchmark.ps1 -CaseId 1
```
确认输出目录 `bench/out/<timestamp>/` 下：
- `runs.csv`
- `margin.csv`
- `summary.txt`

## D. 文档检查（发布前建议）

- README（外部读者 3 分钟能跑通）
  - 快速开始
  - “只构建核心库”的路径
  - vcpkg/SuiteSparse 固定依赖说明
  - 私有数据与竞赛工具安装说明
- `docs/ARCHITECTURE.md`（结构/模块图、核心术语）
- `docs/BUILD.md`（构建与运行）
- `docs/DATASET.md`（数据集发布）

## E. CI（必须）

Windows CI 覆盖：
- 公开 core-only build、CTest、最小示例和展示资源检查
- 维护者手动触发的私有 Final15 重复基准与 artifact 上传
