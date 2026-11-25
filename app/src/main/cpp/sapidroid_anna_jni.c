/* JNI bridge from the SapiDroid worker to the Microsoft Anna engine. */
#include <jni.h>
#include <android/log.h>
#include <stdlib.h>
#include <string.h>
#include "anna_tts.h"
#include "pcm_stream.h"

JNIEXPORT jlong JNICALL
Java_org_sapidroid_tts_NativeAnna_open(JNIEnv *env, jclass clazz, jstring dir) {
    (void)clazz;
    if (!dir) return 0;
    const char *c_dir = (*env)->GetStringUTFChars(env, dir, NULL);
    char err[256];
    err[0] = 0;
    anna_tts *t = anna_tts_open(c_dir, err, sizeof err);
    if (!t) {
        __android_log_print(4, "SapiDroidNative", "anna open failed: %s", err);
    }
    (*env)->ReleaseStringUTFChars(env, dir, c_dir);
    return (jlong)(intptr_t)t;
}

JNIEXPORT void JNICALL
Java_org_sapidroid_tts_NativeAnna_close(JNIEnv *env, jclass clazz, jlong handle) {
    (void)env; (void)clazz;
    if (handle) anna_tts_close((anna_tts *)(intptr_t)handle);
}

JNIEXPORT void JNICALL
Java_org_sapidroid_tts_NativeAnna_setRate(JNIEnv *env, jclass clazz, jlong h, jint rate) {
    (void)env; (void)clazz;
    if (h) anna_tts_set_rate((anna_tts *)(intptr_t)h, rate);
}

JNIEXPORT void JNICALL
Java_org_sapidroid_tts_NativeAnna_setPitch(JNIEnv *env, jclass clazz, jlong h, jint pitch) {
    (void)env; (void)clazz;
    if (h) anna_tts_set_pitch((anna_tts *)(intptr_t)h, pitch);
}

JNIEXPORT void JNICALL
Java_org_sapidroid_tts_NativeAnna_setVolume(JNIEnv *env, jclass clazz, jlong h, jint volume) {
    (void)env; (void)clazz;
    if (h) anna_tts_set_volume((anna_tts *)(intptr_t)h, volume);
}

JNIEXPORT jint JNICALL
Java_org_sapidroid_tts_NativeAnna_speak(JNIEnv *env, jclass clazz, jlong h, jstring text, jobject sink) {
    (void)clazz;
    if (!h || !text) return -1;
    pcm_stream stream;
    if (pcm_stream_init(&stream, env, sink)) return -1;
    const char *c_text = (*env)->GetStringUTFChars(env, text, NULL);
    if (!c_text) return -1;

    anna_callbacks cb;
    memset(&cb, 0, sizeof cb);
    cb.audio = pcm_stream_audio;
    cb.user = &stream;

    int rc = anna_tts_speak((anna_tts *)(intptr_t)h, c_text, 0, &cb);
    (*env)->ReleaseStringUTFChars(env, text, c_text);

    if (rc < 0) {
        __android_log_print(4, "SapiDroidNative", "anna speak failed rc=%d", rc);
    }
    return stream.failed ? -1 : rc;
}

JNIEXPORT void JNICALL
Java_org_sapidroid_tts_NativeAnna_cancel(JNIEnv *env, jclass clazz, jlong h) {
    (void)env; (void)clazz;
    if (h) anna_tts_cancel((anna_tts *)(intptr_t)h);
}
