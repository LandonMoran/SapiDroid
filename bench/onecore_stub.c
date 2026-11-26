/* zb_vocoder_int.c calls zb_ilsf_repair (it lives in zb_synth_int.c). When the benchmark includes the
 * MLPG from zb_synth_int.c as well (-DONECORE_HAVE_SYNTH_INT) that file provides it; otherwise this
 * stand-in does, so the vocoder kernels can be measured on their own. */
#include <stdint.h>
#ifndef ONECORE_HAVE_SYNTH_INT
void zb_ilsf_repair(int32_t *lsf, int rows, int p)
{
    (void)lsf;
    (void)rows;
    (void)p;
}
#endif
