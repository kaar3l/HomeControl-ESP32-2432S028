#include "ui.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdbool.h>

#define TAG "ui"

/* ---- Dark-theme colour palette ---- */
#define CLR_BG        lv_color_hex(0x0A0A0A)  /* near-black background */
#define CLR_SURFACE   lv_color_hex(0x1A1A1A)  /* panel surface */
#define CLR_LOCKED    lv_color_hex(0xEF5350)  /* red accent */
#define CLR_UNLOCKED  lv_color_hex(0x1565C0)  /* blue accent */
#define CLR_VENT_ACT  lv_color_hex(0x7B1FA2)  /* purple – active vent */
#define CLR_VENT_IDL  lv_color_hex(0x2C2C2C)  /* dark idle button */
#define CLR_TEXT      lv_color_hex(0xDEDEDE)  /* primary text */
#define CLR_STATUS    lv_color_hex(0x888888)  /* secondary / dim text */
#define CLR_FEEDBACK  lv_color_hex(0x4CAF50)  /* green status line */

/* Slightly lighter tints used for button pressed states */
#define CLR_LOCKED_PR   lv_color_hex(0xFF7B7B)
#define CLR_UNLOCKED_PR lv_color_hex(0x2196F3)
#define CLR_VENT_IDL_PR lv_color_hex(0x3D3D3D)
#define CLR_VENT_ACT_PR lv_color_hex(0x9C27B0)

static ui_lock_action_cb_t s_lock_cb;
static ui_vent_action_cb_t s_vent_cb;

/* widgets we need to update later */
static lv_obj_t  *s_wifi_label;
static lv_obj_t  *s_mqtt_label;
static lv_obj_t  *s_lock_label;
static lv_obj_t  *s_lock_btn;
static lv_obj_t  *s_lock_btn_label;
static lv_obj_t  *s_vent_btns[3];
static lv_obj_t  *s_feedback_label;
static lv_timer_t *s_feedback_timer;

