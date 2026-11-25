#ifndef SAPIDROID_PCM_STREAM_H
#define SAPIDROID_PCM_STREAM_H

#include <jni.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    JNIEnv *env;
    jobject sink;
    jmethodID on_pcm;
    int failed;
} pcm_stream;

static int pcm_stream_init(pcm_stream *stream, JNIEnv *env, jobject sink) {
    if (!sink) return -1;
    jclass cls = (*env)->GetObjectClass(env, sink);
    if (!cls) return -1;
    jmethodID method = (*env)->GetMethodID(env, cls, "onPcm", "([B)Z");
    (*env)->DeleteLocalRef(env, cls);
    if (!method) return -1;
    stream->env = env;
    stream->sink = sink;
    stream->on_pcm = method;
    stream->failed = 0;
    return 0;
}

/* Engine callbacks run synchronously on the JNI caller's thread. */
static int pcm_stream_audio(const int16_t *pcm, size_t count, void *user) {
    enum { BLOCK_SAMPLES = 2048 };
    pcm_stream *stream = (pcm_stream *)user;
    JNIEnv *env = stream->env;
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
    jbyte bytes[BLOCK_SAMPLES * 2];
#endif
    while (count > 0) {
        size_t n = count < BLOCK_SAMPLES ? count : BLOCK_SAMPLES;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        /* JNI copies the little-endian samples directly into the Java array. */
        const jbyte *data = (const jbyte *)pcm;
#else
        for (size_t i = 0; i < n; i++) {
            uint16_t sample = (uint16_t)pcm[i];
            bytes[2 * i] = (jbyte)(sample & 0xff);
            bytes[2 * i + 1] = (jbyte)(sample >> 8);
        }
        const jbyte *data = bytes;
#endif
        jbyteArray block = (*env)->NewByteArray(env, (jsize)(n * 2));
        if (!block) {
            stream->failed = 1;
            return 1;
        }
        (*env)->SetByteArrayRegion(env, block, 0, (jsize)(n * 2), data);
        jboolean accepted = JNI_FALSE;
        if (!(*env)->ExceptionCheck(env))
            accepted = (*env)->CallBooleanMethod(env, stream->sink, stream->on_pcm, block);
        (*env)->DeleteLocalRef(env, block);
        if ((*env)->ExceptionCheck(env)) {
            stream->failed = 1;
            return 1;
        }
        if (!accepted) return 1;
        pcm += n;
        count -= n;
    }
    return 0;
}

#endif
