#include "../../../../engine/zb_internal.h"
#include <assert.h>
#include <string.h>

static volatile int cancelled;
static int lookups;

const uint8_t *__wrap_zb_tree_leaf(const ZbVoice *v, const ZbModel *m, int phone, int state, const int *features) {
    static const uint8_t leaf[8] = {0};
    (void)v; (void)m; (void)phone; (void)state; (void)features;
    lookups++;
    cancelled = 1;
    return leaf;
}
int __wrap_zb_state_gauss(const ZbVoice *v, const ZbModel *m, const uint8_t *leaf, ZbGauss *gauss) {
    (void)v; (void)m; (void)leaf; (void)gauss;
    return 0;
}

int main(void) {
    ZbVoice voice;
    ZbModel model;
    ZbPhone phones[2];
    ZbUtt utterance = { 2, phones };
    memset(&voice, 0, sizeof voice);
    memset(&model, 0, sizeof model);
    memset(phones, 0, sizeof phones);
    model.streamdim[0] = 3;
    voice.m_lf0 = voice.m_lsf = voice.m_dur = &model;
    for (int fixed = 0; fixed < 2; fixed++) {
        voice.isint = fixed;
        cancelled = 0;
        lookups = 0;
        assert(zb_synth_cancelable(&voice, &utterance, NULL, NULL, NULL, NULL, &cancelled) == 1);
        assert(lookups == 1);
        lookups = 0;
        assert(zb_synth_cancelable(&voice, &utterance, NULL, NULL, NULL, NULL, &cancelled) == 1);
        assert(lookups == 0);
    }
    return 0;
}
