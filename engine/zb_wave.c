/* zb_wave.c - CWaveGenerator post-vocoder stage for SPS units (FUN_18004e460):
 *   rate change on the waveform (FUN_18004dd00: CRateChangerSonic + CWaveSmoother), voice volume (FUN_18001fedc +
 *   FUN_18002c420), per-segment SAPI volume (FUN_18006d71c), segment length redistribution (FUN_1801050c0).
 * The vocoder output is cut into the engine's synthesis blocks (FUN_180009b14 grouping), each block goes through
 * the rate stage. Only segments whose durations could not absorb the speaking rate (see zb_durations_ex, segment
 * +0x4c) are time-stretched, by Sonic (VoiceSetting.WaveScale=Sonic: David) or by CRateChanger (zb_ratechg.c: Zira,
 * Mark); the rate changer state and the previous block rate persist across utterances. */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "zb_internal.h"
#include "zb_sonic.h"
#include "zb_ratechg.h"

#define ZB_SMOOTH 240   /* CWaveSmoother length (FUN_18011287c(wavegen+0xa0, 0xf0, .., 1)) */

struct ZbWave {
    ZbSonic *sonic;     /* VoiceSetting.WaveScale=Sonic (David) */
    ZbRateChg *rc;      /* otherwise: CRateChanger (Zira, Mark) */
    int prev_rate;      /* CWaveGenerator+0x160 */
    float ramp[ZB_SMOOTH];
};

static struct ZbWave *wave_state(ZbVoice *v)
{
    if (!v->wave) {
        int k;
        v->wave = (struct ZbWave *)calloc(1, sizeof(struct ZbWave));
        if (v->cfg.wavescale_sonic) v->wave->sonic = zb_sonic_create(v->rate);
        else v->wave->rc = zb_rc_create(v->rate);
        for (k = 0; k < ZB_SMOOTH; k++) v->wave->ramp[k] = (float)(unsigned)k / (float)ZB_SMOOTH;
    }
    return v->wave;
}

void zb_wave_free(ZbVoice *v)
{
    if (v && v->wave) {
        if (v->wave->sonic) zb_sonic_free(v->wave->sonic);
        if (v->wave->rc) zb_rc_free(v->wave->rc);
        free(v->wave);
        v->wave = NULL;
    }
}

/* FUN_1800665c8 via FUN_180112904 / FUN_1801052cc; fadeout = 1: ramp down (applied to the tail of flushed Sonic
 * output), 0: ramp up (applied to the head of the next block) */
static void smooth(const struct ZbWave *w, short *s, long n, int fadeout)
{
    const unsigned N = ZB_SMOOTH;
    unsigned c, i;
    if (!n) return;
    if (fadeout && (unsigned long)n > N) { s += n - N; n = N; }
    c = (unsigned)n < N ? (unsigned)n : N;
    if (c < N) {
        float f = 1.0f;
        if (c >= 2) f = (float)(N - 1) / (float)(c - 1);
        for (i = 0; i < c; i++) {
            unsigned idx = fadeout ? c - 1 - i : i;
            s[i] = (short)(int)((float)(int)s[i] * w->ramp[(int)((float)(int)idx * f + 0.5f)]);
        }
    } else {
        for (i = 0; i < N; i++) s[i] = (short)(int)((float)(int)s[i] * w->ramp[fadeout ? N - 1 - i : i]);
    }
}

typedef struct { short *p; long n, cap; } Buf;
static void put(Buf *b, const short *x, long n)
{
    if (b->n + n > b->cap) { b->cap = (b->n + n) * 2 + 1024; b->p = (short *)realloc(b->p, sizeof(short) * (size_t)b->cap); }
    if (n) memcpy(b->p + b->n, x, sizeof(short) * (size_t)n);
    b->n += n;
}

/* FUN_180074908 */
static int round_away(float x) { return (int)(0.0f < x ? x + 0.5f : x - 0.5f); }

