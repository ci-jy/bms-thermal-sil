#!/usr/bin/env python3
"""Summarise gcov branch coverage per source file and enforce a minimum."""
import argparse
import os
import re
import subprocess
import sys
import tempfile


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--objdir", required=True)
    ap.add_argument("--min", type=float, default=0.0)
    ap.add_argument("sources", nargs="+")
    args = ap.parse_args()

    rows, total_taken, total_br, total_lines, total_lexec = [], 0, 0, 0, 0
    with tempfile.TemporaryDirectory() as tmp:
        for src in args.sources:
            out = subprocess.run(
                ["gcov", "-b", "-n", "-o", os.path.abspath(args.objdir), os.path.abspath(src)],
                capture_output=True, text=True, cwd=tmp, check=False,
            ).stdout
            block = out.split("File '")[1] if "File '" in out else ""
            lines = re.search(r"Lines executed:([\d.]+)% of (\d+)", block)
            taken = re.search(r"Taken at least once:([\d.]+)% of (\d+)", block)
            lpct, lcount = (float(lines.group(1)), int(lines.group(2))) if lines else (0.0, 0)
            bpct, bcount = (float(taken.group(1)), int(taken.group(2))) if taken else (100.0, 0)
            rows.append((os.path.basename(src), lpct, bpct, bcount))
            total_lines += lcount
            total_lexec += round(lpct * lcount / 100)
            total_br += bcount
            total_taken += round(bpct * bcount / 100)

    print("\nBranch coverage (gcov, unit tests)")
    print(f"{'module':<20}{'lines %':>9}{'branches %':>12}{'branches':>10}")
    failed = []
    for name, lpct, bpct, bcount in rows:
        print(f"{name:<20}{lpct:>9.1f}{bpct:>12.1f}{bcount:>10}")
        if bcount and bpct < args.min:
            failed.append(name)
    tot_b = 100.0 * total_taken / total_br if total_br else 100.0
    tot_l = 100.0 * total_lexec / total_lines if total_lines else 100.0
    print(f"{'TOTAL':<20}{tot_l:>9.1f}{tot_b:>12.1f}{total_br:>10}")
    if failed:
        print(f"branch coverage below {args.min}%: {', '.join(failed)}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
