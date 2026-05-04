#include "display.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_lvgl_port.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

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
    {0xC0, (uint8_t[]){0x23},              1, 0},  /* PWCTR1: VRH=4.60V */
    {0xC1, (uint8_t[]){0x10},              1, 0},  /* PWCTR2 */
    {0xC5, (uint8_t[]){0x3E, 0x28},        2, 0},  /* VMCTR1 */
    {0xC7, (uint8_t[]){0x86},              1, 0},  /* VMCTR2 */
    {0xB1, (uint8_t[]){0x00, 0x18},        2, 0},  /* FRMCTR1: 79 Hz */
    {0xB6, (uint8_t[]){0x08, 0x82, 0x27},  3, 0},  /* DFUNCTR */
    {0xE0, (uint8_t[]){0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
                       0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00}, 15, 0},
    {0xE1, (uint8_t[]){0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
                       0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F}, 15, 0},
};

static const ili9341_vendor_config_t s_ili9341_vendor = {
    .init_cmds      = s_ili9341_cmds,
    .init_cmds_size = sizeof(s_ili9341_cmds) / sizeof(s_ili9341_cmds[0]),
};
#endif /* !CONFIG_DISPLAY_TYPE_ST7789 */

lv_disp_t *display_init(void)
{
    /* Backlight off until first frame renders */
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
        .cs_gpio_num         = LCD_CS,
        .dc_gpio_num         = LCD_DC,
        .pclk_hz             = LCD_SPI_CLK_HZ,
        .lcd_cmd_bits        = 8,
        .lcd_param_bits      = 8,
        .spi_mode            = 0,
        .trans_queue_depth   = 10,
        .on_color_trans_done = NULL,
    };
    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST,
                             &lcd_io_cfg, &lcd_io);

    /* ---- Panel init (controller-specific) ---- */
    esp_lcd_panel_handle_t lcd_panel;

#ifdef CONFIG_DISPLAY_TYPE_ST7789
    {
        esp_lcd_panel_dev_config_t panel_cfg = {
            .reset_gpio_num = LCD_RST,
            .rgb_endian     = LCD_RGB_ENDIAN_RGB,   /* ST7789: native RGB */
            .bits_per_pixel = 16,
        };
        esp_lcd_new_panel_st7789(lcd_io, &panel_cfg, &lcd_panel);
        esp_lcd_panel_reset(lcd_panel);
        esp_lcd_panel_init(lcd_panel);
        esp_lcd_panel_invert_color(lcd_panel, true);  /* ST7789 requires inversion */
    }
#else /* ILI9341 */
    {
        esp_lcd_panel_dev_config_t panel_cfg = {
            .reset_gpio_num = LCD_RST,
            .rgb_endian     = LCD_RGB_ENDIAN_BGR,
            .bits_per_pixel = 16,
            .vendor_config  = (void *)&s_ili9341_vendor,
        };
        esp_lcd_new_panel_ili9341(lcd_io, &panel_cfg, &lcd_panel);
        esp_lcd_panel_reset(lcd_panel);
        esp_lcd_panel_init(lcd_panel);
        esp_lcd_panel_invert_color(lcd_panel, false);
    }
#endif

    esp_lcd_panel_set_gap(lcd_panel, 0, 0);
    esp_lcd_panel_disp_on_off(lcd_panel, true);

    /* ---- Touch SPI bus ---- */
    spi_bus_config_t touch_bus = {
        .mosi_io_num   = TOUCH_MOSI,
        .miso_io_num   = TOUCH_MISO,
        .sclk_io_num   = TOUCH_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    spi_bus_initialize(TOUCH_SPI_HOST, &touch_bus, SPI_DMA_DISABLED);

    esp_lcd_panel_io_handle_t touch_io;
    esp_lcd_panel_io_spi_config_t touch_io_cfg = {
        .cs_gpio_num       = TOUCH_CS,
        .dc_gpio_num       = -1,
        .pclk_hz           = TOUCH_SPI_CLK_HZ,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .trans_queue_depth = 3,
    };
    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TOUCH_SPI_HOST,
                             &touch_io_cfg, &touch_io);

    esp_lcd_touch_handle_t touch;
    esp_lcd_touch_config_t touch_cfg = {
        .x_max        = LCD_H_RES,
        .y_max        = LCD_V_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = TOUCH_IRQ,
        .levels       = { .reset = 0, .interrupt = 0 },
        .flags        = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    esp_lcd_touch_new_spi_xpt2046(touch_io, &touch_cfg, &touch);

    /* ---- LVGL port ---- */
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&lvgl_cfg);

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle     = lcd_io,
        .panel_handle  = lcd_panel,
        .buffer_size   = LCD_H_RES * 40,
        .double_buffer = true,
        .hres          = LCD_H_RES,
        .vres          = LCD_V_RES,
        .monochrome    = false,
        .rotation = {
            .swap_xy  = true,
            .mirror_x = false,
#ifdef CONFIG_DISPLAY_TYPE_ST7789
            /* ST7789 on 2432S028S: MV|MY for correct landscape orientation.
             * If the image appears upside-down, try mirror_x=true, mirror_y=false. */
            .mirror_y = true,
#else
            .mirror_y = false,   /* ILI9341: MADCTL 0x28 (MV+BGR) */
#endif
        },
        .flags = { .buff_dma = true, .swap_bytes = false },
    };
    lv_disp_t *disp = lvgl_port_add_disp(&disp_cfg);

    /* Render RGB565 big-endian (high byte first over SPI) at the LVGL level
     * rather than doing a post-render byte-swap, which breaks partial updates. */
    if (lvgl_port_lock(0)) {
        lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
        lvgl_port_unlock();
    }

    /* GRAM is now covered — turn backlight on for a clean first frame */
    gpio_set_level(LCD_BL, 1);

    const lvgl_port_touch_cfg_t tch_cfg = {
        .disp   = disp,
        .handle = touch,
    };
    lvgl_port_add_touch(&tch_cfg);

    ESP_LOGI(TAG, "display ready %dx%d (%s)", LCD_H_RES, LCD_V_RES,
#ifdef CONFIG_DISPLAY_TYPE_ST7789
             "ST7789"
#else
             "ILI9341"
#endif
    );
    return disp;
}
