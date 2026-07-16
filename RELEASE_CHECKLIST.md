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
  - 明确标注哪些 DLL 来自 SuiteSparse（以及其依赖的 AMD/COLAMD/SuiteSparse_config 等）。

3) 数据集与比赛资料
- `data/`、`contest/` 是否允许对外公开？（尤其是 `contest/` 里比赛主办方提供的材料）
- 如不允许：从发行版中剔除（或改为“下载脚本 + 私有链接/仅指针”）。

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

3) 最小示例运行（需要 SuiteSparse DLL 在 PATH）
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
  - 依赖 DLL 说明（SuiteSparse 运行时依赖）
  - Git LFS（数据/contest 大文件）
- `docs/ARCHITECTURE.md`（结构/模块图、核心术语）
- `docs/BUILD.md`（构建与运行）
- `docs/DATASET.md`（数据集发布）

## E. CI（建议）

Windows CI 至少覆盖：
- core-only build
- tool build（至少编译 benchmark，不跑大数据）

