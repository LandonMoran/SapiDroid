/* JNI bridge from the SapiDroid worker to the SAPI 5 engine (Microsoft Sam, Mike, Mary). */
#include <jni.h>
#include <android/log.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "sam_tts.h"
#include "pcm_stream.h"
#include "jni_text.h"

JNIEXPORT jlong JNICALL
Java_org_sapidroid_tts_NativeSapi5_open(JNIEnv *env, jclass clazz,
                                        jstring dataDir, jstring voice) {
    (void)clazz;
    if (!dataDir || !voice) return 0;
    const char *d = (*env)->GetStringUTFChars(env, dataDir, NULL);
    if (!d) return 0;
    const char *v = (*env)->GetStringUTFChars(env, voice, NULL);
    if (!v) {
        (*env)->ReleaseStringUTFChars(env, dataDir, d);
        return 0;
    }
    char err[256];
    err[0] = 0;
    sam_speech *t = sam_tts_open(d, v, err, sizeof err);
    if (!t) {
        __android_log_print(4, "SapiDroidSapi5", "open(%s) failed: %s", v, err);
    }
    (*env)->ReleaseStringUTFChars(env, dataDir, d);
    (*env)->ReleaseStringUTFChars(env, voice, v);
    return (jlong)(intptr_t)t;
}

JNIEXPORT void JNICALL
Java_org_sapidroid_tts_NativeSapi5_close(JNIEnv *env, jclass clazz, jlong h) {
    (void)env; (void)clazz;
    if (h) sam_tts_close((sam_speech *)(intptr_t)h);
}

JNIEXPORT void JNICALL
Java_org_sapidroid_tts_NativeSapi5_setRate(JNIEnv *env, jclass clazz, jlong h, jint rate) {
    (void)env; (void)clazz;
    if (h) sam_tts_set_rate((sam_speech *)(intptr_t)h, rate);
}

JNIEXPORT void JNICALL
Java_org_sapidroid_tts_NativeSapi5_setPitch(JNIEnv *env, jclass clazz, jlong h, jint pitch) {
    (void)env; (void)clazz;
    if (h) sam_tts_set_pitch((sam_speech *)(intptr_t)h, pitch);
}

JNIEXPORT void JNICALL
Java_org_sapidroid_tts_NativeSapi5_setVolume(JNIEnv *env, jclass clazz, jlong h, jint volume) {
    (void)env; (void)clazz;
    if (h) sam_tts_set_volume((sam_speech *)(intptr_t)h, volume);
}

JNIEXPORT jint JNICALL
Java_org_sapidroid_tts_NativeSapi5_sampleRate(JNIEnv *env, jclass clazz, jlong h) {
    (void)env; (void)clazz;
    return h ? sam_tts_sample_rate((sam_speech *)(intptr_t)h) : 0;
}

JNIEXPORT jint JNICALL
Java_org_sapidroid_tts_NativeSapi5_speak(JNIEnv *env, jclass clazz, jlong h, jstring text, jobject sink) {
    (void)clazz;
    if (!h || !text) return -1;
    pcm_stream stream;
    if (pcm_stream_init(&stream, env, sink)) return -1;
    char *c_text = jni_text_utf8(env, text);
    if (!c_text) return -1;

    sam_callbacks cb;
    memset(&cb, 0, sizeof cb);
    cb.audio = pcm_stream_audio;
    cb.user = &stream;

    int rc = sam_tts_speak_checked((sam_speech *)(intptr_t)h, c_text, 0, &cb, pcm_stream_cancelled, &stream);
    free(c_text);

    if (rc < 0) {
        __android_log_print(4, "SapiDroidSapi5", "speak failed rc=%d", rc);
    }
    return stream.failed ? -1 : rc;
}

JNIEXPORT void JNICALL
Java_org_sapidroid_tts_NativeSapi5_cancel(JNIEnv *env, jclass clazz, jlong h) {
    (void)env; (void)clazz;
    if (h) sam_tts_cancel((sam_speech *)(intptr_t)h);
}
