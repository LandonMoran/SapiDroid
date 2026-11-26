#!/bin/sh
# Benchmark every kernel of the four speech engines in this repository against the same kernels in
# the original SapiDroid (devinprater/SapiDroid, the project this one is forked from), on identical
# deterministic inputs, and report:
#   * whether both produce bit-identical output (the last column), and
#   * ns per sample (or per call for the transforms and one-shot kernels).
#
# Both trees are built with the same compiler, so the first two columns are the original's kernels at
# the flags it shipped with (-O2) and at this repository's flags (-O3 -fno-math-errno); the third is
# this repository. That separates "same code, different flags" from "different code".
#
# Usage: bench/run.sh
#   ORIG_DIR=<dir>     where the original SapiDroid checkout goes (cloned if missing)
#   BENCH_OUT=<dir>    where the binaries and TSV results go (outside the repository by default)
#   OURS_FLAGS=<opts>  override this repository's flags (the digests still have to match)
#   CC=clang           compiler (default: cc)
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
ORIG=${ORIG_DIR:-${TMPDIR:-/tmp}/sapidroid-orig-$(id -u)}
OUT=${BENCH_OUT:-${TMPDIR:-/tmp}/sapidroid-bench-$(id -u)}
CC=${CC:-cc}
COMMON="-std=c99 -ffp-contract=off -fno-fast-math -fno-math-errno -D_DEFAULT_SOURCE"
OURS_FLAGS=${OURS_FLAGS:--O3}

if [ ! -d "$ORIG/app/src/main/cpp" ]; then
    echo "cloning the original SapiDroid into $ORIG" >&2
    mkdir -p "$ORIG"
    git clone --depth 1 -q https://github.com/devinprater/SapiDroid.git "$ORIG"
fi
mkdir -p "$OUT"

# build_variant <prefix> <tree> <optimisation flags>
# The kernel files are force-included: the benchmark's calls and the file's static functions have to
# share one translation unit exactly as they do inside the app.
build_variant() {
    prefix=$1
    tree=$2
    opt=$3

    d=$tree/app/src/main/cpp/sapi4
    # shellcheck disable=SC2086
    $CC $COMMON $opt -include "$d/src/fft.c" -include "$d/src/dsp.c" -include "$d/src/lpc.c" \
        -I"$d/src" "$ROOT/bench/sapi4_bench.c" -lm -o "$OUT/s4_$prefix"

    d=$tree/app/src/main/cpp/sapi5
    # shellcheck disable=SC2086
    $CC $COMMON $opt -include "$d/sam.c" -I"$d" "$ROOT/bench/sapi5_bench.c" -lm -o "$OUT/s5_$prefix"
    # shellcheck disable=SC2086
    $CC $COMMON $opt -include "$d/sam4fx.c" -I"$d" "$ROOT/bench/sapi5_fx_bench.c" -lm -o "$OUT/s5fx_$prefix"

    d=$tree/app/src/main/cpp/anna
    # shellcheck disable=SC2086
    $CC $COMMON $opt -include "$d/anna_dec_wmav.c" -I"$d" "$ROOT/bench/anna_bench.c" -lm -o "$OUT/anna_$prefix"

    d=$tree/engine
    # zb_synth_int.c is pulled in for the integer MLPG (its kernels are static). -ffunction-sections
    # plus --gc-sections drop the parts of it the benchmark does not call, so the engine routines it
    # would otherwise need a link to (durations, the waveform stage) do not have to be stubbed out.
    # shellcheck disable=SC2086
    $CC $COMMON $opt -ffunction-sections -fdata-sections -DONECORE_HAVE_SYNTH_INT \
        -include "$ROOT/bench/onecore_stub.c" -include "$ROOT/bench/onecore_wrap_float.c" \
        -include "$ROOT/bench/onecore_wrap_int.c" -include "$ROOT/bench/onecore_wrap_sonic.c" \
        -include "$ROOT/bench/onecore_wrap_synth.c" \
        -I"$d" "$ROOT/bench/onecore_bench.c" -Wl,--gc-sections -lm -o "$OUT/oc_$prefix"
}

build_variant ours "$ROOT" "$OURS_FLAGS"
build_variant orig_o3 "$ORIG" "-O3"
build_variant orig_o2 "$ORIG" "-O2"

# The variants are interleaved inside each round: the clock on a shared machine drifts, and running
# all of one variant and then all of another charges that drift to whichever ran second.
rm -f "$OUT/ours.tsv" "$OUT/orig_o3.tsv" "$OUT/orig_o2.tsv"
for run in 1 2 3 4 5 6 7; do
    for prog in s4 s5 s5fx anna oc; do
        for variant in ours orig_o3 orig_o2; do
            "$OUT/${prog}_${variant}" >> "$OUT/$variant.tsv"
        done
    done
done

python3 "$ROOT/bench/report.py" "$OUT/ours.tsv" "$OUT/orig_o3.tsv" "$OUT/orig_o2.tsv"
