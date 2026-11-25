#include "../../main/cpp/sapi4/lib/sapi4tts.c"
#include <assert.h>

static int cancelled_before_start(void *user) {
    s4_engine *e = user;
    assert(atomic_load(&e->speaking) && !atomic_load(&e->stop));
    return 1;
}

static int cancelled_during_check(void *user) {
    s4_engine *e = user;
    s4_stop(e);
    return 0;
}

int main(void) {
    FrontEnd fe = {0};
    s4_engine e = {0};
    fe.abort_event = vc_CreateEventA(1, 0);
    assert(fe.abort_event);
    e.fe = &fe;
    assert(s4_speak_checked(&e, "cancel", 0, NULL, NULL, cancelled_before_start, &e) == 1);
    assert(!atomic_load(&e.speaking) && !atomic_load(&e.stop));
    assert(s4_speak_checked(&e, "cancel", 0, NULL, NULL, cancelled_during_check, &e) == 1);
    assert(!atomic_load(&e.speaking) && !atomic_load(&e.stop));
    vc_CloseHandle(fe.abort_event);
    return 0;
}
