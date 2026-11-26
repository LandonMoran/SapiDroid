/* OneCore (David/Zira/Mark) kernel benchmark: excitation generation, the floating and fixed-point
 * all-pole vocoders, the "valid" FIR and the Sonic time-scale modifier. Compiled against this
 * repository's engine/ and against upstream ms-david-zira-decomp (SRC_DIR). */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The engine files are force-included by bench/run.sh with -include. */
#include "zb_internal.h"
#include "zb_sonic.h"

/* out-of-line calls, as in the app (see sapi4_bench.c) */
static void (*volatile call_fir_valid)(const float *, const float *, float *, int) = fir_valid;
static void (*volatile call_exc)(ZbVoice *, ZbExcState *, const float *, int, int, int, float *) = excitation;
static void (*volatile call_synth)(const float *, int, const float *, const float *, int, int, float *, short *) = synth;
static void (*volatile call_iexc)(ZbVoice *, const int32_t *, int, int, int, int32_t *) = iexcitation;
static void (*volatile call_isynth)(ZbVoice *, const int32_t *, int, const int32_t *, const int32_t *, int, int, short *) =
    isynth;
static void (*volatile call_scale_pcm)(short *, long, float) = zb_scale_pcm;
static void (*volatile call_lsf_repair)(float *, int, int) = zb_lsf_repair;
static void (*volatile call_imlpg)(const ZbModel *, ZbIG *const *, int, int, int, int32_t *) = imlpg;

static uint32_t rng_state = 0x6d2b79f5u;
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

static void bench_fir(void)
{
    enum { N = 1024, R = 4000 };
    float h[41], x[N], base[N], out[N];
    int i, r;
    for (i = 0; i < 41; i++) h[i] = (float)(0.2 - 0.004 * i);
    for (i = 0; i < N; i++) base[i] = rf() * 0.1f;
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        memcpy(x, base, sizeof x);
        x[0] += (float)r * 1e-9f;
        call_fir_valid(h, x, out, N);
    }
    t1 = now();
    report("onecore.fir41", t1 - t0, (double)N * R, digest(out, sizeof out));
}

static void bench_float(void)
{
    enum { NFR = 100, SHIFT = 80, N = NFR * SHIFT, R = 200, P = 24 };
    ZbVoice v;
    ZbExcState st;
    float f0[NFR], lsf[N * P], gain[NFR], exc[N], hist[64], base_lsf[N * P];
    short pcm[N];
    int i, r;
    memset(&v, 0, sizeof v);
    v.rate = 16000;
    v.shift = SHIFT;
    zb_vocoder_init(&v);
    for (i = 0; i < NFR; i++) f0[i] = 95.0f + (float)(i % 30) * 3.0f;
    for (i = 0; i < N * P; i++) base_lsf[i] = (float)((i % P) + 1) / (float)(P + 1);
    for (i = 0; i < NFR; i++) gain[i] = 1.0f;
    for (i = 0; i < N; i++) exc[i] = 0.01f * rf();
    double t0, t1, ops = 0.0;
    t0 = now();
    for (r = 0; r < R; r++) {
        memset(&st, 0, sizeof st);
        memset(hist, 0, sizeof hist);
        memcpy(lsf, base_lsf, sizeof lsf);
        for (i = 0; i < NFR; i++) gain[i] = 1.0f + (float)r * 1e-9f;
        call_exc(&v, &st, f0, NFR, N, SHIFT, exc);
        call_synth(lsf, P, gain, exc, NFR, SHIFT, hist, pcm);
        ops += N;
    }
    t1 = now();
    report("onecore.synthf", t1 - t0, ops, digest(pcm, sizeof pcm));
}

static void bench_int(void)
{
    enum { NFR = 100, SHIFT = 80, N = NFR * SHIFT, R = 200, P = 24 };
    ZbVoice v;
    int32_t f0[NFR], lsf[N * P], gain[NFR], exc[N], base_lsf[N * P];
    short pcm[N];
    int i, r;
    memset(&v, 0, sizeof v);
    v.rate = 16000;
    v.shift = SHIFT;
    zb_vocoder_init(&v);
    zb_vocoder_int_reset(&v);
    for (i = 0; i < NFR; i++) f0[i] = 95 + (i % 30) * 3;
    for (i = 0; i < N * P; i++) base_lsf[i] = 400 + ((i % P) * 600);
    for (i = 0; i < NFR; i++) gain[i] = 0x100;
    for (i = 0; i < N; i++) exc[i] = (int32_t)(rf() * 100.0f);
    double t0, t1, ops = 0.0;
    t0 = now();
    for (r = 0; r < R; r++) {
        zb_vocoder_int_reset(&v);
        memcpy(lsf, base_lsf, sizeof lsf);
        for (i = 0; i < NFR; i++) gain[i] = 0x100 + r;
        call_iexc(&v, f0, NFR, N, SHIFT, exc);
        call_isynth(&v, lsf, P, gain, exc, NFR, SHIFT, pcm);
        ops += N;
    }
    t1 = now();
    report("onecore.synthi", t1 - t0, ops, digest(pcm, sizeof pcm));
}

