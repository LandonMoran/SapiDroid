"""Merge the benchmark TSVs (three runs per variant, best time kept) and print the comparison.

Columns: ns/op for the original SapiDroid kernels at the flags it shipped with (-O2) and at this
repository's flags (-O3 -fno-math-errno), then this repository, the speed-up against each, and
whether all three produced identical output (bit-exact).

Each figure is the median of the runs. The median, not the best: the machine's clock moves between
two states about 10% apart, so a best-of-N picks whichever build happened to catch the fast state,
which is noise rather than code.
"""
import sys


def load(path):
    samples = {}
    for line in open(path):
        name, secs, ops, dig = line.split()
        samples.setdefault(name, []).append((float(secs) / float(ops) * 1e9, int(dig, 16)))
    out = {}
    for name, runs in samples.items():
        runs.sort()
        # every run of a build must have produced the same digest; the time is the median
        out[name] = (runs[len(runs) // 2][0], runs[0][1])
    return out


def main():
    ours, same_flags, shipped = (load(p) for p in sys.argv[1:4])
    print(f"{'kernel':<28}{'orig -O2':>11}{'orig -O3':>11}{'ours':>10}"
          f"{'vs -O2':>9}{'vs -O3':>9}{'bit-exact':>11}")
    ok = True
    for name in ours:
        so, do = ours[name]
        sk, dk = same_flags[name]
        sp, dp = shipped[name]
        same = do == dk == dp
        ok = ok and same
        print(f"{name:<28}{sp:>10.2f}{sk:>11.2f}{so:>10.2f}{sp / so:>8.2f}x{sk / so:>8.2f}x"
              f"{'yes' if same else 'NO':>11}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
