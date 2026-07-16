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


def collect_runs(paths: List[Path], label: str):
    return [parse_bench(p, label) for p in paths]


def median_by_case(runs, case_id: int, alg: str) -> Optional[CasePerf]:
    if alg not in ("ua", "ss"):
        raise ValueError("alg must be 'ua' or 'ss'")
    cps = []
    for r in runs:
        m = r.ua_by_case if alg == "ua" else r.ss_by_case
        cp = m.get(case_id)
        if cp is not None:
            cps.append(cp)
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


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--logs", required=True, nargs="+", help="benchmark logs (one or more runs)")
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--label", default="median", help="label for outputs")
    args = ap.parse_args()

    out_dir = Path(args.out_dir)
    paths = [Path(p) for p in args.logs]
    runs = collect_runs(paths, args.label)

    case_ids = sorted(set().union(*(r.ua_by_case.keys() for r in runs), *(r.ss_by_case.keys() for r in runs)))

    # Per-case (median) UA vs SS.
    rows: List[List[str]] = []
    for cid in case_ids:
        ss = median_by_case(runs, cid, "ss")
        ua = median_by_case(runs, cid, "ua")
        if not ss or not ua:
            continue
        delta = ua.total_ms - ss.total_ms
        delta_pct = (delta / ss.total_ms * 100.0) if ss.total_ms > 0 else 0.0
        speedup = (ss.total_ms / ua.total_ms) if ua.total_ms > 0 else 0.0
        rows.append(
            [
                str(cid),
                str(ua.n),
                str(ua.nnz),
                fmt(ss.reorder_ms),
                fmt(ss.lu_ms),
                fmt(ss.solve_ms),
                fmt(ss.total_ms),
                fmt(ua.reorder_ms),
                fmt(ua.lu_ms),
                fmt(ua.solve_ms),
                fmt(ua.total_ms),
                fmt(delta),
                fmt_pct(delta_pct),
                fmt(speedup, 3),
            ]
        )

    write_csv(
        out_dir / "perf_ua_vs_ss_cases_median.csv",
        [
            "case",
            "n",
            "nnz",
            "SuiteSparse_reorder_ms(median)",
            "SuiteSparse_lu_ms(median)",
            "SuiteSparse_solve_ms(median)",
            "SuiteSparse_total_ms(median)",
            "UserAlgorithm_reorder_ms(median)",
            "UserAlgorithm_lu_ms(median)",
            "UserAlgorithm_solve_ms(median)",
            "UserAlgorithm_total_ms(median)",
            "delta_ms(ua-ss)",
            "delta_pct(ua-ss)",
            "speedup(ss/ua)",
        ],
        rows,
    )

    w_ss = median([r.ss_weighted_total_ms for r in runs if r.ss_weighted_total_ms is not None])
    w_ua = median([r.ua_weighted_total_ms for r in runs if r.ua_weighted_total_ms is not None])
    delta = w_ua - w_ss
    delta_pct = (delta / w_ss * 100.0) if w_ss > 0 else 0.0
    speedup = (w_ss / w_ua) if w_ua > 0 else 0.0
    write_csv(
        out_dir / "perf_ua_vs_ss_summary_median.csv",
        ["metric", "suitesparse_median", "useralgorithm_median", "delta(ua-ss)", "delta_pct", "speedup(ss/ua)"],
        [["weighted_total_ms", fmt(w_ss), fmt(w_ua), fmt(delta), fmt_pct(delta_pct), fmt(speedup, 3)]],
    )

    # Weighted stage breakdown from per-case medians.
    med_ss = {cid: median_by_case(runs, cid, "ss") for cid in case_ids}
    med_ua = {cid: median_by_case(runs, cid, "ua") for cid in case_ids}
    ss_wr, ss_wl, ss_ws = weighted_stage(med_ss)
    ua_wr, ua_wl, ua_ws = weighted_stage(med_ua)
    write_csv(
        out_dir / "perf_ua_vs_ss_weighted_breakdown_median.csv",
        ["stage", "suitesparse_weighted_ms(median)", "useralgorithm_weighted_ms(median)", "delta(ua-ss)", "delta_pct"],
        [
            ["reorder", fmt(ss_wr), fmt(ua_wr), fmt(ua_wr - ss_wr), fmt_pct((ua_wr - ss_wr) / ss_wr * 100.0 if ss_wr else 0.0)],
            ["lu", fmt(ss_wl), fmt(ua_wl), fmt(ua_wl - ss_wl), fmt_pct((ua_wl - ss_wl) / ss_wl * 100.0 if ss_wl else 0.0)],
            ["solve", fmt(ss_ws), fmt(ua_ws), fmt(ua_ws - ss_ws), fmt_pct((ua_ws - ss_ws) / ss_ws * 100.0 if ss_ws else 0.0)],
        ],
    )

    svg_bar_chart_weighted(out_dir / "fig_ua_vs_ss_weighted_total_median.svg", "SuiteSparse(median)", w_ss, "UserAlgorithm(median)", w_ua)
    svg_weighted_breakdown_stacked(out_dir / "fig_ua_vs_ss_weighted_breakdown_median.svg", "SuiteSparse(median)", (ss_wr, ss_wl, ss_ws), "UserAlgorithm(median)", (ua_wr, ua_wl, ua_ws))

    if 7 in case_ids:
        ss7 = median_by_case(runs, 7, "ss")
        ua7 = median_by_case(runs, 7, "ua")
        if ss7 and ua7:
            svg_case7_stacked(out_dir / "fig_case7_ua_vs_ss_stacked_median.svg", ss7, ua7, "SuiteSparse(median)", "UserAlgorithm(median)")

    print(f"Wrote: {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

