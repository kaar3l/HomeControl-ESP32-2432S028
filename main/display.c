#include "display.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_lvgl_port.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

#ifndef CONFIG_DISPLAY_TYPE_ST7789
#include "esp_lcd_ili9341.h"
#endif

#define TAG "display"

/* ---- Pin mapping — identical for all 2432S028 variants ---- */
#define LCD_SPI_HOST    SPI2_HOST
#define LCD_MOSI        13
#define LCD_MISO        12
#define LCD_SCLK        14
#define LCD_CS          15
#define LCD_DC           2
#define LCD_RST         -1
#define LCD_BL          21
#define LCD_SPI_CLK_HZ  (40 * 1000 * 1000)

#define TOUCH_SPI_HOST   SPI3_HOST
#define TOUCH_MOSI       32
#define TOUCH_MISO       39
#define TOUCH_SCLK       25
#define TOUCH_CS         33
#define TOUCH_IRQ        36
#define TOUCH_SPI_CLK_HZ (2 * 1000 * 1000)

/* ---- ILI9341 vendor init (power + frame-rate + gamma) ---- */
#ifndef CONFIG_DISPLAY_TYPE_ST7789
static const ili9341_lcd_init_cmd_t s_ili9341_cmds[] = {
    {0xC0, (uint8_t[]){0x23},              1, 0},
    {0xC1, (uint8_t[]){0x10},              1, 0},
    {0xC5, (uint8_t[]){0x3E, 0x28},        2, 0},
    {0xC7, (uint8_t[]){0x86},              1, 0},
    {0xB1, (uint8_t[]){0x00, 0x18},        2, 0},
    {0xB6, (uint8_t[]){0x08, 0x82, 0x27},  3, 0},
    {0xE0, (uint8_t[]){0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
                       0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00}, 15, 0},
    {0xE1, (uint8_t[]){0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
                       0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F}, 15, 0},
};
static const ili9341_vendor_config_t s_ili9341_vendor = {
    .init_cmds      = s_ili9341_cmds,
    .init_cmds_size = sizeof(s_ili9341_cmds) / sizeof(s_ili9341_cmds[0]),
};
#endif

/* ---- Calibration screen target positions (TL, TR, BR, BL) ---- */
const int16_t g_touch_cal_scr_x[TOUCH_CAL_COUNT] = { 40, 279, 279,  40};
const int16_t g_touch_cal_scr_y[TOUCH_CAL_COUNT] = { 40,  40, 199, 199};

/* ---- Module-level handles and calibration coefficients ---- */
static esp_lcd_touch_handle_t s_touch;
static lv_disp_t             *s_disp;

/* Affine transform: screen_x = s_cx[0]*rx + s_cx[1]*ry + s_cx[2]
 *                  screen_y = s_cy[0]*rx + s_cy[1]*ry + s_cy[2] */
static float s_cx[3];
static float s_cy[3];

/* ---- 3×3 linear solver (Gaussian elim + partial pivoting) ---- */
static void solve3x3(float A[3][3], float b[3], float x[3])
{
    for (int col = 0; col < 3; col++) {
        /* Partial pivot */
        int pivot = col;
        float maxv = fabsf(A[col][col]);
        for (int row = col + 1; row < 3; row++) {
            if (fabsf(A[row][col]) > maxv) { maxv = fabsf(A[row][col]); pivot = row; }
        }
        if (pivot != col) {
            for (int j = 0; j < 3; j++) { float t = A[col][j]; A[col][j] = A[pivot][j]; A[pivot][j] = t; }
            float t = b[col]; b[col] = b[pivot]; b[pivot] = t;
        }
        /* Eliminate below */
        for (int row = col + 1; row < 3; row++) {
            if (fabsf(A[col][col]) < 1e-9f) continue;
            float f = A[row][col] / A[col][col];
            for (int j = col; j < 3; j++) A[row][j] -= f * A[col][j];
            b[row] -= f * b[col];
        }
    }
    /* Back substitution */
    for (int row = 2; row >= 0; row--) {
        x[row] = b[row];
        for (int j = row + 1; j < 3; j++) x[row] -= A[row][j] * x[j];
        x[row] = (fabsf(A[row][row]) < 1e-9f) ? 0.0f : x[row] / A[row][row];
    }
}

