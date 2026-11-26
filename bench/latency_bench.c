/* Time to first PCM, attributed: one sentence through the stages the JNI bridge (engine/zira_tts.c
 * speak_piece) runs, timed separately, plus the real zb_synth_cancelable call so "time until the first
 * write()" is measured end to end.  This is the host model of a device profile's speak:first_pcm.
 *
 * Engine files are compiled beside this one, except zb_synth.c and zb_vocoder.c, which are included here
 * so their static stages can be timed individually:
 *
 *   cc -std=c99 -O3 -fno-math-errno -ffp-contract=off -fno-fast-math -fno-finite-math-only \
 *      -D_DEFAULT_SOURCE -Iengine bench/latency_bench.c engine/zira_tts.c engine/zf1_*.c engine/zf2_*.c \
 *      engine/zb_apm.c engine/zb_dur.c engine/zb_mlpg.c engine/zb_io.c engine/zb_sonic.c engine/zb_wave.c \
 *      engine/zb_ratechg.c engine/zb_vocoder_int.c engine/zb_synth_int.c -lm -o /tmp/latency_bench
 *
 * Needs a real voice pack (not in this repository): /tmp/latency_bench <voice-dir> [voice] [sentence] [rounds]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "zb_internal.h"
#include "zf.h"
#include "zf2.h"
#include "zira_tts.h"

#include "zb_synth.c"
#include "zb_vocoder.c"

enum {
    ST_FIRST, ST_CALL, ST_ZF1_SPEAK, ST_ZF1_SENT, ST_ZF2, ST_LOOKUP, ST_DUR, ST_UV, ST_F0, ST_LSF,
    ST_GAIN, ST_EXPORT, ST_VOC_PREP, ST_VOC_BLK0, ST_VOC_REST, ST_VOC_TAIL, ST_WAVE, ST_N
};
static const char *const st_name[ST_N] = {
    "speak:first_pcm", "speak:call", "zf1:speak", "zf1:sentence", "zf2:features", "synth:lookup",
    "synth:durations", "synth:uv", "synth:mlpg lf0", "synth:mlpg lsf", "synth:mlpg gain",
    "synth:export+repair", "voc:prepare", "voc:block0", "voc:rest", "voc:silence", "wave:stages"
};
static double st_acc[ST_N];
static int sapi_rate;   /* engine-site rate (-10..10), so the rate stages can be exercised too */

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec;
}

#define TIME(i, expr) do { double t0_ = now_s(); (expr); st_acc[i] += now_s() - t0_; } while (0)
#define TIME_R(i, expr)                                                                       \
    ({                                                                                        \
        double t0_ = now_s();                                                                 \
        __typeof__(expr) v_ = (expr);                                                         \
        st_acc[i] += now_s() - t0_;                                                            \
        v_;                                                                                    \
    })

/* ---- PCM sink: FNV digest, so two builds can be compared sample for sample ---- */
static unsigned long long dig, dig_rep;
static long ndig, ndig_rep;
static double call_start, first_write;
static int saw_write;

static void digest(unsigned long long *d, const short *p, long n)
{
    const unsigned char *b = (const unsigned char *)p;
    long i;
    for (i = 0; i < 2 * n; i++) {
        *d ^= b[i];
        *d *= 1099511628211ull;
    }
}

/* the real engine call: note when the first block reaches the sink */
static int write_cb(void *user, const short *pcm, int n)
{
    (void)user;
    if (!saw_write) {
        saw_write = 1;
        first_write = now_s() - call_start;
    }
    digest(&dig, pcm, n);
    ndig += n;
    return 0;
}
static void stage_pcm(const short *p, long n)
{
    digest(&dig_rep, p, n);
    ndig_rep += n;
}

/* ---- the vocoder with its internal stages timed (same code as zb_vocoder_float's one-call pass) ---- */
static void voc_timed(ZbVoice *v, const ZbUtt *u, const int *dur, int nstate, int T, const float *f0, float *lsf,
                      const float *gain, const int *stretch, short *pcm)
{
    const ZbConfig *cfg = &v->cfg;
    const int p = 24, shift = v->shift, n = T * shift;
    float *exc, hist[64];
    ZbExcState st;
    int r, *phfr, *blk, nb, b, ph, cap = 0, f = 0;
    double t0 = now_s();
    if (20 < T) {
        for (r = 0; r < 10; r++) memcpy(lsf + r * p, lsf + 10 * p, sizeof(float) * p);
        for (r = T - 10; r < T; r++) memcpy(lsf + r * p, lsf + (T - 11) * p, sizeof(float) * p);
    } else {
        memset(lsf, 0, sizeof(float) * (size_t)T * p);
        memset((float *)gain, 0, sizeof(float) * (size_t)T);
    }
    for (r = 0; r < T; r++) lsf_sharpen(cfg, lsf + r * p, p);
    zb_lsf_repair(lsf, T, p);
    st_acc[ST_VOC_PREP] += now_s() - t0;
    exc = (float *)malloc(sizeof(float) * (size_t)(n ? n : 1));
    memset(&st, 0, sizeof st);
    memset(hist, 0, sizeof hist);
    phfr = (int *)calloc((size_t)u->nphone + 1, sizeof(int));
    blk = (int *)calloc((size_t)u->nphone + 1, sizeof(int));
    for (ph = 0; ph < u->nphone; ph++) {
        for (r = 0; r < nstate; r++) phfr[ph] += dur[ph * nstate + r];
        if (cap < phfr[ph]) cap = phfr[ph];
    }
    nb = zb_split_blocks(u, phfr, 0, u->nphone, stretch, cap, blk);
    for (b = 0, ph = 0; b < nb; b++) {
        int nf = 0, k;
        for (k = 0; k < blk[b]; k++) nf += phfr[ph + k];
        ph += blk[b];
        t0 = now_s();
        excitation(v, &st, f0 + f, nf, nf * shift, shift, exc + (size_t)f * shift);
        synth(lsf + (size_t)f * p, p, gain + f, exc + (size_t)f * shift, nf, shift, hist, pcm + (size_t)f * shift);
        st_acc[b == 0 ? ST_VOC_BLK0 : ST_VOC_REST] += now_s() - t0;
        f += nf;
    }
    free(phfr);
    free(blk);
    free(exc);
    TIME(ST_VOC_TAIL, zb_silence_zero(v, u, dur, nstate, pcm));
}

