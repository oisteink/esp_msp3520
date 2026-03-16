#include "screen_protect.h"
#include "backlight.h"

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "msp3520_sp";

#define WAKE_SUPPRESS_US  250000  /* 250ms touch suppression after wake/startup */
#define DIM_BRIGHTNESS    10      /* percent */

enum screen_state {
    SP_ACTIVE = 0,
    SP_DIMMED,
    SP_OFF,
    SP_WAKING,
};

static const char *state_names[] = { "active", "dimmed", "off", "waking" };

static void dispoff_timer_cb(void *arg)
{
    msp3520_handle_t h = (msp3520_handle_t)arg;
    esp_lcd_panel_disp_on_off(h->panel, false);
    ESP_LOGI(TAG, "display OFF");
}

static void schedule_dispoff(msp3520_handle_t h)
{
    /* One-shot timer to send DISPOFF after fade completes */
    if (!h->dispoff_timer) {
        const esp_timer_create_args_t args = {
            .callback = dispoff_timer_cb,
            .arg = h,
            .name = "dispoff",
        };
        if (esp_timer_create(&args, &h->dispoff_timer) != ESP_OK) {
            ESP_LOGE(TAG, "failed to create dispoff timer");
            return;
        }
    }
    esp_timer_start_once(h->dispoff_timer, (uint64_t)CONFIG_MSP3520_SCREEN_FADE_OUT_MS * 1000);
}

static void cancel_dispoff(msp3520_handle_t h)
{
    if (h->dispoff_timer) {
        esp_timer_stop(h->dispoff_timer);
    }
}

static void enter_waking(msp3520_handle_t h)
{
    h->screen_state = SP_WAKING;
    h->wake_timestamp_us = esp_timer_get_time();
    backlight_fade(h->saved_brightness, CONFIG_MSP3520_SCREEN_FADE_IN_MS);
}

static void idle_check_cb(lv_timer_t *timer)
{
    msp3520_handle_t h = (msp3520_handle_t)lv_timer_get_user_data(timer);
    uint32_t idle_ms = lv_display_get_inactive_time(h->display);

    /* Transition out of WAKING after suppression window */
    if (h->screen_state == SP_WAKING) {
        int64_t elapsed = esp_timer_get_time() - h->wake_timestamp_us;
        if (elapsed >= WAKE_SUPPRESS_US) {
            h->screen_state = SP_ACTIVE;
            ESP_LOGD(TAG, "wake suppression ended, now active");
        }
        return;
    }

    uint32_t dim_ms = (uint32_t)h->dim_timeout_s * 1000;
    uint32_t off_ms = (uint32_t)h->off_timeout_s * 1000;

    if (h->screen_state == SP_ACTIVE) {
        if (dim_ms > 0 && idle_ms >= dim_ms) {
            ESP_LOGI(TAG, "dimming (idle %"PRIu32"ms)", idle_ms);
            backlight_fade(DIM_BRIGHTNESS, CONFIG_MSP3520_SCREEN_FADE_OUT_MS);
            h->screen_state = SP_DIMMED;
        } else if (dim_ms == 0 && off_ms > 0 && idle_ms >= off_ms) {
            ESP_LOGI(TAG, "turning off (idle %"PRIu32"ms)", idle_ms);
            backlight_fade(0, CONFIG_MSP3520_SCREEN_FADE_OUT_MS);
            schedule_dispoff(h);
            h->screen_state = SP_OFF;
        }
    } else if (h->screen_state == SP_DIMMED) {
        if (off_ms > 0 && idle_ms >= dim_ms + off_ms) {
            ESP_LOGI(TAG, "turning off (idle %"PRIu32"ms)", idle_ms);
            backlight_fade(0, CONFIG_MSP3520_SCREEN_FADE_OUT_MS);
            schedule_dispoff(h);
            h->screen_state = SP_OFF;
        }
    }
}

