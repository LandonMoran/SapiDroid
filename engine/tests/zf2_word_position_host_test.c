/* cc -std=c99 -Wall -Wextra -ffunction-sections -fdata-sections -fsanitize=address,undefined
 * engine/tests/zf2_word_position_host_test.c engine/zf2_feat.c engine/zf2_tree.c
 * -Wl,--gc-sections -o zf2_word_position_host_test */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../zf2_int.h"

int main(void)
{
    static const char *const paths[] = {
        "Phone.Syllable.Word.FwPosInIntonationPhrase",
        "Phone.Syllable.Word.BwPosInIntonationPhrase",
        "Phone.Syllable.Word.FwPosInSentence",
        "Phone.Syllable.Word.BwPosInSentence"
    };
    static const int expected[4][4] = {
        {1, 2, 1, 2}, {2, 1, 2, 1}, {1, 2, 3, 4}, {4, 3, 2, 1}
    };
    Z2Sent s = {0};
    Z2W w[6] = {{0}};
    Z2S syl[6] = {{0}};
    Z2P phone[6] = {{0}};
    zf2_voice v = {0};
    zf2_value val[6];
    int i, p;

    v.short_pause = -1;
    s.v = &v; s.w = w; s.nw = 6; s.s = syl; s.ns = 6; s.p = phone; s.np = 6;
    for (i = 0; i < 6; i++) {
        w[i].type = (i == 0 || i == 5) ? 3 : 0;
        w[i].npron = 1; w[i].ns = 1; w[i].s0 = i;
        w[i].bi = (i == 0 || i == 4 || i == 5) ? 5 : i == 2 ? 4 : 3;
        syl[i].word = i; syl[i].p0 = i; syl[i].np = 1;
        phone[i].syl = i; phone[i].id = (i == 0 || i == 5) ? 3 : 7;
    }
    z2_build_phrases(&s, 1);
    assert(s.nph == 6 && s.nip == 4);
    for (p = 0; p < 4; p++) {
        assert(zf2_eval_path(&s, paths[p], val) == 0);
        assert(val[0].kind == -1 && val[5].kind == -1);
        for (i = 1; i <= 4; i++) {
            assert(val[i].kind == 0);
            assert(val[i].value == expected[p][i - 1]);
        }
    }
    free(s.ph); free(s.ip); free(s.pw);
    return 0;
}
