/* Synthetic driver test: cc -std=c99 -ffunction-sections -fdata-sections
 * engine/tests/zira_tts_host_test.c -pthread -Wl,--gc-sections,--wrap=malloc -o zira_tts_host_test */
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>

#include "../zira_tts.c"

static char pieces[8][128];
static int npieces, sentence_result, process_result, delivered, ends;
static int wait_for_cancel, synth_ready;
static int fail_malloc, last_xml;

void *__real_malloc(size_t size);
void *__wrap_malloc(size_t size)
{
    if (fail_malloc) { fail_malloc = 0; return NULL; }
    return __real_malloc(size);
}

int zf1_speak_utf8(zf1_engine *e, const char *text, int xml)
{
    (void)e;
    last_xml = xml;
    assert(npieces < 8);
    snprintf(pieces[npieces++], sizeof pieces[0], "%s", text);
    return 0;
}
int zf1_next_sentence(zf1_engine *e, zf_sentence *s)
{
    int r = sentence_result;
    (void)e;
    memset(s, 0, sizeof *s);
    sentence_result = 0;
    return r;
}
void zf_sentence_free(zf_sentence *s) { (void)s; }
int zf2_process(const zf2_voice *v, const zf_sentence *s, ZbUtt *u, zf2_events *ev)
{
    (void)v; (void)s; (void)ev;
    u->nphone = 1;
    return process_result;
}
void zf2_events_free(zf2_events *ev) { (void)ev; }
void zf2_utt_free(ZbUtt *u) { (void)u; }
int zb_emotion(const ZbVoice *v, const char *name, ZbStyle *style)
{
    (void)v; (void)name;
    memset(style, 0, sizeof *style);
    return 0;
}
void zb_trace_free(ZbTrace *tr) { (void)tr; }
int zb_synth_cancelable(ZbVoice *v, const ZbUtt *u, const ZbSapi *sapi, zb_write_fn write, void *user,
                        ZbTrace *trace, const volatile int *cancel)
{
    const short pcm[] = {1, 2};
    (void)v; (void)u; (void)sapi; (void)trace;
    if (wait_for_cancel) {
        __atomic_store_n(&synth_ready, 1, __ATOMIC_RELEASE);
        while (!zb_cancel_load(cancel)) sched_yield();
        return 1;
    }
    return write(user, pcm, 2);
}
ZbVoice *zb_voice_load(const char *path, char *err, int len)
{
    (void)path; (void)err; (void)len;
    return (ZbVoice *)1;
}
zf2_voice *zf2_voice_load(const char *path) { (void)path; return (zf2_voice *)1; }

static int audio(const int16_t *pcm, size_t n, void *user)
{
    (void)user;
    assert(n == 2 && pcm[0] == 1 && pcm[1] == 2);
    delivered++;
    return 1;
}
static void event(const zira_event *e, void *user)
{
    (void)user;
    if (e->type == ZIRA_EV_END) ends++;
}
static void *speak_thread(void *arg)
{
    zira_tts *t = (zira_tts *)arg;
    return (void *)(intptr_t)zira_tts_speak(t, "cancel", 0, NULL);
}

int main(void)
{
    zira_tts t = {0};
    zira_callbacks cb = {NULL, event, NULL};
    t.male = 1;

    assert(zira_tts_speak(&t, "<lang", ZIRA_SPEAK_XML, &cb) == 0);
    assert(npieces == 1 && !strcmp(pieces[0], "<lang") && ends == 1);
    npieces = ends = 0;
    assert(zira_tts_speak(&t, "before <lang id='x'>text", ZIRA_SPEAK_XML, &cb) == 0);
    assert(npieces == 2 && !strcmp(pieces[0], "before ") && !strcmp(pieces[1], "<lang id='x'>text"));
    npieces = ends = 0;
    assert(zira_tts_speak(&t, "a<lang id='x'>b</lang>c", ZIRA_SPEAK_XML, &cb) == 0);
    assert(npieces == 3 && !strcmp(pieces[0], "a") && !strcmp(pieces[1], "b") && !strcmp(pieces[2], "c"));

    npieces = 0;
    fail_malloc = 1;
    assert(zira_tts_speak(&t, "plain", 0, NULL) == 0);
    assert(fail_malloc == 1 && npieces == 1 && !strcmp(pieces[0], "plain") && last_xml == 0);
    fail_malloc = 0;
    t.male = 0;
    npieces = 0;
    fail_malloc = 1;
    assert(zira_tts_speak(&t, "<s>female</s>", ZIRA_SPEAK_XML, NULL) == 0);
    assert(fail_malloc == 1 && npieces == 1 && last_xml == 1);
    t.male = 1;
    fail_malloc = 1;
    assert(zira_tts_speak(&t, "male", ZIRA_SPEAK_XML, NULL) == -1 && fail_malloc == 0);

    npieces = ends = 0;
    sentence_result = -1;
    assert(zira_tts_speak(&t, "error", 0, &cb) == -1 && ends == 0);
    sentence_result = 1;
    process_result = -1;
    assert(zira_tts_speak(&t, "error", 0, &cb) == -1 && ends == 0);

    npieces = ends = process_result = 0;
    sentence_result = 1;
    cb.event = NULL;
    cb.audio = audio;
    assert(zira_tts_speak(&t, "stop", 0, &cb) == 1 && delivered == 1);
    {
        pthread_t thread;
        void *result;
        npieces = 0;
        sentence_result = 1;
        wait_for_cancel = 1;
        assert(pthread_create(&thread, NULL, speak_thread, &t) == 0);
        while (!__atomic_load_n(&synth_ready, __ATOMIC_ACQUIRE)) sched_yield();
        zira_tts_cancel(&t);
        assert(pthread_join(thread, &result) == 0);
        assert((intptr_t)result == 1);
    }
    return 0;
}
