#include "unity.h"
#include "msp3520.h"
#include "screen_protect.h"
#include "test_indev_sim.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "test_sp";

/* From test_app_main.c */
extern msp3520_handle_t test_handle;

/* Test indev — created once */
static lv_indev_t *s_test_indev;

/* Button click counter for touch suppression tests */
static uint32_t s_btn_click_count;
static lv_obj_t *s_test_btn;

static void btn_click_cb(lv_event_t *e)
{
    s_btn_click_count++;
}

/* -- Helpers ------------------------------------------------------- */

static void reset_state(uint16_t dim_s, uint16_t off_s)
{
    msp3520_lvgl_lock(test_handle, 0);
    screen_protect_set_dim_timeout(test_handle, dim_s);
    screen_protect_set_off_timeout(test_handle, off_s);
    /* Reset to active by setting backlight (also triggers activity) */
    msp3520_set_backlight(test_handle, 100);
    msp3520_lvgl_unlock(test_handle);
}

static void get_state(const char **state, uint32_t *idle_ms)
{
    msp3520_lvgl_lock(test_handle, 0);
    screen_protect_get_status(test_handle, state, NULL, NULL, idle_ms);
    msp3520_lvgl_unlock(test_handle);
}

static void sim_touch(void)
{
    msp3520_lvgl_lock(test_handle, 0);
    test_indev_sim_press(160, 240);
    msp3520_lvgl_unlock(test_handle);
    vTaskDelay(pdMS_TO_TICKS(100));
    msp3520_lvgl_lock(test_handle, 0);
    test_indev_sim_release();
    msp3520_lvgl_unlock(test_handle);
    vTaskDelay(pdMS_TO_TICKS(100));
}

static void ensure_test_indev(void)
{
    if (!s_test_indev) {
        msp3520_lvgl_lock(test_handle, 0);
        s_test_indev = test_indev_sim_create();
        screen_protect_register_indev(test_handle, s_test_indev);
        msp3520_lvgl_unlock(test_handle);
    }
}

static void ensure_test_button(void)
{
    if (!s_test_btn) {
        msp3520_lvgl_lock(test_handle, 0);
        s_test_btn = lv_button_create(lv_screen_active());
        lv_obj_set_size(s_test_btn, 100, 60);
        lv_obj_set_pos(s_test_btn, 110, 210); /* centered around 160,240 */
        lv_obj_add_event_cb(s_test_btn, btn_click_cb, LV_EVENT_CLICKED, NULL);
        msp3520_lvgl_unlock(test_handle);
    }
    s_btn_click_count = 0;
}

/* -- Automated Tests ---------------------------------------------- */

TEST_CASE("dim after timeout", "[screen_protect]")
{
    reset_state(3, 0);
    ESP_LOGI(TAG, "waiting 4s for dim...");
    vTaskDelay(pdMS_TO_TICKS(4000));

    const char *state;
    get_state(&state, NULL);
    TEST_ASSERT_EQUAL_STRING("dimmed", state);
}

TEST_CASE("off after dim+off timeout", "[screen_protect]")
{
    reset_state(2, 2);
    ESP_LOGI(TAG, "waiting 5s for off...");
    vTaskDelay(pdMS_TO_TICKS(5000));

    const char *state;
    get_state(&state, NULL);
    TEST_ASSERT_EQUAL_STRING("off", state);
}

TEST_CASE("skip dim when dim=0", "[screen_protect]")
{
    reset_state(0, 3);
    ESP_LOGI(TAG, "waiting 4s for off (no dim)...");
    vTaskDelay(pdMS_TO_TICKS(4000));

    const char *state;
    get_state(&state, NULL);
    TEST_ASSERT_EQUAL_STRING("off", state);
}

TEST_CASE("no action when both=0", "[screen_protect]")
{
    reset_state(0, 0);
    ESP_LOGI(TAG, "waiting 3s...");
    vTaskDelay(pdMS_TO_TICKS(3000));

    const char *state;
    get_state(&state, NULL);
    TEST_ASSERT_EQUAL_STRING("active", state);
}

TEST_CASE("wake from dimmed", "[screen_protect]")
{
    ensure_test_indev();
    reset_state(2, 0);
    ESP_LOGI(TAG, "waiting 3s for dim...");
    vTaskDelay(pdMS_TO_TICKS(3000));

    const char *state;
    get_state(&state, NULL);
    TEST_ASSERT_EQUAL_STRING("dimmed", state);

    ESP_LOGI(TAG, "simulating touch to wake...");
    sim_touch();
    vTaskDelay(pdMS_TO_TICKS(1500)); /* wait past 250ms wake window + idle_check (1s) */

    get_state(&state, NULL);
    TEST_ASSERT_EQUAL_STRING("active", state);
}