static void bench_sonic(void)
{
    enum { N = 22050, BLK = 1024, OUT = 8192, R = 300 };
    short in[N], buf[OUT];
    int i, r;
    for (i = 0; i < N; i++) in[i] = (short)(((i * 37) % 2000) - 1000 + (i % 7));
    double t0, t1, ops = 0.0;
    t0 = now();
    for (r = 0; r < R; r++) {
        ZbSonic *s = zb_sonic_create(16000);
        int pos = 0;
        zb_sonic_set_speed(s, 0.8f + (float)r * 1e-9f);
        while (pos < N) {
            int n = N - pos < BLK ? N - pos : BLK;
            zb_sonic_write(s, in + pos, n);
            pos += n;
            while (zb_sonic_available(s) > 0) zb_sonic_read(s, buf, OUT);
        }
        zb_sonic_flush(s);
        while (zb_sonic_available(s) > 0) zb_sonic_read(s, buf, OUT);
        ops += N;
        zb_sonic_free(s);
    }
    t1 = now();
    report("onecore.sonic", t1 - t0, ops, digest(buf, sizeof buf));
}

static void bench_scale_pcm(void)
{
    enum { N = 22050, R = 300 };
    short base[N], x[N];
    int i, r;
    for (i = 0; i < N; i++) base[i] = (short)((i * 37) % 20000 - 10000);
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        memcpy(x, base, sizeof x);
        call_scale_pcm(x, N, 0.9f + (float)r * 1e-9f);
    }
    t1 = now();
    report("onecore.scale_pcm", t1 - t0, (double)N * R, digest(x, sizeof x));
}

static void bench_lsf_repair(void)
{
    enum { ROWS = 100, P = 24, R = 50000 };
    float base[ROWS * P], lsf[ROWS * P];
    int i, r;
    for (i = 0; i < ROWS * P; i++) base[i] = (float)(i % P + 1) / (float)(P + 2);
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        memcpy(lsf, base, sizeof lsf);
        /* a few out-of-order rows so the sort and the refill run, as on damaged frames */
        lsf[(r % ROWS) * P + 3] = lsf[(r % ROWS) * P + 3] * 0.5f;
        call_lsf_repair(lsf, ROWS, P);
    }
    t1 = now();
    report("onecore.lsf_repair", t1 - t0, (double)ROWS * R, digest(lsf, sizeof lsf));
}

/* the integer MLPG (FUN_18000c0f0): the band window / W^T P W pass that runs for every output
 * dimension of every LSF chunk, which is the largest single piece of arithmetic on the Zira / Mark
 * path. Frames are synthetic but of the shape the engine builds them in: three 3-wide windows per
 * frame, int64 means and positive Q precisions. */
static void bench_mlpg(void)
{
    enum { T = 64, D = 24, R = 4000 };
    ZbModel m;
    ZbIG *fr[T];
    ZbIG *pool = (ZbIG *)calloc(T, sizeof(ZbIG));
    int32_t out[T * D];
    int i, j, k, r;
    memset(&m, 0, sizeof m);
    m.nwin = 3;
    for (j = 0; j < 3; j++) m.winw[j] = 3;
    /* the standard delta / delta-delta windows, as ZbModel carries them */
    m.win[0][0] = -0.5f; m.win[0][1] = 0.0f; m.win[0][2] = 0.5f;
    m.win[1][0] = -0.5f; m.win[1][1] = 0.0f; m.win[1][2] = 0.5f;
    m.win[2][0] = 1.0f; m.win[2][1] = -2.0f; m.win[2][2] = 1.0f;
    for (i = 0; i < T; i++) {
        fr[i] = &pool[i];
        for (k = 0; k < 3 * D; k++) {
            pool[i].m[k] = (int64_t)(rnd() % 200001u) - 100000;
            pool[i].p[k] = (int32_t)(1 + rnd() % 4096u);
        }
        pool[i].w = 12000;
    }
    memset(out, 0, sizeof out);
    {
        double t0 = now();
        for (r = 0; r < R; r++) {
            fr[0]->m[0] += r;   /* defeat hoisting the whole call out of the loop */
            call_imlpg(&m, fr, T, D, 1, out);
        }
        double t1 = now();
        report("onecore.mlpg", t1 - t0, (double)T * D * R, digest(out, sizeof out));
    }
    free(pool);
}

int main(void)
{
    bench_fir();
    bench_float();
    bench_int();
    bench_mlpg();
    bench_sonic();
    bench_scale_pcm();
    bench_lsf_repair();
    return 0;
}