/* Fit affine: screen = k[0]*rx + k[1]*ry + k[2], using all 4 calibration points. */
static void compute_cal_coeffs(const touch_cal_t *cal)
{
    float srx2 = 0, sry2 = 0, srxry = 0, srx = 0, sry = 0;
    float srx_sx = 0, sry_sx = 0, ssx = 0;
    float srx_sy = 0, sry_sy = 0, ssy = 0;

    for (int i = 0; i < TOUCH_CAL_COUNT; i++) {
        float rx = cal->rx[i], ry = cal->ry[i];
        float sx = g_touch_cal_scr_x[i], sy = g_touch_cal_scr_y[i];
        srx2  += rx*rx; sry2  += ry*ry; srxry += rx*ry;
        srx   += rx;    sry   += ry;
        srx_sx += rx*sx; sry_sx += ry*sx; ssx += sx;
        srx_sy += rx*sy; sry_sy += ry*sy; ssy += sy;
    }

    float AtA[3][3] = {
        {srx2,  srxry, srx},
        {srxry, sry2,  sry},
        {srx,   sry,   TOUCH_CAL_COUNT},
    };
    float bx[3] = {srx_sx, sry_sx, ssx};
    float by[3] = {srx_sy, sry_sy, ssy};
    float AtA2[3][3];

    memcpy(AtA2, AtA, sizeof(AtA));
    solve3x3(AtA2, bx, s_cx);

    memcpy(AtA2, AtA, sizeof(AtA));
    solve3x3(AtA2, by, s_cy);

    ESP_LOGI(TAG, "cal X: %.4f*rx + %.4f*ry + %.4f", s_cx[0], s_cx[1], s_cx[2]);
    ESP_LOGI(TAG, "cal Y: %.4f*rx + %.4f*ry + %.4f", s_cy[0], s_cy[1], s_cy[2]);
}

