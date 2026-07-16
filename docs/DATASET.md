# Final15 数据管理

Final15 数据不进入公开 Git 仓库。公开仓库只保存安装、校验、测试和基准逻辑。

## 安装方式

使用已有压缩包：

```powershell
.\scripts\install_final15.ps1 -ArchivePath <FinalTestCases.zip> -Sha256 <sha256>
```

使用私有下载地址：

```powershell
$env:BLOCKDAGLU_DATASET_URL = "<private-url>"
$env:BLOCKDAGLU_DATASET_TOKEN = "<optional-token>"
$env:BLOCKDAGLU_DATASET_SHA256 = "<sha256>"
.\scripts\install_final15.ps1
```

脚本会验证 SHA-256、`1~15` 目录以及每个 case 的四个必需文件。目标目录已存在时默认拒绝覆盖；维护者必须显式使用 `-Force`。

## GitHub Secrets

私有完整基准工作流使用：

- `BLOCKDAGLU_DATASET_URL`
- `BLOCKDAGLU_DATASET_TOKEN`
- `BLOCKDAGLU_DATASET_SHA256`
- `BLOCKDAGLU_TOOLCHAIN_URL`
- `BLOCKDAGLU_TOOLCHAIN_TOKEN`
- `BLOCKDAGLU_TOOLCHAIN_SHA256`

Secrets 不会提供给外部 fork，因此普通 PR CI 只运行公开合成测试。

## 结果发布规则

每次正式复跑必须保存：

- `runs.csv`
- `timing_summary.csv`
- `margin.csv`
- `summary.txt`
- `metadata.json`

展示页只能引用带 commit、数据校验和、硬件信息和重复次数的结果。修复前的 `1.99x` 仅标记为历史结果。
