#pragma once
#include "lvgl.h"

/* LCD resolution */
#define LCD_H_RES  320
#define LCD_V_RES  240

/* Initialise ILI9341, XPT2046 touch, and LVGL port.
   Returns the LVGL display handle. */
lv_disp_t *display_init(void);
