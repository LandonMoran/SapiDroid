/* prof.h - opt-in timing of the synthesis path (voice load, warm-up, speak, JNI handoff, audio
 * queue). Compiled in only when the build defines SAPIDROID_PROFILE (gradle -Pprofile); without it
 * every macro here expands to nothing, so release builds carry no code and no calls.
 *
 * The Kotlin side reports into the same table through NativeProfile.addNs, so a device log shows
 * one ranked list with both native and Java-side costs. */
#ifndef SAPIDROID_PROF_H
#define SAPIDROID_PROF_H

typedef struct {
    const char *name;
    long long start;
} prof_scope;

long long prof_now_ns(void);
void prof_add_ns(const char *name, long long ns);      /* accumulate one sample */
void prof_count_bytes(const char *name, long long bytes);
void prof_dump(const char *tag);                      /* ranked table to logcat/stdout */
void prof_reset(void);
void prof_mark_speak_start(void);                     /* start of one engine speak call */
void prof_first_block(void);                          /* first PCM block of that call, once */

#if defined(SAPIDROID_PROFILE)

#define PROF_ON 1

static inline prof_scope prof_begin(const char *name)
{
    prof_scope s;
    s.name = name;
    s.start = prof_now_ns();
    return s;
}
static inline void prof_end(prof_scope s) { prof_add_ns(s.name, prof_now_ns() - s.start); }

#define PROF_SCOPE(name) prof_scope _prof_scope = prof_begin(name)
#define PROF_END() prof_end(_prof_scope)

#else

#define PROF_ON 0
#define PROF_SCOPE(name) do { } while (0)
#define PROF_END() do { } while (0)

#endif
#endif
