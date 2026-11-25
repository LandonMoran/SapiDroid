#include "../../main/cpp/sapi5/sam4fx.c"

#include <assert.h>

static void check_case(int preset, int n)
{
    sam4fx *s16_fx = sam4fx_new(preset, 22050);
    sam4fx *float_fx = sam4fx_new(preset, 22050);
    int16_t *samples = malloc(sizeof(*samples) * (size_t)n);
    float *reference = malloc(sizeof(*reference) * (size_t)n);
    int i;

    assert(s16_fx && float_fx && samples && reference);
    for (i = 0; i < n; i++) {
        samples[i] = (int16_t)(((i * 7919) % 24001) - 12000);
        reference[i] = samples[i];
    }

    sam4fx_process_s16(s16_fx, samples, n);
    sam4fx_process(float_fx, reference, n);
    for (i = 0; i < n; i++) {
        float y = reference[i];
        int16_t expected = (int16_t)(y < -32768.0f ? -32768.0f : y > 32767.0f ? 32767.0f : y);
        assert(samples[i] == expected);
    }

    free(samples);
    free(reference);
    sam4fx_free(s16_fx);
    sam4fx_free(float_fx);
}

int main(void)
{
    check_case(SAM4FX_HALL, 768);
    check_case(SAM4FX_ROBO_A, 1024);
    check_case(SAM4FX_ROBO_A, 1500);
    return 0;
}
