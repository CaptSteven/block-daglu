const innovations = [
  {
    title: "结构分解",
    body:
      "通过 DM/BTF 把原始稀疏矩阵置换成 UBTF 结构，将问题从“一个大系统”变成“多个对角块 + 跨块依赖”，从结构上限制 fill-in 传播。",
  },
  {
    title: "并行调度",
    body:
      "把对角块映射到 Block-DAG 后，使用线程池和 work-stealing 执行并行 factor，并优先关注关键路径和长尾块，而不是盲目拉高线程数。",
  },
  {
    title: "AUTO 决策",
    body:
      "项目不会对 case 编号硬编码，而是根据规模、NNZ、块结构、长尾风险等画像特征，在全局 KLU 与 BLOCK 路线间做自适应选择。",
  },
  {
    title: "稳健回退",
    body:
      "将 rcond、pivot growth、NaN/Inf、残差等信号接入求解链路，当风险升高时触发更保守的参数或回退路径，优先保证 err_user ≤ ε。",
  },
];

const chapters = [
  {
    index: "01",
    title: "需求分析",
    body:
      "围绕应用场景、核心痛点、目标用户、竞品对标和性能指标展开，先把“为什么要做”讲清楚。",
  },
  {
    index: "02",
    title: "概要设计",
    body:
      "将作品拆成求解引擎、展示界面、数据流、模块层次和部署形态，建立全局结构视角。",
  },
  {
    index: "03",
    title: "详细设计",
    body:
      "落到界面、接口、DM/BTF、Block-DAG、AUTO、缓存和稳健回退等关键实现细节。",
  },
  {
    index: "04",
    title: "测试报告",
    body:
      "覆盖功能、精度、性能、Final15 分 case 结果、线程甜点区和 P/E 核亲和性实验。",
  },
  {
    index: "05",
    title: "安装及使用",
    body:
      "整理环境要求、构建脚本、目录结构、典型流程和常见问题，让作品更像可交付软件。",
  },
  {
    index: "06",
    title: "项目总结",
    body:
      "总结项目推进过程、团队收获、作品价值、局限与未来演进方向，形成完整闭环。",
  },
];

const codeModules = [
  {
    title: "Solver 总控",
    body:
      "统一承接 set_matrix / analyze / factor / solve_in_place，并在 analyze 阶段执行 AUTO 选路和线程策略。",
    href: "../blockdaglu/include/blockdaglu/solver.h",
    path: "blockdaglu/include/blockdaglu/solver.h",
  },
  {
    title: "AUTO 与缓存逻辑",
    body:
      "矩阵签名、AUTO 路由、线程上限、KLU 参数、decomposition 缓存等工程策略都在这里落地。",
    href: "../blockdaglu/src/solver.cpp",
    path: "blockdaglu/src/solver.cpp",
  },
  {
    title: "UBTF / Block 求解器",
    body:
      "定义 DM/BTF 之后的分块结构、Block-DAG 依赖、块内因子、并行 factor 与 solve 的接口。",
    href: "../blockdaglu/src/ubtf_block_solver.h",
    path: "blockdaglu/src/ubtf_block_solver.h",
  },
  {
    title: "线程池",
    body:
      "最小 work-stealing 线程池，支持本地 LIFO、窃取 FIFO 和 chunked parallel_for，是并行 factor 的直接基础设施。",
    href: "../blockdaglu/src/thread_pool.h",
    path: "blockdaglu/src/thread_pool.h",
  },
];

const deliveries = [
  {
    kind: "site",
    title: "静态展示站",
    body:
      "面向答辩和演示的前端首页，汇总项目故事线、技术路线、结果区和材料入口。",
    href: "./index.html",
  },
  {
    kind: "md",
    title: "正式版 Markdown",
    body:
      "适合继续改正文、补图补表和协作修稿，是整套作品文档最灵活的源文件。",
    href: "./中国大学生计算机设计大赛_软件开发类作品文档_正式版.md",
  },
  {
    kind: "docx",
    title: "正式版 DOCX",
    body:
      "适合老师批注、格式微调和最终交稿前的常规 Word 流程。",
    href: "./中国大学生计算机设计大赛_软件开发类作品文档_正式版.docx",
  },
  {
    kind: "tex",
    title: "正式版 LaTeX",
    body:
      "适合精细排版、统一题注和稳定输出 PDF，是长稿排版版的核心源文件。",
    href: "./中国大学生计算机设计大赛_软件开发类作品文档_正式版.tex",
  },
  {
    kind: "pdf",
    title: "正式版 PDF",
    body:
      "当前可直接用于提交和打印的长稿版本，已经与扩写后的内容同步。",
    href: "./中国大学生计算机设计大赛_软件开发类作品文档_正式版.pdf",
  },
  {
    kind: "code",
    title: "核心求解器入口",
    body:
      "从展示回到实现层的最短路径，便于评委或开发者继续追踪算法落点。",
    href: "../blockdaglu/include/blockdaglu/solver.h",
  },
];

const materials = [
  {
    kind: "pdf",
    title: "技术报告",
    body:
      "完整说明问题背景、评价口径、DM/BTF、Block-DAG、AUTO 策略、实验结果与结论，是项目最完整的文字版本。",
    href: "../contest/Rerank_技术报告.pdf",
  },
  {
    kind: "md",
    title: "路演讲稿",
    body:
      "26 页答辩/路演版讲稿，把故事线、页面结构、每页讲什么都提前写好了，特别适合二次制作展示页或 PPT。",
    href: "../contest/项目报告/路演PPT.md",
  },
  {
    kind: "md",
    title: "答辩押题",
    body:
      "把“是不是只是换库”“为什么线程不是越多越好”“加速主要来自哪一段”等常见问题都准备了回答。",
    href: "../contest/项目报告/押题.md",
  },
  {
    kind: "md",
    title: "README 总览",
    body:
      "仓库级视角的项目介绍，概括了 BlockDAGLU 的定位、快速开始、目录结构和对外 API。",
    href: "../README.md",
  },
  {
    kind: "md",
    title: "架构文档",
    body:
      "用对外可读的方式总结总体流程：输入 → 画像 → AUTO → KLU / BLOCK → factor / solve。",
    href: "../docs/ARCHITECTURE.md",
  },
  {
    kind: "code",
    title: "核心源码入口",
    body:
      "如果要从实现角度往下读，这个入口最直接：Solver、分块求解器、线程池和竞赛兼容层都在 repo 内可对上。",
    href: "../blockdaglu/README.md",
  },
];

const gallery = [
  {
    title: "作品总览图",
    body:
      "把问题背景、AUTO、KLU、BLOCK、测试与交付关系放在一张图里，适合做全局介绍。",
    src: "./assets/rerank_overview.svg",
  },
  {
    title: "AUTO 选路示意",
    body:
      "把输入画像、策略判断和最终路线选择可视化，特别适合解释为什么不是所有 case 都强推 BLOCK。",
    src: "./assets/auto_strategy.svg",
  },
  {
    title: "关键性能快照",
    body:
      "把核心指标、代表性 case 和系统级优化放在同一张图里，适合作为结果页摘要。",
    src: "./assets/performance_snapshot.svg",
  },
  {
    title: "系统流程图",
    body:
      "项目报告配图中的总体系统流程，适合拿来解释从输入到输出的全链路结构。",
    src: "../contest/项目报告/image/system_flow.jpg",
  },
  {
    title: "算法流程图",
    body:
      "用于说明算法内部的处理次序和关键步骤，是页面中“技术路线”部分的天然补充。",
    src: "../contest/项目报告/image/algorithm_flow.jpg",
  },
  {
    title: "KLU vs BLOCK 管线",
    body:
      "一张图把两条主路径放到同一坐标系里，便于解释 AUTO 不是二选一口号，而是体系化选路。",
    src: "../contest/项目报告/image/klu_vs_block_pipeline.jpeg",
  },
  {
    title: "Work-stealing 调度",
    body:
      "对应“并行来自哪里”这一节，能直观看出线程窃取和长尾抑制的思路。",
    src: "../contest/项目报告/image/work_stealing_pool.jpeg",
  },
  {
    title: "块内重排与合并",
    body:
      "对应块内核优化与小块合并的工程细节，说明项目不仅有高层路线，也打磨了实现颗粒度。",
    src: "../contest/项目报告/image/block_inner_reorder_merge.jpeg",
  },
  {
    title: "结构到性能的证据链",
    body:
      "把结构变化、fill-in 变化和性能变化联系起来，是 Rerank 整体叙事里非常关键的一张图。",
    src: "../contest/项目报告/image/evidence_chain.jpeg",
  },
];

