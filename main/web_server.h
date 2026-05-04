#pragma once
#include "settings.h"

typedef void (*settings_saved_cb_t)(const app_settings_t *new_settings);

void web_server_start(app_settings_t *settings, settings_saved_cb_t cb);
void web_server_stop(void);
