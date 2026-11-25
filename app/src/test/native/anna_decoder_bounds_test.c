#include "../../main/cpp/anna/anna_dec_wmav.c"
#include <assert.h>

int main(void)
{
    anna_decoder d = {0};
    int16_t sample = 123;

    assert(anna_decoder_read(NULL, 0, 1, &sample) == -1);
    assert(anna_decoder_read(&d, -1, 1, &sample) == -1);
    assert(anna_decoder_read(&d, 0, -1, &sample) == -1);
    assert(anna_decoder_read(&d, 0, 1, NULL) == -1);
    assert(anna_decoder_read(&d, 0, 0, &sample) == 0);
    assert(sample == 123);
    return 0;
}
