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

    ledc_channel_config_t ch_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BKL_LEDC_CHANNEL,
        .timer_sel = BKL_LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = gpio,
        .duty = active_high ? BKL_DUTY_MAX : 0,
        .hpoint = 0,
    };
    err = ledc_channel_config(&ch_cfg);
    if (err != ESP_OK) return err;

    s_initialized = true;
    ESP_LOGI(TAG, "backlight initialized on GPIO %d (active %s)",
             gpio, active_high ? "high" : "low");
    return ESP_OK;
}

esp_err_t backlight_set(uint8_t brightness)
{
    if (!s_initialized) return ESP_OK;
    if (brightness > 100) brightness = 100;

    uint32_t duty = (uint32_t)brightness * BKL_DUTY_MAX / 100;
    if (!s_active_high) {
        duty = BKL_DUTY_MAX - duty;
    }

    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, BKL_LEDC_CHANNEL, duty);
    if (err != ESP_OK) return err;
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, BKL_LEDC_CHANNEL);
}
