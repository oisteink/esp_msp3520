#include "test_reboot.h"
#include "msp3520.h"
#include "screen_protect.h"

#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "test_reboot";

/* RTC slow memory — survives software reset, cleared on power-on */
/* RTC_NOINIT_ATTR: not loaded from binary on boot, survives software reset */
static RTC_NOINIT_ATTR uint8_t s_phase;
static RTC_NOINIT_ATTR uint8_t s_result;

extern msp3520_handle_t test_handle;

static void test_fadein(void)
{
    /* Verify this was a software reset */
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason != ESP_RST_SW) {
        ESP_LOGE(TAG, "expected SW reset, got %d", reason);
        s_result = REBOOT_RESULT_FAIL;
        return;
    }

    /* Right after init, screen_protect should be in WAKING state
     * (enter_waking is called at the end of screen_protect_init) */
    const char *state;
    msp3520_lvgl_lock(test_handle, 0);
    screen_protect_get_status(test_handle, &state, NULL, NULL, NULL);
    msp3520_lvgl_unlock(test_handle);

    if (strcmp(state, "waking") != 0) {
        ESP_LOGE(TAG, "expected waking state after boot, got %s", state);
        s_result = REBOOT_RESULT_FAIL;
        return;
    }
    ESP_LOGI(TAG, "state is waking — fade-in in progress");

    /* Wait for fade to complete + idle_check to transition WAKING → ACTIVE */
    vTaskDelay(pdMS_TO_TICKS(2000));

    msp3520_lvgl_lock(test_handle, 0);
    screen_protect_get_status(test_handle, &state, NULL, NULL, NULL);
    msp3520_lvgl_unlock(test_handle);

    if (strcmp(state, "active") != 0) {
        ESP_LOGE(TAG, "expected active state after fade, got %s", state);
        s_result = REBOOT_RESULT_FAIL;
        return;
    }

    ESP_LOGI(TAG, "fade-in test PASSED");
    s_result = REBOOT_RESULT_PASS;
}

bool reboot_tests_run(void)
{
    esp_reset_reason_t reason = esp_reset_reason();

    /* On power-on, RTC_NOINIT vars are garbage — clear them */
    if (reason == ESP_RST_POWERON) {
        s_phase = REBOOT_NONE;
        s_result = REBOOT_RESULT_NONE;
        return false;
    }

    if (s_phase == REBOOT_NONE) {
        return false;
    }

    ESP_LOGI(TAG, "reboot test phase %d detected", s_phase);
    uint8_t phase = s_phase;
    s_phase = REBOOT_NONE;  /* clear before running to avoid loops */

    switch (phase) {
    case REBOOT_FADEIN:
        test_fadein();
        break;
    default:
        ESP_LOGW(TAG, "unknown reboot test phase %d", phase);
        break;
    }

    return true;
}

uint8_t reboot_test_get_result(uint8_t phase)
{
    (void)phase;  /* currently only one test, result is shared */
    uint8_t result = s_result;
    s_result = REBOOT_RESULT_NONE;
    return result;
}

void reboot_test_trigger(uint8_t phase)
{
    s_phase = phase;
    ESP_LOGI(TAG, "triggering reboot for test phase %d", phase);
    esp_restart();
}
