#include "ui.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdbool.h>

#define TAG "ui"

/* ---- colour palette ---- */
#define CLR_BG       lv_color_hex(0x1A1A2E)
#define CLR_PANEL    lv_color_hex(0x16213E)
#define CLR_LOCKED   lv_color_hex(0xE94560)
#define CLR_UNLOCKED lv_color_hex(0x0F3460)
#define CLR_VENT_ACT lv_color_hex(0x533483)
#define CLR_VENT_IDL lv_color_hex(0x2D2D44)
#define CLR_TEXT     lv_color_hex(0xEEEEEE)
#define CLR_STATUS   lv_color_hex(0xAAAAAA)
#define CLR_FEEDBACK lv_color_hex(0x4CAF50)

static ui_lock_action_cb_t s_lock_cb;
static ui_vent_action_cb_t s_vent_cb;

/* widgets we need to update later */
static lv_obj_t  *s_wifi_label;
static lv_obj_t  *s_mqtt_label;
static lv_obj_t  *s_lock_label;
static lv_obj_t  *s_lock_btn;
static lv_obj_t  *s_lock_btn_label;
static lv_obj_t  *s_vent_btns[3];   /* Low, Normal, Fast */
static lv_obj_t  *s_feedback_label;
static lv_timer_t *s_feedback_timer;

static lock_state_t s_lock_state  = LOCK_STATE_UNKNOWN;
static uint8_t      s_vent_speed  = 3;

