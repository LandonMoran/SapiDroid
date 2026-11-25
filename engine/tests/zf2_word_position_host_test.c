#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include "../zf2_int.h"

static int fail_malloc, fail_calloc;

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__wrap_malloc(size_t size)
{
    if (fail_malloc) { fail_malloc = 0; return NULL; }
    return __real_malloc(size);
}
void *__wrap_calloc(size_t count, size_t size)
{
    if (fail_calloc) { fail_calloc = 0; return NULL; }
    return __real_calloc(count, size);
}

static void *check_features(void *arg)
{
    const Z2Sent *s = arg;
    for (int pass = 0; pass < 2; pass++) {
        ZbUtt out = {0};
        zf2_value vals[12];
        assert(zf2_features(s, &out) == 0 && out.nphone == s->np);
        for (int f = 0; f < ZB_NFEAT; f++) {
            int rc = zf2_eval_path(s, zb_feature_path[f], vals);
            for (int i = 0; i < s->np; i++) {
                int expected = rc || vals[i].kind < 0 ? ZB_NULL : vals[i].value;
                assert(out.ph[i].f[f] == expected);
            }
        }
        zf2_utt_free(&out);
    }
    return NULL;
}

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
    Z2P phone[12] = {{0}};
    zf2_voice v = {0};
    zf2_value val[12];
    int i, p;

    v.short_pause = -1;
    s.v = &v; s.w = w; s.nw = 6; s.s = syl; s.ns = 6; s.p = phone; s.np = 12;
    for (i = 0; i < 6; i++) {
        w[i].frag = -1;
        w[i].type = (i == 0 || i == 5) ? 3 : 0;
        w[i].npron = 1; w[i].ns = 1; w[i].s0 = i;
        w[i].bi = (i == 0 || i == 4 || i == 5) ? 5 : i == 2 ? 4 : 1;
        syl[i].word = i; syl[i].p0 = 2 * i; syl[i].np = 2;
        phone[2 * i].syl = phone[2 * i + 1].syl = i;
        phone[2 * i].id = (i == 0 || i == 5) ? 3 : 7;
        phone[2 * i + 1].id = (i == 0 || i == 5) ? 3 : 8;
    }
    z2_build_phrases(&s, 1);
    assert(s.nph == 4 && s.nip == 4);
    for (p = 0; p < 4; p++) {
        assert(zf2_eval_path(&s, paths[p], val) == 0);
        assert(val[0].kind == -1 && val[1].kind == -1);
        assert(val[10].kind == -1 && val[11].kind == -1);
        for (i = 1; i <= 4; i++) {
            assert(val[2 * i].kind == 0 && val[2 * i + 1].kind == 0);
            assert(val[2 * i].value == expected[p][i - 1]);
            assert(val[2 * i + 1].value == expected[p][i - 1]);
        }
    }
    assert(zf2_eval_path(&s, "Phone.PhoneIdentity", val) == 0);
    for (i = 0; i < 12; i++) assert(val[i].value == phone[i].id);
    assert(zf2_eval_path(&s, "Phone.Sentence.WordNumber", val) == 0);
    for (i = 0; i < 12; i++) assert(val[i].kind == 0 && val[i].value == 4);
    {
        ZbUtt out = {0};
        fail_malloc = 1;
        assert(zf2_features(&s, &out) == -1 && !out.ph && out.nphone == 0);
        fail_calloc = 1;
        assert(zf2_features(&s, &out) == -1 && !out.ph && out.nphone == 0);
    }
    {
        pthread_t thread;
        assert(pthread_create(&thread, NULL, check_features, &s) == 0);
        check_features(&s);
        assert(pthread_join(thread, NULL) == 0);
    }
    free(s.ph); free(s.ip); free(s.pw);
    return 0;
}
