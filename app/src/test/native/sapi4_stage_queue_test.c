#include <stdlib.h>

static int fail_alloc;
static void *test_malloc(size_t n) { return fail_alloc ? NULL : malloc(n); }
#define malloc test_malloc
#include "../../main/cpp/sapi4/lib/sapi4tts.c"
#undef malloc
#include <assert.h>

static uint32_t queued;
static int fail_take;

uint32_t queue_count(Queue *q) { (void)q; return queued; }
int queue_take_all(Queue *q, QItem *out, uint32_t capacity, uint32_t *count) {
    (void)q;
    if (fail_take || queued > capacity || (queued && !out)) return 0;
    for (uint32_t i = 0; i < queued; i++) out[i].code = i + 1;
    *count = queued;
    queued = 0;
    return 1;
}

int main(void) {
    Queue q = {0};
    QItem *items = (QItem *)&q;
    uint32_t count = 99;
    fail_alloc = 1;
    assert(queue_take_items(&q, &items, &count));
    assert(!items && count == 0);

    queued = 2;
    assert(!queue_take_items(&q, &items, &count));
    assert(queued == 2);
    fail_alloc = 0;
    assert(queue_take_items(&q, &items, &count));
    assert(count == 2 && items[0].code == 1 && items[1].code == 2);
    free(items);

    fail_take = 1;
    assert(!queue_take_items(&q, &items, &count));
    return 0;
}