const cases = [
  {
    id: 1,
    n: 8226,
    nnz: 94723,
    weight: 0.1,
    strategy: "KLU",
    baseline: 28.176,
    ours: 16.538,
    speedup: 1.704,
    story: "小规模输入保持 KLU 稳健路径，避免为了并行而引入额外结构分析开销。",
  },
  {
    id: 2,
    n: 8392,
    nnz: 98146,
    weight: 0.1,
    strategy: "KLU",
    baseline: 27.573,
    ours: 18.277,
    speedup: 1.509,
    story: "同样属于小规模输入，AUTO 保守选择单体路径。",
  },
  {
    id: 3,
    n: 17033,
    nnz: 389381,
    weight: 0.1,
    strategy: "KLU",
    baseline: 159.192,
    ours: 97.516,
    speedup: 1.632,
    story: "收益主要来自全局重排与参数调节，而不是块级并行。",
  },
  {
    id: 4,
    n: 19698,
    nnz: 718507,
    weight: 0.1,
    strategy: "BLOCK",
    baseline: 385.355,
    ours: 122.43,
    speedup: 3.148,
    story: "结构收益开始明显，BLOCK 路线把矩阵拆成可并行的块后获得超过 3 倍提速。",
  },
  {
    id: 5,
    n: 37979,
    nnz: 1045703,
    weight: 0.15,
    strategy: "KLU",
    baseline: 928.495,
    ours: 621.608,
    speedup: 1.494,
    story: "规模更大，但分块收益还不足以覆盖构建成本，因此仍走 KLU。",
  },
  {
    id: 6,
    n: 43866,
    nnz: 630049,
    weight: 0.25,
    strategy: "KLU",
    baseline: 283.359,
    ours: 170.915,
    speedup: 1.658,
    story: "说明 AUTO 并不会简单用规模阈值硬切分，而是结合结构一起判断。",
  },
  {
    id: 7,
    n: 61531,
    nnz: 3047885,
    weight: 0.5,
    strategy: "BLOCK",
    baseline: 4634.0,
    ours: 1830.19,
    speedup: 2.532,
    story: "典型权重大用例。文档特别用它证明重排后 UBTF 结构清晰，decomp 阶段收益最明显。",
  },
  {
    id: 8,
    n: 9589,
    nnz: 191370,
    weight: 0.1,
    strategy: "KLU",
    baseline: 101.849,
    ours: 71.347,
    speedup: 1.428,
    story: "KLU 路线依然能稳定带来收益，说明项目不是只有 BLOCK 才有效。",
  },
  {
    id: 9,
    n: 57450,
    nnz: 3334561,
    weight: 0.5,
    strategy: "BLOCK",
    baseline: 3642.62,
    ours: 981.908,
    speedup: 3.71,
    story: "默认配置下的最佳单 case 加速，是 BLOCK 路线最有代表性的胜利样本。",
  },
  {
    id: 10,
    n: 33236,
    nnz: 1017920,
    weight: 0.15,
    strategy: "KLU",
    baseline: 807.67,
    ours: 617.403,
    speedup: 1.308,
    story: "说明某些中大型输入虽然不慢，但仍不适合分块，AUTO 选择更保守。",
  },
  {
    id: 11,
    n: 33468,
    nnz: 1624194,
    weight: 0.15,
    strategy: "KLU",
    baseline: 964.799,
    ours: 429.715,
    speedup: 2.245,
    story: "KLU 路线下也有超过 2 倍的例子，进一步说明优化并非只来自并行。",
  },
  {
    id: 12,
    n: 67869,
    nnz: 3486957,
    weight: 0.5,
    strategy: "KLU",
    baseline: 5575.37,
    ours: 4077.82,
    speedup: 1.367,
    story: "这是大规模但全局耦合更强的典型，分块后仍可能出现超大关键块，因此保留 KLU 路径更划算。",
  },
  {
    id: 13,
    n: 51278,
    nnz: 1166362,
    weight: 0.25,
    strategy: "BLOCK",
    baseline: 1587.32,
    ours: 709.366,
    speedup: 2.238,
    story: "又一个 BLOCK 成功样本，说明可利用块结构不是只存在于 case 7/9。",
  },
  {
    id: 14,
    n: 31537,
    nnz: 180612,
    weight: 0.15,
    strategy: "KLU",
    baseline: 243.627,
    ours: 172.205,
    speedup: 1.415,
    story: "结构分析成本与后续收益不成正比时，AUTO 会拒绝强行 BLOCK。",
  },
  {
    id: 15,
    n: 17978,
    nnz: 577035,
    weight: 0.1,
    strategy: "KLU",
    baseline: 318.941,
    ours: 218.699,
    speedup: 1.458,
    story: "以稳健方式收尾，体现 Rerank 的默认价值是整体优化而不是个别样本冒险提速。",
  },
];

