import argparse
import statistics
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

from perf_report import (
    CasePerf,
    parse_bench,
    svg_bar_chart_weighted,
    svg_case7_stacked,
    svg_weighted_breakdown_stacked,
    write_csv,
    fmt,
    fmt_pct,
    weight_for_n,
)


def median(xs: Sequence[float]) -> float:
    return float(statistics.median(xs)) if xs else 0.0


def collect_runs(paths: List[Path], label: str) -> List:
    runs = []
    for p in paths:
        runs.append(parse_bench(p, label))
    return runs


def median_by_case(runs, case_id: int) -> Optional[CasePerf]:
    cps = [r.ua_by_case.get(case_id) for r in runs]
    cps = [c for c in cps if c is not None]
    if not cps:
        return None
    return CasePerf(
        case_id=case_id,
        n=cps[0].n,
        nnz=cps[0].nnz,
        reorder_ms=median([c.reorder_ms for c in cps]),
        lu_ms=median([c.lu_ms for c in cps]),
        solve_ms=median([c.solve_ms for c in cps]),
        total_ms=median([c.total_ms for c in cps]),
    )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--klu", required=True, nargs="+", help="KLU logs (one or more)")
    ap.add_argument("--auto", required=True, nargs="+", help="AUTO logs (one or more)")
    ap.add_argument("--out-dir", required=True)
    args = ap.parse_args()

    out_dir = Path(args.out_dir)
    klu_paths = [Path(p) for p in args.klu]
    auto_paths = [Path(p) for p in args.auto]
    klu_runs = collect_runs(klu_paths, "KLU")
    auto_runs = collect_runs(auto_paths, "AUTO")

    case_ids = sorted(set().union(*(r.ua_by_case.keys() for r in klu_runs), *(r.ua_by_case.keys() for r in auto_runs)))
    rows: List[List[str]] = []
    for cid in case_ids:
        b = median_by_case(klu_runs, cid)
        a = median_by_case(auto_runs, cid)
        if not b or not a:
            continue
        delta = a.total_ms - b.total_ms
        delta_pct = (delta / b.total_ms * 100.0) if b.total_ms > 0 else 0.0
        speedup = (b.total_ms / a.total_ms) if a.total_ms > 0 else 0.0
        rows.append(
            [
                str(cid),
                str(a.n),
                str(a.nnz),
                fmt(b.reorder_ms),
                fmt(b.lu_ms),
                fmt(b.solve_ms),
                fmt(b.total_ms),
                fmt(a.reorder_ms),
                fmt(a.lu_ms),
                fmt(a.solve_ms),
                fmt(a.total_ms),
                fmt(delta),
                fmt_pct(delta_pct),
                fmt(speedup, 3),
            ]
        )

    write_csv(
        out_dir / "perf_diff_cases_median.csv",
        [
            "case",
            "n",
            "nnz",
            "KLU_reorder_ms(median)",
            "KLU_lu_ms(median)",
            "KLU_solve_ms(median)",
            "KLU_total_ms(median)",
            "AUTO_reorder_ms(median)",
            "AUTO_lu_ms(median)",
            "AUTO_solve_ms(median)",
            "AUTO_total_ms(median)",
            "delta_ms(after-before)",
            "delta_pct(after-before)",
            "speedup(before/after)",
        ],
        rows,
    )

    w_klu = median([r.ua_weighted_total_ms for r in klu_runs if r.ua_weighted_total_ms is not None])
    w_auto = median([r.ua_weighted_total_ms for r in auto_runs if r.ua_weighted_total_ms is not None])
    delta = w_auto - w_klu
    delta_pct = (delta / w_klu * 100.0) if w_klu > 0 else 0.0
    speedup = (w_klu / w_auto) if w_auto > 0 else 0.0
    write_csv(
        out_dir / "perf_summary_median.csv",
        ["metric", "klu_median", "auto_median", "delta", "delta_pct", "speedup"],
        [["weighted_total_ms", fmt(w_klu), fmt(w_auto), fmt(delta), fmt_pct(delta_pct), fmt(speedup, 3)]],
    )

    svg_bar_chart_weighted(out_dir / "fig_weighted_total_median.svg", "KLU(median)", w_klu, "AUTO(median)", w_auto)
    if 7 in case_ids:
        b7 = median_by_case(klu_runs, 7)
        a7 = median_by_case(auto_runs, 7)
        if b7 and a7:
            svg_case7_stacked(out_dir / "fig_case7_stacked_median.svg", b7, a7, "KLU(median)", "AUTO(median)")

    # Weighted stage breakdown from per-case medians.
    med_klu = {cid: median_by_case(klu_runs, cid) for cid in case_ids}
    med_auto = {cid: median_by_case(auto_runs, cid) for cid in case_ids}
    def weighted_stage(meds: Dict[int, Optional[CasePerf]]) -> Tuple[float, float, float]:
        wr = wl = ws = 0.0
        for cp in meds.values():
            if not cp:
                continue
            w = weight_for_n(cp.n)
            wr += w * cp.reorder_ms
            wl += w * cp.lu_ms
            ws += w * cp.solve_ms
        return wr, wl, ws

    k_wr, k_wl, k_ws = weighted_stage(med_klu)
    a_wr, a_wl, a_ws = weighted_stage(med_auto)
    write_csv(
        out_dir / "perf_weighted_breakdown_median.csv",
        ["stage", "klu_weighted_ms(median)", "auto_weighted_ms(median)", "delta", "delta_pct"],
        [
            ["reorder", fmt(k_wr), fmt(a_wr), fmt(a_wr - k_wr), fmt_pct((a_wr - k_wr) / k_wr * 100.0 if k_wr else 0.0)],
            ["lu", fmt(k_wl), fmt(a_wl), fmt(a_wl - k_wl), fmt_pct((a_wl - k_wl) / k_wl * 100.0 if k_wl else 0.0)],
            ["solve", fmt(k_ws), fmt(a_ws), fmt(a_ws - k_ws), fmt_pct((a_ws - k_ws) / k_ws * 100.0 if k_ws else 0.0)],
        ],
    )
    svg_weighted_breakdown_stacked(out_dir / "fig_weighted_breakdown_median.svg", "KLU(median)", (k_wr, k_wl, k_ws), "AUTO(median)", (a_wr, a_wl, a_ws))

    print(f"Wrote: {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
