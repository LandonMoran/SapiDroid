/* zb_synth_int.c and zb_vocoder_int.c both include zb_voctab_int.h, which carries its tables as
 * static arrays with no include guard. In the app they are separate translation units; the benchmark
 * puts them in one, so the second copy's tables are renamed here (the same trick that keeps the two
 * static softclip()s apart in onecore_wrap_float.c / onecore_wrap_int.c). Only the acoustic-model
 * driver functions use these names, and those are dropped by --gc-sections. */
#define zb_cos2048 zb_cos2048_synth
#define zb_exp1331 zb_exp1331_synth
#define zb_f0exp800 zb_f0exp800_synth
#include <zb_synth_int.c>
