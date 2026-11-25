/* zb_vocoder.c and zb_vocoder_int.c each have a static softclip(); in the app they are separate
 * translation units, here they are one, so the name is renamed for this include. */
#define softclip zb_softclip_float
#include <zb_vocoder.c>
#undef softclip
