/* Anna (WMA Voice decoder) kernel benchmark: the order-16 all-pole synthesis, its FIR post-filter,
 * the x87 dot product and the FFTs the decoder runs per frame. Compiled against this repository's
 * app/src/main/cpp/anna and against upstream ms-ana-decomp (SRC_DIR). */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* anna_dec_wmav.c is force-included by bench/run.sh with -include. */
#include "anna.h"

/* out-of-line calls, as in the app (see sapi4_bench.c) */
static void (*volatile call_synth16)(float *, const float *, const float *, float *, int) = synth16;
static void (*volatile call_fir16)(float *, const float *, const float *, float *, int) = fir16;
static float (*volatile call_dot)(const float *, const float *, int) = x87_dot;
static void (*volatile call_fft64)(float *, float *) = fft64;
static void (*volatile call_rfft128)(float *) = rfft128;

static uint32_t rng_state = 0x2545f491u;
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

static void bench_synth16(void)
{
    enum { R = 60000 };
    float y[16 + WV_FRAME], base[WV_FRAME], x[WV_FRAME], a[WV_ORDER], mem[WV_ORDER];
    int i, r;
    for (i = 0; i < WV_FRAME; i++) base[i] = rf() * 0.05f;
    for (i = 0; i < WV_ORDER; i++) a[i] = (float)(((i % 3) - 1) * 0.02);
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        memset(mem, 0, sizeof mem);
        memset(y, 0, sizeof y);
        memcpy(x, base, sizeof x);
        x[0] += (float)r * 1e-9f;
        call_synth16(y + 16, x, a, mem, WV_FRAME);
    }
    t1 = now();
    report("anna.synth16", t1 - t0, (double)WV_FRAME * R, digest(y, sizeof y));
}

static void bench_fir16(void)
{
    enum { R = 60000 };
    float out[WV_FRAME], in[WV_FRAME], base[WV_FRAME], h[WV_ORDER], mem[WV_ORDER];
    int i, r;
    for (i = 0; i < WV_FRAME; i++) base[i] = rf();
    for (i = 0; i < WV_ORDER; i++) h[i] = (float)(0.3 - 0.01 * i);
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        memset(mem, 0, sizeof mem);
        memcpy(in, base, sizeof in);
        in[0] += (float)r * 1e-9f;
        call_fir16(out, in, h, mem, WV_FRAME);
    }
    t1 = now();
    report("anna.fir16", t1 - t0, (double)WV_FRAME * R, digest(out, sizeof out));
}

static uint32_t (*volatile call_getbits)(const uint8_t *, uint32_t, int) = getbits;

static void bench_getbits(void)
{
    enum { NB = 450, R = 400000 };
    uint8_t b[NB];
    uint32_t v = 0, pos = 0;
    int i, r;
    for (i = 0; i < NB; i++) b[i] = (uint8_t)(i * 37 + 11);
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        pos = 0;
        for (i = 0; i < 100; i++) {
            pos += (uint32_t)call_getbits(b, pos, 3 + (i % 7));
            if (pos > (uint32_t)NB * 8 - 40) pos = 0;
        }
        v += pos;
    }
    t1 = now();
    report("anna.getbits", t1 - t0, (double)R * 100, digest(&v, sizeof v));
}

static void bench_dot(void)
{
    enum { N = WV_FRAME, R = 500000 };
    float a[N], b[N];
    float acc = 0.0f;
    int i, r;
    for (i = 0; i < N; i++) {
        a[i] = rf();
        b[i] = rf();
    }
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        a[0] += (float)r * 1e-9f;
        acc += call_dot(a, b, N);
    }
    t1 = now();
    report("anna.x87dot160", t1 - t0, (double)N * R, digest(&acc, sizeof acc));
}

static void bench_fft(void)
{
    enum { R = 60000 };
    float re[64], im[64], bre[64], bim[64], x[130], bx[130];
    int i, r;
    for (i = 0; i < 64; i++) {
        bre[i] = rf();
        bim[i] = rf();
    }
    for (i = 0; i < 130; i++) bx[i] = rf();
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        memcpy(re, bre, sizeof re);
        memcpy(im, bim, sizeof im);
        re[0] += (float)r * 1e-9f;
        call_fft64(re, im);
        memcpy(x, bx, sizeof x);
        call_rfft128(x);
    }
    t1 = now();
    report("anna.fft64+rfft128", t1 - t0, 2.0 * R, digest(re, sizeof re) ^ digest(x, sizeof x));
}

int main(void)
{
    bench_getbits();
    bench_synth16();
    bench_fir16();
    bench_dot();
    bench_fft();
    return 0;
}
