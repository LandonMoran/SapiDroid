/* JNI bridge from the SapiDroid worker to the OneCore voice engine. */
#include <jni.h>
#include <android/log.h>
#include <stdlib.h>
#include <string.h>
#include "zira_tts.h"
#include "pcm_stream.h"

JNIEXPORT jlong JNICALL
Java_org_sapidroid_tts_NativeZira_open(JNIEnv *env, jclass clazz,
                                       jstring dir, jstring voice) {
    (void)clazz;
    if (!dir || !voice) return 0;
    const char *c_dir = (*env)->GetStringUTFChars(env, dir, NULL);
    const char *c_voice = (*env)->GetStringUTFChars(env, voice, NULL);
    char err[256];
    err[0] = 0;
    zira_tts *t = zira_tts_open(c_dir, c_voice, err, sizeof err);
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
    const char *c_text = (*env)->GetStringUTFChars(env, text, NULL);
    if (!c_text) return -1;

    zira_callbacks cb;
    memset(&cb, 0, sizeof cb);
    cb.audio = pcm_stream_audio;
    cb.user = &stream;

    int rc = zira_tts_speak((zira_tts *)(intptr_t)h, c_text, 0, &cb);
    (*env)->ReleaseStringUTFChars(env, text, c_text);

    if (rc < 0) {
        __android_log_print(4, "SapiDroidNative", "speak failed rc=%d", rc);
    }
    return stream.failed ? -1 : rc;
}

JNIEXPORT void JNICALL
Java_org_sapidroid_tts_NativeZira_cancel(JNIEnv *env, jclass clazz, jlong h) {
    (void)env; (void)clazz;
    if (h) zira_tts_cancel((zira_tts *)(intptr_t)h);
}
