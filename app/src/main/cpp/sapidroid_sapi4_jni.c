/* JNI bridge from the SapiDroid worker to the SAPI 4 engine (Sam, Mike, Mary and their modes). */
#include <jni.h>
#include <android/log.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "sapi4tts.h"
#include "pcm_stream.h"
#include "prof.h"

JNIEXPORT jint JNICALL
Java_org_sapidroid_tts_NativeSapi4_init(JNIEnv *env, jclass clazz, jstring dllPath) {
    (void)clazz;
    if (!dllPath) return -1;
    const char *p = (*env)->GetStringUTFChars(env, dllPath, NULL);
    if (!p) return -1;
    int rc = s4_init(p);
    (*env)->ReleaseStringUTFChars(env, dllPath, p);
    __android_log_print(4, "SapiDroidSapi4", "s4_init rc=%d", rc);
    return rc;
}

JNIEXPORT jobjectArray JNICALL
Java_org_sapidroid_tts_NativeSapi4_listModes(JNIEnv *env, jclass clazz, jstring voiceDir) {
    (void)clazz;
    s4_mode first[64], *modes = first;
    if (!voiceDir) return NULL;
    const char *d = (*env)->GetStringUTFChars(env, voiceDir, NULL);
    if (!d) return NULL;
    int n = s4_list_modes(d, first, 64);
    if (n > 64) {
        modes = (size_t)n > SIZE_MAX / sizeof(*modes) ? NULL :
            malloc((size_t)n * sizeof(*modes));
        if (!modes) {
            (*env)->ReleaseStringUTFChars(env, voiceDir, d);
            jclass oom = (*env)->FindClass(env, "java/lang/OutOfMemoryError");
            if (oom) (*env)->ThrowNew(env, oom, "cannot list SAPI 4 voices");
            return NULL;
        }
        int found = s4_list_modes(d, modes, n);
        if (found > n) {
            __android_log_print(5, "SapiDroidSapi4", "voice list grew while being read");
        } else {
            n = found;
        }
    }
    (*env)->ReleaseStringUTFChars(env, voiceDir, d);
    if (n <= 0) {
        if (modes != first) free(modes);
        return NULL;
    }
    jclass strClass = (*env)->FindClass(env, "java/lang/String");
    jobjectArray out = strClass ? (*env)->NewObjectArray(env, n, strClass, NULL) : NULL;
    for (int i = 0; out && i < n; i++) {
        jstring s = (*env)->NewStringUTF(env, modes[i].name);
        if (!s) break;
        (*env)->SetObjectArrayElement(env, out, i, s);
        (*env)->DeleteLocalRef(env, s);
        if ((*env)->ExceptionCheck(env)) break;
    }
    if (modes != first) free(modes);
    return out;
}

JNIEXPORT jlong JNICALL
Java_org_sapidroid_tts_NativeSapi4_open(JNIEnv *env, jclass clazz,
                                        jstring voiceDir, jstring mode) {
    (void)clazz;
    if (!voiceDir || !mode) return 0;
    const char *d = (*env)->GetStringUTFChars(env, voiceDir, NULL);
    if (!d) return 0;
    const char *m = (*env)->GetStringUTFChars(env, mode, NULL);
    if (!m) {
        (*env)->ReleaseStringUTFChars(env, voiceDir, d);
        return 0;
    }
    char err[256];
    err[0] = 0;
    PROF_SCOPE("load:voice");
    s4_engine *e = s4_open(d, m, err, sizeof err);
    PROF_END();
    if (!e) {
        __android_log_print(4, "SapiDroidSapi4", "s4_open(%s) failed: %s", m, err);
    }
    (*env)->ReleaseStringUTFChars(env, voiceDir, d);
    (*env)->ReleaseStringUTFChars(env, mode, m);
    return (jlong)(intptr_t)e;
}

JNIEXPORT jintArray JNICALL
Java_org_sapidroid_tts_NativeSapi4_limits(JNIEnv *env, jclass clazz, jlong h) {
    (void)clazz;
    s4_limits lim;
    memset(&lim, 0, sizeof lim);
    if (!h) return NULL;
    s4_get_limits((s4_engine *)(intptr_t)h, &lim);
    jint vals[6];
    vals[0] = (jint)lim.pitch_default;
    vals[1] = (jint)lim.pitch_min;
    vals[2] = (jint)lim.pitch_max;
    vals[3] = (jint)lim.speed_default;
    vals[4] = (jint)lim.speed_min;
    vals[5] = (jint)lim.speed_max;
    jintArray out = (*env)->NewIntArray(env, 6);
    if (out) (*env)->SetIntArrayRegion(env, out, 0, 6, vals);
    return out;
}

JNIEXPORT jint JNICALL
Java_org_sapidroid_tts_NativeSapi4_sampleRate(JNIEnv *env, jclass clazz, jlong h) {
    (void)env; (void)clazz;
    return h ? s4_sample_rate((s4_engine *)(intptr_t)h) : 0;
}

JNIEXPORT void JNICALL
Java_org_sapidroid_tts_NativeSapi4_setSpeed(JNIEnv *env, jclass clazz, jlong h, jint wpm) {
    (void)env; (void)clazz;
    if (h) s4_set_speed((s4_engine *)(intptr_t)h, (unsigned)wpm);
}

JNIEXPORT void JNICALL
Java_org_sapidroid_tts_NativeSapi4_setPitch(JNIEnv *env, jclass clazz, jlong h, jint pitch) {
    (void)env; (void)clazz;
    if (h) s4_set_pitch((s4_engine *)(intptr_t)h, (unsigned)pitch);
}

JNIEXPORT jint JNICALL
Java_org_sapidroid_tts_NativeSapi4_speak(JNIEnv *env, jclass clazz, jlong h, jbyteArray text, jobject sink) {
    (void)clazz;
    if (!h || !text) return -1;
    pcm_stream stream;
    if (pcm_stream_init(&stream, env, sink)) return -1;
    jsize len = (*env)->GetArrayLength(env, text);
    char *c_text = (char *)malloc((size_t)len + 1);
    if (!c_text) return -1;
    {
        PROF_SCOPE("speak:text");
        (*env)->GetByteArrayRegion(env, text, 0, len, (jbyte *)c_text);
        PROF_END();
    }
    if ((*env)->ExceptionCheck(env)) {
        free(c_text);
        return -1;
    }
    c_text[len] = 0;

#if PROF_ON
    prof_mark_speak_start();
#endif
    int rc;
    {
        PROF_SCOPE("speak:call");
        rc = s4_speak_checked((s4_engine *)(intptr_t)h, c_text, 0, pcm_stream_audio, &stream,
                              pcm_stream_cancelled, &stream);
        PROF_END();
    }
    free(c_text);
    if (rc < 0) {
        __android_log_print(4, "SapiDroidSapi4", "s4_speak failed rc=%d", rc);
    }
#if PROF_ON
    prof_dump("utterance");
#endif
    return stream.failed ? -1 : rc;
}

JNIEXPORT void JNICALL
Java_org_sapidroid_tts_NativeSapi4_cancel(JNIEnv *env, jclass clazz, jlong h) {
    (void)env; (void)clazz;
    if (h) s4_stop((s4_engine *)(intptr_t)h);
}

JNIEXPORT void JNICALL
Java_org_sapidroid_tts_NativeSapi4_close(JNIEnv *env, jclass clazz, jlong h) {
    (void)env; (void)clazz;
    if (h) s4_close((s4_engine *)(intptr_t)h);
}
