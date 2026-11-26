#include "../../../../engine/zira_tts.c"
#include <assert.h>

static int sentences, expected_events, event_count, audio_count, stop_audio;
static short output[8];

int zf1_speak_utf8(zf1_engine *e, const char *text, int xml) {
    (void)e; (void)text; (void)xml;
    sentences = 0;
    return 0;
}
int zf1_next_sentence(zf1_engine *e, zf_sentence *out) {
    (void)e; (void)out;
    return sentences++ == 0;
}
/* The neural voices' back end (eva_voice.c) and the sentence tree its feature paths need: not part of this test. */
EvaModel *eva_model_open(const char *vp, int lcid, zf2_voice *vf, char *err, int errlen) {
    (void)vp; (void)lcid; (void)vf; (void)err; (void)errlen;
    return NULL;
}
void eva_model_close(EvaModel *m) { (void)m; }
int eva_synth(EvaModel *m, ZbVoice *vb, const zf2_sent *s, const ZbUtt *u, const ZbSapi *sapi, zb_write_fn write,
              void *user, ZbTrace *trace) {
    (void)m; (void)vb; (void)s; (void)u; (void)sapi; (void)write; (void)user; (void)trace;
    return 0;
}
int zb_voice_has_acoustics(const ZbVoice *v) { (void)v; return 1; }
zf2_sent *zf2_run(const zf2_voice *v, const zf_sentence *in) { (void)v; (void)in; return NULL; }
int zf2_features(const zf2_sent *s, ZbUtt *u) { (void)s; (void)u; return -1; }
int zf2_make_events(const zf2_sent *s, zf2_events *ev) { (void)s; (void)ev; return -1; }
void zf2_sent_free(zf2_sent *s) { (void)s; }

int zf2_process(const zf2_voice *v, const zf_sentence *in, ZbUtt *out, zf2_events *ev) {
    (void)v; (void)in;
    assert((ev != NULL) == expected_events);
    out->nphone = 1;
    if (ev) {
        ev->n = 1;
        ev->ev = calloc(1, sizeof(*ev->ev));
        ev->ev[0].id = ZF2_EV_WORD;
        ev->ev[0].wparam = 3;
    }
    return 0;
}
int zb_emotion(const ZbVoice *v, const char *name, ZbStyle *out) {
    (void)v; (void)name; (void)out;
    return 0;
}
int zb_synth_cancelable(ZbVoice *v, const ZbUtt *u, const ZbSapi *sapi, zb_write_fn write, void *user,
                        ZbTrace *trace, const volatile int *cancel) {
    (void)v; (void)u; (void)sapi;
    assert(cancel != NULL);
    assert((trace != NULL) == expected_events);
    if (trace) {
        trace->nphone = trace->nstate = trace->nframe = 1;
        trace->dur = malloc(sizeof(*trace->dur));
        trace->dur[0] = 1;
    }
    const short pcm[] = { -32768, -1, 0, 1, 32767 };
    return write(user, pcm, 5);
}
void zb_trace_free(ZbTrace *tr) { free(tr->dur); }
void zf2_utt_free(ZbUtt *u) { (void)u; }
void zf2_events_free(zf2_events *ev) { free(ev->ev); }
void zf_sentence_free(zf_sentence *s) { (void)s; }

static int audio(const int16_t *pcm, size_t n, void *user) {
    (void)user;
    for (size_t i = 0; i < n; i++) output[audio_count++] = pcm[i];
    return stop_audio;
}
static void event(const zira_event *ev, void *user) {
    (void)user;
    assert(ev->type == ZIRA_EV_WORD && ev->text_pos == 0 && ev->text_len == 3);
    event_count++;
}

int main(void) {
    zira_tts t = {0};
    zira_callbacks cb = { audio, NULL, NULL };
    speak_ctx c = {0};
    const short expected[] = { -32768, -1, 0, 1, 32767 };
    c.t = &t;
    c.cb = &cb;
    assert(speak_piece(&t, &c, "abc", 0, NULL, NULL) == 0);
    assert(audio_count == 5 && memcmp(output, expected, sizeof(expected)) == 0);
    expected_events = 1;
    cb.event = event;
    audio_count = 0;
    assert(speak_piece(&t, &c, "abc", 0, NULL, NULL) == 0);
    assert(event_count == 1 && audio_count == 5 && memcmp(output, expected, sizeof(expected)) == 0);
    expected_events = 0;
    cb.event = NULL;
    audio_count = 0;
    stop_audio = 1;
    assert(speak_piece(&t, &c, "abc", 0, NULL, NULL) == 0 && c.stopped && t.cancel);
    return 0;
}
