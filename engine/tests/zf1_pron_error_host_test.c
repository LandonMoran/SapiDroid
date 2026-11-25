#include "../zf1_pron.h"
#include <assert.h>
#include <stdlib.h>

static int allocations, fail_at;
void *__real_malloc(size_t size);
void *__wrap_malloc(size_t size)
{
    if (++allocations == fail_at) return NULL;
    return __real_malloc(size);
}

int main(void)
{
    zf1_pron p = {0};
    zf_char text[] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 0};
    zf_word word = {0};
    zf_wordlist words = {&word, 1, 1};
    word.text = text;

    allocations = 0;
    fail_at = 1;
    assert(zf1_pron_sentence(&p, &words) < 0);

    allocations = 0;
    fail_at = 2;
    assert(zf1_pron_sentence(&p, &words) < 0);
    return 0;
}