static lock_state_t s_lock_state = LOCK_STATE_UNKNOWN;
static uint8_t      s_vent_speed = 3;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Style a container: opaque dark surface, no border, custom radius */
static void style_panel(lv_obj_t *obj, int radius)
{
    lv_obj_set_style_bg_color(obj, CLR_SURFACE, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
}

/* Style a button: explicit default AND pressed colours so the
 * LVGL built-in white-overlay never flashes on touch. */
static void style_btn(lv_obj_t *btn, lv_color_t clr_default, lv_color_t clr_pressed)
{
    lv_obj_set_style_bg_color(btn, clr_default,  LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa (btn, LV_OPA_COVER,  LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, clr_pressed,  LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa (btn, LV_OPA_COVER,  LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
}

/* ------------------------------------------------------------------ */
/* Button callbacks                                                    */
/* ------------------------------------------------------------------ */
static void lock_btn_cb(lv_event_t *e)
{
    if (!s_lock_cb) return;
    bool do_lock = (s_lock_state == LOCK_STATE_UNLOCKED);
    s_lock_cb(do_lock);
    /* feedback is set by on_lock_action before queuing the command */
}

static void vent_btn_cb(lv_event_t *e)
{
    uint8_t *speed = (uint8_t *)lv_event_get_user_data(e);
    if (!s_vent_cb || !speed) return;
    s_vent_cb(*speed);
}

static void feedback_clear_cb(lv_timer_t *t)
{
    (void)t;
    lv_label_set_text(s_feedback_label, "");
    lv_timer_pause(s_feedback_timer);
}

/* ------------------------------------------------------------------ */
/* Build the main screen                                               */
/* ------------------------------------------------------------------ */
static const uint8_t VENT_SPEEDS[3] = {2, 3, 4};
static const char   *VENT_LABELS[3] = {"Low", "Normal", "Fast"};

void ui_init(ui_lock_action_cb_t lock_cb, ui_vent_action_cb_t vent_cb)
{
    s_lock_cb = lock_cb;
    s_vent_cb = vent_cb;

    if (!lvgl_port_lock(0)) return;

    /* Black root screen — explicit bg_opa so nothing leaks through */
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* ---- Status bar ---- */
    lv_obj_t *statusbar = lv_obj_create(scr);
    lv_obj_set_size(statusbar, 320, 28);
    lv_obj_align(statusbar, LV_ALIGN_TOP_LEFT, 0, 0);
    style_panel(statusbar, 0);
    lv_obj_set_style_pad_all(statusbar, 4, 0);

    s_wifi_label = lv_label_create(statusbar);
    lv_label_set_text(s_wifi_label, "Wi-Fi: connecting...");
    lv_obj_set_style_text_color(s_wifi_label, CLR_STATUS, 0);
    lv_obj_align(s_wifi_label, LV_ALIGN_LEFT_MID, 0, 0);

    s_mqtt_label = lv_label_create(statusbar);
    lv_label_set_text(s_mqtt_label, "MQTT -");
    lv_obj_set_style_text_color(s_mqtt_label, CLR_STATUS, 0);
    lv_obj_align(s_mqtt_label, LV_ALIGN_RIGHT_MID, 0, 0);

    /* ---- Lock panel (left) ---- */
    lv_obj_t *lock_panel = lv_obj_create(scr);
    lv_obj_set_size(lock_panel, 148, 190);
    lv_obj_align(lock_panel, LV_ALIGN_TOP_LEFT, 4, 32);
    style_panel(lock_panel, 8);
    lv_obj_set_style_pad_all(lock_panel, 8, 0);

    lv_obj_t *lock_title = lv_label_create(lock_panel);
    lv_label_set_text(lock_title, "Front Door");
    lv_obj_set_style_text_color(lock_title, CLR_TEXT, 0);
    lv_obj_align(lock_title, LV_ALIGN_TOP_MID, 0, 0);

    s_lock_label = lv_label_create(lock_panel);
    lv_label_set_text(s_lock_label, LV_SYMBOL_EYE_CLOSE " ...");
    lv_obj_set_style_text_color(s_lock_label, CLR_STATUS, 0);
    lv_obj_set_style_text_font(s_lock_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_lock_label, LV_ALIGN_CENTER, 0, -14);

    s_lock_btn = lv_btn_create(lock_panel);
    lv_obj_set_size(s_lock_btn, 120, 44);
    lv_obj_align(s_lock_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
    style_btn(s_lock_btn, CLR_UNLOCKED, CLR_UNLOCKED_PR);
    lv_obj_add_event_cb(s_lock_btn, lock_btn_cb, LV_EVENT_CLICKED, NULL);

    s_lock_btn_label = lv_label_create(s_lock_btn);
    lv_label_set_text(s_lock_btn_label, "...");
    lv_obj_center(s_lock_btn_label);
    lv_obj_set_style_text_color(s_lock_btn_label, CLR_TEXT, 0);

    /* ---- Vent panel (right) ---- */
    lv_obj_t *vent_panel = lv_obj_create(scr);
    lv_obj_set_size(vent_panel, 156, 190);
    lv_obj_align(vent_panel, LV_ALIGN_TOP_RIGHT, -4, 32);
    style_panel(vent_panel, 8);
    lv_obj_set_style_pad_all(vent_panel, 8, 0);

    lv_obj_t *vent_title = lv_label_create(vent_panel);
    lv_label_set_text(vent_title, "Ventilation");
    lv_obj_set_style_text_color(vent_title, CLR_TEXT, 0);
    lv_obj_align(vent_title, LV_ALIGN_TOP_MID, 0, 0);

    for (int i = 0; i < 3; i++) {
        s_vent_btns[i] = lv_btn_create(vent_panel);
        lv_obj_set_size(s_vent_btns[i], 128, 44);
        lv_obj_align(s_vent_btns[i], LV_ALIGN_TOP_MID, 0, 22 + i * 52);
        style_btn(s_vent_btns[i], CLR_VENT_IDL, CLR_VENT_IDL_PR);
        lv_obj_add_event_cb(s_vent_btns[i], vent_btn_cb, LV_EVENT_CLICKED,
                            (void *)&VENT_SPEEDS[i]);

        lv_obj_t *lbl = lv_label_create(s_vent_btns[i]);
        lv_label_set_text(lbl, VENT_LABELS[i]);
        lv_obj_center(lbl);
        lv_obj_set_style_text_color(lbl, CLR_TEXT, 0);
    }

    /* ---- Feedback label (bottom) ---- */
    s_feedback_label = lv_label_create(scr);
    lv_label_set_text(s_feedback_label, "");
    lv_obj_set_style_text_color(s_feedback_label, CLR_FEEDBACK, 0);
    lv_obj_align(s_feedback_label, LV_ALIGN_BOTTOM_MID, 0, -4);

    s_feedback_timer = lv_timer_create(feedback_clear_cb, 3500, NULL);
    lv_timer_pause(s_feedback_timer);

    lvgl_port_unlock();

    ui_set_vent_speed(s_vent_speed);
}

/* ------------------------------------------------------------------ */
/* State update functions (call from any task — acquire LVGL lock)    */
/* ------------------------------------------------------------------ */
void ui_set_wifi_state(wifi_state_t state)
{
    if (!lvgl_port_lock(100)) return;
    switch (state) {
    case WIFI_STATE_CONNECTED:
        lv_label_set_text(s_wifi_label, LV_SYMBOL_WIFI " Connected");
        lv_obj_set_style_text_color(s_wifi_label, lv_color_hex(0x4CAF50), 0);
        break;
    case WIFI_STATE_CONNECTING:
        lv_label_set_text(s_wifi_label, "Wi-Fi: connecting...");
        lv_obj_set_style_text_color(s_wifi_label, CLR_STATUS, 0);
        break;
    case WIFI_STATE_AP_MODE:
        lv_label_set_text(s_wifi_label, LV_SYMBOL_WIFI " AP: 192.168.4.1");
        lv_obj_set_style_text_color(s_wifi_label, lv_color_hex(0xFFC107), 0);
        break;
    default:
        lv_label_set_text(s_wifi_label, LV_SYMBOL_WIFI " Disconnected");
        lv_obj_set_style_text_color(s_wifi_label, CLR_LOCKED, 0);
        break;
    }
    lvgl_port_unlock();
}

void ui_set_lock_state(lock_state_t state)
{
    s_lock_state = state;
    if (!lvgl_port_lock(100)) return;

    switch (state) {
    case LOCK_STATE_LOCKED:
        lv_label_set_text(s_lock_label,     LV_SYMBOL_CLOSE " Locked");
        lv_obj_set_style_text_color(s_lock_label, CLR_LOCKED, 0);
        lv_label_set_text(s_lock_btn_label, "Unlock");
        style_btn(s_lock_btn, CLR_UNLOCKED, CLR_UNLOCKED_PR);
        break;
    case LOCK_STATE_UNLOCKED:
        lv_label_set_text(s_lock_label,     LV_SYMBOL_OK " Unlocked");
        lv_obj_set_style_text_color(s_lock_label, lv_color_hex(0x4CAF50), 0);
        lv_label_set_text(s_lock_btn_label, "Lock");
        style_btn(s_lock_btn, CLR_LOCKED, CLR_LOCKED_PR);
        break;
    default:
        lv_label_set_text(s_lock_label,     "Unknown");
        lv_obj_set_style_text_color(s_lock_label, CLR_STATUS, 0);
        lv_label_set_text(s_lock_btn_label, "...");
        break;
    }
    lvgl_port_unlock();
}

void ui_set_vent_speed(uint8_t speed)
{
    s_vent_speed = speed;
    if (!lvgl_port_lock(100)) return;
    for (int i = 0; i < 3; i++) {
        bool active = (VENT_SPEEDS[i] == speed);
        style_btn(s_vent_btns[i],
            active ? CLR_VENT_ACT  : CLR_VENT_IDL,
            active ? CLR_VENT_ACT_PR : CLR_VENT_IDL_PR);
    }
    lvgl_port_unlock();
}

void ui_set_mqtt_state(bool connected)
{
    if (!lvgl_port_lock(100)) return;
    if (connected) {
        lv_label_set_text(s_mqtt_label, "MQTT " LV_SYMBOL_OK);
        lv_obj_set_style_text_color(s_mqtt_label, lv_color_hex(0x4CAF50), 0);
    } else {
        lv_label_set_text(s_mqtt_label, "MQTT -");
        lv_obj_set_style_text_color(s_mqtt_label, CLR_STATUS, 0);
    }
    lvgl_port_unlock();
}

void ui_show_feedback(const char *msg)
{
    if (!lvgl_port_lock(100)) return;
    lv_label_set_text(s_feedback_label, msg);
    lv_timer_reset(s_feedback_timer);
    lv_timer_resume(s_feedback_timer);
    lvgl_port_unlock();
}