static void wake_touch_cb(lv_event_t *e)
{
    msp3520_handle_t h = (msp3520_handle_t)lv_event_get_user_data(e);
    lv_indev_t *indev = lv_event_get_indev(e);
    lv_event_code_t code = lv_event_get_code(e);

    /* While waking, suppress all events */
    if (h->screen_state == SP_WAKING) {
        lv_indev_stop_processing(indev);
        return;
    }

    /* Normal operation — let everything through */
    if (h->screen_state == SP_ACTIVE) {
        return;
    }

    /* DIMMED or OFF — suppress all events, only wake on PRESSED */
    lv_indev_stop_processing(indev);

    if (code != LV_EVENT_PRESSED) {
        return;
    }

    /* Wake the screen */
    backlight_fade_stop();
    cancel_dispoff(h);

    if (h->screen_state == SP_OFF) {
        esp_lcd_panel_disp_on_off(h->panel, true);
        ESP_LOGI(TAG, "display ON");
    }

    ESP_LOGI(TAG, "waking from %s", state_names[h->screen_state]);
    enter_waking(h);
    lv_display_trigger_activity(h->display);
}

/* -- Public API ---------------------------------------------------- */

esp_err_t screen_protect_init(msp3520_handle_t h)
{
    h->saved_brightness = 100;
    h->dim_timeout_s = CONFIG_MSP3520_SCREEN_DIM_TIMEOUT * 60;
    h->off_timeout_s = CONFIG_MSP3520_SCREEN_OFF_TIMEOUT * 60;
    h->dispoff_timer = NULL;

    /* Register touch wake callback on indev */
    lv_indev_add_event_cb(h->indev, wake_touch_cb, LV_EVENT_ALL, h);

    /* Periodic idle check — runs in LVGL task context */
    h->screen_protect_timer = lv_timer_create(idle_check_cb, 1000, h);
    if (!h->screen_protect_timer) {
        ESP_LOGE(TAG, "failed to create idle check timer");
        return ESP_FAIL;
    }

    /* Start with fade-in from dark */
    enter_waking(h);

    ESP_LOGI(TAG, "initialized (dim=%us, off=%us, fade_in=%ums, fade_out=%ums)",
             h->dim_timeout_s, h->off_timeout_s,
             CONFIG_MSP3520_SCREEN_FADE_IN_MS, CONFIG_MSP3520_SCREEN_FADE_OUT_MS);
    return ESP_OK;
}

void screen_protect_deinit(msp3520_handle_t h)
{
    if (!h) return;

    if (h->screen_protect_timer) {
        lv_timer_delete(h->screen_protect_timer);
        h->screen_protect_timer = NULL;
    }
    if (h->dispoff_timer) {
        esp_timer_stop(h->dispoff_timer);
        esp_timer_delete(h->dispoff_timer);
        h->dispoff_timer = NULL;
    }
}

void screen_protect_set_dim_timeout(msp3520_handle_t h, uint16_t seconds)
{
    h->dim_timeout_s = seconds;
    ESP_LOGI(TAG, "dim timeout set to %us", seconds);
}

void screen_protect_set_off_timeout(msp3520_handle_t h, uint16_t seconds)
{
    h->off_timeout_s = seconds;
    ESP_LOGI(TAG, "off timeout set to %us", seconds);
}

void screen_protect_get_status(msp3520_handle_t h, const char **state,
                                uint16_t *dim_s, uint16_t *off_s,
                                uint32_t *idle_ms)
{
    if (state) *state = state_names[h->screen_state];
    if (dim_s) *dim_s = h->dim_timeout_s;
    if (off_s) *off_s = h->off_timeout_s;
    if (idle_ms) *idle_ms = lv_display_get_inactive_time(h->display);
}

void screen_protect_register_indev(msp3520_handle_t h, lv_indev_t *indev)
{
    lv_indev_add_event_cb(indev, wake_touch_cb, LV_EVENT_ALL, h);
    lv_indev_set_display(indev, h->display);
}
