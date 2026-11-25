#include "../zf1_pron_oov.c"
#include <assert.h>

static int parse_output;
size_t zf_strlen(const zf_char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}
int zf1_net_parse(zf1_net *net, const zf_char *text, int len, int start, int mode,
                  const zf_char *category, int tokmode, zf1_netres *res)
{
    (void)net; (void)text; (void)start; (void)mode; (void)category; (void)tokmode;
    if (parse_output) { res->found = 1; res->len = len; }
    return 0;
}
void zf1_netres_free(zf1_netres *res) { (void)res; }
zf_char *zf1_netres_text(const zf1_netres *res)
{
    (void)res;
    zf_char *text = malloc(257 * sizeof *text);
    assert(text);
    text[0] = 'w'; text[1] = 'o'; text[2] = 'r'; text[3] = 'd'; text[4] = ' ';
    for (int i = 5; i < 256; i++) text[i] = 'a';
    text[256] = 0;
    return text;
}
int zf1_lat_next(int mode, const zf_char *text, int len, int pos)
{
    (void)mode; (void)text;
    return pos == 0 ? 4 : len;
}
int zf1_iswupper(zf_char c) { return c >= 'A' && c <= 'Z'; }
int zf1_iswlower(zf_char c) { return c >= 'a' && c <= 'z'; }
int zf1_pron_spell(zf1_pron *p, const zf_char *w, uint16_t *out, int cap, int mode5)
{
    (void)p; (void)w; (void)out; (void)cap; (void)mode5;
    return 1;
}
int zf1_pron_lookup_dom(zf1_pron *p, const zf_char *w, int n, const zf_char *domain, zf1_lexhit *out)
{
    (void)p; (void)w; (void)n; (void)domain;
    out->p[0].ph[0] = 33;
    out->p[0].ph[1] = 0;
    return 1;
}
int zf1_pron_lts_word2(zf1_pron *p, const zf_char *w, uint16_t *out, int cap, int *src, int vscan)
{
    (void)p; (void)w; (void)out; (void)cap; (void)src; (void)vscan;
    return 1;
}
int zf1_phonestr_to_ids(const zf1_phoneset *ps, const zf_char *s, uint16_t *ph, int cap)
{
    (void)ps; (void)s; (void)ph; (void)cap;
    return 0;
}

int main(void)
{
    zf1_pron p = {0};
    uint16_t part[0x200] = {11, 0}, out[16] = {22, 0};
    zf_char long_piece[251];
    zf_char single[] = {'A'};
    zf_char word[] = {'w', 'o', 'r', 'd'};
    zf_char compound[256] = {'w', 'o', 'r', 'd'};
    p.sylmark = 2;
    for (int i = 0; i < 251; i++) long_piece[i] = 'a';

    assert(cpn_piece(&p, long_piece, 251, part, out, 16) == 1);
    assert(out[0] == 22 && out[1] == 0);
    assert(cpn_piece(&p, single, 1, part, out, 16) == 1);
    assert(out[0] == 22 && out[1] == 0);
    assert(cpn_piece(&p, word, 4, part, out, 3) == 1);
    assert(out[0] == 22 && out[1] == 0);
    assert(cpn_piece(&p, word, 4, part, out, 16) == 0);
    assert(out[0] == 22 && out[1] == 2 && out[2] == 33 && out[3] == 0);
    for (int i = 4; i < 255; i++) compound[i] = 'a';
    compound[255] = 0;
    p.compound = (void *)1;
    assert(zf1_pron_compound(&p, compound, out, 16) == 1);
    assert(out[0] == 33 && out[1] == 0);
    parse_output = 1;
    assert(zf1_pron_compound(&p, compound, out, 16) == 1);
    assert(out[0] == 33 && out[1] == 0);
    return 0;
}
