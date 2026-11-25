#include <jni.h>
#include <android/log.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../../main/cpp/sapi4/lib/sapi4tts.h"

static int first_count;
static int second_count;
static int list_calls;
static int warning_logs;
static int fail_malloc;

int __android_log_print(int priority, const char *tag, const char *format, ...) {
    (void)tag;
    (void)format;
    if (priority == ANDROID_LOG_WARN) warning_logs++;
    return 0;
}

void *__real_malloc(size_t size);
void *__wrap_malloc(size_t size) {
    if (fail_malloc) {
        fail_malloc = 0;
        return NULL;
    }
    return __real_malloc(size);
}

int s4_init(const char *dll_path) { (void)dll_path; return 0; }

int s4_list_modes(const char *voice_dir, s4_mode *modes, int max) {
    (void)voice_dir;
    int call = list_calls++;
    int count = call == 0 ? first_count : second_count;
    int write_count = count < max ? count : max;
    for (int i = 0; i < write_count; i++)
        snprintf(modes[i].name, sizeof modes[i].name, "mode-%03d", i);
    return count;
}

s4_engine *s4_open(const char *voice_dir, const char *mode_name, char *err, size_t errlen) {
    (void)voice_dir; (void)mode_name; (void)err; (void)errlen;
    return NULL;
}
void s4_close(s4_engine *engine) { (void)engine; }
const s4_mode *s4_mode_info(const s4_engine *engine) { (void)engine; return NULL; }
int s4_sample_rate(const s4_engine *engine) { (void)engine; return 0; }
void s4_get_limits(const s4_engine *engine, s4_limits *limits) { (void)engine; (void)limits; }
unsigned s4_get_pitch(const s4_engine *engine) { (void)engine; return 0; }
unsigned s4_get_speed(const s4_engine *engine) { (void)engine; return 0; }
int s4_set_pitch(s4_engine *engine, unsigned pitch) { (void)engine; (void)pitch; return 0; }
int s4_set_speed(s4_engine *engine, unsigned speed) { (void)engine; (void)speed; return 0; }
int s4_speak(s4_engine *engine, const char *text, unsigned flags, s4_pcm_fn fn, void *user) {
    (void)engine; (void)text; (void)flags; (void)fn; (void)user;
    return 0;
}
void s4_stop(s4_engine *engine) { (void)engine; }
int s4_reset(s4_engine *engine) { (void)engine; return 0; }

JNIEXPORT void JNICALL
Java_org_sapidroid_tts_Sapi4JniListModesTest_configure(JNIEnv *env, jclass clazz, jint first, jint second) {
    (void)env; (void)clazz;
    first_count = first;
    second_count = second;
    list_calls = 0;
    warning_logs = 0;
    fail_malloc = 0;
}

JNIEXPORT void JNICALL
Java_org_sapidroid_tts_Sapi4JniListModesTest_failNextMalloc(JNIEnv *env, jclass clazz) {
    (void)env; (void)clazz;
    fail_malloc = 1;
}

JNIEXPORT jint JNICALL
Java_org_sapidroid_tts_Sapi4JniListModesTest_listCalls(JNIEnv *env, jclass clazz) {
    (void)env; (void)clazz;
    return list_calls;
}

JNIEXPORT jint JNICALL
Java_org_sapidroid_tts_Sapi4JniListModesTest_warningLogs(JNIEnv *env, jclass clazz) {
    (void)env; (void)clazz;
    return warning_logs;
}

#include "../../main/cpp/sapidroid_sapi4_jni.c"
