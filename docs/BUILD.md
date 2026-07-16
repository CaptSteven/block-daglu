# 构建与验证

## 公开核心构建

推荐 Windows 11、Visual Studio 2022、CMake 3.20+ 和固定版本 vcpkg：

```powershell
git clone --branch 2024.05.24 https://github.com/microsoft/vcpkg.git .\vcpkg
.\vcpkg\bootstrap-vcpkg.bat -disableMetrics
.\vcpkg\vcpkg.exe install suitesparse:x64-windows
$env:VCPKG_ROOT = "$PWD\vcpkg"

.\scripts\build.ps1 -Config Release
ctest --test-dir .\build\blockdaglu -C Release --output-on-failure
.\build\blockdaglu\Release\blockdaglu_minimal.exe
```

默认构建目标：

- `blockdaglu_core`
- `blockdaglu_minimal`
- `blockdaglu_tests`

`BLOCKDAGLU_USE_LINKED_SUITESPARSE=ON` 是公开构建默认值。它通过 CMake package 或显式 include/library 路径链接 SuiteSparse，不从当前工作目录搜索 DLL。

## 旧评测环境兼容

原竞赛环境可继续使用动态 DLL 模式：

```powershell
cmake -S .\blockdaglu -B .\build\legacy `
  -DBLOCKDAGLU_USE_LINKED_SUITESPARSE=OFF `
  -DSUITESPARSE_INCLUDE_DIR=.\third_party\SuiteSparse\include
```

此模式仅用于受控的原评测包，不推荐公开部署。

## 竞赛工具与完整基准

竞赛适配需要私有 `SparseMatrixLUTestTool`：

```powershell
.\scripts\install_competition_toolchain.ps1 -ArchivePath <zip> -Sha256 <sha256>
.\scripts\install_final15.ps1 -ArchivePath <zip> -Sha256 <sha256>
.\scripts\build.ps1 -CompetitionToolchain
.\scripts\run_benchmark.ps1 -Warmup 1 -Repetitions 10
```

也可显式配置：

```powershell
cmake -S .\blockdaglu -B .\build\blockdaglu `
  -DBLOCKDAGLU_BUILD_COMPAT_SPARSEMATRIXLUTEST=ON `
  -DBLOCKDAGLU_BUILD_SMOKETEST=ON
```

PowerShell 脚本会检查所有原生命令的退出码，不会在 CMake 或基准失败后继续报告成功。

## CI

- `windows.yml`：公开 PR/Push 构建、CTest、最小示例和展示页资源检查。
- `final15-benchmark.yml`：维护者手动触发，通过 GitHub Secrets 安装私有工具和 Final15，上传完整基准证据。