/* FUN_18004e254 in fine mode (wavegen+0x20c set: rates in 1/1000 steps; identical results to the coarse mode for integer
 * rates): clamp(1000*xml + segment+0x40, -10000, 10000) + 1000*site rate, plus the percent adjustments RateAdjustment
 * and the word's domain RateAdjustment converted by FUN_180067e20: round_away(log3(pct/100) * 10 * 1000), unless the
 * word is exempt (word+0x90 == 1); clamp to [-20000, 20000]. (No emotion / utterance rate object for SAPI text.) */
static int block_rate(const ZbVoice *v, const ZbPhoneCtl *c, const ZbSapi *sapi)
{
    const ZbConfig *cfg = &v->cfg;
    int r = 1000 * c->rate + c->seg_rate, site = sapi ? sapi->rate : 0;
    if (r < -10000) r = -10000;
    if (r > 10000) r = 10000;
    if (site < -10) site = -10;
    if (site > 10) site = 10;
    r += 1000 * site;
    if (c->word90 != 1) {
        /* + emotion.RateAdjustment - 100 (decompile 0x18004e2e5; see ZbStyle in zb.h) */
        int pct = zb_style_pct(cfg->rate_adj, sapi ? sapi->style.rate : 0), dom = 100, k;
        unsigned u;
        for (k = 0; c->domain[0] && k < cfg->ndomain; k++)
            if (cfg->domain[k][0] && !zb_stricmp(cfg->domain[k], c->domain)) dom = cfg->domain_rate[k];
        u = (unsigned)(dom - 100 + pct);
        if (0.0f < (float)u / 100.0f) r += round_away(((logf((float)u / 100.0f) / logf(3.0f)) * 10.0f) * 1000.0f);
    }
    if (r < -20000) r = -20000;
    if (r > 20000) r = 20000;
    return r;
}

/* FUN_18004dd00: speed = powf(3, (float)(rate / 10000.0)) (times MSTTS.SpeakRate factor and absolute rate, both 1) */
static float rate_speed(int r)
{
    if (r % 1000 == 0) return zb_pow3_tenth(r / 1000);
    return (float)pow(3.0, (double)(float)((double)r / 10000.0));
}

/* FUN_1801050c0: distribute m output samples over the block's segments (lengths len[], in samples) */
static void redistribute(long *len, int nseg, long m)
{
    long tot = 0, done = 0;
    int k;
    float scale, acc = 0.0f;
    for (k = 0; k < nseg; k++) tot += len[k];
    scale = tot ? (float)(unsigned)m / (float)(unsigned)tot : 0.0f;
    for (k = 0; k < nseg; k++) {
        long nk;
        if (k == nseg - 1) nk = m - done;
        else {
            double fl;
            acc = (float)(unsigned)len[k] * scale + acc;
            fl = floor((double)(acc + 0.5f));
            nk = (long)(unsigned)(long long)fl;
            acc = acc - (float)(unsigned)(long long)fl;
        }
        done += nk;
        len[k] = nk;
    }
}

/* ---------------------------------------------------------------- one synthesis block at a time */
/* Everything the stage carries between blocks (the rate changer or Sonic stream, the previous block rate,
 * the block flag of "first block", the input offset) lives in the run, so a block's output samples do not
 * depend on a later block existing.  zb_wave_sps() is the same loop with the block outputs concatenated. */
struct ZbWaveRun {
    ZbVoice *v;
    const ZbUtt *u;
    const ZbSapi *sapi;
    const int *dur;
    int nstate;
    const int *stretch;
    int np, i, first, cap, bi, nblk, *blk;   /* blk: the caller's block grouping (int path), NULL = computed */
    long off;
    long *seglen;
    int *fr;
    Buf o, fl;
    short *tmp;
    long tmpcap;
};

