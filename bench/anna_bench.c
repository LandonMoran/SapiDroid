/* Anna (WMA Voice decoder) kernel benchmark: every numeric kernel the decoder runs per frame —
 * LSP decode/stabilise/poly, the adaptive and fixed codebooks, the synthesis filter, its FIR,
 * the FFTs, both post-filters, the AGC ratio, the transcendental emulations and the bit reader.
 * Compiled against this repository and against the original SapiDroid checkout by bench/run.sh. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* anna_dec_wmav.c is force-included by bench/run.sh with -include. */
#include "anna.h"

static void (*volatile call_synth16)(float *, const float *, const float *, float *, int) = synth16;
static void (*volatile call_fir16)(float *, const float *, const float *, float *, int) = fir16;
static float (*volatile call_dot)(const float *, const float *, int) = x87_dot;
static void (*volatile call_fft64)(float *, float *) = fft64;
static void (*volatile call_rfft128)(float *) = rfft128;
static void (*volatile call_irfft128)(float *) = irfft128;
static uint32_t (*volatile call_getbits)(const uint8_t *, uint32_t, int) = getbits;
static float (*volatile call_logf)(float) = wv_logf;
static float (*volatile call_expf)(float) = wv_expf;
static float (*volatile call_cosf)(float) = wv_cosf;
static float (*volatile call_powf)(float, float) = wv_powf;
static void (*volatile call_stab)(float *) = lsp_stabilize;
static void (*volatile call_poly)(const float *, float *, int) = lsp_poly;
static void (*volatile call_vq)(float *, const uint8_t *, int, const int *, const int16_t *, int, const float *,
                                const float *) = vq_add;
static void (*volatile call_acb)(float *, int, float *, float) = acb_vector;
static void (*volatile call_pulse)(int16_t, int, int, int16_t *, int16_t *) = pulse_pos;
static void (*volatile call_fcb)(int, int, float *, const int16_t *) = fcb_pulses;
static void (*volatile call_pitchpf)(float *, const float *, float *, int, int) = pitch_postfilter;
static float (*volatile call_agc)(const float *, const float *, int) = agc_ratio;
static void (*volatile call_specpf)(const float *, float *, const float *, float *, int, int) = spectral_postfilter;

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

/* ------------------------------------------------------------------ LSP decoding */
static void bench_stabilize(void)
{
    enum { R = 400000 };
    float lsp[WV_ORDER], base[WV_ORDER];
    int i, r;
    for (i = 0; i < WV_ORDER; i++) base[i] = (float)(i + 1) / (float)(2 * WV_ORDER + 2);
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        memcpy(lsp, base, sizeof lsp);
        /* put one pair out of order so the insertion sort runs, as it does on bad frames */
        lsp[5] -= 0.004f * (float)(r & 1);
        call_stab(lsp);
    }
    t1 = now();
    report("anna.lsp_stabilize", t1 - t0, R, digest(lsp, sizeof lsp));
}

static void bench_poly(void)
{
    enum { R = 400000 };
    float c[8], p[8];
    int i, r;
    for (i = 0; i < 8; i++) c[i] = rf() * 0.9f;
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        c[0] += (float)r * 1e-12f;
        call_poly(c, p, 8);
    }
    t1 = now();
    report("anna.lsp_poly8", t1 - t0, R, digest(p, sizeof p));
}

static void bench_vq(void)
{
    enum { NST = 3, DIM = 16, R = 400000 };
    uint8_t cb[3 * 256 * DIM];
    int sizes[NST], i, r;
    int16_t idx[NST];
    float scale[NST], offs[NST], out[DIM];
    for (i = 0; i < (int)sizeof cb; i++) cb[i] = (uint8_t)(i * 29 + 7);
    sizes[0] = 256;
    sizes[1] = 64;
    sizes[2] = 128;
    for (i = 0; i < NST; i++) {
        idx[i] = (int16_t)(i * 17);
        scale[i] = 0.5f + 0.1f * (float)i;
        offs[i] = -0.2f * (float)i;
    }
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        idx[0] = (int16_t)(r & 255);
        call_vq(out, cb, NST, sizes, idx, DIM, scale, offs);
    }
    t1 = now();
    report("anna.vq_add", t1 - t0, R, digest(out, sizeof out));
}

/* ------------------------------------------------------------------ codebooks and pulses */
static void bench_acb(void)
{
    enum { NB = 4096, N = WV_FRAME, R = 60000 };
    float buf[NB], out[N], base[NB];
    float *src = buf + 1024;
    int i, r;
    for (i = 0; i < NB; i++) base[i] = rf() * 0.1f;
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        memcpy(buf, base, sizeof buf);
        buf[1024] += (float)r * 1e-9f;
        call_acb(src, N, out, 95.5f);
    }
    t1 = now();
    report("anna.acb_vector", t1 - t0, (double)N * R, digest(out, sizeof out));
}

