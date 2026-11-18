#include "pc_state.h"

#include <string.h>

static pc_state_t s_state;

void pc_set(const pc_state_t *s)
{
    if (!s) {
        memset(&s_state, 0, sizeof(s_state));
        return;
    }
    s_state = *s;
    s_state.host[sizeof(s_state.host) - 1] = 0;
    s_state.valid = true;
}

const pc_state_t *pc_get(void)
{
    return s_state.valid ? &s_state : NULL;
}
