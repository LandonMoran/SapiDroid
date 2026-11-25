/* zb_vocoder_int.c calls zb_ilsf_repair (it lives in zb_synth_int.c, which would pull in the whole
 * acoustic model). The benchmark only measures the vocoder, so this replaces it; it is force-included
 * before zb_vocoder_int.c. */
#include <stdint.h>
void zb_ilsf_repair(int32_t *lsf, int rows, int p)
{
    (void)lsf;
    (void)rows;
    (void)p;
}
