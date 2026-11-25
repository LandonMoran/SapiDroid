/* SAPI 4 kernel benchmark. Compiled twice by bench/run.sh: once against this repository's
 * app/src/main/cpp/sapi4 and once against the upstream sapi4-decomp checkout (SRC_DIR).
 *
 * The same deterministic inputs go through both builds; the digest of every output is printed so the
 * script can prove the two implementations stay bit-identical, and the time is printed so it can
 * compare them. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The implementation files are force-included by bench/run.sh with -include. */
#include "fft.h"
#include "dsp.h"

/* dsp.c's dsp_sort_floats() refers to the engine's VC-CRT qsort; the benchmark never calls it, but
 * the reference has to resolve. libc's qsort stands in (it is not part of any measurement). */
void vc_qsort(void *base, size_t num, size_t width, int (*cmp)(const void *, const void *))
{
    qsort(base, num, width, cmp);
}

/* Called through volatile pointers so the compiler cannot inline the kernel into the loop and
 * re-optimise it with constant arguments; the app always calls them as out-of-line functions. */
static void (*volatile call_fft)(float *, int, int, const float *) = fft_inverse_real;
static void (*volatile call_allpole)(const float *, int, float *, float *, int) = dsp_allpole;
static void (*volatile call_fir)(float *, int, const float *, float *, int) = dsp_fir;
static void (*volatile call_iir)(float *, int, const float *, float *, int) = dsp_iir_c0;
static void (*volatile call_pcm)(int16_t *, const float *, int) = dsp_to_pcm16;

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
        x[3] += (float)r * 1e-9f; /* keep the optimiser from hoisting the whole call */
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

static void bench_pcm(void)
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
        call_pcm(out, x, N);
    }
    t1 = now();
    report("s4.pcm16", t1 - t0, (double)N * R, digest(out, sizeof out));
}

int main(void)
{
    bench_fft();
    bench_allpole();
    bench_fir();
    bench_iir();
    bench_pcm();
    return 0;
}
