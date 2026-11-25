/* Host test: the en-US phone ids the frontend produces must be translated into the phone numbering the en-GB and
 * en-AU acoustic models were trained on (zf2_tree.c z2_phone_local, zb_apm.c zb_voice_phone_set).
 *
 * cc -std=c99 -O2 -ffunction-sections -fdata-sections engine/tests/zf2_phone_map_host_test.c engine/zb_apm.c \
 *    -Wl,--gc-sections -lm -o zf2_phone_map_host_test */
#include "../zf2_tree.c"
#include "../zb_internal.h"
#include <assert.h>
#include <string.h>

/* The en-GB/en-AU phone set, as Microsoft ships it in MSTTSLocEnGB.dat / MSTTSLocEnAU.dat (resource
 * 29a5584b/153f1b64), indexed by its own ids. Same phones as the en-US set above plus AR, IR, O, OO, UR and J,
 * and numbered from AA = 6 instead of AA = 7. */
static const char *const gb_name[50] = {
    "", "&", "-", "-SIL-", "1", "2", "AA", "AE", "AH", "AR", "AW", "AX", "AY", "B", "CH", "D", "DH", "EH", "ER",
    "EY", "F", "G", "H", "IH", "IR", "IY", "J", "JH", "K", "L", "M", "N", "NG", "O", "OO", "OW", "OY", "P", "R",
    "S", "SH", "T", "TH", "UH", "UR", "UW", "V", "W", "Z", "ZH"
};

/* The phone each en-US id must be asked about in the second database. Three have no same-named counterpart: a
 * short pause is a silence there, and the GB lexicon spells the sounds of AO and Y as OO ("thought") and J
 * ("yes"). */
static const char *gb_name_of(int us)
{
    const char *name = z2_phone_name[us];
    if (!strcmp(name, "-SP-")) return "-SIL-";
    if (!strcmp(name, "AO")) return "OO";
    if (!strcmp(name, "Y")) return "J";
    return name;
}

static void question_set(ZbVoice *v, ZbQuestion *q, int32_t *vals, const int *ids, int n, int feat)
{
    int i;
    memset(v, 0, sizeof *v);
    memset(q, 0, sizeof *q);
    for (i = 0; i < n; i++) vals[i] = ids[i];
    q->feat = feat;
    q->nval = n;
    q->val = vals;
    v->q = q;
    v->nq = 1;
}

int main(void)
{
    int us, gb_questions[] = {3, 6, 7, 24, 25, 33, 44, 48, 49};
    int us_questions[] = {3, 7, 8, 24, 26, 33, 42, 46, 47};
    ZbVoice v;
    ZbQuestion q;
    int32_t qvals[16];

    /* the en-US database is the one the frontend speaks: no translation */
    for (us = 1; us < Z2_MAXPH; us++) assert(z2_phone_local(us, ZB_PHONES_EN_US) == us);

    /* every en-US phone is asked about as the same phone in the other database */
    for (us = 1; us < Z2_MAXPH; us++) {
        const char *name = z2_phone_name[us];
        int got;
        if (!*name) continue;
        got = z2_phone_local(us, ZB_PHONES_EN_GB);
        assert(got > 0 && got < 50);
        assert(!strcmp(gb_name[got], gb_name_of(us)));
    }

    /* a few ids spelled out with their comments, so the table cannot drift silently */
    assert(z2_phone_local(7, ZB_PHONES_EN_GB) == 6);    /* AA */
    assert(z2_phone_local(9, ZB_PHONES_EN_GB) == 8);    /* AH */
    assert(z2_phone_local(10, ZB_PHONES_EN_GB) == 34);  /* AO -> OO */
    assert(z2_phone_local(25, ZB_PHONES_EN_GB) == 25);  /* unused id, left alone */
    assert(z2_phone_local(26, ZB_PHONES_EN_GB) == 25);  /* IY */
    assert(z2_phone_local(33, ZB_PHONES_EN_GB) == 35);  /* OW */
    assert(z2_phone_local(42, ZB_PHONES_EN_GB) == 45);  /* UW */
    assert(z2_phone_local(45, ZB_PHONES_EN_GB) == 26);  /* Y -> J */
    assert(z2_phone_local(47, ZB_PHONES_EN_GB) == 49);  /* ZH */
    assert(z2_phone_local(48, ZB_PHONES_EN_GB) == 48);  /* out of range, left alone */

    /* the voice is classified from its own phone-identity questions, not from its name */
    question_set(&v, &q, qvals, gb_questions, (int)(sizeof gb_questions / sizeof gb_questions[0]), ZB_F_PhoneIdentity);
    assert(zb_voice_phone_set(&v) == ZB_PHONES_EN_GB);
    question_set(&v, &q, qvals, us_questions, (int)(sizeof us_questions / sizeof us_questions[0]), ZB_F_PrevPhoneIdentity);
    assert(zb_voice_phone_set(&v) == ZB_PHONES_EN_US);
    /* a question that does not name a phone does not decide anything */
    question_set(&v, &q, qvals, gb_questions, (int)(sizeof gb_questions / sizeof gb_questions[0]), ZB_F_SylFwPosInWord);
    assert(zb_voice_phone_set(&v) == ZB_PHONES_EN_US);
    /* and a voice with no questions at all is en-US */
    memset(&v, 0, sizeof v);
    assert(zb_voice_phone_set(&v) == ZB_PHONES_EN_US);
    return 0;
}