/* ---- LVGL indev read callback ---- */
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint16_t tx[1] = {0}, ty[1] = {0};
    uint8_t  tcount = 0;

    esp_lcd_touch_read_data(s_touch);
    bool pressed = esp_lcd_touch_get_coordinates(s_touch, tx, ty, NULL, &tcount, 1)
                   && (tcount > 0);

    if (pressed) {
        float rx = tx[0], ry = ty[0];
        int sx = (int)(s_cx[0]*rx + s_cx[1]*ry + s_cx[2] + 0.5f);
        int sy = (int)(s_cy[0]*rx + s_cy[1]*ry + s_cy[2] + 0.5f);
        if (sx < 0) sx = 0; else if (sx >= LCD_H_RES) sx = LCD_H_RES - 1;
        if (sy < 0) sy = 0; else if (sy >= LCD_V_RES) sy = LCD_V_RES - 1;
        data->point.x = (lv_coord_t)sx;
        data->point.y = (lv_coord_t)sy;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

lv_disp_t *display_init(void)
{
    gpio_set_direction(LCD_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_BL, 0);

    /* ---- LCD SPI bus ---- */
    spi_bus_config_t lcd_bus = {
        .mosi_io_num     = LCD_MOSI,
        .miso_io_num     = LCD_MISO,
        .sclk_io_num     = LCD_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_H_RES * 40 * sizeof(uint16_t),
    };
    spi_bus_initialize(LCD_SPI_HOST, &lcd_bus, SPI_DMA_CH_AUTO);

    esp_lcd_panel_io_handle_t lcd_io;
    esp_lcd_panel_io_spi_config_t lcd_io_cfg = {
        .cs_gpio_num       = LCD_CS,
        .dc_gpio_num       = LCD_DC,
        .pclk_hz           = LCD_SPI_CLK_HZ,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .spi_mode          = 0,
        .trans_queue_depth = 10,
    };
    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &lcd_io_cfg, &lcd_io);

    /* ---- Panel (controller-specific) ---- */
    esp_lcd_panel_handle_t lcd_panel;
#ifdef CONFIG_DISPLAY_TYPE_ST7789
    {
        esp_lcd_panel_dev_config_t pc = {
            .reset_gpio_num = LCD_RST, .rgb_endian = LCD_RGB_ENDIAN_RGB, .bits_per_pixel = 16,
        };
        esp_lcd_new_panel_st7789(lcd_io, &pc, &lcd_panel);
        esp_lcd_panel_reset(lcd_panel);
        esp_lcd_panel_init(lcd_panel);
        esp_lcd_panel_invert_color(lcd_panel, false);
    }
#else
    {
        esp_lcd_panel_dev_config_t pc = {
            .reset_gpio_num = LCD_RST, .rgb_endian = LCD_RGB_ENDIAN_BGR,
            .bits_per_pixel = 16, .vendor_config = (void *)&s_ili9341_vendor,
        };
        esp_lcd_new_panel_ili9341(lcd_io, &pc, &lcd_panel);
        esp_lcd_panel_reset(lcd_panel);
        esp_lcd_panel_init(lcd_panel);
        esp_lcd_panel_invert_color(lcd_panel, false);
    }
#endif
    esp_lcd_panel_set_gap(lcd_panel, 0, 0);
    esp_lcd_panel_disp_on_off(lcd_panel, true);

    /* ---- Touch SPI bus ---- */
    spi_bus_config_t touch_bus = {
        .mosi_io_num = TOUCH_MOSI, .miso_io_num = TOUCH_MISO,
        .sclk_io_num = TOUCH_SCLK, .quadwp_io_num = -1, .quadhd_io_num = -1,
    };
    spi_bus_initialize(TOUCH_SPI_HOST, &touch_bus, SPI_DMA_DISABLED);

    esp_lcd_panel_io_handle_t touch_io;
    esp_lcd_panel_io_spi_config_t touch_io_cfg = {
        .cs_gpio_num = TOUCH_CS, .dc_gpio_num = -1,
        .pclk_hz = TOUCH_SPI_CLK_HZ, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
        .trans_queue_depth = 3,
    };
    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TOUCH_SPI_HOST, &touch_io_cfg, &touch_io);

    esp_lcd_touch_config_t touch_cfg = {
        .x_max = LCD_H_RES, .y_max = LCD_V_RES,
        .rst_gpio_num = -1, .int_gpio_num = TOUCH_IRQ,
        .levels = {.reset = 0, .interrupt = 0},
        .flags  = {.swap_xy = 0, .mirror_x = 0, .mirror_y = 0},
    };
    esp_lcd_touch_new_spi_xpt2046(touch_io, &touch_cfg, &s_touch);

    /* ---- LVGL port ---- */
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&lvgl_cfg);

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd_io, .panel_handle = lcd_panel,
        .buffer_size = LCD_H_RES * 40, .double_buffer = true,
        .hres = LCD_H_RES, .vres = LCD_V_RES, .monochrome = false,
        .rotation = {
            .swap_xy = true, .mirror_x = false,
#ifdef CONFIG_DISPLAY_TYPE_ST7789
            .mirror_y = true,
#else
            .mirror_y = false,
#endif
        },
        .flags = {.buff_dma = true, .swap_bytes = false},
    };
    s_disp = lvgl_port_add_disp(&disp_cfg);

    if (lvgl_port_lock(0)) {
        lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
        lvgl_port_unlock();
    }

    gpio_set_level(LCD_BL, 1);

    ESP_LOGI(TAG, "display ready %dx%d (%s)", LCD_H_RES, LCD_V_RES,
#ifdef CONFIG_DISPLAY_TYPE_ST7789
             "ST7789"
#else
             "ILI9341"
#endif
    );
    return s_disp;
}

void display_add_touch_indev(const touch_cal_t *cal)
{
    compute_cal_coeffs(cal);

    if (!lvgl_port_lock(100)) return;
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_display(indev, s_disp);
    lvgl_port_unlock();
}

bool display_touch_wait_tap(uint16_t *x, uint16_t *y, uint32_t timeout_ms)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    bool     was_pressed = false;
    uint32_t sum_x = 0, sum_y = 0;
    int      count = 0;

    while (xTaskGetTickCount() < deadline) {
        uint16_t tx[1] = {0}, ty[1] = {0};
        uint8_t  tcount = 0;
        esp_lcd_touch_read_data(s_touch);
        bool pressed = esp_lcd_touch_get_coordinates(s_touch, tx, ty, NULL, &tcount, 1)
                       && (tcount > 0);

        if (pressed) {
            was_pressed = true;
            sum_x += tx[0]; sum_y += ty[0]; count++;
        } else if (was_pressed) {
            if (count >= 3) {
                *x = (uint16_t)(sum_x / count);
                *y = (uint16_t)(sum_y / count);
                return true;
            }
            was_pressed = false; sum_x = 0; sum_y = 0; count = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}
