import argparse
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Tuple


ALG_HEADER = "alg,case,n,nnz,reorder_ms,lu_ms,solve_ms,total_ms,rel_err,rel_residual,ok"


def read_text_smart(path: Path) -> List[str]:
    data = path.read_bytes()
    for enc in ("utf-16", "utf-8"):
        try:
            return data.decode(enc).splitlines()
        except Exception:
            pass
    return data.decode("utf-8", errors="ignore").splitlines()


@dataclass
class CasePerf:
    case_id: int
    n: int
    nnz: int
    reorder_ms: float
    lu_ms: float
    solve_ms: float
    total_ms: float


@dataclass
class BenchRun:
    label: str
    ua_by_case: Dict[int, CasePerf]
    ss_by_case: Dict[int, CasePerf]
    ua_weighted_total_ms: Optional[float]
    ss_weighted_total_ms: Optional[float]


def parse_bench(path: Path, label: str) -> BenchRun:
    lines = read_text_smart(path)
    hi = None
    for i, ln in enumerate(lines):
        if ALG_HEADER in ln:
            hi = i
            break
    if hi is None:
        raise RuntimeError(f"Cannot find alg CSV header in {path}")

    ua: Dict[int, CasePerf] = {}
    ss: Dict[int, CasePerf] = {}
    for ln in lines[hi + 1 :]:
        if ln.startswith("case,n,nnz"):
            break
        if not ln or "," not in ln:
            continue
        cols = ln.split(",")
        if len(cols) < 11:
            continue
        alg = cols[0].strip()
        try:
            case_id = int(cols[1])
        except Exception:
            continue
        try:
            cp = CasePerf(
                case_id=case_id,
                n=int(cols[2]),
                nnz=int(cols[3]),
                reorder_ms=float(cols[4]),
                lu_ms=float(cols[5]),
                solve_ms=float(cols[6]),
                total_ms=float(cols[7]),
            )
        except Exception:
            continue
        if alg == "UserAlgorithm":
            ua[case_id] = cp
        elif alg == "SuiteSparse":
            ss[case_id] = cp

    def pick_weighted(prefix: str) -> Optional[float]:
        key = f"{prefix}_weighted_total_ms,"
        for ln in lines:
            if ln.startswith(key):
                try:
                    return float(ln.split(",")[1])
                except Exception:
                    return None
        return None

    return BenchRun(
        label=label,
        ua_by_case=ua,
        ss_by_case=ss,
        ua_weighted_total_ms=pick_weighted("UserAlgorithm"),
        ss_weighted_total_ms=pick_weighted("SuiteSparse"),
    )


def parse_profile_lines(path: Path) -> Dict[str, Dict[str, str]]:
    lines = read_text_smart(path)
    out: Dict[str, Dict[str, str]] = {}
    for ln in lines:
        if not ln.startswith("USERALGO_PROFILE,"):
            continue
        parts = ln.split(",")
        if len(parts) < 3:
            continue
        tag = parts[1].strip()
        kv: Dict[str, str] = {}
        for p in parts[2:]:
            if "=" not in p:
                continue
            k, v = p.split("=", 1)
            kv[k.strip()] = v.strip()
        out[tag] = kv
    return out


