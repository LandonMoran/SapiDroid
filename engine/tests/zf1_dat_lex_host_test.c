/* cc -std=c99 -Wall -Wextra -ffunction-sections -fdata-sections -fsanitize=address,undefined
 * engine/tests/zf1_dat_lex_host_test.c engine/zf1_lex.c engine/zf1_util.c
 * -Wl,--gc-sections -o zf1_dat_lex_host_test */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../zf1_lex.h"
#include "../zf1_dat.c"

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

int main(void)
{
    uint8_t bytes[80] = {0};
    zf1_dat d = {0};
    zf1_lex lx;
    zf1_lexent ent;
    zf_char word[] = {'a', 0};
    uint8_t lex[0xa0] = {0};

    d.buf = bytes;
    d.size = sizeof bytes;
    memset(bytes + 32, 0xff, 8);
    walk(&d, 0, sizeof bytes, 2);
    assert(d.nres == 0);

    memset(bytes, 0, sizeof bytes);
    put32(bytes + 32, 4);
    walk(&d, 0, 44, 2);
    assert(d.nres == 1 && d.res[0].p == bytes + 40 && d.res[0].n == 4);

    put32(lex + 0x24, sizeof lex);
    assert(zf1_lex_init(&lx, lex, sizeof lex) < 0);
    zf1_lex_free(&lx);
    memset(&lx, 0, sizeof lx);
    lx.L = lex;
    assert(zf1_lex_lookup(&lx, word, 1, &ent) == 0);
    return 0;
}