ZbWaveRun *zb_wave_begin(ZbVoice *v, const ZbUtt *u, const ZbSapi *sapi, const int *dur, int nstate, const int *stretch,
                         const int *blk, int nblk)
{
    ZbWaveRun *r = (ZbWaveRun *)calloc(1, sizeof *r);
    int p, s;
    if (!r) return NULL;
    r->v = v;
    r->u = u;
    r->sapi = sapi;
    r->dur = dur;
    r->nstate = nstate;
    r->stretch = stretch;
    r->np = u->nphone;
    r->first = 1;
    wave_state(v);   /* the rate stage state persists across utterances, as in the engine */
    r->seglen = (long *)calloc((size_t)r->np + 1, sizeof(long));
    r->fr = (int *)calloc((size_t)r->np + 1, sizeof(int));
    if (!r->seglen || !r->fr) { zb_wave_end(r); return NULL; }
    for (p = 0; p < r->np; p++) {
        for (s = 0; s < nstate; s++) r->fr[p] += dur[p * nstate + s];
        if (r->cap < r->fr[p]) r->cap = r->fr[p];
    }
    if (blk && nblk > 0) {
        r->blk = (int *)malloc(sizeof(int) * (size_t)nblk);
        if (!r->blk) { zb_wave_end(r); return NULL; }
        memcpy(r->blk, blk, sizeof(int) * (size_t)nblk);
        r->nblk = nblk;
    }
    return r;
}

/* Process the next block of pcm (the utterance buffer; the run tracks the offset).  Returns 0 at the end,
 * else 1 with the block's output in *out and its sample count in *n.  The buffer belongs to the run and is
 * valid until the next call or zb_wave_end. */
