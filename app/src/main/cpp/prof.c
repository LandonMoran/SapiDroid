/* prof.c - the opt-in profiler behind SAPIDROID_PROFILE. Accumulates named timings from the JNI
 * bridges, the PCM handoff and (through NativeProfile.addNs) the Kotlin audio path, then prints
 * them ranked by total time so a single utterance on a device says where the milliseconds went. */
/* -std=c99 hides clock_gettime and struct timespec behind a feature-test macro on both glibc and
 * bionic; the profiler is the only place in the native layer that reads the monotonic clock. */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include "prof.h"

long long prof_now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000000000LL + (long long)t.tv_nsec;
}

#if defined(SAPIDROID_PROFILE)

enum { PROF_MAX_ROWS = 48, PROF_NAME = 40 };
#define PROF_TAG "SapiDroidProf"

typedef struct {
    char name[PROF_NAME];
    long long total_ns;
    long long count;
    long long bytes;
    long long max_ns;
} prof_row;

static prof_row rows[PROF_MAX_ROWS];
static int nrows;
static int rows_lock;
static long long speak_start_ns;
static int first_block_done;
static int dump_seq;

static void lock_rows(void)
{
    while (__atomic_exchange_n(&rows_lock, 1, __ATOMIC_ACQUIRE)) {
    }
}
static void unlock_rows(void) { __atomic_store_n(&rows_lock, 0, __ATOMIC_RELEASE); }

/* caller holds the lock */
static prof_row *row_for(const char *name)
{
    int i;
    for (i = 0; i < nrows; i++)
        if (!strcmp(rows[i].name, name)) return &rows[i];
    if (nrows >= PROF_MAX_ROWS) return NULL;
    snprintf(rows[nrows].name, PROF_NAME, "%s", name);
    memset(&rows[nrows].total_ns, 0, sizeof rows[nrows].total_ns);
    rows[nrows].count = 0;
    rows[nrows].bytes = 0;
    rows[nrows].max_ns = 0;
    return &rows[nrows++];
}

void prof_add_ns(const char *name, long long ns)
{
    prof_row *r;
    if (!name) return;
    lock_rows();
    r = row_for(name);
    if (r) {
        r->total_ns += ns;
        r->count++;
        if (ns > r->max_ns) r->max_ns = ns;
    }
    unlock_rows();
}

void prof_count_bytes(const char *name, long long bytes)
{
    prof_row *r;
    if (!name) return;
    lock_rows();
    r = row_for(name);
    if (r) r->bytes += bytes;
    unlock_rows();
}

/* "name,total_ns,count,max_ns,bytes;..." for one request, then clears the table. */
int prof_snapshot(char *buf, int cap)
{
    int i, used = 0;
    if (!buf || cap <= 1) return 0;
    buf[0] = 0;
    lock_rows();
    for (i = 0; i < nrows; i++) {
        int n = snprintf(buf + used, (size_t)(cap - used), "%s%s,%lld,%lld,%lld,%lld", i ? ";" : "",
                         rows[i].name, rows[i].total_ns, rows[i].count, rows[i].max_ns, rows[i].bytes);
        if (n < 0 || n >= cap - used) break;
        used += n;
    }
    nrows = 0;
    unlock_rows();
    return used;
}

/* Folds another process's row (already aggregated) into this table. */
void prof_merge(const char *name, long long total_ns, long long count, long long max_ns, long long bytes)
{
    prof_row *r;
    if (!name) return;
    lock_rows();
    r = row_for(name);
    if (r) {
        r->total_ns += total_ns;
        r->count += count;
        r->bytes += bytes;
        if (max_ns > r->max_ns) r->max_ns = max_ns;
    }
    unlock_rows();
}

void prof_mark_speak_start(void)
{
    speak_start_ns = prof_now_ns();
    first_block_done = 0;
}

/* Time from the engine call starting to the first PCM block reaching Java: the user's real
 * "how long before sound" figure, and it includes frontend and warm-up. */
void prof_first_block(void)
{
    long long now = prof_now_ns();
    lock_rows();
    if (!first_block_done) {
        first_block_done = 1;
        {
            prof_row *r = row_for("speak:first_pcm");
            if (r) {
                r->total_ns += now - speak_start_ns;
                r->count++;
                if (now - speak_start_ns > r->max_ns) r->max_ns = now - speak_start_ns;
            }
        }
    }
    unlock_rows();
}

