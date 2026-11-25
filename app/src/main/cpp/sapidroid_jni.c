/* JNI bridge from the SapiDroid worker to the OneCore voice engine. */
#include <jni.h>
#include <android/log.h>
#include <stdlib.h>
#include <string.h>
#include "zira_tts.h"
#include "pcm_stream.h"
#include "jni_text.h"
#include "prof.h"

JNIEXPORT jlong JNICALL
Java_org_sapidroid_tts_NativeZira_open(JNIEnv *env, jclass clazz,
                                       jstring dir, jstring voice) {
    (void)clazz;
    if (!dir || !voice) return 0;
    const char *c_dir = (*env)->GetStringUTFChars(env, dir, NULL);
    if (!c_dir) return 0;
    const char *c_voice = (*env)->GetStringUTFChars(env, voice, NULL);
    if (!c_voice) {
        (*env)->ReleaseStringUTFChars(env, dir, c_dir);
        return 0;
    }
    char err[256];
    err[0] = 0;
    PROF_SCOPE("load:voice");
    zira_tts *t = zira_tts_open(c_dir, c_voice, err, sizeof err);
    PROF_END();
    if (!t) {
        __android_log_print(4, "SapiDroidNative", "open failed: %s", err);
    }
    (*env)->ReleaseStringUTFChars(env, dir, c_dir);
    (*env)->ReleaseStringUTFChars(env, voice, c_voice);
    return (jlong)(intptr_t)t;
}

JNIEXPORT void JNICALL
Java_org_sapidroid_tts_NativeZira_close(JNIEnv *env, jclass clazz, jlong handle) {
    (void)env; (void)clazz;
    if (handle) zira_tts_close((zira_tts *)(intptr_t)handle);
}

JNIEXPORT void JNICALL
Java_org_sapidroid_tts_NativeZira_setRate(JNIEnv *env, jclass clazz, jlong h, jint rate) {
    (void)env; (void)clazz;
    if (h) zira_tts_set_rate((zira_tts *)(intptr_t)h, rate);
}

JNIEXPORT void JNICALL
Java_org_sapidroid_tts_NativeZira_setPitch(JNIEnv *env, jclass clazz, jlong h, jint pitch) {
    (void)env; (void)clazz;
    if (h) zira_tts_set_pitch((zira_tts *)(intptr_t)h, pitch);
}

JNIEXPORT void JNICALL
Java_org_sapidroid_tts_NativeZira_setVolume(JNIEnv *env, jclass clazz, jlong h, jint volume) {
    (void)env; (void)clazz;
    if (h) zira_tts_set_volume((zira_tts *)(intptr_t)h, volume);
}

JNIEXPORT jint JNICALL
Java_org_sapidroid_tts_NativeZira_speak(JNIEnv *env, jclass clazz, jlong h, jstring text, jobject sink) {
    (void)clazz;
    if (!h || !text) return -1;
    pcm_stream stream;
    if (pcm_stream_init(&stream, env, sink)) return -1;
    char *c_text;
    {
        PROF_SCOPE("speak:text");
        c_text = jni_text_utf8(env, text);
        PROF_END();
    }
    if (!c_text) return -1;

    zira_callbacks cb;
    memset(&cb, 0, sizeof cb);
    cb.audio = pcm_stream_audio;
    cb.user = &stream;

#if PROF_ON
    prof_mark_speak_start();
#endif
    int rc;
    {
        PROF_SCOPE("speak:call");
        rc = zira_tts_speak_checked((zira_tts *)(intptr_t)h, c_text, 0, &cb, pcm_stream_cancelled, &stream);
        PROF_END();
    }
    free(c_text);

    if (rc < 0) {
        __android_log_print(4, "SapiDroidNative", "speak failed rc=%d", rc);
    }
    return stream.failed ? -1 : rc;
}

/* Profiler entry points used by Prof.kt; no-ops unless the build enabled SAPIDROID_PROFILE. */
JNIEXPORT void JNICALL
Java_org_sapidroid_tts_NativeProfile_addNs(JNIEnv *env, jclass clazz, jstring name, jlong ns) {
    char buf[64];
    jsize len;
    (void)clazz;
    if (!name) return;
    len = (*env)->GetStringLength(env, name);
    if (len < 0 || len >= (jsize)sizeof buf) return;
    (*env)->GetStringUTFRegion(env, name, 0, len, buf);
    prof_add_ns(buf, (long long)ns);
}

/* The request's table as text: the service writes it to logcat and to the file it can export. */
JNIEXPORT jstring JNICALL
Java_org_sapidroid_tts_NativeProfile_table(JNIEnv *env, jclass clazz, jstring tag) {
    char table[8192];
    char name[64];
    jsize len;
    (void)clazz;
    name[0] = 0;
    if (tag) {
        len = (*env)->GetStringLength(env, tag);
        if (len > 0 && len < (jsize)sizeof name) (*env)->GetStringUTFRegion(env, tag, 0, len, name);
    }
    if (prof_table(table, (int)sizeof table, name) <= 0) return (*env)->NewStringUTF(env, "");
    return (*env)->NewStringUTF(env, table);
}

JNIEXPORT void JNICALL
Java_org_sapidroid_tts_NativeProfile_reset(JNIEnv *env, jclass clazz) {
    (void)env;
    (void)clazz;
    prof_reset();
}

/* Take-and-clear: the engine process hands one request's rows to the service process, which owns
 * the table that gets printed. */
JNIEXPORT jstring JNICALL
Java_org_sapidroid_tts_NativeProfile_take(JNIEnv *env, jclass clazz) {
    char buf[4096];
    (void)clazz;
    if (prof_snapshot(buf, (int)sizeof buf) <= 0) return (*env)->NewStringUTF(env, "");
    return (*env)->NewStringUTF(env, buf);
}

JNIEXPORT void JNICALL
Java_org_sapidroid_tts_NativeProfile_merge(JNIEnv *env, jclass clazz, jstring name, jlong total_ns,
                                           jlong count, jlong max_ns, jlong bytes) {
    char buf[64];
    jsize len;
    (void)clazz;
    if (!name) return;
    len = (*env)->GetStringLength(env, name);
    if (len < 0 || len >= (jsize)sizeof buf) return;
    (*env)->GetStringUTFRegion(env, name, 0, len, buf);
    prof_merge(buf, (long long)total_ns, (long long)count, (long long)max_ns, (long long)bytes);
}

JNIEXPORT void JNICALL
Java_org_sapidroid_tts_NativeZira_cancel(JNIEnv *env, jclass clazz, jlong h) {
    (void)env; (void)clazz;
    if (h) zira_tts_cancel((zira_tts *)(intptr_t)h);
}