static void bench_pulses(void)
{
    enum { R = 400000 };
    int16_t pos[2], sign[2], codes[5];
    float out[WV_FRAME];
    int i, r;
    for (i = 0; i < 5; i++) codes[i] = (int16_t)(i * 977 + 13);
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        codes[0] = (int16_t)(r & 0x7fff);
        call_pulse(codes[0], 32, 2, pos, sign);
        call_fcb(7, WV_FRAME, out, codes);
    }
    t1 = now();
    report("anna.fcb_pulses", t1 - t0, R, digest(out, sizeof out) ^ digest(pos, sizeof pos));
}

/* ------------------------------------------------------------------ synthesis filters */
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

/* ------------------------------------------------------------------ post-filters */
static void bench_pitchpf(void)
{
    enum { ST = 768, R = 20000 };
    float out[WV_FRAME], lpc[WV_ORDER], st[ST], zero[ST], in[WV_FRAME];
    int i, r;
    for (i = 0; i < WV_ORDER; i++) lpc[i] = (float)(((i % 3) - 1) * 0.02);
    for (i = 0; i < WV_FRAME; i++) in[i] = rf() * 0.05f;
    memset(zero, 0, sizeof zero);
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        memcpy(st, zero, sizeof st);
        /* the subframe's excitation sits where the decoder leaves it */
        for (i = 0; i < WV_FRAME; i++) st[40 + 296 + i] = in[i] + (float)r * 1e-9f;
        call_pitchpf(out, lpc, st, 95, WV_FRAME);
    }
    t1 = now();
    report("anna.pitch_postfilter", t1 - t0, (double)WV_FRAME * R, digest(out, sizeof out));
}

static void bench_agc(void)
{
    enum { N = WV_FRAME, R = 400000 };
    float a[N], b[N];
    float acc = 0.0f;
    int i, r;
    for (i = 0; i < N; i++) {
        a[i] = rf();
        b[i] = rf() * 0.5f;
    }
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        a[0] += (float)r * 1e-9f;
        acc += call_agc(a, b, N);
    }
    t1 = now();
    report("anna.agc_ratio", t1 - t0, (double)N * R, digest(&acc, sizeof acc));
}

static void bench_specpf(void)
{
    enum { ST = 704, R = 20000 };
    float ref[WV_FRAME], out[WV_FRAME], lpc[WV_ORDER], st[ST], zero[ST];
    int i, r;
    for (i = 0; i < WV_ORDER; i++) lpc[i] = (float)(((i % 3) - 1) * 0.02);
    for (i = 0; i < WV_FRAME; i++) ref[i] = rf() * 0.05f;
    memset(zero, 0, sizeof zero);
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        memcpy(st, zero, sizeof st);
        memcpy(out, ref, sizeof out);
        out[0] += (float)r * 1e-9f;
        call_specpf(ref, out, lpc, st, WV_FRAME, 6);
    }
    t1 = now();
    report("anna.spectral_postfilter", t1 - t0, (double)WV_FRAME * R, digest(out, sizeof out));
}

/* ------------------------------------------------------------------ transforms */
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

static void bench_irfft(void)
{
    enum { R = 60000 };
    float x[130], bx[130];
    int i, r;
    for (i = 0; i < 130; i++) bx[i] = rf();
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        memcpy(x, bx, sizeof x);
        x[0] += (float)r * 1e-9f;
        call_irfft128(x);
    }
    t1 = now();
    report("anna.irfft128", t1 - t0, R, digest(x, sizeof x));
}

/* ------------------------------------------------------------------ transcendental emulations */
static void bench_math(void)
{
    enum { N = 512, R = 20000 };
    float v[N], acc = 0.0f;
    int i, r;
    for (i = 0; i < N; i++) v[i] = (float)(0.001 + 3.9 * (double)(i + 1) / N);
    double t0, t1;
    t0 = now();
    for (r = 0; r < R; r++) {
        v[0] += (float)r * 1e-12f;
        for (i = 0; i < N; i++) {
            acc += call_logf(v[i]);
            acc += call_expf(v[i] * 0.01f - 1.0f);
            acc += call_cosf(v[i]);
            acc += call_powf(v[i] / 4.0f, 0.3f);
        }
    }
    t1 = now();
    report("anna.logf+expf+cosf+powf", t1 - t0, (double)N * R * 4, digest(&acc, sizeof acc));
}

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

int main(void)
{
    bench_getbits();
    bench_stabilize();
    bench_poly();
    bench_vq();
    bench_acb();
    bench_pulses();
    bench_synth16();
    bench_fir16();
    bench_dot();
    bench_pitchpf();
    bench_agc();
    bench_specpf();
    bench_fft();
    bench_irfft();
    bench_math();
    return 0;
}