def write_csv(path: Path, header: List[str], rows: List[List[str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as f:
        f.write(",".join(header) + "\n")
        for r in rows:
            f.write(",".join(r) + "\n")


def fmt(x: float, digits: int = 3) -> str:
    return f"{x:.{digits}f}"


def fmt_pct(x: float, digits: int = 1) -> str:
    return f"{x:.{digits}f}%"


def weight_for_n(n: int) -> float:
    if n <= 20000:
        return 0.1
    if n <= 40000:
        return 0.15
    if n <= 55000:
        return 0.25
    return 0.5


def svg_bar_chart_weighted(out: Path, a_label: str, a: float, b_label: str, b: float) -> None:
    # simple 2-bar svg
    w, h = 820, 280
    pad = 60
    bw = 220
    gap = 140
    maxv = max(a, b) * 1.15
    if maxv <= 0:
        maxv = 1.0

    def y(v: float) -> float:
        return pad + (h - 2 * pad) * (1.0 - v / maxv)

    y0 = pad + (h - 2 * pad)
    xa = pad + 80
    xb = xa + bw + gap

    ra = y0 - y(a)
    rb = y0 - y(b)

    svg = f"""<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">
  <style>
    .axis {{ stroke: #111; stroke-width: 1; }}
    .barA {{ fill: #2563eb; }}
    .barB {{ fill: #16a34a; }}
    .t {{ font: 14px sans-serif; fill: #111; }}
    .tt {{ font: 16px sans-serif; font-weight: 600; fill: #111; }}
    .sub {{ font: 12px sans-serif; fill: #444; }}
  </style>
  <text x="{pad}" y="28" class="tt">加权总耗时对比（越小越好）</text>
  <line x1="{pad}" y1="{y0}" x2="{w - pad}" y2="{y0}" class="axis"/>
  <line x1="{pad}" y1="{pad}" x2="{pad}" y2="{y0}" class="axis"/>

  <rect x="{xa}" y="{y(a)}" width="{bw}" height="{ra}" class="barA"/>
  <rect x="{xb}" y="{y(b)}" width="{bw}" height="{rb}" class="barB"/>

  <text x="{xa + bw/2}" y="{y0 + 22}" text-anchor="middle" class="t">{a_label}</text>
  <text x="{xb + bw/2}" y="{y0 + 22}" text-anchor="middle" class="t">{b_label}</text>
  <text x="{xa + bw/2}" y="{y(a) - 10}" text-anchor="middle" class="sub">{fmt(a)} ms</text>
  <text x="{xb + bw/2}" y="{y(b) - 10}" text-anchor="middle" class="sub">{fmt(b)} ms</text>
</svg>
"""
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(svg, encoding="utf-8", newline="\n")


def svg_case7_stacked(out: Path, before: CasePerf, after: CasePerf, before_label: str, after_label: str) -> None:
    # 2 stacked bars: reorder / lu / solve
    w, h = 980, 360
    pad = 70
    bw = 220
    gap = 220
    maxv = max(before.total_ms, after.total_ms) * 1.15
    if maxv <= 0:
        maxv = 1.0

    def y(v: float) -> float:
        return pad + (h - 2 * pad) * (1.0 - v / maxv)

    y0 = pad + (h - 2 * pad)
    xb0 = pad + 120
    xb1 = xb0 + bw + gap

    segs = [
        ("reorder_ms", "#f59e0b", "Reorder"),
        ("lu_ms", "#ef4444", "LU"),
        ("solve_ms", "#10b981", "Solve"),
    ]

    def stacked_rects(x: float, perf: CasePerf) -> str:
        top = 0.0
        parts = []
        for key, color, _ in segs:
            v = getattr(perf, key)
            y_top = y(top + v)
            y_bot = y(top)
            parts.append(f'<rect x="{x}" y="{y_top}" width="{bw}" height="{y_bot - y_top}" fill="{color}"/>')
            top += v
        return "\n  ".join(parts)

    def label_block(x: float, perf: CasePerf, label: str) -> str:
        return (
            f'<text x="{x + bw/2}" y="{y0 + 26}" text-anchor="middle" class="t">{label}</text>'
            f'<text x="{x + bw/2}" y="{y(perf.total_ms) - 10}" text-anchor="middle" class="sub">{fmt(perf.total_ms)} ms</text>'
        )

    svg = f"""<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">
  <style>
    .axis {{ stroke: #111; stroke-width: 1; }}
    .t {{ font: 14px sans-serif; fill: #111; }}
    .tt {{ font: 16px sans-serif; font-weight: 600; fill: #111; }}
    .sub {{ font: 12px sans-serif; fill: #444; }}
  </style>
  <text x="{pad}" y="28" class="tt">Case7 耗时构成（Reorder / LU / Solve）</text>
  <line x1="{pad}" y1="{y0}" x2="{w - pad}" y2="{y0}" class="axis"/>
  <line x1="{pad}" y1="{pad}" x2="{pad}" y2="{y0}" class="axis"/>

  {stacked_rects(xb0, before)}
  {stacked_rects(xb1, after)}

  {label_block(xb0, before, before_label)}
  {label_block(xb1, after, after_label)}

  <!-- legend -->
  <rect x="{w - pad - 260}" y="{pad - 10}" width="12" height="12" fill="{segs[0][1]}"/><text x="{w - pad - 240}" y="{pad}" class="sub">{segs[0][2]}</text>
  <rect x="{w - pad - 180}" y="{pad - 10}" width="12" height="12" fill="{segs[1][1]}"/><text x="{w - pad - 160}" y="{pad}" class="sub">{segs[1][2]}</text>
  <rect x="{w - pad - 100}" y="{pad - 10}" width="12" height="12" fill="{segs[2][1]}"/><text x="{w - pad - 80}" y="{pad}" class="sub">{segs[2][2]}</text>
</svg>
"""
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(svg, encoding="utf-8", newline="\n")


def svg_weighted_breakdown_stacked(out: Path, before_label: str, before_vals: Tuple[float, float, float], after_label: str, after_vals: Tuple[float, float, float]) -> None:
    # 2 stacked bars: weighted reorder / lu / solve
    w, h = 980, 360
    pad = 70
    bw = 220
    gap = 220
    before_total = sum(before_vals)
    after_total = sum(after_vals)
    maxv = max(before_total, after_total) * 1.15
    if maxv <= 0:
        maxv = 1.0

    def y(v: float) -> float:
        return pad + (h - 2 * pad) * (1.0 - v / maxv)

    y0 = pad + (h - 2 * pad)
    xb0 = pad + 120
    xb1 = xb0 + bw + gap

    segs = [
        ("Reorder", before_vals[0], after_vals[0], "#f59e0b"),
        ("LU", before_vals[1], after_vals[1], "#ef4444"),
        ("Solve", before_vals[2], after_vals[2], "#10b981"),
    ]

    def draw_stack(x: float, vals: Tuple[float, float, float]) -> str:
        top = 0.0
        rects = []
        for (_, _, _, color), v in zip(segs, vals):
            y_top = y(top + v)
            y_bot = y(top)
            rects.append(f'<rect x="{x}" y="{y_top}" width="{bw}" height="{y_bot - y_top}" fill="{color}"/>')
            top += v
        return "\n  ".join(rects)

    svg = f"""<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">
  <style>
    .axis {{ stroke: #111; stroke-width: 1; }}
    .t {{ font: 14px sans-serif; fill: #111; }}
    .tt {{ font: 16px sans-serif; font-weight: 600; fill: #111; }}
    .sub {{ font: 12px sans-serif; fill: #444; }}
  </style>
  <text x="{pad}" y="28" class="tt">加权耗时分解（Reorder / LU / Solve）</text>
  <line x1="{pad}" y1="{y0}" x2="{w - pad}" y2="{y0}" class="axis"/>
  <line x1="{pad}" y1="{pad}" x2="{pad}" y2="{y0}" class="axis"/>

  {draw_stack(xb0, before_vals)}
  {draw_stack(xb1, after_vals)}

  <text x="{xb0 + bw/2}" y="{y0 + 26}" text-anchor="middle" class="t">{before_label}</text>
  <text x="{xb1 + bw/2}" y="{y0 + 26}" text-anchor="middle" class="t">{after_label}</text>
  <text x="{xb0 + bw/2}" y="{y(before_total) - 10}" text-anchor="middle" class="sub">{fmt(before_total)} ms</text>
  <text x="{xb1 + bw/2}" y="{y(after_total) - 10}" text-anchor="middle" class="sub">{fmt(after_total)} ms</text>

  <!-- legend -->
  <rect x="{w - pad - 260}" y="{pad - 10}" width="12" height="12" fill="{segs[0][3]}"/><text x="{w - pad - 240}" y="{pad}" class="sub">{segs[0][0]}</text>
  <rect x="{w - pad - 180}" y="{pad - 10}" width="12" height="12" fill="{segs[1][3]}"/><text x="{w - pad - 160}" y="{pad}" class="sub">{segs[1][0]}</text>
  <rect x="{w - pad - 100}" y="{pad - 10}" width="12" height="12" fill="{segs[2][3]}"/><text x="{w - pad - 80}" y="{pad}" class="sub">{segs[2][0]}</text>
</svg>
"""
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(svg, encoding="utf-8", newline="\n")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--before", required=True, help="benchmark log file (before)")
    ap.add_argument("--after", required=True, help="benchmark log file (after)")
    ap.add_argument("--before-label", default="Before")
    ap.add_argument("--after-label", default="After")
    ap.add_argument("--case7-profile", default="", help="benchmark log containing USERALGO_PROFILE lines for case7 (optional)")
    ap.add_argument("--out-dir", required=True, help="output directory for csv/svg")
    args = ap.parse_args()

    out_dir = Path(args.out_dir)
    before = parse_bench(Path(args.before), args.before_label)
    after = parse_bench(Path(args.after), args.after_label)

    # Per-case diff (UserAlgorithm).
    rows: List[List[str]] = []
    for cid in sorted(set(before.ua_by_case.keys()) | set(after.ua_by_case.keys())):
        b = before.ua_by_case.get(cid)
        a = after.ua_by_case.get(cid)
        if not b or not a:
            continue
        delta = a.total_ms - b.total_ms
        speedup = (b.total_ms / a.total_ms) if a.total_ms > 0 else math.inf
        delta_pct = (delta / b.total_ms * 100.0) if b.total_ms > 0 else 0.0
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
        out_dir / "perf_diff_cases.csv",
        [
            "case",
            "n",
            "nnz",
            f"{args.before_label}_reorder_ms",
            f"{args.before_label}_lu_ms",
            f"{args.before_label}_solve_ms",
            f"{args.before_label}_total_ms",
            f"{args.after_label}_reorder_ms",
            f"{args.after_label}_lu_ms",
            f"{args.after_label}_solve_ms",
            f"{args.after_label}_total_ms",
            "delta_ms(after-before)",
            "delta_pct(after-before)",
            "speedup(before/after)",
        ],
        rows,
    )

    # Summary
    w_b = before.ua_weighted_total_ms
    w_a = after.ua_weighted_total_ms
    if w_b is None or w_a is None:
        raise RuntimeError("Missing UserAlgorithm_weighted_total_ms in input logs")
    w_speedup = w_b / w_a if w_a > 0 else math.inf
    w_delta_pct = (w_a - w_b) / w_b * 100.0 if w_b > 0 else 0.0
    write_csv(
        out_dir / "perf_summary.csv",
        ["metric", "before", "after", "delta(after-before)", "delta_pct", "speedup(before/after)"],
        [
            ["weighted_total_ms", fmt(w_b), fmt(w_a), fmt(w_a - w_b), fmt_pct(w_delta_pct), fmt(w_speedup, 3)],
        ],
    )

    # Weighted stage breakdown (UserAlgorithm).
    def weighted_breakdown(run: BenchRun) -> Tuple[float, float, float]:
        wr = wl = ws = 0.0
        for cp in run.ua_by_case.values():
            w = weight_for_n(cp.n)
            wr += w * cp.reorder_ms
            wl += w * cp.lu_ms
            ws += w * cp.solve_ms
        return (wr, wl, ws)

    b_wr, b_wl, b_ws = weighted_breakdown(before)
    a_wr, a_wl, a_ws = weighted_breakdown(after)
    write_csv(
        out_dir / "perf_weighted_breakdown.csv",
        ["stage", f"{args.before_label}_weighted_ms", f"{args.after_label}_weighted_ms", "delta_ms(after-before)", "delta_pct(after-before)"],
        [
            ["reorder", fmt(b_wr), fmt(a_wr), fmt(a_wr - b_wr), fmt_pct((a_wr - b_wr) / b_wr * 100.0 if b_wr else 0.0)],
            ["lu", fmt(b_wl), fmt(a_wl), fmt(a_wl - b_wl), fmt_pct((a_wl - b_wl) / b_wl * 100.0 if b_wl else 0.0)],
            ["solve", fmt(b_ws), fmt(a_ws), fmt(a_ws - b_ws), fmt_pct((a_ws - b_ws) / b_ws * 100.0 if b_ws else 0.0)],
        ],
    )

    # SVG charts
    svg_bar_chart_weighted(out_dir / "fig_weighted_total.svg", args.before_label, w_b, args.after_label, w_a)
    svg_weighted_breakdown_stacked(out_dir / "fig_weighted_breakdown.svg", args.before_label, (b_wr, b_wl, b_ws), args.after_label, (a_wr, a_wl, a_ws))

    # Case7 stacked bar
    if 7 in before.ua_by_case and 7 in after.ua_by_case:
        svg_case7_stacked(
            out_dir / "fig_case7_stacked.svg",
            before.ua_by_case[7],
            after.ua_by_case[7],
            args.before_label,
            args.after_label,
        )

    # Optional profile extraction
    if args.case7_profile:
        prof = parse_profile_lines(Path(args.case7_profile))
        # Save a small CSV for profile key metrics.
        rows2: List[List[str]] = []
        for tag in sorted(prof.keys()):
            kv = prof[tag]
            for k in sorted(kv.keys()):
                rows2.append([tag, k, kv[k]])
        write_csv(out_dir / "perf_profile_kv.csv", ["tag", "key", "value"], rows2)

    print(f"Wrote: {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
