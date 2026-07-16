# 数据目录

公开仓库不包含 Final15 原始数据。

使用以下命令安装经过 SHA-256 校验的私有数据包：

```powershell
.\scripts\install_final15.ps1 -ArchivePath <FinalTestCases.zip> -Sha256 <sha256>
```

安装成功后生成 `data/FinalTestCases/1` 至 `data/FinalTestCases/15`。该目录已被 `.gitignore` 排除。

完整授权、Secrets 和结果发布规则见 `docs/DATASET.md`。
