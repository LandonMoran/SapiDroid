/* SAPI 4 voice effects (sam4fx.c): the allpass chain and FIR the "in Hall"/"in Stadium"/RoboSoft
 * voices run over every sample. Separate program because sam4fx.c is its own translation unit. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* sam4fx.c is force-included by bench/run.sh with -include. */
#include "sam4fx.h"

static uint32_t rng_state = 0x51ed270bu;
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

static void bench_hall(void)
{
    enum { N = 2048, R = 2000 };
    float x[N], base[N];
    int i, r;
    for (i = 0; i < N; i++) base[i] = rf() * 8000.0f;
    double t0, t1;
    sam4fx *fx = sam4fx_new(SAM4FX_HALL, 16000);
    const float taps[3] = {0.25f, -0.5f, 0.25f};
    sam4fx_set_fir(fx, taps, 3);
    t0 = now();
    for (r = 0; r < R; r++) {
        memcpy(x, base, sizeof x);
        x[0] += (float)r * 1e-9f;
        sam4fx_process(fx, x, N);
    }
    t1 = now();
    report("s4fx.hall", t1 - t0, (double)N * R, digest(x, sizeof x));
    sam4fx_free(fx);
}

static void bench_s16(void)
{
    enum { N = 2048, R = 2000 };
    int16_t x[N], base[N];
    int i, r;
    for (i = 0; i < N; i++) base[i] = (int16_t)((int)(rnd() % 16000) - 8000);
    double t0, t1;
    sam4fx *fx = sam4fx_new(SAM4FX_ROOM, 16000);
    t0 = now();
    for (r = 0; r < R; r++) {
        memcpy(x, base, sizeof x);
        x[0] = (int16_t)(x[0] + r);
        sam4fx_process_s16(fx, x, N);
    }
    t1 = now();
    report("s4fx.room_s16", t1 - t0, (double)N * R, digest(x, sizeof x));
    sam4fx_free(fx);
}

int main(void)
{
    bench_hall();
    bench_s16();
    return 0;
}
