/* SAPI 4 kernel benchmark: every numeric kernel in src/dsp.c, src/fft.c and src/lpc.c.
 *
 * Compiled once against this repository and once against the original SapiDroid checkout
 * (devinprater/SapiDroid) by bench/run.sh. The same deterministic inputs go through both builds,
 * every output is digested so the script can prove the two stay bit-identical, and the time is
 * printed so it can compare them. Kernels are called through volatile function pointers so the
 * compiler cannot inline them into the loop with constant arguments the way the app never does.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The implementation files are force-included by bench/run.sh with -include. */
#include "fft.h"
#include "dsp.h"
#include "lpc.h"

/* dsp.c's dsp_sort_floats() refers to the engine's VC-CRT qsort; libc's stands in (not measured). */
void vc_qsort(void *base, size_t num, size_t width, int (*cmp)(const void *, const void *))
{
    qsort(base, num, width, cmp);
}

static void (*volatile call_fft)(float *, int, int, const float *) = fft_inverse_real;
static void (*volatile call_allpole)(const float *, int, float *, float *, int) = dsp_allpole;
static void (*volatile call_fir)(float *, int, const float *, float *, int) = dsp_fir;
static void (*volatile call_iir)(float *, int, const float *, float *, int) = dsp_iir_c0;
static double (*volatile call_sum_abs)(const float *, int) = dsp_sum_abs;
static double (*volatile call_max_abs)(const float *, int) = dsp_max_abs;
static void (*volatile call_scale)(float *, int, float) = dsp_scale;
static void (*volatile call_scale_b)(float *, int, float) = dsp_scale_b;
static void (*volatile call_reverse)(const float *, int, float *) = dsp_reverse;
static void (*volatile call_pcm16)(int16_t *, const float *, int) = dsp_to_pcm16;
static void (*volatile call_pcm8)(uint8_t *, const float *, int) = dsp_to_pcm8;
static void (*volatile call_fold)(float *, int, const float *, int) = dsp_fold_pulse;
static void (*volatile call_place)(float *, const float *, int, int, int) = dsp_place_halves;
static void (*volatile call_addh)(float *, const float *, int, int, int) = dsp_add_halves;
static int (*volatile call_sort)(float *, int) = dsp_sort_floats;
static void (*volatile call_fitp)(const float *, int, float *, int, const float *, int) = dsp_fit_period;
static void (*volatile call_period)(const float *, int, float *, int, float, int, const float *, int) = dsp_period;
static void (*volatile call_lsf2lpc)(float *, float *, int, int) = lpc_from_lsf;

static uint32_t rng_state = 0x12345678u;
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

/* ------------------------------------------------------------------ transforms and filters */
static void bench_fft(void)
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
        call_fft(x, N, m, sine);
    }
    t1 = now();
    report("s4.fft256", t1 - t0, R, digest(x, sizeof x));
}

static void bench_allpole(void)
{
    enum { N = 22050, R = 300, ORDER = 16 };
    float a[ORDER + 1], mem[ORDER + 1], base[N], x[N];
    int i, r;
    for (i = 1; i <= ORDER; i++) a[i] = (float)(((i % 3) - 1) * 0.04);
    for (i = 0; i < N; i++) base[i] = rf() * 0.25f;
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        memset(mem, 0, sizeof mem);
        memcpy(x, base, sizeof x);
        x[0] += (float)r * 1e-9f;
        call_allpole(a, ORDER, mem, x, N);
    }
    t1 = now();
    report("s4.allpole16", t1 - t0, (double)N * R, digest(x, sizeof x));
}

static void bench_fir(void)
{
    enum { N = 22050, R = 300, ORDER = 16 };
    float c[ORDER], mem[ORDER], base[N], x[N];
    int i, r;
    for (i = 0; i < ORDER; i++) c[i] = (float)(0.5 - 0.03 * i);
    for (i = 0; i < N; i++) base[i] = rf();
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        memset(mem, 0, sizeof mem);
        memcpy(x, base, sizeof x);
        x[0] += (float)r * 1e-9f;
        call_fir(x, N, c, mem, ORDER);
    }
    t1 = now();
    report("s4.fir16", t1 - t0, (double)N * R, digest(x, sizeof x));
}

static void bench_iir(void)
{
    enum { N = 22050, R = 300, ORDER = 16 };
    float c[ORDER], mem[ORDER], base[N], x[N];
    int i, r;
    for (i = 0; i < ORDER; i++) c[i] = (float)(0.4 - 0.02 * i);
    for (i = 0; i < N; i++) base[i] = rf() * 0.5f;
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        memset(mem, 0, sizeof mem);
        memcpy(x, base, sizeof x);
        x[0] += (float)r * 1e-9f;
        call_iir(x, N, c, mem, ORDER);
    }
    t1 = now();
    report("s4.iir16", t1 - t0, (double)N * R, digest(x, sizeof x));
}

/* ------------------------------------------------------------------ conversions and scaling */
static void bench_pcm16(void)
{
    enum { N = 22050, R = 300 };
    float base[N], x[N];
    int16_t out[N];
    int i, r;
    for (i = 0; i < N; i++) base[i] = rf() * 30000.0f;
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        memcpy(x, base, sizeof x);
        call_pcm16(out, x, N);
    }
    t1 = now();
    report("s4.pcm16", t1 - t0, (double)N * R, digest(out, sizeof out));
}

static void bench_pcm8(void)
{
    enum { N = 22050, R = 300 };
    float base[N], x[N];
    uint8_t out[N];
    int i, r;
    for (i = 0; i < N; i++) base[i] = rf() * 30000.0f;
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        memcpy(x, base, sizeof x);
        call_pcm8(out, x, N);
    }
    t1 = now();
    report("s4.pcm8", t1 - t0, (double)N * R, digest(out, sizeof out));
}

