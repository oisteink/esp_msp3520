#include "msp3520.h"
#include "nvs_flash.h"
#include "esp_console.h"
#include "esp_log.h"

static const char *TAG = "example";

/* -- UI ------------------------------------------------------------ */

static void btn_event_cb(lv_event_t *e)
{
    static int count = 0;
    lv_obj_t *label = lv_event_get_user_data(e);
    lv_label_set_text_fmt(label, "Tapped: %d", ++count);
}

static void scr_press_cb(lv_event_t *e)
{
    lv_obj_t *coord_label = lv_event_get_user_data(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_label_set_text_fmt(coord_label, "x:%d y:%d", (int)p.x, (int)p.y);
}

static void create_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);

    lv_obj_t *coord_label = lv_label_create(scr);
    lv_label_set_text(coord_label, "");
    lv_obj_set_style_text_font(coord_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(coord_label, lv_color_make(0x99, 0x99, 0x99), 0);
    lv_obj_align(coord_label, LV_ALIGN_BOTTOM_MID, 0, -20);

    lv_obj_t *counter = lv_label_create(scr);
    lv_label_set_text(counter, "Tapped: 0");
    lv_obj_set_style_text_font(counter, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(counter, lv_color_black(), 0);
    lv_obj_align(counter, LV_ALIGN_CENTER, 0, 60);

    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 200, 80);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, -30);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, counter);

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Tap me!");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_28, 0);
    lv_obj_center(btn_label);

    lv_obj_add_event_cb(scr, scr_press_cb, LV_EVENT_PRESSED, coord_label);
}

/* -- Main ---------------------------------------------------------- */

void app_main(void)
{
    /* NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* MSP3520 display + touch */
    msp3520_config_t cfg = MSP3520_CONFIG_DEFAULT();
    msp3520_handle_t display;
    ESP_ERROR_CHECK(msp3520_create(&cfg, &display));

    /* Build UI */
    msp3520_lvgl_lock(display, 0);
    create_ui();
    msp3520_lvgl_unlock(display);

    /* Console */
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "msp3520> ";
    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl));

    esp_console_register_help_command();
    msp3520_register_console_commands(display);

    ESP_LOGI(TAG, "starting REPL");
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
