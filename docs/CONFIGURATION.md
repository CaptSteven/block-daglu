# Runtime configuration

All historical `USERALGO_*` variables remain supported. Prefer the equivalent `BLOCKDAGLU_*` name; when both are set, `BLOCKDAGLU_*` wins.

| Variable suffix | Default | Purpose |
| --- | --- | --- |
| `MODE` | `auto` | `auto`, `klu`, or `block` route selection |
| `THREADS` | adaptive, capped | Factor/solve worker count; values are capped to a practical maximum |
| `CACHE` | `1` | Enable symbolic/decomposition caches |
| `CACHE_MAX` | `8` | Maximum entries per cache |
| `PROFILE` | `0` | Emit structured timing and route diagnostics |
| `BTF_IMPL` | `btf` | Select BTF implementation (`btf` or KLU-backed mode) |
| `BTF_MAXWORK` | backend default | BTF matching work limit |
| `DROP_ABS_LT` | adaptive | Ignore values below an absolute threshold during structural analysis |
| `BLOCK_MERGE` | enabled | Merge adjacent small BTF blocks |
| `BLOCK_SCHED` | adaptive | Reverse, level, or DAG solve scheduling |
| `FACTOR_ORDER` | adaptive | Stable or estimated-cost block factor order |
| `COND_REFINE` | `0` | Enable condition-driven iterative refinement |
| `REFINE_ITERS` | `0` | Explicit refinement iteration count |
| `AFFINITY` | platform default | Windows CPU affinity policy |
| `POWER` | platform default | Windows process/thread power policy |

KLU/BLOCK kernel tuning and experimental superblock/GP variables remain available for regression scripts under `blockdaglu/scripts/`. They are intentionally not part of the stable public configuration contract and may change between releases.

Configuration values that affect decomposition are included in the cache key, so changing them in a long-running process cannot reuse an incompatible decomposition.