/* ------------------------------------------------------------------ */
/* Button callbacks                                                    */
/* ------------------------------------------------------------------ */
static void lock_btn_cb(lv_event_t *e)
{
    if (!s_lock_cb) return;
    bool do_lock = (s_lock_state == LOCK_STATE_UNLOCKED);
    s_lock_cb(do_lock);
    ui_show_feedback(do_lock ? "Locking..." : "Unlocking...");
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

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* ---- Status bar (top strip) ---- */
    lv_obj_t *statusbar = lv_obj_create(scr);
    lv_obj_set_size(statusbar, 320, 28);
    lv_obj_align(statusbar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(statusbar, CLR_PANEL, 0);
    lv_obj_set_style_border_width(statusbar, 0, 0);
    lv_obj_set_style_radius(statusbar, 0, 0);
    lv_obj_set_style_pad_all(statusbar, 4, 0);

    s_wifi_label = lv_label_create(statusbar);
    lv_label_set_text(s_wifi_label, "Wi-Fi: connecting...");
    lv_obj_set_style_text_color(s_wifi_label, CLR_STATUS, 0);
    lv_obj_align(s_wifi_label, LV_ALIGN_LEFT_MID, 0, 0);

    s_mqtt_label = lv_label_create(statusbar);
    lv_label_set_text(s_mqtt_label, "MQTT -");
    lv_obj_set_style_text_color(s_mqtt_label, CLR_STATUS, 0);
    lv_obj_align(s_mqtt_label, LV_ALIGN_RIGHT_MID, 0, 0);

    /* ---- Lock panel (left half below status bar) ---- */
    lv_obj_t *lock_panel = lv_obj_create(scr);
    lv_obj_set_size(lock_panel, 148, 190);
    lv_obj_align(lock_panel, LV_ALIGN_TOP_LEFT, 4, 32);
    lv_obj_set_style_bg_color(lock_panel, CLR_PANEL, 0);
    lv_obj_set_style_border_width(lock_panel, 0, 0);
    lv_obj_set_style_radius(lock_panel, 8, 0);
    lv_obj_set_style_pad_all(lock_panel, 8, 0);

    lv_obj_t *lock_title = lv_label_create(lock_panel);
    lv_label_set_text(lock_title, "Front Door");
    lv_obj_set_style_text_color(lock_title, CLR_TEXT, 0);
    lv_obj_align(lock_title, LV_ALIGN_TOP_MID, 0, 0);

    /* Large lock icon/state label */
    s_lock_label = lv_label_create(lock_panel);
    lv_label_set_text(s_lock_label, LV_SYMBOL_EYE_CLOSE " ...");
    lv_obj_set_style_text_color(s_lock_label, CLR_STATUS, 0);
    lv_obj_set_style_text_font(s_lock_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_lock_label, LV_ALIGN_CENTER, 0, -14);

    /* Action button */
    s_lock_btn = lv_btn_create(lock_panel);
    lv_obj_set_size(s_lock_btn, 120, 44);
    lv_obj_align(s_lock_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_lock_btn, CLR_UNLOCKED, 0);
    lv_obj_add_event_cb(s_lock_btn, lock_btn_cb, LV_EVENT_CLICKED, NULL);

    s_lock_btn_label = lv_label_create(s_lock_btn);
    lv_label_set_text(s_lock_btn_label, "...");
    lv_obj_center(s_lock_btn_label);
    lv_obj_set_style_text_color(s_lock_btn_label, CLR_TEXT, 0);

    /* ---- Vent panel (right half) ---- */
    lv_obj_t *vent_panel = lv_obj_create(scr);
    lv_obj_set_size(vent_panel, 156, 190);
    lv_obj_align(vent_panel, LV_ALIGN_TOP_RIGHT, -4, 32);
    lv_obj_set_style_bg_color(vent_panel, CLR_PANEL, 0);
    lv_obj_set_style_border_width(vent_panel, 0, 0);
    lv_obj_set_style_radius(vent_panel, 8, 0);
    lv_obj_set_style_pad_all(vent_panel, 8, 0);

    lv_obj_t *vent_title = lv_label_create(vent_panel);
    lv_label_set_text(vent_title, "Ventilation");
    lv_obj_set_style_text_color(vent_title, CLR_TEXT, 0);
    lv_obj_align(vent_title, LV_ALIGN_TOP_MID, 0, 0);

    for (int i = 0; i < 3; i++) {
        s_vent_btns[i] = lv_btn_create(vent_panel);
        lv_obj_set_size(s_vent_btns[i], 128, 44);
        lv_obj_align(s_vent_btns[i], LV_ALIGN_TOP_MID, 0, 22 + i * 52);
        lv_obj_set_style_bg_color(s_vent_btns[i], CLR_VENT_IDL, 0);
        lv_obj_add_event_cb(s_vent_btns[i], vent_btn_cb, LV_EVENT_CLICKED,
                            (void *)&VENT_SPEEDS[i]);

        lv_obj_t *lbl = lv_label_create(s_vent_btns[i]);
        lv_label_set_text(lbl, VENT_LABELS[i]);
        lv_obj_center(lbl);
        lv_obj_set_style_text_color(lbl, CLR_TEXT, 0);
    }

    /* ---- Feedback label (bottom strip) ---- */
    s_feedback_label = lv_label_create(scr);
    lv_label_set_text(s_feedback_label, "");
    lv_obj_set_style_text_color(s_feedback_label, CLR_FEEDBACK, 0);
    lv_obj_align(s_feedback_label, LV_ALIGN_BOTTOM_MID, 0, -4);

    /* Auto-clear feedback after 3.5 s; timer starts paused */
    s_feedback_timer = lv_timer_create(feedback_clear_cb, 3500, NULL);
    lv_timer_pause(s_feedback_timer);

    lvgl_port_unlock();

    /* Apply initial vent highlight */
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
        lv_obj_set_style_bg_color(s_lock_btn, CLR_UNLOCKED, 0);
        break;
    case LOCK_STATE_UNLOCKED:
        lv_label_set_text(s_lock_label,     LV_SYMBOL_OK " Unlocked");
        lv_obj_set_style_text_color(s_lock_label, lv_color_hex(0x4CAF50), 0);
        lv_label_set_text(s_lock_btn_label, "Lock");
        lv_obj_set_style_bg_color(s_lock_btn, CLR_LOCKED, 0);
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
        lv_obj_set_style_bg_color(s_vent_btns[i],
            active ? CLR_VENT_ACT : CLR_VENT_IDL, 0);
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