const algorithmStages = [
  {
    short: "Profile",
    title: "步骤 1：原始矩阵画像",
    description:
      "系统先不急着求解，而是读取稀疏矩阵的规模、NNZ、耦合分布和长尾风险。这个阶段的任务是回答“这是不是一个适合分块的矩阵”。",
    bullets: [
      "提取 n、nnz、块结构、最大块比例和长尾风险",
      "暂时不做硬编码选路，只建立结构画像",
      "把后续决策建立在可观测特征上，而不是 case 编号上",
    ],
    badges: ["n / nnz", "tail-risk", "profile ready"],
    route: {
      klu: 41,
      block: 36,
      note: "当前只完成画像，KLU 与 BLOCK 还没有拉开明显差距。",
    },
    matrix: [
      "10010010",
      "01001001",
      "00110000",
      "10000110",
      "00101001",
      "01000110",
      "10001001",
      "00100101",
    ],
    nodes: [
      { label: "B1", status: "idle", sub: "未拆块" },
      { label: "B2", status: "idle", sub: "未拆块" },
      { label: "B3", status: "idle", sub: "未拆块" },
      { label: "B4", status: "idle", sub: "未拆块" },
      { label: "B5", status: "idle", sub: "未拆块" },
      { label: "B6", status: "idle", sub: "未拆块" },
    ],
    workers: [
      { label: "Worker 1", task: "Profile", load: 46 },
      { label: "Worker 2", task: "等待", load: 8 },
      { label: "Worker 3", task: "等待", load: 4 },
    ],
    stats: [
      { label: "阶段目标", value: "画像建模", sub: "不是立即分解" },
      { label: "结构状态", value: "耦合混杂", sub: "可并行性未显式化" },
      { label: "默认倾向", value: "未决策", sub: "等待 DM/BTF 信号" },
      { label: "工程判断", value: "先看结构", sub: "再谈后端" },
    ],
  },
  {
    short: "DM/BTF",
    title: "步骤 2：DM/BTF 显式化结构",
    description:
      "当系统识别出可利用结构后，就进入 DM/BTF 分解，把原始耦合矩阵置换成 UBTF 形式。此时“一个大问题”开始被改写成“多个对角块 + 跨块依赖”。",
    bullets: [
      "通过匹配、SCC 和块三角化生成 UBTF 结构",
      "对角块开始显式出现，可并行性被暴露出来",
      "跨块依赖被压到上三角区域，后续求解更可控",
    ],
    badges: ["DM/BTF", "UBTF", "block boundary"],
    route: {
      klu: 44,
      block: 63,
      note: "块结构开始清晰，BLOCK 路线的潜在收益开始超过单体路径。",
    },
    matrix: [
      "22000010",
      "22000001",
      "00220010",
      "00220001",
      "00002220",
      "00002220",
      "00000022",
      "00000022",
    ],
    nodes: [
      { label: "B1", status: "ready", sub: "2x2 block" },
      { label: "B2", status: "ready", sub: "2x2 block" },
      { label: "B3", status: "ready", sub: "2x2 block" },
      { label: "B4", status: "ready", sub: "2x2 block" },
      { label: "B5", status: "idle", sub: "尾部依赖" },
      { label: "B6", status: "idle", sub: "尾部依赖" },
    ],
    workers: [
      { label: "Worker 1", task: "BTF", load: 58 },
      { label: "Worker 2", task: "merge", load: 37 },
      { label: "Worker 3", task: "等待", load: 12 },
    ],
    stats: [
      { label: "阶段目标", value: "显式化块", sub: "看到结构边界" },
      { label: "结构状态", value: "UBTF", sub: "上三角依赖出现" },
      { label: "关键收益", value: "降 fill-in", sub: "为 factor 铺路" },
      { label: "工程判断", value: "可并行", sub: "开始值得走 BLOCK" },
    ],
  },
  {
    short: "AUTO",
    title: "步骤 3：AUTO 决定走哪条路线",
    description:
      "AUTO 不会简单按规模阈值切分，而是综合结构收益、构建成本和稳健性信号做决策。对于具有明显块结构的大权重样本，系统会把默认路线切到 BLOCK。",
    bullets: [
      "比较全局 KLU 与 BLOCK 的预估收益和风险",
      "只有当结构收益覆盖构建成本时才切到 BLOCK",
      "避免“为了并行而并行”的误判",
    ],
    badges: ["AUTO", "cost model", "route chosen"],
    route: {
      klu: 39,
      block: 81,
      note: "在这类大规模、块结构明显的样本上，AUTO 会把默认策略切到 BLOCK。",
    },
    matrix: [
      "33000010",
      "33000001",
      "00330010",
      "00330001",
      "00003330",
      "00003330",
      "00000033",
      "00000033",
    ],
    nodes: [
      { label: "B1", status: "ready", sub: "候选" },
      { label: "B2", status: "ready", sub: "候选" },
      { label: "B3", status: "ready", sub: "候选" },
      { label: "B4", status: "ready", sub: "候选" },
      { label: "B5", status: "ready", sub: "候选" },
      { label: "B6", status: "ready", sub: "候选" },
    ],
    workers: [
      { label: "Worker 1", task: "route score", load: 45 },
      { label: "Worker 2", task: "risk check", load: 31 },
      { label: "Worker 3", task: "prepare", load: 24 },
    ],
    stats: [
      { label: "阶段目标", value: "选路完成", sub: "KLU vs BLOCK" },
      { label: "默认路线", value: "BLOCK", sub: "结构收益占优" },
      { label: "拒绝硬编码", value: "按画像", sub: "不是按 case 号" },
      { label: "工程判断", value: "自适应", sub: "不同 case 不同解法" },
    ],
  },
  {
    short: "Block-DAG",
    title: "步骤 4：Block-DAG 并行 factor / solve",
    description:
      "进入 BLOCK 路线后，对角块被映射到 Block-DAG 上。线程池并行 factor，就绪块优先执行，空闲 worker 通过 work-stealing 吸收长尾任务。",
    bullets: [
      "对角块天然独立，可并行执行 factor",
      "work-stealing 缓解静态分配导致的尾部拖慢",
      "收益主要体现在 decomp / factor 阶段",
    ],
    badges: ["parallel factor", "work-stealing", "critical path"],
    route: {
      klu: 22,
      block: 88,
      note: "此时收益已经真正传导到 factor 主工作量，BLOCK 路线处于最强发力阶段。",
    },
    matrix: [
      "33000010",
      "33000001",
      "00330010",
      "00330001",
      "00003330",
      "00004430",
      "00000033",
      "00000033",
    ],
    nodes: [
      { label: "B1", status: "done", sub: "done" },
      { label: "B2", status: "running", sub: "factor" },
      { label: "B3", status: "running", sub: "factor" },
      { label: "B4", status: "ready", sub: "queued" },
      { label: "B5", status: "ready", sub: "queued" },
      { label: "B6", status: "idle", sub: "wait dep" },
    ],
    workers: [
      { label: "Worker 1", task: "B2 factor", load: 94 },
      { label: "Worker 2", task: "B3 factor", load: 86 },
      { label: "Worker 3", task: "steal B4", load: 61 },
    ],
    stats: [
      { label: "阶段目标", value: "压关键路径", sub: "factor 最吃收益" },
      { label: "并行状态", value: "多块就绪", sub: "worker 可窃取" },
      { label: "主要收益", value: "decomp 降低", sub: "总分开始明显拉开" },
      { label: "工程判断", value: "不盲增线程", sub: "先看就绪任务数" },
    ],
  },
  {
    short: "Fallback",
    title: "步骤 5：精度检测与稳健回退",
    description:
      "算法最后不是简单输出结果，而是把残差、裕量、NaN/Inf、rcond 等信号纳入检查。只要风险升高，就切到更保守的参数或回退路径，先把门槛保住。",
    bullets: [
      "监控 err_user、margin、残差和异常信号",
      "快路径失败时切到更稳健的参数组",
      "把性能优化建立在稳定通过精度门槛的前提上",
    ],
    badges: ["err_user ≤ ε", "margin", "retry / fallback"],
    route: {
      klu: 58,
      block: 76,
      note: "最终不是“只选 BLOCK”，而是“BLOCK 快路径 + KLU 稳健边界”共同兜底。",
    },
    matrix: [
      "33000010",
      "33000001",
      "00330010",
      "00330001",
      "00003330",
      "00004440",
      "00000033",
      "00000033",
    ],
    nodes: [
      { label: "B1", status: "done", sub: "ok" },
      { label: "B2", status: "done", sub: "ok" },
      { label: "B3", status: "warn", sub: "retry" },
      { label: "B4", status: "done", sub: "ok" },
      { label: "B5", status: "ready", sub: "fallback" },
      { label: "B6", status: "done", sub: "check" },
    ],
    workers: [
      { label: "Worker 1", task: "residual", load: 48 },
      { label: "Worker 2", task: "retry B3", load: 66 },
      { label: "Worker 3", task: "fallback guard", load: 33 },
    ],
    stats: [
      { label: "阶段目标", value: "先保门槛", sub: "再谈收益" },
      { label: "检查信号", value: "residual / margin", sub: "异常统一接入" },
      { label: "兜底策略", value: "retry + fallback", sub: "保住可用性" },
      { label: "工程判断", value: "快而不冒险", sub: "稳健优先" },
    ],
  },
];

