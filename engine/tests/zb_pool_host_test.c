/* cc -std=c99 -ffunction-sections -fdata-sections -fsanitize=address,undefined
 * engine/tests/zb_pool_host_test.c engine/zb_apm.c -Wl,--gc-sections -o zb_pool_host_test */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../zb_internal.h"

static void put32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

int main(void)
{
    unsigned char pool[12] = {0, 0, 128, 63, 0, 0, 0, 64, 0, 0, 64, 64};
    unsigned char leaf[8] = {1};
    ZbModel m = {0};
    ZbGauss g = {0};
    m.pool = pool;
    m.poolsize = sizeof pool;
    m.nstream = 1;
    m.streamdim[0] = 1;
    assert(zb_state_gauss(NULL, &m, leaf, &g) == 0);
    assert(g.w == 1.0f && g.mp[0] == 2.0f && g.p[0] == 3.0f);
    leaf[4] = sizeof pool - 1;
    assert(zb_state_gauss(NULL, &m, leaf, &g) == -1);
    memset(leaf + 4, 255, 4);
    assert(zb_state_gauss(NULL, &m, leaf, &g) == -1);
    leaf[4] = leaf[5] = leaf[6] = leaf[7] = 0;
    m.streamdim[0] = -1;
    assert(zb_state_gauss(NULL, &m, leaf, &g) == -1);
    {
        unsigned char apm[0x78] = {0};
        const char *path = "zb_pool_host_test_fixture.apm";
        char err[128];
        ZbVoice v = {0};
        FILE *f;
        memcpy(apm, "APM ", 4);
        put32(apm + 0x18, 3);
        put32(apm + 0x18 + 0x1c, 0x58);
        put32(apm + 0x18 + 0x24, 0x70);
        put32(apm + 0x60, 1);
        put32(apm + 0x70, 2);
        f = fopen(path, "wb");
        assert(f && fwrite(apm, 1, sizeof apm, f) == sizeof apm);
        assert(fclose(f) == 0);
        assert(zb_apm_load(&v, path, err, sizeof err) == -1);
        assert(strstr(err, "directory truncated"));
        free(v.q[0].val);
        free(v.q);
        free(v.apm);
        assert(remove(path) == 0);
    }
    return 0;
}