TEST_CASE("wake from off", "[screen_protect]")
{
    ensure_test_indev();
    reset_state(0, 2);
    ESP_LOGI(TAG, "waiting 3s for off...");
    vTaskDelay(pdMS_TO_TICKS(3000));

    const char *state;
    get_state(&state, NULL);
    TEST_ASSERT_EQUAL_STRING("off", state);

    ESP_LOGI(TAG, "simulating touch to wake...");
    sim_touch();
    vTaskDelay(pdMS_TO_TICKS(1500));

    get_state(&state, NULL);
    TEST_ASSERT_EQUAL_STRING("active", state);
}

TEST_CASE("waking state during suppression window", "[screen_protect]")
{
    ensure_test_indev();

    /* Dim the screen, then wake it */
    reset_state(2, 0);
    ESP_LOGI(TAG, "waiting 3s for dim...");
    vTaskDelay(pdMS_TO_TICKS(3000));

    const char *state;
    get_state(&state, NULL);
    TEST_ASSERT_EQUAL_STRING("dimmed", state);

    /* Touch to wake — should enter WAKING state */
    sim_touch();

    /* Immediately check: should still be in waking (within 250ms window) */
    get_state(&state, NULL);
    TEST_ASSERT_EQUAL_STRING("waking", state);

    /* After suppression window + idle check, should be active */
    vTaskDelay(pdMS_TO_TICKS(1500));
    get_state(&state, NULL);
    TEST_ASSERT_EQUAL_STRING("active", state);
}

TEST_CASE("touch passes after wake", "[screen_protect]")
{
    ensure_test_indev();
    ensure_test_button();
    reset_state(0, 0); /* no dim/off — stay active */
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "touching button while active...");
    sim_touch();
    vTaskDelay(pdMS_TO_TICKS(200));

    TEST_ASSERT_EQUAL_UINT32(1, s_btn_click_count);
}

TEST_CASE("manual backlight updates state", "[screen_protect]")
{
    reset_state(2, 0);
    ESP_LOGI(TAG, "waiting 3s for dim...");
    vTaskDelay(pdMS_TO_TICKS(3000));

    const char *state;
    get_state(&state, NULL);
    TEST_ASSERT_EQUAL_STRING("dimmed", state);

    msp3520_set_backlight(test_handle, 50);
    get_state(&state, NULL);
    TEST_ASSERT_EQUAL_STRING("active", state);
}

TEST_CASE("~show results on display", "[screen_protect]")
{
    /* Unity has already counted this test in NumberOfTests, so subtract 1 */
    uint32_t total = Unity.NumberOfTests - 1;
    uint32_t failed = Unity.TestFailures;
    uint32_t passed = total - failed;

    /* Ensure screen is on and bright */
    reset_state(0, 0);

    msp3520_lvgl_lock(test_handle, 0);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, failed ? lv_color_hex(0x600000) : lv_color_hex(0x003000), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_label_set_text(title, "Test Results");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *result = lv_label_create(scr);
    lv_obj_set_style_text_font(result, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(result, lv_color_white(), 0);
    lv_label_set_text_fmt(result, "%s\n\n"
                                   "Total:   %"PRIu32"\n"
                                   "Passed:  %"PRIu32"\n"
                                   "Failed:  %"PRIu32,
                          failed ? "FAIL" : "PASS",
                          total, passed, failed);
    lv_obj_align(result, LV_ALIGN_CENTER, 0, 0);

    msp3520_lvgl_unlock(test_handle);

    ESP_LOGI(TAG, "results displayed on screen: %"PRIu32" passed, %"PRIu32" failed", passed, failed);
}

/* -- Interactive Tests -------------------------------------------- */

TEST_CASE("physical touch wake", "[interactive]")
{
    reset_state(5, 0);
    printf("\n>>> Screen will dim in 5 seconds. Touch the screen to wake it.\n");
    printf(">>> Did the screen restore smoothly? (y/n): ");
    fflush(stdout);

    char c = 0;
    while (c != 'y' && c != 'n') {
        c = fgetc(stdin);
    }
    printf("\n");
    TEST_ASSERT_EQUAL_CHAR('y', c);
}

TEST_CASE("fade-in visible on boot", "[interactive]")
{
    printf("\n>>> Reboot the device (Ctrl-T Ctrl-R) and watch the screen.\n");
    printf(">>> Did you see a smooth fade-in from dark? (y/n): ");
    fflush(stdout);

    char c = 0;
    while (c != 'y' && c != 'n') {
        c = fgetc(stdin);
    }
    printf("\n");
    TEST_ASSERT_EQUAL_CHAR('y', c);
}
