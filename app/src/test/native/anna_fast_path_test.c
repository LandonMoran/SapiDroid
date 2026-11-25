#include "../../main/cpp/anna/anna_tts.c"
#include <assert.h>
#include <pthread.h>

static int sentences, expected_events, events, audio_count, stop_audio;
static volatile int block_render, render_entered, release_render;
static anna_tts *cancel_target;
static int16_t output[4];
static anna_token token;

anna_norm *anna_norm_new(const char *text, int flags) {
    (void)text; (void)flags;
    sentences = 0;
    return (anna_norm *)&token;
}
void anna_norm_free(anna_norm *nm) { (void)nm; }
int anna_norm_sentence(anna_norm *nm, const anna_token **toks, int *ntok) {
    (void)nm;
    *toks = &token;
    *ntok = 1;
    return sentences++ == 0;
}
void anna_mid_reset(anna_mid *mid) { (void)mid; }
int anna_mid_sentence(anna_mid *mid, const anna_token *tokens, int n, anna_word *out, int max) {
    (void)mid; (void)tokens; (void)n; (void)out; (void)max;
    return 1;
}
int anna_words_to_units(const anna_udt *udt, const anna_word *words, int n, anna_unitspec *out, anna_unitinfo *info, int max) {
    (void)udt; (void)words; (void)n; (void)out; (void)max;
    info[1].ctrl = 1;
    info[1].src_pos = 0;
    info[1].src_len = 3;
    return 2;
}
int anna_render_sentence_ex(anna_voice *voice, const anna_unitspec *specs, int n,
                            const anna_render_opts *opt, anna_pcm_cb cb, void *user) {
    (void)voice; (void)specs; (void)n;
    assert((opt->item_cb != NULL) == expected_events);
    if (__atomic_load_n(&block_render, __ATOMIC_RELAXED)) {
        __atomic_store_n(&render_entered, 1, __ATOMIC_RELAXED);
        while (!__atomic_load_n(&release_render, __ATOMIC_RELAXED)) {}
        return anna_cancel_requested(opt->cancel) ? 1 : 0;
    }
    if (opt->item_cb) opt->item_cb(opt->user, 1, 0);
    const int16_t pcm[] = { -100, -1, 0, 100 };
    cb(pcm, 4, user);
    return 0;
}
static int audio(const int16_t *pcm, size_t n, void *user) {
    (void)user;
    assert(n == 4);
    memcpy(output, pcm, sizeof output);
    audio_count++;
    return stop_audio;
}
static void event(const anna_event *ev, void *user) {
    (void)user;
    if (ev->type == ANNA_EV_WORD) {
        assert(ev->text_pos == 0 && ev->text_len == 3);
        events++;
    }
}

static void *cancel_while_rendering(void *unused) {
    (void)unused;
    while (!__atomic_load_n(&render_entered, __ATOMIC_RELAXED)) {}
    anna_tts_cancel(cancel_target);
    __atomic_store_n(&release_render, 1, __ATOMIC_RELAXED);
    return NULL;
}

static int cancelled_at_start(void *arg) {
    anna_tts *t = arg;
    assert(!anna_cancel_requested(&t->cancel));
    return 1;
}
static int cancel_during_check(void *arg) {
    anna_tts_cancel(arg);
    return 0;
}

int main(void) {
    anna_unitinfo info[2] = {{0}};
    anna_tts t = {0};
    anna_callbacks cb = { audio, NULL, NULL };
    pthread_t canceller;
    const int16_t pcm[] = { -100, -1, 0, 100 };
    t.info = info;
    anna_tts_cancel(&t);
    assert(anna_tts_speak_checked(&t, "abc", 0, &cb, cancelled_at_start, &t) == 1);
    assert(audio_count == 0 && anna_cancel_requested(&t.cancel));
    assert(anna_tts_speak_checked(&t, "abc", 0, &cb, cancel_during_check, &t) == 1);
    assert(audio_count == 0 && anna_cancel_requested(&t.cancel));
    assert(anna_tts_speak(&t, "abc", 0, &cb) == 0);
    assert(audio_count == 1 && memcmp(output, pcm, sizeof pcm) == 0);
    expected_events = 1;
    cb.event = event;
    assert(anna_tts_speak(&t, "abc", 0, &cb) == 0);
    assert(events == 1 && audio_count == 2 && memcmp(output, pcm, sizeof pcm) == 0);
    expected_events = 0;
    cb.event = NULL;
    stop_audio = 1;
    assert(anna_tts_speak(&t, "abc", 0, &cb) == 1);
    stop_audio = 0;
    block_render = 1;
    cancel_target = &t;
    assert(pthread_create(&canceller, NULL, cancel_while_rendering, NULL) == 0);
    assert(anna_tts_speak(&t, "abc", 0, &cb) == 1);
    assert(pthread_join(canceller, NULL) == 0);
    return 0;
}
