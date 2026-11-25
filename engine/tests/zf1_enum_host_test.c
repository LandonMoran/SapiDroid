/* cc -std=c99 -ffunction-sections -fdata-sections -fsanitize=address,undefined
 * engine/tests/zf1_enum_host_test.c engine/zf1_ana.c engine/zf1_util.c
 * -Wl,--gc-sections -o zf1_enum_host_test */
#include <assert.h>
#include <string.h>
#include "../zf1_ana.h"

int main(void)
{
    zf_char text[1002];
    zf1_node node = {0};
    zf1_frags fr = {0};
    zf1_enum en = {0};
    zf1_span sp;
    int i;
    for (i = 0; i < 1001; i++) text[i] = 'a';
    text[1001] = 0;
    node.text = text; node.len = 1001;
    fr.nd = &node; fr.nnd = 1;
    zf1_enum_start(&en, &fr, 0);
    assert(zf1_enum_next(&en, &fr, &sp) == 1 && sp.p0 == 0 && sp.p1 == 500);
    assert(zf1_enum_next(&en, &fr, &sp) == 1 && sp.p0 == 500 && sp.p1 == 1000);
    assert(zf1_enum_next(&en, &fr, &sp) == 1 && sp.p0 == 1000 && sp.n1 == -1);
    assert(zf1_enum_next(&en, &fr, &sp) == 0);
    return 0;
}