static void emit(const char *line)
{
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, PROF_TAG, "%s", line);
#else
    printf("%s\n", line);
    fflush(stdout);
#endif
}

/* The one formatter: a ranked table as text, so logcat, the host tests and the exported file all
 * say the same thing. */
static int render_table(char *buf, int cap, const char *tag)
{
    int order[PROF_MAX_ROWS];
    int n, i, j, used = 0;
    char line[256];

    if (!buf || cap <= 1) return 0;
    buf[0] = 0;
    lock_rows();
    n = nrows;
    for (i = 0; i < n; i++) order[i] = i;
    /* ranked by total time, most expensive first */
    for (i = 1; i < n; i++) {
        int v = order[i];
        for (j = i - 1; j >= 0 && rows[order[j]].total_ns < rows[v].total_ns; j--) order[j + 1] = order[j];
        order[j + 1] = v;
    }
#define APPEND_LINE(...)                                                                   \
    do {                                                                                   \
        int wrote_ = snprintf(line, sizeof line, __VA_ARGS__);                             \
        if (wrote_ < 0 || wrote_ >= (int)sizeof line) break;                               \
        if (used + wrote_ + 2 > cap) break;                                                \
        memcpy(buf + used, line, (size_t)wrote_);                                          \
        used += wrote_;                                                                    \
        buf[used++] = '\n';                                                                \
        buf[used] = 0;                                                                     \
    } while (0)

    dump_seq++;
    APPEND_LINE("=== profile %s #%d (%d points, ms of accumulated time)", tag ? tag : "", dump_seq, n);
    APPEND_LINE("%-22s %7s %11s %10s %10s %9s", "point", "count", "total ms", "avg us", "max us", "Mbytes");
    for (i = 0; i < n; i++) {
        const prof_row *r = &rows[order[i]];
        double total_ms = (double)r->total_ns / 1e6;
        double avg_us = r->count ? (double)r->total_ns / (double)r->count / 1e3 : 0.0;
        double max_us = (double)r->max_ns / 1e3;
        if (r->bytes) {
            APPEND_LINE("%-22s %7lld %11.3f %10.1f %10.1f %9.2f", r->name, r->count, total_ms, avg_us, max_us,
                        (double)r->bytes / (1024.0 * 1024.0));
        } else {
            APPEND_LINE("%-22s %7lld %11.3f %10.1f %10.1f %9s", r->name, r->count, total_ms, avg_us, max_us, "-");
        }
    }
#undef APPEND_LINE
    unlock_rows();
    return used;
}

int prof_table(char *buf, int cap, const char *tag)
{
    return render_table(buf, cap, tag);
}

void prof_dump(const char *tag)
{
    char all[8192];
    if (render_table(all, (int)sizeof all, tag) > 0) emit(all);
}

void prof_reset(void)
{
    lock_rows();
    nrows = 0;
    speak_start_ns = 0;
    first_block_done = 0;
    dump_seq = 0;
    unlock_rows();
}

#else /* !SAPIDROID_PROFILE: the symbols exist so NativeProfile.addNs/dump never fail to link */

void prof_add_ns(const char *name, long long ns)
{
    (void)name;
    (void)ns;
}
void prof_count_bytes(const char *name, long long bytes)
{
    (void)name;
    (void)bytes;
}
void prof_dump(const char *tag) { (void)tag; }
int prof_table(char *buf, int cap, const char *tag)
{
    (void)tag;
    (void)cap;
    if (buf && cap > 0) buf[0] = 0;
    return 0;
}
void prof_reset(void) { }
void prof_mark_speak_start(void) { }
void prof_first_block(void) { }
int prof_snapshot(char *buf, int cap)
{
    (void)cap;
    if (buf && cap > 0) buf[0] = 0;
    return 0;
}
void prof_merge(const char *name, long long total_ns, long long count, long long max_ns, long long bytes)
{
    (void)name;
    (void)total_ns;
    (void)count;
    (void)max_ns;
    (void)bytes;
}

#endif
