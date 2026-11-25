#!/bin/sh
# Build the engine kernels in this repository and the same kernels from the four upstream
# reconstructions, run them on identical deterministic inputs, and report
#   * whether the output is bit-identical (last column), and
#   * the time difference, in ns per sample (or per call for the FFTs).
#
# Usage: bench/run.sh
#   UPSTREAM_DIR=<dir>   where the upstream checkouts go (cloned if missing)
#   BENCH_OUT=<dir>      where the binaries and TSV results go
#   CC=clang             compiler (default: cc)
#
# The clones and the binaries live outside the repository so nothing large is added to it.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
UP=${UPSTREAM_DIR:-${TMPDIR:-/tmp}/sapidroid-upstream-$(id -u)}
OUT=${BENCH_OUT:-${TMPDIR:-/tmp}/sapidroid-bench-$(id -u)}
CC=${CC:-cc}
COMMON="-std=c99 -ffp-contract=off -fno-fast-math -fno-math-errno -D_DEFAULT_SOURCE"

if [ ! -d "$UP/s4" ] || [ ! -d "$UP/s5" ] || [ ! -d "$UP/anna" ] || [ ! -d "$UP/onecore" ]; then
    echo "cloning upstream engines into $UP" >&2
    mkdir -p "$UP"
    [ -d "$UP/s4" ] || git clone --depth 1 -q https://github.com/Quinton1110/sapi4-decomp.git "$UP/s4" &
    [ -d "$UP/s5" ] || git clone --depth 1 -q https://github.com/KamiKitsune420/ms-sam-mike-mary-decomp.git "$UP/s5" &
    [ -d "$UP/anna" ] || git clone --depth 1 -q https://github.com/KamiKitsune420/ms-ana-decomp.git "$UP/anna" &
    [ -d "$UP/onecore" ] || git clone --depth 1 -q https://github.com/KamiKitsune420/ms-david-zira-decomp.git "$UP/onecore" &
    wait
fi

mkdir -p "$OUT"

# build <binary> <bench source> <kernel dir> <optimisation flags>
# The kernel files are force-included, so the benchmark's calls and the file's static functions share
# one translation unit exactly as they do inside the app.
build() {
    name=$1
    src=$2
    dir=$3
    opt=$4
    case $src in
    sapi4_bench.c)   incs="-include $dir/src/fft.c -include $dir/src/dsp.c" ;;
    sapi5_bench.c)   incs="-include $dir/sam.c" ;;
    anna_bench.c)    incs="-include $dir/anna_dec_wmav.c" ;;
    onecore_bench.c) incs="-include $ROOT/bench/onecore_stub.c -include $ROOT/bench/onecore_wrap_float.c -include $ROOT/bench/onecore_wrap_int.c -include $ROOT/bench/onecore_wrap_sonic.c" ;;
    esac
    # shellcheck disable=SC2086
    $CC $COMMON $opt $incs -I"$dir" -I"$dir/src" -I"$dir/port" \
        "$ROOT/bench/$src" -lm -o "$OUT/$name"
}

# The four upstream engines build with -O2 (their Makefiles). SapiDroid ships -O3 and
# -fno-math-errno, so the report separates "same compiler flags" from "what ships".
for v in sapi4 sapi5 anna onecore; do
    case $v in
    sapi4)   src=sapi4_bench.c;   ours="$ROOT/app/src/main/cpp/sapi4"; up="$UP/s4" ;;
    sapi5)   src=sapi5_bench.c;   ours="$ROOT/app/src/main/cpp/sapi5"; up="$UP/s5/src" ;;
    anna)    src=anna_bench.c;    ours="$ROOT/app/src/main/cpp/anna"; up="$UP/anna/src" ;;
    onecore) src=onecore_bench.c; ours="$ROOT/engine"; up="$UP/onecore/src" ;;
    esac
    build "${v}_ours"        "$src" "$ours" "-O3"
    build "${v}_upstream_o3" "$src" "$up"   "-O3"
    build "${v}_upstream_o2" "$src" "$up"   "-O2"
done

rm -f "$OUT/ours.tsv" "$OUT/upstream_o3.tsv" "$OUT/upstream_o2.tsv"
for v in sapi4 sapi5 anna onecore; do
    for variant in ours upstream_o3 upstream_o2; do
        for run in 1 2 3; do
            "$OUT/${v}_${variant}" >> "$OUT/$variant.tsv"
        done
    done
done

python3 "$ROOT/bench/report.py" "$OUT/ours.tsv" "$OUT/upstream_o3.tsv" "$OUT/upstream_o2.tsv"
