#include "../../main/cpp/pcm_stream.h"
#include "../../main/cpp/jni_text.h"
#include <string.h>

JNIEXPORT jint JNICALL Java_PcmStreamTest_check(JNIEnv *env, jclass cls, jobject sink) {
    (void)cls;
    pcm_stream stream;
    if (pcm_stream_init(&stream, env, sink)) return -1;
    return pcm_stream_cancelled(&stream);
}

JNIEXPORT jint JNICALL Java_PcmStreamTest_emit(JNIEnv *env, jclass cls, jobject sink) {
    (void)cls;
    pcm_stream stream;
    if (pcm_stream_init(&stream, env, sink)) return -1;
    int16_t samples[5003];
    for (size_t i = 0; i < 5003; i++)
        samples[i] = (int16_t)((i * 193u + 32768u) % 65536u - 32768);
    return pcm_stream_audio(samples, 5003, &stream);
}

JNIEXPORT jbyteArray JNICALL Java_PcmStreamTest_encode(JNIEnv *env, jclass cls, jstring text) {
    (void)cls;
    char *utf8 = jni_text_utf8(env, text);
    if (!utf8) return NULL;
    jsize n = (jsize)strlen(utf8);
    jbyteArray out = (*env)->NewByteArray(env, n);
    if (out) (*env)->SetByteArrayRegion(env, out, 0, n, (const jbyte *)utf8);
    free(utf8);
    return out;
}