static void bench_scale(void)
{
    enum { N = 22050, R = 300 };
    float base[N], x[N];
    int i, r;
    for (i = 0; i < N; i++) base[i] = rf();
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        memcpy(x, base, sizeof x);
        call_scale(x, N, 0.7f + (float)r * 1e-9f);
        call_scale_b(x, N, 1.1f);
    }
    t1 = now();
    report("s4.scale+scale_b", t1 - t0, (double)N * R, digest(x, sizeof x));
}

static void bench_reverse(void)
{
    enum { N = 22050, R = 300 };
    float base[N], dst[N];
    int i, r;
    for (i = 0; i < N; i++) base[i] = rf();
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        base[0] += (float)r * 1e-9f;
        call_reverse(base, N, dst);
    }
    t1 = now();
    report("s4.reverse", t1 - t0, (double)N * R, digest(dst, sizeof dst));
}

static void bench_summax(void)
{
    enum { N = 22050, R = 300 };
    float base[N];
    volatile double sink = 0.0;
    int i, r;
    for (i = 0; i < N; i++) base[i] = rf();
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        base[0] += (float)r * 1e-9f;
        sink += call_sum_abs(base, N);
        sink += call_max_abs(base, N);
    }
    t1 = now();
    report("s4.sum_abs+max_abs", t1 - t0, (double)N * R * 2, digest(base, sizeof base) ^ (uint64_t)sink);
}

/* ------------------------------------------------------------------ buffer reshaping */
static void bench_fold(void)
{
    enum { N = 512, LEN = 1024, R = 300000 };
    float dst[N], src[LEN];
    int i, r;
    for (i = 0; i < LEN; i++) src[i] = rf();
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        src[0] += (float)r * 1e-9f;
        call_fold(dst, N, src, LEN);
    }
    t1 = now();
    report("s4.fold_pulse", t1 - t0, R, digest(dst, sizeof dst));
}

static void bench_halves(void)
{
    enum { N = 128, LEN = 1024, OFF = 64, R = 300000 };
    float dst[LEN], src[2 * N];
    int i, r;
    for (i = 0; i < 2 * N; i++) src[i] = rf();
    for (i = 0; i < LEN; i++) dst[i] = 0.0f;
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        src[0] += (float)r * 1e-9f;
        call_place(dst, src, OFF, N, LEN);
        call_addh(dst, src, OFF, N, LEN);
    }
    t1 = now();
    report("s4.place+add_halves", t1 - t0, 2.0 * R, digest(dst, sizeof dst));
}

static void bench_sort(void)
{
    enum { N = 256, R = 20000 };
    float base[N], x[N];
    int i, r, sorted = 0;
    for (i = 0; i < N; i++) base[i] = rf();
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        memcpy(x, base, sizeof x);
        x[0] += (float)r * 1e-9f;
        sorted += call_sort(x, N);
    }
    t1 = now();
    report("s4.sort_floats", t1 - t0, R, digest(x, sizeof x) ^ (uint64_t)sorted);
}

/* ------------------------------------------------------------------ period fitting */
static void bench_fit(void)
{
    enum { SRC = 100, DST = 160, WIN = 256, R = 200000 };
    float win[WIN], src[SRC], dst[DST], base[SRC];
    int i, r;
    for (i = 0; i < WIN; i++) win[i] = (float)(1.0 - (double)i / WIN);
    for (i = 0; i < SRC; i++) base[i] = rf();
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        memcpy(src, base, sizeof src);
        src[0] += (float)r * 1e-9f;
        call_fitp(src, SRC, dst, DST, win, WIN);
    }
    t1 = now();
    report("s4.fit_period", t1 - t0, R, digest(dst, sizeof dst));
}

static void bench_period(void)
{
    enum { SRC = 120, DST = 160, WIN = 256, R = 100000 };
    float win[WIN], src[SRC], dst[DST], base[SRC];
    int i, r;
    for (i = 0; i < WIN; i++) win[i] = (float)(1.0 - (double)i / WIN);
    for (i = 0; i < SRC; i++) base[i] = rf();
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        memcpy(src, base, sizeof src);
        src[0] += (float)r * 1e-9f;
        call_period(src, SRC, dst, DST, 0.7f, 0, win, WIN);
        call_period(src, SRC, dst, DST, 1.0f, 1, win, WIN);
    }
    t1 = now();
    report("s4.period", t1 - t0, 2.0 * R, digest(dst, sizeof dst));
}

/* ------------------------------------------------------------------ LSF -> LPC */
static void bench_lsf2lpc(void)
{
    enum { ORDER = 16, R = 200000 };
    float lsf[ORDER], out[ORDER + 1], base[ORDER];
    int i, r;
    for (i = 0; i < ORDER; i++) base[i] = (float)(i + 1) / (float)(ORDER + 2);
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        memcpy(lsf, base, sizeof lsf);
        lsf[0] += (float)r * 1e-9f;
        call_lsf2lpc(lsf, out, ORDER, 0);
    }
    t1 = now();
    report("s4.lpc_from_lsf", t1 - t0, R, digest(out, sizeof out));
}

int main(void)
{
    bench_fft();
    bench_allpole();
    bench_fir();
    bench_iir();
    bench_pcm16();
    bench_pcm8();
    bench_scale();
    bench_reverse();
    bench_summax();
    bench_fold();
    bench_halves();
    bench_sort();
    bench_fit();
    bench_period();
    bench_lsf2lpc();
    return 0;
}