int zb_wave_next(ZbWaveRun *r, const short *pcm, short **out, long *n)
{
    ZbVoice *v;
    struct ZbWave *w;
    const ZbUtt *u;
    int shift, mix;
    int start, count, frames, flag, xr, rate, last, p;
    long nin, pos, so;
    const short *in;
    if (!r || r->i >= r->np) return 0;
    v = r->v;
    u = r->u;
    w = wave_state(v);
    shift = v->shift;
    mix = v->cfg.mix_rate_scale;
    start = r->i;
    count = 0;
    frames = 0;
    flag = r->stretch ? r->stretch[r->i] : 0;
    xr = u->ph[r->i].c.rate;
    in = pcm + r->off;
    /* FUN_180009b14 block grouping (or the caller's blocks: int path, grouped per LSF chunk) */
    if (r->blk) {
        int c = r->bi < r->nblk ? r->blk[r->bi++] : r->np - r->i;
        for (; c > 0 && r->i < r->np; c--) { frames += r->fr[r->i]; count++; r->i++; }
    } else for (;;) {
        if (frames + r->fr[r->i] > r->cap && count) break;
        frames += r->fr[r->i];
        count++;
        r->i++;
        if (r->i >= r->np) break;
        if ((r->stretch ? r->stretch[r->i] : 0) != flag || u->ph[r->i].c.rate != xr) break;
        if (r->first && count > 1) break;
    }
    nin = (long)frames * shift;
    last = r->i >= r->np;
    /* FUN_18004dd00 */
    rate = block_rate(v, &u->ph[start].c, r->sapi);
    r->fl.n = 0;
    if ((rate != w->prev_rate || flag == 0) && w->rc) {   /* CRateChanger slot 5 */
        long m = zb_rc_max_out(w->rc, 0);
        if (r->fl.cap < m + 1) { r->fl.cap = m + 1; r->fl.p = (short *)realloc(r->fl.p, sizeof(short) * (size_t)r->fl.cap); }
        r->fl.n = zb_rc_flush(w->rc, r->fl.p);
        if (mix) smooth(w, r->fl.p, r->fl.n, 1);
    } else if (rate != w->prev_rate || flag == 0) {
        zb_sonic_flush(w->sonic);
        put(&r->fl, NULL, 0);
        if (zb_sonic_available(w->sonic)) {
            long m = zb_sonic_available(w->sonic);
            if (r->fl.cap < m) { r->fl.cap = m; r->fl.p = (short *)realloc(r->fl.p, sizeof(short) * (size_t)m); }
            r->fl.n = zb_sonic_read(w->sonic, r->fl.p, (int)m);
            if (mix) smooth(w, r->fl.p, r->fl.n, 1);
        }
    }
    if (flag == 0) rate = 0;
    r->o.n = 0;
    pos = r->o.n;
    if (rate == 0) {
        if (r->fl.n == 0) put(&r->o, in, nin);          /* passthrough: segment lengths unchanged */
        else {
            put(&r->o, r->fl.p, r->fl.n);
            put(&r->o, in, nin);
            if (mix) smooth(w, r->o.p + pos + r->fl.n, nin, 0);
        }
    } else {
        long m;
        int flags = r->first | (last ? 2 : 0);
        if (w->rc) {   /* CRateChanger slots 1 and 4: the block flags go to process (bit0 reset, bit1 flush) */
            zb_rc_set_speed(w->rc, (double)rate_speed(rate));
            m = zb_rc_max_out(w->rc, (int)nin);
            if (r->tmpcap < m + 1) { r->tmpcap = m + 1; r->tmp = (short *)realloc(r->tmp, sizeof(short) * (size_t)r->tmpcap); }
            m = zb_rc_process(w->rc, in, (int)nin, r->tmp, flags);
        } else {
            zb_sonic_set_speed(w->sonic, rate_speed(rate));
            if (nin > 0) zb_sonic_write(w->sonic, in, (int)nin);
            if (flags == 2) zb_sonic_flush(w->sonic);
            m = zb_sonic_available(w->sonic);
            if (r->tmpcap < m + 1) { r->tmpcap = m + 1; r->tmp = (short *)realloc(r->tmp, sizeof(short) * (size_t)r->tmpcap); }
            m = zb_sonic_read(w->sonic, r->tmp, (int)m);
        }
        if (r->fl.n == 0) put(&r->o, r->tmp, m);
        else {
            put(&r->o, r->fl.p, r->fl.n);
            if (m == 0) { put(&r->o, in, nin); if (mix) smooth(w, r->o.p + pos + r->fl.n, nin, 0); }
            else { put(&r->o, r->tmp, m); if (mix) smooth(w, r->o.p + pos + r->fl.n, m, 0); }
        }
    }
    for (p = start; p < r->i; p++) r->seglen[p] = (long)r->fr[p] * shift;
    if (!(rate == 0 && r->fl.n == 0)) redistribute(r->seglen + start, r->i - start, r->o.n - pos);
    w->prev_rate = rate;
    r->off += nin;
    r->first = 0;
    /* voice volume (FUN_18001fedc: SpsVolumeAdjustment, the voices have NUS domain data;
     * + emotion.VolumeAdjustment - 100, decompile 0x18001ff8e) */
    {
        int vol = zb_style_pct(v->cfg.sps_volume_adj, r->sapi ? r->sapi->style.volume : 0);
        if (vol != 100) zb_scale_pcm(r->o.p, r->o.n, (float)vol / 100.0f);
    }
    /* SAPI volume per segment (FUN_18006d71c) */
    {
        int sitevol = r->sapi ? r->sapi->volume : 100;
        so = 0;
        for (p = start; p < r->i; p++) {
            if ((float)u->ph[p].c.volume != 100.0f || sitevol != 100) {
                float x = (((float)sitevol * (float)u->ph[p].c.volume) / 100.0f) / 100.0f;
                zb_scale_pcm(r->o.p + so, r->seglen[p], zb_sapi_vol_gain(x));
            }
            so += r->seglen[p];
        }
    }
    *out = r->o.p;
    *n = r->o.n;
    return 1;
}

void zb_wave_end(ZbWaveRun *r)
{
    if (!r) return;
    free(r->seglen);
    free(r->fr);
    free(r->blk);
    free(r->o.p);
    free(r->fl.p);
    free(r->tmp);
    free(r);
}

long zb_wave_sps(ZbVoice *v, const ZbUtt *u, const ZbSapi *sapi, const int *dur, int nstate, const int *stretch,
                 const int *blk, int nblk, const short *pcm, short **out)
{
    ZbWaveRun *r = zb_wave_begin(v, u, sapi, dur, nstate, stretch, blk, nblk);
    Buf all = {0, 0, 0};
    short *o;
    long n;
    if (!r) {
        *out = NULL;
        return 0;
    }
    while (zb_wave_next(r, pcm, &o, &n)) put(&all, o, n);
    zb_wave_end(r);
    *out = all.p;
    return all.n;
}
