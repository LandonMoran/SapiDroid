/* Internal glue between the libsam modules. */
#ifndef SAM_INTERNAL_H
#define SAM_INTERNAL_H

#include "sam.h"
#include "sam_lex.h"

#if defined(__GNUC__) || defined(__clang__)
static inline int sam_cancelled(const volatile int *cancel)
{
    return cancel && __atomic_load_n(cancel, __ATOMIC_RELAXED) != 0;
}

static inline void sam_cancel_set(volatile int *cancel, int value)
{
    __atomic_store_n(cancel, value, __ATOMIC_RELAXED);
}
#else
static inline int sam_cancelled(const volatile int *cancel) { return cancel && *cancel != 0; }
static inline void sam_cancel_set(volatile int *cancel, int value) { *cancel = value; }
#endif

struct sam_tts {
    sam_voice *voice;
    sam_lexicon *lex;
    sam_lts *lts;
    sam_synth *synth;
    float base_pitch; /* Hz, from the voice's .sdf */
    int monotone;     /* SAPI 4 RoboSoft: flat pitch */
    uint32_t rand_state; /* MSVC rand(): the engine's accent prominences come from it */
    /* quote / parenthesis state of the word builder (engine this+0xe8/0xe9, pitch offset 0xec,
     * range 0xf0, rate 0x28); kept across sentences like the engine does */
    int in_quote, in_paren;
    float st_off, st_rng, st_rate;
    /* output held back until the engine would have flushed it (see sam_tts_speak) */
    sam_pcm_cb out_cb;
    void *out_user;
    int16_t *pend;
    size_t npend, pcap;
    int output_error;
    float transpose; /* sam_params.transpose, for sam_tts_sing */
    /* library hosting (sam_tts_speak_ex): options, running sample count, current sentence span */
    const sam_speak_opts *opts;
    long long ev_total;
    int sent_pos, sent_len;
};

int sam_synth_chunk_pos(const sam_synth *s);
void sam_synth_chunk_reset(sam_synth *s);

/* Raw access to a section of the .spd voice file (0..4). */
const uint8_t *sam_voice_section(const sam_voice *v, int index, uint32_t *size);

#endif
