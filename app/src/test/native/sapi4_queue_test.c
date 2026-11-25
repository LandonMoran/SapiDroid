#include "../../main/cpp/sapi4/src/queue.c"
#include <assert.h>
#include <stdlib.h>

static uint32_t next_event = 1;
static const char queue_name[] = "queue";

void *decomp_dll_data(uint32_t addr) { (void)addr; return (void *)queue_name; }
void *vc_malloc(size_t n) { return malloc(n); }
void *vc_realloc(void *p, size_t n) { return realloc(p, n); }
void vc_free(void *p) { free(p); }
void vc_EnterCriticalSection(void *cs) { (void)cs; }
void vc_LeaveCriticalSection(void *cs) { (void)cs; }
void vc_InitializeCriticalSection(void *cs) { (void)cs; }
uint32_t vc_CreateEventA(int32_t manual, int32_t initial) {
    (void)manual;
    (void)initial;
    return next_event++;
}
int32_t vc_SetEvent(uint32_t h) { (void)h; return 1; }
int32_t vc_ResetEvent(uint32_t h) { (void)h; return 1; }

int main(void) {
    Queue q = {0};
    queue_ctor(&q);
    QItem in[3] = {
        { .code = 1, .v2 = 10 },
        { .code = 2, .v2 = 20 },
        { .code = 3, .v2 = 30 },
    };
    assert(queue_push_n(&q, in, 3));

    QItem out[3];
    uint32_t count = 99;
    assert(!queue_take_all(&q, out, 2, &count));
    assert(queue_count(&q) == 3 && count == 99);

    assert(queue_take_all(&q, out, 3, &count));
    assert(count == 3 && queue_count(&q) == 0);
    for (uint32_t i = 0; i < count; i++) {
        assert(out[i].code == in[i].code);
        assert(out[i].v2 == in[i].v2);
    }

    QItem item;
    assert(queue_push(&q, &in[0]));
    assert(queue_pop(&q, &item) && item.code == in[0].code);
    assert(!queue_pop(&q, &item));

    assert(queue_take_all(&q, NULL, 0, &count));
    assert(count == 0);
    vc_free(q.items);
    return 0;
}
