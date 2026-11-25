#include <stdlib.h>

static int fail_alloc;
static void *test_malloc(size_t n) { return fail_alloc ? NULL : malloc(n); }
static void *test_calloc(size_t n, size_t size) { return fail_alloc ? NULL : calloc(n, size); }
static void *test_realloc(void *ptr, size_t n) { return fail_alloc ? NULL : realloc(ptr, n); }
#define malloc test_malloc
#define calloc test_calloc
#define realloc test_realloc
#include "../../main/cpp/sapi5/sam_tts.c"
#undef malloc
#undef calloc
#undef realloc
#include <assert.h>

static int calls, stop_audio, expected_events, events;
static int word_pos, word_len = 3, expected_pos, expected_len = 3;
static int16_t received[4];
static const char *const *expected_runs;
static int run_index;

void sam4fx_process_s16(sam4fx *fx, int16_t *pcm, int n) {
    (void)fx;
    for (int i = 0; i < n; i++) pcm[i]++;
}
sam4fx *sam4fx_new(int preset, int rate) { (void)preset; (void)rate; return NULL; }
int sam4fx_set_fir(sam4fx *fx, const float *taps, int n) { (void)fx; (void)taps; (void)n; return 0; }
void sam4fx_set_input_gain(sam4fx *fx, float gain) { (void)fx; (void)gain; }
float sam4fx_default_trim_db(int preset) { (void)preset; return 0; }
void sam4fx_free(sam4fx *fx) { (void)fx; }
int sam_tts_speak_ex(sam_tts *t, const char *text, const sam_speak_opts *opts, sam_pcm_cb cb, void *user) {
    (void)t; (void)text;
    assert((opts->mark_cb != NULL) == expected_events);
    if (expected_runs) assert(strcmp(text, expected_runs[run_index++]) == 0);
    if (opts->mark_cb) {
        sam_mark mark = {1, word_pos, word_len, 0, 3, 0};
        opts->mark_cb(opts->user, &mark);
    }
    const int16_t pcm[] = { -100, -1, 0, 100 };
    cb(pcm, 4, user);
    return 0;
}
static int audio(const int16_t *pcm, size_t n, void *user) {
    (void)user;
    assert(n == 4);
    memcpy(received, pcm, n * sizeof(*pcm));
    calls++;
    return stop_audio;
}
static void event(const sam_event *ev, void *user) {
    (void)user;
    if (ev->type == SAM_EV_WORD) {
        assert(ev->text_pos == expected_pos && ev->text_len == expected_len);
        events++;
    }
}

static int cancelled_at_start(void *arg) {
    sam_speech *t = arg;
    assert(!sam_cancelled(&t->cancel));
    return 1;
}
static int cancel_during_check(void *arg) {
    sam_tts_cancel(arg);
    return 0;
}

int main(void) {
    sam_callbacks cb = { audio, NULL, NULL };
    sam_speech t = {0};
    pcm_ctx c = { &t, 100 };
    const int16_t pcm[] = { -100, -1, 0, 100 };
    t.cb = &cb;
    on_pcm(pcm, 4, &c);
    assert(calls == 1 && t.base == 4 && memcmp(received, pcm, sizeof pcm) == 0);
    c.volume = 50;
    on_pcm(pcm, 4, &c);
    assert(calls == 2 && received[0] == -50 && received[3] == 50 && pcm[0] == -100);
    c.volume = 100;
    t.fx = (sam4fx *)&t;
    on_pcm(pcm, 4, &c);
    assert(calls == 3 && received[0] == -99 && received[3] == 101 && pcm[0] == -100);
    t.fx = NULL;
    stop_audio = 1;
    on_pcm(pcm, 4, &c);
    assert(t.cancel && t.stopped);
    on_pcm(pcm, 4, &c);
    assert(calls == 4);
    memset(&t, 0, sizeof t);
    t.volume = 100;
    stop_audio = 0;
    sam_tts_cancel(&t);
    assert(sam_tts_speak_checked(&t, "abc", 0, &cb, cancelled_at_start, &t) == 1);
    assert(calls == 4 && sam_cancelled(&t.cancel));
    assert(sam_tts_speak_checked(&t, "abc", 0, &cb, cancel_during_check, &t) == 1);
    assert(calls == 4 && sam_cancelled(&t.cancel));
    fail_alloc = 1;
    assert(sam_tts_speak(&t, "abc", 0, &cb) == 0);
    assert(sam_tts_speak(&t, "<rate speed=\"1\">abc</rate>", SAM_SPEAK_XML, &cb) == -1);
    fail_alloc = 0;
    assert(calls == 5 && memcmp(received, pcm, sizeof pcm) == 0);
    cb.event = event;
    expected_events = 1;
    assert(sam_tts_speak(&t, "abc", 0, &cb) == 0);
    assert(events == 1 && calls == 6 && memcmp(received, pcm, sizeof pcm) == 0);
    word_pos = 1;
    expected_pos = 2;
    assert(sam_tts_speak(&t, "\xc3\xa9" "abc", 0, &cb) == 0);
    assert(events == 2);
    cb.event = NULL;
    expected_events = 0;
    stop_audio = 1;
    assert(sam_tts_speak(&t, "abc", 0, &cb) == 1);
    stop_audio = 0;
    const char *runs[] = { "abc", "def" };
    expected_runs = runs;
    assert(sam_tts_speak(&t, "<rate speed=\"1\">abc</rate>def", SAM_SPEAK_XML, &cb) == 0);
    assert(run_index == 2);
    return 0;
}
