# 架构概览（对外版本）

本文档用“外部读者可读”的方式描述 BlockDAGLU 的整体设计与核心机制。

## 总体流程

输入（A,b）
  → 画像（profile / features）
  → 路由（AUTO）
    - 路线 A：单体稀疏 LU（稳健路径，适合小规模或结构不利于分块的输入）
    - 路线 B：BLOCK
        DM/BTF → UBTF（块上三角结构）
        → 构建块元数据与依赖
        → 分解阶段：对角块 factor（并行）
        → 求解阶段：按跨块依赖回代与跨块更新

## 并行来自哪里

- **块级并行**：UBTF 结构中的对角块在数值分解阶段天然可并行（前驱依赖满足后即可执行）。
- **调度与长尾抑制**：采用 work-stealing 线程池在运行期做负载均衡，缓解“少数大块主导”的尾部拖慢。

## 稳健性（病态/近奇异）

- 把病态/近奇异作为工程一等公民处理：通过轻量信号（返回码、NaN/Inf、条件诊断信号等）识别风险。
- 输入在 `analyze()` 内完成边界检查、有限值检查、重复项合并和结构规范化。
- BLOCK 任一对角块 factor 失败时，AUTO 立即释放块状态并回退全局 KLU；强制 BLOCK 模式返回明确错误。
- `SolverError` 与 `last_error_message()` 为调用方提供稳定诊断，不再只返回无上下文的 `false`。

## 缓存与并发

- symbolic/decomposition 缓存使用完整规范化 sparsity pattern 的双 64 位签名。
- 缓存键包含影响结构分解的环境配置，避免不同 BTF、drop、merge 或调度策略相互污染。
- 全局缓存有界并使用互斥保护；线程池任务异常会回到调用线程，不允许直接终止进程。

## 可插拔后端

- 框架将“前端结构分解/调度”与“后端数值核（LU kernel）”解耦。
- 公开构建通过 CMake/vcpkg 链接 SuiteSparse；旧 DLL 动态加载只作为原竞赛环境兼容模式。
- 竞赛 `getP/getQ/getRearrangedMatrix/getL/getU` 使用独立的懒加载 KLU 导出状态，确保五个结果来自同一分解且不影响 BLOCK 求解计时。