const stageImpactProfiles = [
  {
    kicker: "先看见结构",
    summary:
      "画像阶段的作用不是直接加速，而是把矩阵规模、稀疏度、块性和长尾风险先量化出来，为后续所有判断提供依据。",
    direct: [
      { label: "直接动作", value: "提取画像特征" },
      { label: "避免问题", value: "盲目并行" },
      { label: "系统意义", value: "后续路线有依据" },
    ],
    meters: [
      { label: "结构可见度", value: 48 },
      { label: "选路把握度", value: 58 },
      { label: "并行准备度", value: 24 },
      { label: "稳健余量", value: 26 },
    ],
    chips: ["profile first", "not case-id based", "prepare decision"],
    flow: [
      { title: "看见", body: "把黑盒矩阵拆成可分析对象" },
      { title: "估计", body: "预估 fill-in 与关键路径风险" },
      { title: "筛选", body: "为 AUTO 提供输入信号" },
      { title: "约束", body: "避免无收益的复杂路径" },
    ],
  },
  {
    kicker: "把大系统切成块",
    summary:
      "DM/BTF 把原本耦合的大系统重排成 UBTF 结构，让对角块与跨块依赖显式化。它真正解决的是 fill-in 传播和依赖不可见的问题。",
    direct: [
      { label: "直接动作", value: "重排到 UBTF" },
      { label: "避免问题", value: "全局 fill-in 扩散" },
      { label: "系统意义", value: "为并行创造边界" },
    ],
    meters: [
      { label: "结构可见度", value: 92 },
      { label: "选路把握度", value: 66 },
      { label: "并行准备度", value: 64 },
      { label: "稳健余量", value: 38 },
    ],
    chips: ["UBTF", "block boundary", "fill-in control"],
    flow: [
      { title: "置换", body: "把耦合模式换成块上三角" },
      { title: "分区", body: "识别对角块与跨块边" },
      { title: "限制", body: "把传播范围缩到块边界内" },
      { title: "暴露", body: "让后续调度真正有任务可拿" },
    ],
  },
  {
    kicker: "不盲推并行",
    summary:
      "AUTO 的作用是把算法从“固定一种策略”变成“按结构画像自适应选择”。它保证 BLOCK 只在值得的时候启用，而 KLU 作为稳健边界始终可回退。",
    direct: [
      { label: "直接动作", value: "KLU / BLOCK 选路" },
      { label: "避免问题", value: "为了并行而并行" },
      { label: "系统意义", value: "不同 case 不同解法" },
    ],
    meters: [
      { label: "结构可见度", value: 62 },
      { label: "选路把握度", value: 94 },
      { label: "并行准备度", value: 46 },
      { label: "稳健余量", value: 68 },
    ],
    chips: ["adaptive route", "KLU boundary", "BLOCK only when worth it"],
    flow: [
      { title: "输入画像", body: "读取规模、块性、长尾风险" },
      { title: "评估收益", body: "比较构建成本与并行潜力" },
      { title: "落路线", body: "选择更合适的默认方案" },
      { title: "保边界", body: "保留稳健回退余地" },
    ],
  },
  {
    kicker: "把结构优势变成时间收益",
    summary:
      "Block-DAG 阶段把已经显式化的块结构转成可调度任务。真正的收益开始从“结构更好看”传导到“factor 更快、关键路径更短、尾部更平”。",
    direct: [
      { label: "直接动作", value: "并行 factor / solve" },
      { label: "避免问题", value: "大块拖尾" },
      { label: "系统意义", value: "收益传导到主耗时" },
    ],
    meters: [
      { label: "结构可见度", value: 78 },
      { label: "选路把握度", value: 72 },
      { label: "并行准备度", value: 96 },
      { label: "稳健余量", value: 56 },
    ],
    chips: ["work-stealing", "critical path", "parallel factor"],
    flow: [
      { title: "映射", body: "对角块变成 DAG 节点" },
      { title: "就绪", body: "多块可并行进入 factor" },
      { title: "窃取", body: "空闲 worker 吸收长尾任务" },
      { title: "压缩", body: "关键路径和总时间一起下降" },
    ],
  },
  {
    kicker: "先保门槛，再追收益",
    summary:
      "稳健回退阶段把残差、margin、NaN/Inf、rcond 等异常信号统一接入。它的价值不是更快，而是确保所有优化都建立在可用、可交付的精度门槛之上。",
    direct: [
      { label: "直接动作", value: "retry + fallback" },
      { label: "避免问题", value: "快但不稳" },
      { label: "系统意义", value: "15/15 全部过门槛" },
    ],
    meters: [
      { label: "结构可见度", value: 38 },
      { label: "选路把握度", value: 76 },
      { label: "并行准备度", value: 26 },
      { label: "稳健余量", value: 98 },
    ],
    chips: ["err_user <= epsilon", "retry", "stable delivery"],
    flow: [
      { title: "检查", body: "统一监控 residual 与异常信号" },
      { title: "重试", body: "快路径失败时切稳健参数" },
      { title: "回退", body: "必要时退到更保守路线" },
      { title: "验收", body: "先通过精度门槛再谈性能" },
    ],
  },
];

const weightedBaselineTotal = cases.reduce((sum, item) => sum + item.weight * item.baseline, 0);
const weightedOursTotal = cases.reduce((sum, item) => sum + item.weight * item.ours, 0);
const weightedSavings = weightedBaselineTotal - weightedOursTotal;
const blockCaseCount = cases.filter((item) => item.strategy === "BLOCK").length;
const kluCaseCount = cases.length - blockCaseCount;
const affinityBaseline = 5198.264;
const affinityPcores = 4266.575;
const affinitySavings = affinityBaseline - affinityPcores;
const positiveContributionTotal = cases.reduce(
  (sum, item) => sum + Math.max(item.weight * (item.baseline - item.ours), 0),
  0
);
const contributorCases = [...cases]
  .map((item) => ({
    ...item,
    weightedGain: item.weight * (item.baseline - item.ours),
  }))
  .sort((a, b) => b.weightedGain - a.weightedGain);

const maxBaseline = Math.max(...cases.map((item) => item.baseline));
const maxOurs = Math.max(...cases.map((item) => item.ours));
const maxBar = Math.max(maxBaseline, maxOurs);
const maxWeightedGain = Math.max(...contributorCases.map((item) => item.weightedGain));

const innovationGrid = document.getElementById("innovation-grid");
const codeGrid = document.getElementById("code-grid");
const scoreGrid = document.getElementById("score-grid");
const impactList = document.getElementById("impact-list");
const chapterGrid = document.getElementById("chapter-grid");
const deliveryGrid = document.getElementById("delivery-grid");
const effectOrbit = document.getElementById("effect-orbit");
const effectStageStrip = document.getElementById("effect-stage-strip");
const effectExplainer = document.getElementById("effect-explainer");
const demoStepbar = document.getElementById("demo-stepbar");
const demoMeta = document.querySelector(".demo-meta");
const demoShell = document.querySelector(".demo-shell");
const demoMatrix = document.getElementById("demo-matrix");
const demoRouteMeter = document.getElementById("demo-route-meter");
const demoBadges = document.getElementById("demo-badges");
const demoDag = document.getElementById("demo-dag");
const demoWorkers = document.getElementById("demo-workers");
const demoDetail = document.getElementById("demo-detail");
const demoProgressFill = document.getElementById("demo-progress-fill");
const demoStatusCopy = document.getElementById("demo-status-copy");
const demoPrev = document.getElementById("demo-prev");
const demoNext = document.getElementById("demo-next");
const demoToggle = document.getElementById("demo-toggle");
const materialsGrid = document.getElementById("materials-grid");
const galleryGrid = document.getElementById("gallery-grid");
const trendChart = document.getElementById("trend-chart");
const gainChart = document.getElementById("gain-chart");
const caseList = document.getElementById("case-list");
const caseDetail = document.getElementById("case-detail");
const filterContainer = document.getElementById("strategy-filter");
const sortSelect = document.getElementById("sort-select");
const navLinks = [...document.querySelectorAll(".nav-links a")];
const navSections = navLinks
  .map((link) => document.querySelector(link.getAttribute("href")))
  .filter(Boolean);

let selectedCaseId = 7;
let activeFilter = "all";
let demoStageIndex = 0;
let demoAutoplayEnabled = !window.matchMedia("(prefers-reduced-motion: reduce)").matches;
let demoTimer = null;
let demoTransitionToken = 0;
let demoProgressFrame = null;
let demoCycleStartedAt = 0;
let demoHoverPaused = false;
let caseDetailTransitionToken = 0;

const demoCycleMs = 3200;

function formatNumber(value) {
  return new Intl.NumberFormat("zh-CN").format(value);
}

function formatMs(value) {
  return `${value.toFixed(3)} ms`;
}

function formatRatio(value) {
  return `${value.toFixed(3)}x`;
}

function formatPercent(value) {
  return `${value.toFixed(1)}%`;
}

