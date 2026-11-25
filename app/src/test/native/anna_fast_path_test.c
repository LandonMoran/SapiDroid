#include "../../main/cpp/anna/anna_tts.c"
#include <assert.h>

static int sentences, expected_events, events, audio_count, stop_audio;
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

int main(void) {
    anna_unitinfo info[2] = {{0}};
    anna_tts t = {0};
    anna_callbacks cb = { audio, NULL, NULL };
    const int16_t pcm[] = { -100, -1, 0, 100 };
    t.info = info;
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
    return 0;
}
