#include "../../main/cpp/pcm_stream.h"

JNIEXPORT jint JNICALL Java_PcmStreamTest_emit(JNIEnv *env, jclass cls, jobject sink) {
    (void)cls;
    pcm_stream stream;
    if (pcm_stream_init(&stream, env, sink)) return -1;
    int16_t samples[5003];
    for (size_t i = 0; i < 5003; i++)
        samples[i] = (int16_t)((i * 193u + 32768u) % 65536u - 32768);
    return pcm_stream_audio(samples, 5003, &stream);
}
