#pragma once
#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

/* LCD resolution (landscape) */
#define LCD_H_RES  320
#define LCD_V_RES  240

/* Number of calibration crosshair targets */
#define TOUCH_CAL_COUNT 4

/* Screen-pixel positions of the TOUCH_CAL_COUNT calibration targets
 * (order: top-left, top-right, bottom-right, bottom-left) */
extern const int16_t g_touch_cal_scr_x[TOUCH_CAL_COUNT];
extern const int16_t g_touch_cal_scr_y[TOUCH_CAL_COUNT];

/* Raw XPT2046 driver-normalised values recorded at each calibration point.
 * Used to solve the affine transform: screen = A * [rx ry 1]^T */
typedef struct {
    uint16_t rx[TOUCH_CAL_COUNT];
    uint16_t ry[TOUCH_CAL_COUNT];
} touch_cal_t;

/* Initialise display hardware, touch IC, and LVGL. Returns the LVGL display handle.
   The touch input device is NOT registered; call display_add_touch_indev() afterwards. */
lv_disp_t *display_init(void);

/* Register the LVGL pointer indev using a 4-point affine calibration.
   Must be called once from a non-LVGL task after calibration data is ready. */
void display_add_touch_indev(const touch_cal_t *cal);

/* Block the calling task until a tap (press+release) is detected; returns averaged
   raw touch coordinates. Returns false if timeout_ms elapses without a valid tap. */
bool display_touch_wait_tap(uint16_t *x, uint16_t *y, uint32_t timeout_ms);