/* one sentence: the real call's time to first PCM, then the same stages timed one by one */
static void run_sentence(ZbVoice *vb, zf2_voice *vf, zf1_engine *e, const char *text, int xml)
{
    zf_sentence s;
    ZbUtt u;
    ZbSapi sapi;
    ZbUttModels um;
    int ns, nst, T = 0, k, t, nv;
    int *vuv = NULL;
    float *w = NULL, *lf0 = NULL, *lsf = NULL, *gain = NULL, *f0c = NULL;
    ZbGauss **fr = NULL, *pool = NULL;
    short *pcm = NULL, *wav = NULL;
    int *stretch = NULL;
    long nwav = 0;
    const int D = 24;
    double fe0, fe;

    memset(&s, 0, sizeof s);
    memset(&u, 0, sizeof u);
    memset(&sapi, 0, sizeof sapi);
    sapi.rate = sapi_rate;
    sapi.volume = 100;
    memset(&um, 0, sizeof um);

    fe0 = st_acc[ST_ZF1_SPEAK] + st_acc[ST_ZF1_SENT] + st_acc[ST_ZF2];
    TIME(ST_ZF1_SPEAK, zf1_speak_utf8(e, text, xml));
    if (TIME_R(ST_ZF1_SENT, zf1_next_sentence(e, &s)) != 1) { printf("no sentence\n"); exit(1); }
    if (TIME_R(ST_ZF2, zf2_process(vf, &s, &u, NULL)) != 0) { printf("zf2 failed\n"); exit(1); }
    fe = st_acc[ST_ZF1_SPEAK] + st_acc[ST_ZF1_SENT] + st_acc[ST_ZF2] - fe0;

    /* the real engine call: first PCM = frontend + whatever the call did before its first write */
    {
        double t0 = now_s(), spent;
        int rc;
        call_start = t0;
        saw_write = 0;
        rc = zb_synth_cancelable(vb, &u, &sapi, write_cb, NULL, NULL, NULL);
        spent = now_s() - t0;
        st_acc[ST_CALL] += spent;
        st_acc[ST_FIRST] += fe + (saw_write ? first_write : spent);
        if (rc) { printf("synthesis failed rc=%d\n", rc); exit(1); }
    }

    /* the same stages of the one-call pipeline, timed one by one */
    k = TIME_R(ST_LOOKUP, lookup(vb, &u, &um, NULL));
    if (k) { printf("lookup %d\n", k); exit(1); }
    ns = um.nst;
    nst = u.nphone * ns;
    um.dur = (int *)calloc((size_t)nst, sizeof(int));
    stretch = (int *)calloc((size_t)u.nphone + 1, sizeof(int));
    TIME(ST_DUR, zb_durations_ex(vb, &u, &sapi, um.gdur, um.gpdur, ns, um.dur, stretch));
    for (k = 0; k < nst; k++) T += um.dur[k];

    {
        double t0 = now_s();
        w = (float *)malloc(sizeof(float) * (size_t)nst);
        for (k = 0; k < nst; k++) w[k] = um.glf0[k].wmax;
        vuv = (int *)calloc((size_t)T + 1, sizeof(int));
        if (vb->cfg.uv_frame_level) uv_frame_level(w, um.dur, nst, vb->cfg.voiced_thr, vuv);
        else uv_state_level(w, um.dur, nst, vb->cfg.voiced_thr, vuv);
        st_acc[ST_UV] += now_s() - t0;
    }

    fr = (ZbGauss **)malloc(sizeof(ZbGauss *) * (size_t)(T + 1));
    pool = (ZbGauss *)malloc(sizeof(ZbGauss) * (size_t)(T + 1));
    lf0 = (float *)calloc((size_t)T + 1, sizeof(float));
    f0c = (float *)calloc((size_t)T + 1, sizeof(float));
    nv = build_frames(vb->m_lf0, um.glf0, um.dur, nst, T, vuv, 1, fr, pool);
    TIME(ST_F0, zb_mlpg_float(vb->m_lf0, fr, nv, 1, f0c));
    {
        double t0 = now_s();
        for (t = 0, k = 0; t < T; t++) lf0[t] = vuv[t] ? f0c[k++] : 0.0f;
        if (vb->m_lf0->has_gen) {
            f0_fir(lf0, T, vb->m_lf0->genw, vb->m_lf0->genlen);
            for (t = 0; t < T; t++)
                if (lf0[t] != 0.0f) lf0[t] = (lf0[t] - vb->m_lf0->gen_mean) * vb->m_lf0->gen_enhance + vb->m_lf0->gen_mean;
        }
        st_acc[ST_F0] += now_s() - t0;
    }

    lsf = (float *)calloc((size_t)T * D + 1, sizeof(float));
    gain = (float *)calloc((size_t)T + 1, sizeof(float));
    k = build_frames(vb->m_lsf, um.glsf, um.dur, nst, T, NULL, D, fr, pool);
    TIME(ST_LSF, zb_mlpg_float(vb->m_lsf, fr, k, D, lsf));
    k = build_frames(vb->m_lsf, um.ggain, um.dur, nst, T, NULL, 1, fr, pool);
    TIME(ST_GAIN, zb_mlpg_float(vb->m_lsf, fr, k, 1, gain));

    {
        double t0 = now_s();
        for (t = 0; t < T; t++)
            if (lf0[t] != 0.0f) lf0[t] = (float)exp((double)lf0[t]);
        zb_lsf_repair(lsf, T, D);
        apply_pitch(vb, &u, &sapi, um.dur, ns, lf0, T);
        st_acc[ST_EXPORT] += now_s() - t0;
    }

    pcm = (short *)calloc((size_t)T * vb->shift + 1, sizeof(short));
    voc_timed(vb, &u, um.dur, ns, T, lf0, lsf, gain, stretch, pcm);
    nwav = TIME_R(ST_WAVE, zb_wave_sps(vb, &u, &sapi, um.dur, ns, stretch, NULL, 0, pcm, &wav));
    if (nwav) stage_pcm(wav, nwav);
    printf("sentence: %d phones, %d frames, %ld samples\n", u.nphone, T, nwav);

    free(um.gdur); free(um.gpdur); free(um.glf0); free(um.glsf); free(um.ggain); free(um.dur);
    free(w); free(vuv); free(lf0); free(f0c); free(lsf); free(gain); free(fr); free(pool); free(pcm); free(wav);
    free(stretch);
    zf2_utt_free(&u);
    zf_sentence_free(&s);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "/tmp/v";
    const char *voice = argc > 2 ? argv[2] : "David";
    const char *text = argc > 3 ? argv[3] : "The quick brown fox jumps over the lazy dog while the band plays a long, steady tune.";
    int rounds = argc > 4 ? atoi(argv[4]) : 20;
    char err[256], vpath[1200], dat[1200];
    ZbVoice *vb;
    zf2_voice *vf;
    zf1_engine *e;
    double load = 0.0, tot = 0.0;
    int r, i;

    sapi_rate = argc > 5 ? atoi(argv[5]) : 0;
    err[0] = 0;
    snprintf(vpath, sizeof vpath, "%s/M1033%s", dir, voice);
    snprintf(dat, sizeof dat, "%s/MSTTSLocEnUS.dat", dir);
    {
        double t0 = now_s();
        vb = zb_voice_load(vpath, err, sizeof err);
        if (!vb) { printf("voice load: %s\n", err); return 1; }
        vf = zf2_voice_load(vpath);
        e = zf1_open(dat);
        if (!vf || !e) { printf("frontend load failed\n"); return 1; }
        zf2_voice_set_phone_set(vf, zb_voice_phone_set(vb));
        load = now_s() - t0;
    }

    for (r = 0; r < rounds; r++) run_sentence(vb, vf, e, text, 0);

    printf("\n%-24s %12s\n", "stage", "us/sentence");
    for (i = 0; i < ST_N; i++) {
        st_acc[i] = st_acc[i] / rounds * 1e6;
        if (i != ST_FIRST && i != ST_CALL) tot += st_acc[i];
    }
    for (i = 0; i < ST_N; i++) printf("%-24s %12.1f\n", st_name[i], st_acc[i]);
    printf("%-24s %12.1f\n", "stages total (no first/call)", tot);
    printf("\ndigest %016llx samples %ld | stages digest %016llx samples %ld | load %.1f ms\n", dig, ndig, dig_rep,
           ndig_rep, load * 1e3);
    printf("first_pcm %.2f ms  call %.2f ms  -> %.2fx between them\n", st_acc[ST_FIRST] / 1e3,
           st_acc[ST_CALL] / 1e3, st_acc[ST_FIRST] > 0.0 ? st_acc[ST_CALL] / st_acc[ST_FIRST] : 0.0);
    zf1_close(e);
    zf2_voice_free(vf);
    zb_voice_free(vb);
    return 0;
}
