#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_ili9488.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "app";

// Display dimensions (landscape)
#define LCD_H_RES          480
#define LCD_V_RES          320
#define LCD_BPP            3   // bytes per pixel (RGB888/RGB666)

// LVGL draw buffer: 1/10 of screen height
#define LVGL_DRAW_BUF_LINES  (LCD_V_RES / 10)

// LVGL tick period
#define LVGL_TICK_PERIOD_MS   2

// LVGL task config
#define LVGL_TASK_STACK_SIZE  4096
#define LVGL_TASK_PRIORITY    2

static _lock_t lvgl_lock;

/* ── LVGL callbacks ─────────────────────────────────────────────── */

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area,
                           uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = lv_display_get_user_data(disp);
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, px_map);
}

static bool flush_ready_cb(esp_lcd_panel_io_handle_t panel_io,
                            esp_lcd_panel_io_event_data_t *edata,
                            void *user_ctx)
{
    lv_display_flush_ready((lv_display_t *)user_ctx);
    return false;
}

static void lvgl_tick_cb(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

/* ── LVGL task ──────────────────────────────────────────────────── */

static void lvgl_task(void *arg)
{
    uint32_t time_threshold_ms = 1000 / CONFIG_FREERTOS_HZ;
    while (1) {
        _lock_acquire(&lvgl_lock);
        uint32_t next_ms = lv_timer_handler();
        _lock_release(&lvgl_lock);
        if (next_ms < time_threshold_ms) {
            next_ms = time_threshold_ms;
        }
        vTaskDelay(pdMS_TO_TICKS(next_ms));
    }
}

/* ── UI ─────────────────────────────────────────────────────────── */

static void create_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);

    // Colored rectangle
    lv_obj_t *rect = lv_obj_create(scr);
    lv_obj_set_size(rect, 200, 120);
    lv_obj_align(rect, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_color(rect, lv_color_make(0x00, 0x7A, 0xCC), 0);
    lv_obj_set_style_radius(rect, 10, 0);
    lv_obj_set_style_border_width(rect, 0, 0);

    // Label
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Hello LVGL");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 80);
}

/* ── Main ───────────────────────────────────────────────────────── */

void app_main(void)
{
    // Backlight
    if (CONFIG_LCD_BKL_GPIO >= 0) {
        gpio_config_t bkl_cfg = {
            .pin_bit_mask = BIT64(CONFIG_LCD_BKL_GPIO),
            .mode = GPIO_MODE_OUTPUT,
        };
        ESP_ERROR_CHECK(gpio_config(&bkl_cfg));
        gpio_set_level(CONFIG_LCD_BKL_GPIO, 1);
    }

    // SPI bus
    ESP_LOGI(TAG, "initializing SPI bus");
    size_t buf_sz = LCD_H_RES * LVGL_DRAW_BUF_LINES * LCD_BPP;
    spi_bus_config_t bus_cfg = {
        .sclk_io_num = CONFIG_LCD_SPI_SCLK_GPIO,
        .mosi_io_num = CONFIG_LCD_SPI_MOSI_GPIO,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = buf_sz,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    // Panel IO (no on_color_trans_done here — registered via callback API below)
    ESP_LOGI(TAG, "installing panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = CONFIG_LCD_DC_GPIO,
        .cs_gpio_num = CONFIG_LCD_CS_GPIO,
        .pclk_hz = CONFIG_LCD_SPI_CLOCK_MHZ * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_cfg, &io_handle));

    // Panel
    ESP_LOGI(TAG, "installing ILI9488 panel driver");
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = CONFIG_LCD_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 24,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9488(io_handle, &panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    // Landscape orientation (experimental — mirror flags may need adjustment on hardware)
    // Note: LVGL could also modify MADCTL via the same swap_xy/mirror driver calls,
    // so avoid using lv_display_set_rotation() to prevent conflicting register writes.
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, false, false));

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    // ── LVGL init ──────────────────────────────────────────────
    ESP_LOGI(TAG, "initializing LVGL");
    lv_init();

    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB888);
    lv_display_set_user_data(disp, panel);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);

    // Allocate DMA-safe draw buffers
    void *buf1 = spi_bus_dma_memory_alloc(SPI2_HOST, buf_sz, 0);
    assert(buf1);
    void *buf2 = spi_bus_dma_memory_alloc(SPI2_HOST, buf_sz, 0);
    assert(buf2);
    lv_display_set_buffers(disp, buf1, buf2, buf_sz,
                            LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Register SPI transfer-done callback to notify LVGL
    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = flush_ready_cb,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(
        io_handle, &cbs, disp));

    // LVGL tick timer (2ms periodic)
    esp_timer_handle_t tick_timer = NULL;
    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer,
                                              LVGL_TICK_PERIOD_MS * 1000));

    // Create UI
    _lock_acquire(&lvgl_lock);
    create_ui();
    _lock_release(&lvgl_lock);

    // Start LVGL task
    ESP_LOGI(TAG, "starting LVGL task");
    xTaskCreate(lvgl_task, "lvgl", LVGL_TASK_STACK_SIZE, NULL,
                LVGL_TASK_PRIORITY, NULL);
}
