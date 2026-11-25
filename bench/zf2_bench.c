/* OneCore frontend benchmark: zf2_features() over a synthetic sentence (word/syllable/phone tree
 * built here, phrase and intonation-phrase layers from z2_build_phrases, as
 * engine/tests/zf2_word_position_host_test.c does). The tree walk and the feature getters are
 * integer code, so this measures the part of the frontend that runs once per phone per utterance.
 *
 * cc -std=c99 -O3 -ffp-contract=off -fno-math-errno -Iengine bench/zf2_bench.c \
 *    engine/zf2_feat.c engine/zf2_tree.c engine/zf2_units.c engine/zb_apm.c -lm -o zf2_bench */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "zb.h"
#include "zf2.h"
#include "zf2_int.h"

enum { NW = 120, NS = NW, NP = 2 * NW, ROUNDS = 300 };

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

static void build(Z2Sent *s, Z2W *w, Z2S *syl, Z2P *phone, zf2_voice *v)
{
    int i;
    memset(s, 0, sizeof *s);
    memset(v, 0, sizeof *v);
    v->short_pause = -1;
    s->v = v;
    s->w = w;
    s->nw = NW;
    s->s = syl;
    s->ns = NS;
    s->p = phone;
    s->np = NP;
    for (i = 0; i < NW; i++) {
        w[i].frag = -1;
        w[i].type = (i == 0 || i == NW - 1) ? 3 : 0;
        w[i].npron = 1;
        w[i].ns = 1;
        w[i].s0 = i;
        w[i].bi = (i == 0 || i == NW - 1) ? 5 : (i % 7 == 3) ? 4 : (i % 5 == 0) ? 1 : 2;
        syl[i].word = i;
        syl[i].p0 = 2 * i;
        syl[i].np = 2;
        syl[i].tobi = i % 3;
        phone[2 * i].syl = phone[2 * i + 1].syl = i;
        phone[2 * i].id = (i == 0 || i == NW - 1) ? 3 : 7 + (i % 11);
        phone[2 * i + 1].id = (i == 0 || i == NW - 1) ? 3 : 20 + (i % 9);
        phone[2 * i].st = phone[2 * i + 1].st = 1 + (i & 1);
        phone[2 * i].tone = phone[2 * i + 1].tone = i % 4;
    }
}

int main(void)
{
    static Z2W w[NW];
    static Z2S syl[NS];
    static Z2P phone[NP];
    static zf2_voice v;
    Z2Sent s;
    ZbUtt u = {0};
    int r, i, f;
    double t0, t1;
    uint64_t d = 0;

    build(&s, w, syl, phone, &v);
    z2_build_phrases(&s, 1);

    /* warm the cached feature paths and the allocator */
    if (zf2_features(&s, &u) != 0) return 1;
    for (i = 0; i < u.nphone; i++)
        for (f = 0; f < ZB_NFEAT; f++) d = d * 31u + (uint64_t)u.ph[i].f[f];
    zf2_utt_free(&u);

    t0 = now();
    for (r = 0; r < ROUNDS; r++) {
        ZbUtt out = {0};
        if (zf2_features(&s, &out) != 0) return 1;
        for (i = 0; i < out.nphone; i++)
            for (f = 0; f < ZB_NFEAT; f++) d = d * 31u + (uint64_t)out.ph[i].f[f];
        zf2_utt_free(&out);
    }
    t1 = now();
    printf("zf2.features %.9f %.0f %016llx\n", t1 - t0, (double)NP * ROUNDS, (unsigned long long)digest(&d, sizeof d));
    return 0;
}
