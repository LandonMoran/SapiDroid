/* cc -std=c99 -ffunction-sections -fdata-sections -fsanitize=address,undefined
 * engine/tests/zb_int_pool_host_test.c -Wl,--gc-sections -o zb_int_pool_host_test */
#include <assert.h>
#include "../zb_synth_int.c"

static unsigned char leaf[8] = {1};
static ZbModel *bad_model;
const uint8_t *zb_tree_leaf(const ZbVoice *v, const ZbModel *m, int id, int state, const int *f)
{
    (void)v; (void)id; (void)state; (void)f;
    memset(leaf + 4, m == bad_model ? 255 : 0, 4);
    return leaf;
}
int zb_state_gauss(const ZbVoice *v, const ZbModel *m, const uint8_t *l, ZbGauss *g)
{
    (void)v; (void)m; (void)l; (void)g;
    return 0;
}
static void release(ZbIUtt *um)
{
    free(um->gdur); free(um->gpdur); free(um->gf0); free(um->glsf);
}
int main(void)
{
    unsigned char pool[153] = {0};
    ZbVoice v = {0};
    ZbModel f0 = {0}, lsf = {0}, dur = {0};
    ZbPhone ph = {0};
    ZbUtt u = {1, &ph};
    ZbIUtt um;
    f0.nstate = 1; f0.pool = pool; f0.poolsize = sizeof pool;
    lsf.streamdim[0] = 75; lsf.pool = pool; lsf.poolsize = sizeof pool;
    v.m_lf0 = &f0; v.m_lsf = &lsf; v.m_dur = &dur;
    bad_model = &f0;
    memset(&um, 0, sizeof um);
    assert(ilookup(&v, &u, &um, NULL) == -1);
    release(&um);
    bad_model = &lsf;
    memset(&um, 0, sizeof um);
    assert(ilookup(&v, &u, &um, NULL) == -1);
    release(&um);
    bad_model = NULL;
    memset(&um, 0, sizeof um);
    assert(ilookup(&v, &u, &um, NULL) == 0);
    release(&um);
    return 0;
}
