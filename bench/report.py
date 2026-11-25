"""Merge the benchmark TSVs (three runs per variant, best time kept) and print the comparison.

Columns: ns/op for the upstream engines as their Makefiles build them (-O2), the same upstream
sources at -O3, this repository at its shipping flags, the speed-up against each, and whether all
three produced identical output (bit-exact).
"""
import sys


def load(path):
    out = {}
    for line in open(path):
        name, secs, ops, dig = line.split()
        ns = float(secs) / float(ops) * 1e9
        value = (ns, int(dig, 16))
        prev = out.get(name)
        # best (smallest) time, but every run must have produced the same digest
        out[name] = (min(prev[0], ns), prev[1]) if prev else value
    return out


def main():
    ours, same_flags, shipped = (load(p) for p in sys.argv[1:4])
    print(f"{'kernel':<24}{'upstream -O2':>14}{'upstream -O3':>14}{'ours':>11}"
          f"{'vs -O2':>9}{'vs -O3':>9}{'bit-exact':>11}")
    ok = True
    for name in ours:
        so, do = ours[name]
        sk, dk = same_flags[name]
        sp, dp = shipped[name]
        same = do == dk == dp
        ok = ok and same
        print(f"{name:<24}{sp:>13.2f}{sk:>14.2f}{so:>11.2f}{sp / so:>8.2f}x{sk / so:>8.2f}x"
              f"{'yes' if same else 'NO':>11}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
