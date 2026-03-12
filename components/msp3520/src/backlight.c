#include "backlight.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "msp3520_bkl";

#define BKL_LEDC_TIMER    LEDC_TIMER_0
#define BKL_LEDC_CHANNEL  LEDC_CHANNEL_0
#define BKL_LEDC_FREQ_HZ  5000
#define BKL_LEDC_DUTY_RES LEDC_TIMER_8_BIT
#define BKL_DUTY_MAX      255

static bool s_active_high;
static bool s_initialized;

static uint32_t brightness_to_duty(uint8_t brightness)
{
    if (brightness > 100) brightness = 100;
    uint32_t duty = (uint32_t)brightness * BKL_DUTY_MAX / 100;
    if (!s_active_high) {
        duty = BKL_DUTY_MAX - duty;
    }
    return duty;
}

esp_err_t backlight_init(int gpio, bool active_high)
{
    if (gpio < 0) {
        ESP_LOGI(TAG, "backlight disabled (gpio=-1)");
        return ESP_OK;
    }

    s_active_high = active_high;

    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = BKL_LEDC_TIMER,
        .duty_resolution = BKL_LEDC_DUTY_RES,
        .freq_hz = BKL_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) return err;

    /* Start dark — duty 0 for active-high, max for active-low */
    ledc_channel_config_t ch_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BKL_LEDC_CHANNEL,
        .timer_sel = BKL_LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = gpio,
        .duty = active_high ? 0 : BKL_DUTY_MAX,
        .hpoint = 0,
    };
    err = ledc_channel_config(&ch_cfg);
    if (err != ESP_OK) return err;

    err = ledc_fade_func_install(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        /* ESP_ERR_INVALID_STATE means already installed — that's fine */
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "backlight initialized on GPIO %d (active %s, starting dark)",
             gpio, active_high ? "high" : "low");
    return ESP_OK;
}

esp_err_t backlight_set(uint8_t brightness)
{
    if (!s_initialized) return ESP_OK;

    uint32_t duty = brightness_to_duty(brightness);
    return ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, BKL_LEDC_CHANNEL, duty, 0);
}

esp_err_t backlight_fade(uint8_t brightness, uint32_t fade_ms)
{
    if (!s_initialized) return ESP_OK;

    uint32_t duty = brightness_to_duty(brightness);
    if (fade_ms == 0) {
        return ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, BKL_LEDC_CHANNEL, duty, 0);
    }
    return ledc_set_fade_time_and_start(LEDC_LOW_SPEED_MODE, BKL_LEDC_CHANNEL,
                                         duty, fade_ms, LEDC_FADE_NO_WAIT);
}

esp_err_t backlight_fade_stop(void)
{
    if (!s_initialized) return ESP_OK;
    return ledc_fade_stop(LEDC_LOW_SPEED_MODE, BKL_LEDC_CHANNEL);
}
