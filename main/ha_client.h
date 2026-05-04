#pragma once
#include <stdbool.h>

typedef enum {
    LOCK_STATE_UNKNOWN,
    LOCK_STATE_LOCKED,
    LOCK_STATE_UNLOCKED,
} lock_state_t;

typedef void (*lock_state_cb_t)(lock_state_t state);

void ha_client_init(const char *base_url, const char *token, lock_state_cb_t cb);
void ha_client_poll(void);   /* call periodically from main loop */
bool ha_client_lock(void);
bool ha_client_unlock(void);
lock_state_t ha_client_get_state(void);