function clamp(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

function renderScoreGrid() {
  const cards = [
    {
      label: "Precision Gate",
      value: "15 / 15",
      body: "全部用例满足 err_user ≤ ε，是所有性能叙事成立的前提。",
    },
    {
      label: "Weighted Savings",
      value: formatMs(weightedSavings),
      body: "Final15 加权总耗时的净节省量，直接对应最终评分收益。",
    },
    {
      label: "Strategy Split",
      value: `${blockCaseCount} BLOCK / ${kluCaseCount} KLU`,
      body: "AUTO 只在结构收益足够明显时才切到 BLOCK，避免误判。",
    },
    {
      label: "P-core Gain",
      value: formatMs(affinitySavings),
      body: "在一组亲和性消融实验里，pcores 相比 off 进一步压缩尾部抖动。",
    },
  ];

  scoreGrid.innerHTML = cards
    .map(
      (item) => `
        <article class="score-card">
          <p class="metric-label">${item.label}</p>
          <h4>${item.value}</h4>
          <p>${item.body}</p>
        </article>
      `
    )
    .join("");
}

function renderImpactList() {
  impactList.innerHTML = contributorCases
    .slice(0, 5)
    .map((item) => {
      const width = maxWeightedGain > 0 ? (item.weightedGain / maxWeightedGain) * 100 : 0;
      const share = positiveContributionTotal > 0 ? (item.weightedGain / positiveContributionTotal) * 100 : 0;

      return `
        <article class="impact-item">
          <div class="impact-topline">
            <strong>Case ${item.id}</strong>
            <span class="case-chip ${item.strategy === "BLOCK" ? "case-chip-block" : "case-chip-klu"}">${item.strategy}</span>
          </div>
          <div class="impact-bar" aria-hidden="true">
            <span style="width:${width}%"></span>
          </div>
          <div class="impact-bottomline">
            <span>加权贡献 ${formatMs(item.weightedGain)}</span>
            <strong>${formatPercent(share)}</strong>
          </div>
        </article>
      `;
    })
    .join("");
}

function renderEffectOrbit() {
  if (!effectOrbit) {
    return;
  }

  effectOrbit.innerHTML = `
    <div class="effect-ring" aria-hidden="true"></div>
    <div class="effect-core">Rerank<span>system effect</span></div>
    ${algorithmStages
      .map(
        (stage, index) => `
          <button class="effect-node stage-${index} ${index === demoStageIndex ? "is-active" : ""}" data-effect-stage="${index}" type="button">
            <strong>${stage.short}</strong>
            <span>${stageImpactProfiles[index]?.kicker ?? "作用链"}</span>
          </button>
        `
      )
      .join("")}
  `;

  effectOrbit.querySelectorAll(".effect-node").forEach((button) => {
    button.addEventListener("click", () => setDemoStage(Number(button.dataset.effectStage)));
  });
}

function renderEffectStageStrip() {
  if (!effectStageStrip) {
    return;
  }

  effectStageStrip.innerHTML = algorithmStages
    .map(
      (stage, index) => `
        <button class="effect-step ${index === demoStageIndex ? "is-active" : ""}" data-effect-stage="${index}" type="button">
          <span>${String(index + 1).padStart(2, "0")}</span>
          <strong>${stage.short}</strong>
        </button>
      `
    )
    .join("");

  effectStageStrip.querySelectorAll(".effect-step").forEach((button) => {
    button.addEventListener("click", () => setDemoStage(Number(button.dataset.effectStage)));
  });
}

function renderEffectExplainer() {
  if (!effectExplainer) {
    return;
  }

  const profile = stageImpactProfiles[demoStageIndex];
  const stage = algorithmStages[demoStageIndex];

  if (!profile || !stage) {
    return;
  }

  effectExplainer.innerHTML = `
    <p class="eyebrow">Stage Impact</p>
    <span class="effect-kicker">${profile.kicker}</span>
    <h3>${stage.title}</h3>
    <p class="effect-summary">${profile.summary}</p>

    <div class="effect-direct">
      ${profile.direct
        .map(
          (item) => `
            <article>
              <span>${item.label}</span>
              <strong>${item.value}</strong>
            </article>
          `
        )
        .join("")}
    </div>

    <div class="effect-meter-grid">
      ${profile.meters
        .map(
          (meter, index) => `
            <div class="effect-meter-row">
              <div class="effect-meter-head">
                <span>${meter.label}</span>
                <strong>${meter.value}</strong>
              </div>
              <div class="effect-meter-track">
                <div class="effect-meter-fill" style="width:${meter.value}%; animation-delay:${index * 90}ms;"></div>
              </div>
            </div>
          `
        )
        .join("")}
    </div>

    <div class="effect-chip-row">
      ${profile.chips.map((chip) => `<span class="effect-chip">${chip}</span>`).join("")}
    </div>

    <div class="effect-flow">
      ${profile.flow
        .map(
          (item, index) => `
            <article class="effect-flow-card ${index === profile.flow.length - 1 ? "is-active" : ""}">
              <strong>${item.title}</strong>
              <span>${item.body}</span>
            </article>
          `
        )
        .join("")}
    </div>
  `;
}

function renderEffectSection() {
  renderEffectOrbit();
  renderEffectStageStrip();
  renderEffectExplainer();
}

function buildLinePath(points) {
  return points.map((point, index) => `${index === 0 ? "M" : "L"} ${point.x} ${point.y}`).join(" ");
}

function renderTrendChart() {
  if (!trendChart) {
    return;
  }

  const compactLabels = window.innerWidth < 860;
  const width = 720;
  const height = 300;
  const padding = { top: 24, right: 20, bottom: 44, left: 52 };
  const plotWidth = width - padding.left - padding.right;
  const plotHeight = height - padding.top - padding.bottom;
  const maxValue = Math.max(...cases.map((item) => item.baseline), ...cases.map((item) => item.ours));
  const gridLevels = [0.25, 0.5, 0.75, 1];

  const baselinePoints = cases.map((item, index) => ({
    x: padding.left + (index / (cases.length - 1)) * plotWidth,
    y: padding.top + (1 - item.baseline / maxValue) * plotHeight,
    value: item.baseline,
    id: item.id,
  }));

  const oursPoints = cases.map((item, index) => ({
    x: padding.left + (index / (cases.length - 1)) * plotWidth,
    y: padding.top + (1 - item.ours / maxValue) * plotHeight,
    value: item.ours,
    id: item.id,
  }));

  trendChart.innerHTML = `
    <svg class="chart-frame" viewBox="0 0 ${width} ${height}" role="img" aria-label="Final15 baseline 与 Rerank 耗时趋势图">
      ${gridLevels
        .map((level) => {
          const y = padding.top + (1 - level) * plotHeight;
          return `
            <line class="grid-line" x1="${padding.left}" y1="${y}" x2="${width - padding.right}" y2="${y}"></line>
            <text x="${padding.left - 12}" y="${y + 4}" text-anchor="end">${Math.round(maxValue * level)} ms</text>
          `;
        })
        .join("")}
      <line class="axis-line" x1="${padding.left}" y1="${height - padding.bottom}" x2="${width - padding.right}" y2="${height - padding.bottom}"></line>
      <line class="axis-line" x1="${padding.left}" y1="${padding.top}" x2="${padding.left}" y2="${height - padding.bottom}"></line>
      <path class="series-line series-baseline" d="${buildLinePath(baselinePoints)}"></path>
      <path class="series-line series-ours" d="${buildLinePath(oursPoints)}" style="animation-delay:180ms;"></path>
      ${baselinePoints
        .map(
          (point, index) => `
            <circle class="point" cx="${point.x}" cy="${point.y}" r="4.8" fill="rgba(184, 106, 54, 0.82)" style="animation-delay:${index * 30}ms;"></circle>
            ${
              !compactLabels || index % 2 === 0
                ? `<text x="${point.x}" y="${height - 18}" text-anchor="middle">C${point.id}</text>`
                : ""
            }
          `
        )
        .join("")}
      ${oursPoints
        .map(
          (point, index) => `
            <circle class="point" cx="${point.x}" cy="${point.y}" r="4.4" fill="rgba(29, 127, 134, 0.92)" style="animation-delay:${index * 30 + 160}ms;"></circle>
          `
        )
        .join("")}
    </svg>
    <div class="chart-caption">
      <div class="chart-legend">
        <span data-tone="baseline">Baseline</span>
        <span data-tone="ours">Rerank</span>
      </div>
      <span><strong>观察重点：</strong>高耗时 case 上两条曲线分离最明显，说明收益集中发生在最影响总分的场景。</span>
    </div>
  `;
}

function renderGainChart() {
  if (!gainChart) {
    return;
  }

  const compactLabels = window.innerWidth < 860;
  const chartCases = contributorCases.slice(0, 8);
  const width = 720;
  const height = 300;
  const padding = { top: 24, right: 16, bottom: 48, left: 52 };
  const plotWidth = width - padding.left - padding.right;
  const plotHeight = height - padding.top - padding.bottom;
  const maxValue = Math.max(...chartCases.map((item) => Math.max(item.weightedGain, 0.1)));
  const barWidth = plotWidth / chartCases.length * 0.58;

  gainChart.innerHTML = `
    <svg class="chart-frame" viewBox="0 0 ${width} ${height}" role="img" aria-label="Final15 加权贡献柱状图">
      <line class="axis-line" x1="${padding.left}" y1="${height - padding.bottom}" x2="${width - padding.right}" y2="${height - padding.bottom}"></line>
      <line class="axis-line" x1="${padding.left}" y1="${padding.top}" x2="${padding.left}" y2="${height - padding.bottom}"></line>
      ${chartCases
        .map((item, index) => {
          const xCenter = padding.left + ((index + 0.5) / chartCases.length) * plotWidth;
          const barHeight = clamp((Math.max(item.weightedGain, 0) / maxValue) * plotHeight, 6, plotHeight);
          const y = height - padding.bottom - barHeight;
          const fill =
            item.strategy === "BLOCK"
              ? "rgba(29, 127, 134, 0.86)"
              : "rgba(184, 106, 54, 0.76)";

          return `
            <rect
              class="gain-bar"
              x="${xCenter - barWidth / 2}"
              y="${y}"
              width="${barWidth}"
              height="${barHeight}"
              rx="12"
              fill="${fill}"
              style="animation-delay:${index * 80}ms;"
            ></rect>
            <text class="gain-label" x="${xCenter}" y="${y - 8}" text-anchor="middle">${item.weightedGain.toFixed(1)}</text>
            ${
              !compactLabels || index % 2 === 0
                ? `<text x="${xCenter}" y="${height - 18}" text-anchor="middle">C${item.id}</text>`
                : ""
            }
          `;
        })
        .join("")}
    </svg>
    <div class="chart-caption">
      <div class="chart-legend">
        <span data-tone="gain">Weighted Gain</span>
      </div>
      <span><strong>观察重点：</strong>少数高权重大 case 拉开了总分差距，AUTO 的价值就是把 BLOCK 用在这些真正值得的地方。</span>
    </div>
  `;
}

function renderDemoStepbar() {
  demoStepbar.innerHTML = algorithmStages
    .map(
      (stage, index) => `
        <button class="demo-step ${index === demoStageIndex ? "is-active" : ""}" data-demo-step="${index}" type="button">
          <strong>${stage.short}</strong>
          <span>${String(index + 1).padStart(2, "0")} / ${algorithmStages.length}</span>
        </button>
      `
    )
    .join("");

  demoStepbar.querySelectorAll(".demo-step").forEach((button) => {
    button.addEventListener("click", () => {
      setDemoStage(Number(button.dataset.demoStep));
    });
  });
}

function renderDemoStage() {
  const stage = algorithmStages[demoStageIndex];
  if (!stage) {
    return;
  }

  renderDemoStepbar();

  demoMatrix.innerHTML = stage.matrix
    .join("")
    .split("")
    .map((cell, index) => `<span class="demo-cell level-${cell}" style="--stagger:${index};"></span>`)
    .join("");

  demoRouteMeter.innerHTML = `
    <div class="route-meter-row" style="--stagger:0;">
      <div class="route-meter-meta">
        <span>BLOCK 候选得分</span>
        <strong>${stage.route.block}</strong>
      </div>
      <div class="route-meter-track">
        <div class="route-meter-fill block" style="width:${stage.route.block}%"></div>
      </div>
    </div>
    <div class="route-meter-row" style="--stagger:1;">
      <div class="route-meter-meta">
        <span>KLU 候选得分</span>
        <strong>${stage.route.klu}</strong>
      </div>
      <div class="route-meter-track">
        <div class="route-meter-fill klu" style="width:${stage.route.klu}%"></div>
      </div>
    </div>
    <p>${stage.route.note}</p>
  `;

  demoBadges.innerHTML = stage.badges
    .map((badge, index) => `<span class="demo-badge" style="--stagger:${index};">${badge}</span>`)
    .join("");

  demoDag.innerHTML = stage.nodes
    .map(
      (node, index) => `
        <article class="dag-node ${node.status}" style="--stagger:${index};">
          <strong>${node.label}</strong>
          <span>${node.sub}</span>
        </article>
      `
    )
    .join("");

  demoWorkers.innerHTML = stage.workers
    .map(
      (worker, index) => `
        <div class="worker-row" style="--stagger:${index};">
          <div class="worker-meta">
            <span>${worker.label}</span>
            <strong>${worker.task}</strong>
          </div>
          <div class="worker-track">
            <div class="worker-fill" style="width:${worker.load}%"></div>
          </div>
        </div>
      `
    )
    .join("");

  demoDetail.innerHTML = `
    <p class="eyebrow" style="--stagger:0;">Step Detail</p>
    <span class="detail-pill" style="--stagger:1;">Step ${String(demoStageIndex + 1).padStart(2, "0")} / ${algorithmStages.length}</span>
    <h3 style="--stagger:2;">${stage.title}</h3>
    <p style="--stagger:3;">${stage.description}</p>
    <ul class="demo-detail-list">
      ${stage.bullets.map((bullet, index) => `<li style="--stagger:${index + 4};">${bullet}</li>`).join("")}
    </ul>
    <div class="demo-stat-grid">
      ${stage.stats
        .map(
          (stat, index) => `
            <article class="demo-stat" style="--stagger:${index + 7};">
              <span>${stat.label}</span>
              <strong>${stat.value}</strong>
              <span>${stat.sub}</span>
            </article>
          `
        )
        .join("")}
    </div>
  `;

  renderEffectSection();
}

function renderInnovationGrid() {
  innovationGrid.innerHTML = innovations
    .map(
      (item, index) => `
        <article class="innovation-card">
          <span class="innovation-number">${String(index + 1).padStart(2, "0")}</span>
          <h3>${item.title}</h3>
          <p>${item.body}</p>
        </article>
      `
    )
    .join("");
}

function renderCodeGrid() {
  codeGrid.innerHTML = codeModules
    .map(
      (item) => `
        <article class="code-card">
          <h3>${item.title}</h3>
          <p>${item.body}</p>
          <a href="${item.href}">${item.path}</a>
        </article>
      `
    )
    .join("");
}

function renderChapterGrid() {
  chapterGrid.innerHTML = chapters
    .map(
      (item) => `
        <article class="chapter-card">
          <span class="chapter-index">${item.index}</span>
          <h3>${item.title}</h3>
          <p>${item.body}</p>
        </article>
      `
    )
    .join("");
}

function renderDeliveryGrid() {
  deliveryGrid.innerHTML = deliveries
    .map(
      (item) => `
        <article class="delivery-card">
          <span class="delivery-meta">${item.kind}</span>
          <h3>${item.title}</h3>
          <p>${item.body}</p>
          <a href="${item.href}">打开内容</a>
        </article>
      `
    )
    .join("");
}

function renderMaterialsGrid() {
  materialsGrid.innerHTML = materials
    .map(
      (item) => `
        <article class="material-card">
          <span class="material-meta">${item.kind}</span>
          <h3>${item.title}</h3>
          <p>${item.body}</p>
          <a href="${item.href}">打开材料</a>
        </article>
      `
    )
    .join("");
}

function renderGalleryGrid() {
  galleryGrid.innerHTML = gallery
    .map(
      (item) => `
        <article class="gallery-card">
          <img src="${item.src}" alt="${item.title}" loading="lazy" decoding="async">
          <div class="gallery-copy">
            <h3>${item.title}</h3>
            <p>${item.body}</p>
          </div>
        </article>
      `
    )
    .join("");
}

function getFilteredCases() {
  let data = [...cases];

  if (activeFilter === "block") {
    data = data.filter((item) => item.strategy === "BLOCK");
  } else if (activeFilter === "klu") {
    data = data.filter((item) => item.strategy === "KLU");
  }

  switch (sortSelect.value) {
    case "speedup":
      data.sort((a, b) => b.speedup - a.speedup);
      break;
    case "weight":
      data.sort((a, b) => b.weight - a.weight || a.id - b.id);
      break;
    case "baseline":
      data.sort((a, b) => b.baseline - a.baseline);
      break;
    case "case":
    default:
      data.sort((a, b) => a.id - b.id);
      break;
  }

  if (!data.some((item) => item.id === selectedCaseId)) {
    selectedCaseId = data[0]?.id ?? null;
  }

  return data;
}

function renderCaseList() {
  const data = getFilteredCases();

  caseList.innerHTML = data
    .map((item) => {
      const baselineWidth = (item.baseline / maxBar) * 100;
      const oursWidth = (item.ours / maxBar) * 100;
      const selectedClass = item.id === selectedCaseId ? "is-selected" : "";
      const chipClass =
        item.strategy === "BLOCK" ? "case-chip case-chip-block" : "case-chip case-chip-klu";

      return `
        <article class="case-card ${selectedClass}" data-case-id="${item.id}" aria-selected="${item.id === selectedCaseId}" role="button" tabindex="0">
          <div class="case-topline">
            <div>
              <h3>Case ${item.id}</h3>
              <p>${formatNumber(item.n)} × ${formatNumber(item.nnz)} pattern</p>
            </div>
            <span class="${chipClass}">${item.strategy}</span>
          </div>
          <div class="case-bar" aria-hidden="true">
            <span class="bar-baseline" style="width:${baselineWidth}%"></span>
            <span class="bar-ours" style="width:${oursWidth}%"></span>
          </div>
          <div class="case-bottomline">
            <div class="case-meta">
              <span>权重 ${item.weight.toFixed(2)}</span>
              <span>Baseline ${item.baseline.toFixed(3)} ms</span>
              <span>Ours ${item.ours.toFixed(3)} ms</span>
            </div>
            <strong>${item.speedup.toFixed(3)}x</strong>
          </div>
        </article>
      `;
    })
    .join("");

  caseList.querySelectorAll(".case-card").forEach((card) => {
    const activate = () => {
      selectedCaseId = Number(card.dataset.caseId);
      renderCaseList();
      renderCaseDetail({ animate: true });
    };

    card.addEventListener("click", activate);
    card.addEventListener("keydown", (event) => {
      if (event.key === "Enter" || event.key === " ") {
        event.preventDefault();
        activate();
      }
    });
  });
}

function paintCaseDetail() {
  const item = cases.find((entry) => entry.id === selectedCaseId);
  if (!item) {
    caseDetail.innerHTML = `
      <p class="eyebrow">Case Spotlight</p>
      <h3>没有匹配到当前用例</h3>
      <p>请切换筛选器或排序条件后重试。</p>
    `;
    return;
  }

  const strategyLabel =
    item.strategy === "BLOCK" ? "BLOCK 路线：结构分解 + 并行 factor" : "KLU 路线：单体稳健求解";
  const weightedGain = item.weight * (item.baseline - item.ours);
  const weightedShare =
    positiveContributionTotal > 0 ? (Math.max(weightedGain, 0) / positiveContributionTotal) * 100 : 0;
  const savedMs = item.baseline - item.ours;

  caseDetail.innerHTML = `
    <p class="eyebrow">Case Spotlight</p>
    <span class="detail-pill">${strategyLabel}</span>
    <h3>Case ${item.id}</h3>
    <p>${item.story}</p>

    <div class="detail-stats">
      <article>
        <span>规模</span>
        <strong>${formatNumber(item.n)}</strong>
        <small>变量维度 n</small>
      </article>
      <article>
        <span>非零元</span>
        <strong>${formatNumber(item.nnz)}</strong>
        <small>NNZ / pattern</small>
      </article>
      <article>
        <span>基线耗时</span>
        <strong>${item.baseline.toFixed(3)} ms</strong>
        <small>SuiteSparse baseline</small>
      </article>
      <article>
        <span>Rerank 耗时</span>
        <strong>${formatMs(item.ours)}</strong>
        <small>UserAlgorithm ours</small>
      </article>
      <article>
        <span>净节省</span>
        <strong>${formatMs(savedMs)}</strong>
        <small>Baseline - Ours</small>
      </article>
      <article>
        <span>加权贡献</span>
        <strong>${formatMs(weightedGain)}</strong>
        <small>weight × delta</small>
      </article>
    </div>

    <p><strong>加速比：</strong>${formatRatio(item.speedup)}</p>
    <p><strong>权重：</strong>${item.weight.toFixed(2)}</p>
    <p><strong>贡献占比：</strong>${formatPercent(weightedShare)}</p>
    <p>
      ${
        item.strategy === "BLOCK"
          ? "这类 case 往往具备更适合块化并行的结构，因此 DM/BTF 和 Block-DAG 的收益能够覆盖构建成本。"
          : "这类 case 更适合稳健单体路径，AUTO 在这里的价值是避免“为了并行而并行”的误判。"
      }
    </p>
  `;
}

function renderCaseDetail({ animate = false } = {}) {
  if (!animate || !caseDetail) {
    paintCaseDetail();
    return;
  }

  const token = ++caseDetailTransitionToken;
  caseDetail.classList.remove("is-switching-in");
  caseDetail.classList.add("is-switching-out");

  window.setTimeout(() => {
    if (token !== caseDetailTransitionToken) {
      return;
    }

    paintCaseDetail();

    caseDetail.classList.remove("is-switching-out");
    caseDetail.classList.add("is-switching-in");

    window.setTimeout(() => {
      if (token !== caseDetailTransitionToken) {
        return;
      }

      caseDetail.classList.remove("is-switching-in");
    }, 380);
  }, 140);
}

function setDemoProgress(progress) {
  if (!demoProgressFill) {
    return;
  }

  const normalized = Math.max(0, Math.min(progress, 1));
  demoProgressFill.style.width = `${normalized * 100}%`;
}

function stopDemoProgressLoop() {
  if (demoProgressFrame) {
    window.cancelAnimationFrame(demoProgressFrame);
    demoProgressFrame = null;
  }
}

function syncDemoStatus() {
  if (!demoStatusCopy || !demoMeta) {
    return;
  }

  let message = "自动播放中 · 3.2 秒 / 步";
  if (!demoAutoplayEnabled) {
    message = "手动浏览中 · 可用上一步 / 下一步切换";
  } else if (demoHoverPaused) {
    message = "悬停已暂停 · 移出演示区后继续播放";
  }

  demoStatusCopy.textContent = message;
  demoMeta.classList.toggle("is-paused", !demoAutoplayEnabled || demoHoverPaused);
}

function startDemoProgressLoop() {
  stopDemoProgressLoop();

  if (!demoAutoplayEnabled || demoHoverPaused) {
    return;
  }

  demoCycleStartedAt = window.performance.now();

  const step = (now) => {
    const progress = Math.min((now - demoCycleStartedAt) / demoCycleMs, 1);
    setDemoProgress(progress);

    if (progress < 1 && demoAutoplayEnabled && !demoHoverPaused) {
      demoProgressFrame = window.requestAnimationFrame(step);
    }
  };

  setDemoProgress(0);
  demoProgressFrame = window.requestAnimationFrame(step);
}

function setActiveNavLink(id) {
  navLinks.forEach((link) => {
    const targetId = link.getAttribute("href").slice(1);
    link.classList.toggle("is-active", targetId === id);
  });
}

function syncDemoToggle() {
  demoToggle.textContent = demoAutoplayEnabled ? "暂停自动播放" : "恢复自动播放";
  demoToggle.classList.toggle("is-active", demoAutoplayEnabled);
  syncDemoStatus();
}

function stopDemoAutoplay({ resetProgress = false } = {}) {
  if (demoTimer) {
    window.clearTimeout(demoTimer);
    demoTimer = null;
  }

  stopDemoProgressLoop();

  if (resetProgress) {
    setDemoProgress(0);
  }
}

function startDemoAutoplay() {
  stopDemoAutoplay({ resetProgress: true });

  if (!demoAutoplayEnabled || demoHoverPaused) {
    syncDemoToggle();
    return;
  }

  demoTimer = window.setTimeout(() => {
    setDemoStage((demoStageIndex + 1) % algorithmStages.length, { restartTimer: false });
  }, demoCycleMs);

  startDemoProgressLoop();
  syncDemoToggle();
}

function setDemoStage(index, { restartTimer = true } = {}) {
  const nextIndex = (index + algorithmStages.length) % algorithmStages.length;

  if (nextIndex === demoStageIndex) {
    if (restartTimer && demoAutoplayEnabled) {
      startDemoAutoplay();
    }
    return;
  }

  const token = ++demoTransitionToken;

  if (!demoShell) {
    demoStageIndex = nextIndex;
    renderDemoStage();
    if (demoAutoplayEnabled && !demoHoverPaused) {
      startDemoAutoplay();
    }
    return;
  }

  demoShell.classList.remove("is-switching-in");
  demoShell.classList.add("is-switching-out");

  window.setTimeout(() => {
    if (token !== demoTransitionToken) {
      return;
    }

    demoStageIndex = nextIndex;
    renderDemoStage();

    demoShell.classList.remove("is-switching-out");
    demoShell.classList.add("is-switching-in");

    window.setTimeout(() => {
      if (token !== demoTransitionToken) {
        return;
      }
      demoShell.classList.remove("is-switching-in");
    }, 430);

    if (demoAutoplayEnabled && !demoHoverPaused) {
      startDemoAutoplay();
    }
  }, 180);
}

function setActiveFilter(value) {
  activeFilter = value;
  filterContainer.querySelectorAll(".chip").forEach((button) => {
    button.classList.toggle("is-active", button.dataset.filter === value);
  });
  renderCaseList();
  renderCaseDetail({ animate: true });
}

function bindControls() {
  filterContainer.querySelectorAll(".chip").forEach((button) => {
    button.addEventListener("click", () => setActiveFilter(button.dataset.filter));
  });

  sortSelect.addEventListener("change", () => {
    renderCaseList();
    renderCaseDetail({ animate: true });
  });
}

function bindDemoControls() {
  demoPrev.addEventListener("click", () => setDemoStage(demoStageIndex - 1));
  demoNext.addEventListener("click", () => setDemoStage(demoStageIndex + 1));
  demoToggle.addEventListener("click", () => {
    demoAutoplayEnabled = !demoAutoplayEnabled;
    demoHoverPaused = false;
    if (demoAutoplayEnabled) {
      startDemoAutoplay();
    } else {
      stopDemoAutoplay({ resetProgress: true });
      syncDemoToggle();
    }
  });

  if (demoShell) {
    demoShell.addEventListener("mouseenter", () => {
      if (!demoAutoplayEnabled || demoHoverPaused) {
        return;
      }

      demoHoverPaused = true;
      stopDemoAutoplay();
      syncDemoToggle();
    });

    demoShell.addEventListener("mouseleave", () => {
      if (!demoAutoplayEnabled || !demoHoverPaused) {
        return;
      }

      demoHoverPaused = false;
      startDemoAutoplay();
    });
  }

  document.addEventListener("visibilitychange", () => {
    if (document.hidden) {
      demoHoverPaused = false;
      stopDemoAutoplay({ resetProgress: true });
      syncDemoToggle();
      return;
    }

    if (demoAutoplayEnabled) {
      startDemoAutoplay();
    }
  });

  document.addEventListener("keydown", (event) => {
    if (event.metaKey || event.ctrlKey || event.altKey) {
      return;
    }

    const targetTag = event.target?.tagName;
    if (targetTag === "INPUT" || targetTag === "TEXTAREA" || event.target?.isContentEditable) {
      return;
    }

    if (event.key === "ArrowLeft") {
      event.preventDefault();
      setDemoStage(demoStageIndex - 1);
    }

    if (event.key === "ArrowRight") {
      event.preventDefault();
      setDemoStage(demoStageIndex + 1);
    }
  });
}

function initRevealObserver() {
  const reveals = document.querySelectorAll(".reveal");

  if (!("IntersectionObserver" in window)) {
    reveals.forEach((element) => element.classList.add("is-visible"));
    return;
  }

  const observer = new IntersectionObserver(
    (entries) => {
      entries.forEach((entry) => {
        if (!entry.isIntersecting) {
          return;
        }

        entry.target.classList.add("is-visible");
        observer.unobserve(entry.target);
      });
    },
    {
      threshold: 0.18,
      rootMargin: "0px 0px -32px 0px",
    }
  );

  reveals.forEach((element) => observer.observe(element));
}

function initNavSpy() {
  if (!navLinks.length || !navSections.length) {
    return;
  }

  const initialSection = [...navSections]
    .map((section) => ({
      id: section.id,
      top: Math.abs(section.getBoundingClientRect().top),
    }))
    .sort((a, b) => a.top - b.top)[0];

  navLinks.forEach((link) => {
    link.addEventListener("click", () => {
      setActiveNavLink(link.getAttribute("href").slice(1));
    });
  });

  if (!("IntersectionObserver" in window)) {
    if (initialSection) {
      setActiveNavLink(initialSection.id);
    }
    return;
  }

  const visibleSections = new Map();
  const observer = new IntersectionObserver(
    (entries) => {
      entries.forEach((entry) => {
        visibleSections.set(entry.target.id, entry.isIntersecting ? entry.intersectionRatio : 0);
      });

      const activeEntry = [...visibleSections.entries()]
        .filter(([, ratio]) => ratio > 0.12)
        .sort((a, b) => b[1] - a[1])[0];

      if (activeEntry) {
        setActiveNavLink(activeEntry[0]);
      }
    },
    {
      threshold: [0.12, 0.2, 0.36, 0.52, 0.68],
      rootMargin: "-14% 0px -48% 0px",
    }
  );

  navSections.forEach((section) => observer.observe(section));
  if (initialSection) {
    setActiveNavLink(initialSection.id);
  }
}

function bindResizeRefresh() {
  let resizeTimer = null;

  window.addEventListener("resize", () => {
    window.clearTimeout(resizeTimer);
    resizeTimer = window.setTimeout(() => {
      renderTrendChart();
      renderGainChart();
    }, 120);
  });
}

renderScoreGrid();
renderImpactList();
renderTrendChart();
renderGainChart();
renderDemoStage();
renderInnovationGrid();
renderCodeGrid();
renderChapterGrid();
renderDeliveryGrid();
renderMaterialsGrid();
renderGalleryGrid();
bindControls();
bindDemoControls();
renderCaseList();
renderCaseDetail();
initRevealObserver();
initNavSpy();
bindResizeRefresh();
startDemoAutoplay();
