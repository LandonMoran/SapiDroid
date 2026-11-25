/* SAPI 5 (Sam/Mike/Mary) kernel benchmark: the all-pole filter that runs over every output sample,
 * the excitation's inverse FFT and the period fitter. Compiled against this repository's
 * app/src/main/cpp/sapi5 and against upstream ms-sam-mike-mary-decomp (SRC_DIR). */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* sam.c is force-included by bench/run.sh with -include. */
#include "sam.h"

/* out-of-line calls, as in the app (see sapi4_bench.c) */
static void (*volatile call_lpc)(sam_synth *, const float *, float *, int, float) = lpc_filter;
static void (*volatile call_ifft)(float *, int, int, const float *) = ifft_split_radix;
static void (*volatile call_fit)(const float *, int, float *, int, const float *, int) = fit_period;

static uint32_t rng_state = 0x9e3779b9u;
static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
static float rf(void) { return (float)((double)(rnd() >> 8) / 16777216.0 * 2.0 - 1.0); }

static uint64_t digest(const void *p, size_t n)
{
    const unsigned char *b = p;
    uint64_t h = 1469598103934665603ull;
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= b[i];
        h *= 1099511628211ull;
    }
    return h;
}

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec;
}

static void report(const char *name, double secs, double ops, uint64_t d)
{
    printf("%s %.9f %.0f %016llx\n", name, secs, ops, (unsigned long long)d);
}

static void bench_lpc(void)
{
    enum { ORDER = 16, N = 22050, R = 300 };
    sam_voice v;
    sam_synth s;
    float a[ORDER + 1], base[N], x[N];
    int i, r;
    memset(&v, 0, sizeof v);
    memset(&s, 0, sizeof s);
    v.order = ORDER;
    s.v = &v;
    s.mem = (float *)calloc(ORDER + 1, sizeof(float));
    a[0] = 1.0f;
    for (i = 1; i <= ORDER; i++) a[i] = (float)(((i % 3) - 1) * 0.04);
    for (i = 0; i < N; i++) base[i] = rf() * 0.1f;
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        memset(s.mem, 0, sizeof(float) * (ORDER + 1));
        memcpy(x, base, sizeof x);
        x[0] += (float)r * 1e-9f;
        call_lpc(&s, a, x, N, 0.5f);
    }
    t1 = now();
    report("s5.lpc16", t1 - t0, (double)N * R, digest(x, sizeof x));
    free(s.mem);
}

static void bench_ifft(void)
{
    enum { N = 256, R = 20000 };
    float sine[2 * N], base[N], x[N];
    int i, r, m = 0;
    while ((1 << m) < N) m++;
    for (i = 0; i < 2 * N; i++) sine[i] = (float)sin(2.0 * 3.141592653589793 * (double)i / (double)(2 * N));
    for (i = 0; i < N; i++) base[i] = rf();
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        memcpy(x, base, sizeof x);
        x[3] += (float)r * 1e-9f;
        call_ifft(x, N, m, sine);
    }
    t1 = now();
    report("s5.ifft256", t1 - t0, R, digest(x, sizeof x));
}

static void bench_fit(void)
{
    enum { SRC = 100, DST = 160, R = 200000 };
    float win[256], src[SRC], dst[DST], base[SRC];
    int i, r;
    for (i = 0; i < 256; i++) win[i] = (float)(1.0 - (double)i / 256.0);
    for (i = 0; i < SRC; i++) base[i] = rf();
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        memcpy(src, base, sizeof src);
        src[0] += (float)r * 1e-9f;
        call_fit(src, SRC, dst, DST, win, 256);
    }
    t1 = now();
    report("s5.fitperiod", t1 - t0, R, digest(dst, sizeof dst));
}

int main(void)
{
    bench_lpc();
    bench_ifft();
    bench_fit();
    return 0;
}
