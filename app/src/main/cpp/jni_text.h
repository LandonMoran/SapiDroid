#ifndef SAPIDROID_JNI_TEXT_H
#define SAPIDROID_JNI_TEXT_H

#include <jni.h>
#include <stdint.h>
#include <stdlib.h>

/* JNI's modified UTF-8 represents supplementary characters as surrogate pairs;
 * the native frontends instead expect standard UTF-8. */
static char *jni_text_utf8(JNIEnv *env, jstring text) {
    jsize len = (*env)->GetStringLength(env, text);
    const jchar *chars = (*env)->GetStringChars(env, text, NULL);
    if (!chars) return NULL;
    if ((size_t)len > (SIZE_MAX - 1) / 3) {
        (*env)->ReleaseStringChars(env, text, chars);
        jclass oom = (*env)->FindClass(env, "java/lang/OutOfMemoryError");
        if (oom) (*env)->ThrowNew(env, oom, "text is too long");
        return NULL;
    }
    char *out = malloc((size_t)len * 3 + 1);
    if (!out) {
        (*env)->ReleaseStringChars(env, text, chars);
        jclass oom = (*env)->FindClass(env, "java/lang/OutOfMemoryError");
        if (oom) (*env)->ThrowNew(env, oom, "cannot encode speech text");
        return NULL;
    }
    size_t n = 0;
    for (jsize i = 0; i < len; i++) {
        uint32_t cp = chars[i];
        if (cp >= 0xd800 && cp <= 0xdbff && i + 1 < len &&
            chars[i + 1] >= 0xdc00 && chars[i + 1] <= 0xdfff) {
            cp = 0x10000 + ((cp - 0xd800) << 10) + (chars[++i] - 0xdc00);
        } else if (cp >= 0xd800 && cp <= 0xdfff) {
            cp = 0xfffd;
        } else if (cp == 0) {
            cp = ' ';
        }
        if (cp < 0x80) out[n++] = (char)cp;
        else if (cp < 0x800) {
            out[n++] = (char)(0xc0 | (cp >> 6));
            out[n++] = (char)(0x80 | (cp & 0x3f));
        } else if (cp < 0x10000) {
            out[n++] = (char)(0xe0 | (cp >> 12));
            out[n++] = (char)(0x80 | ((cp >> 6) & 0x3f));
            out[n++] = (char)(0x80 | (cp & 0x3f));
        } else {
            out[n++] = (char)(0xf0 | (cp >> 18));
            out[n++] = (char)(0x80 | ((cp >> 12) & 0x3f));
            out[n++] = (char)(0x80 | ((cp >> 6) & 0x3f));
            out[n++] = (char)(0x80 | (cp & 0x3f));
        }
    }
    out[n] = 0;
    (*env)->ReleaseStringChars(env, text, chars);
    return out;
}

#endif
