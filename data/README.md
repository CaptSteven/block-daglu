# 数据集说明

本目录包含可公开的竞赛测试用例数据，目录结构保持与原始工具链一致：

- `FinalTestCases/1` … `FinalTestCases/15`

每个用例目录下通常包含：

- `*_JacobiMatrixInfo.txt`：稀疏矩阵/结构信息（体积可能较大）
- `*_VarInfo.csv`：变量信息
- `*_PerturbVarValue.csv`：扰动/输入相关数据
- `*_ResidualValue.csv`：残差信息（用于对齐/诊断）

注意：
- 这些文件格式以原始评测工具为准；本仓库后续会在 `examples/` 与 `bench/` 提供读取与复现实验脚本。

