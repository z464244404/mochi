#include "music_state.h"

#include <string.h>

static music_state_t s_state;

void music_set(const music_state_t *s)
{
    if (!s) {
        memset(&s_state, 0, sizeof(s_state));
        return;
    }
    s_state = *s;
    s_state.title[sizeof(s_state.title) - 1] = 0;
    s_state.artist[sizeof(s_state.artist) - 1] = 0;
    s_state.valid = s->title[0] != 0;
}

const music_state_t *music_get(void)
{
    return s_state.valid ? &s_state : NULL;
}
