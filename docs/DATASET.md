# 数据集发布与 Git LFS

本仓库包含 `data/FinalTestCases/`（竞赛测试用例）。其中部分文件体积超过 100MB（例如 `*_JacobiMatrixInfo.txt`），无法以普通 git 对象上传到 GitHub。

因此，建议使用 **Git LFS** 发布数据集。

## 1) 安装与初始化（发布者/贡献者）

```powershell
git lfs install
```

本仓库已提供 `.gitattributes`，默认将以下内容走 LFS：

- `BlockDAGLU/.gitattributes`

其中包含：

- `data/**`（数据集）
- `contest/**`（比赛资料备份，可选）

## 2) 克隆（使用者）

```powershell
git lfs install
git clone <your_repo_url>
git lfs pull
```

如果未安装 Git LFS，克隆后会看到数据文件占位符（文本指针），运行基准会失败。
