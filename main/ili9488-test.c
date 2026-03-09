#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_chip_info.h"
#include "esp_console.h"
#include "esp_lcd_ili9488.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "app";

// Display dimensions (portrait native)
#define LCD_H_RES          320
#define LCD_V_RES          480
#define LCD_BPP            3   // bytes per pixel (RGB888/RGB666)

// LVGL draw buffer: 1/4 of screen height
#define LVGL_DRAW_BUF_LINES  (LCD_V_RES / 4)

// LVGL tick period
#define LVGL_TICK_PERIOD_MS   2

// LVGL task config
#define LVGL_TASK_STACK_SIZE  8192
#define LVGL_TASK_PRIORITY    2

static _lock_t lvgl_lock;

/* -- App context for console commands ------------------------------ */

typedef struct {
    esp_lcd_panel_handle_t panel;
    esp_lcd_touch_handle_t touch;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
} app_context_t;

static app_context_t app_ctx;

/* -- LVGL display callbacks ---------------------------------------- */

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

/* -- Touch read callback ------------------------------------------- */

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    static uint32_t poll_count = 0;
    poll_count++;
    if (poll_count % 500 == 0) {
        ESP_LOGI(TAG, "touch poll #%lu (alive)", (unsigned long)poll_count);
    }

    esp_lcd_touch_handle_t touch = lv_indev_get_user_data(indev);
    esp_lcd_touch_read_data(touch);

    uint16_t x, y;
    uint8_t count = 0;
    esp_lcd_touch_get_coordinates(touch, &x, &y, NULL, &count, 1);

    if (count > 0) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
        ESP_LOGI(TAG, "touch: x=%d y=%d (poll #%lu)", x, y, (unsigned long)poll_count);
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* -- LVGL task ----------------------------------------------------- */

static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started");
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

/* -- Console commands ---------------------------------------------- */

static int cmd_log_level(int argc, char **argv)
{
    if (argc != 3) {
        printf("Usage: log_level <tag> <none|error|warn|info|debug|verbose>\n");
        return 1;
    }
    const char *tag = argv[1];
    const char *lvl = argv[2];
    esp_log_level_t level;
    if      (strcmp(lvl, "none") == 0)    level = ESP_LOG_NONE;
    else if (strcmp(lvl, "error") == 0)   level = ESP_LOG_ERROR;
    else if (strcmp(lvl, "warn") == 0)    level = ESP_LOG_WARN;
    else if (strcmp(lvl, "info") == 0)    level = ESP_LOG_INFO;
    else if (strcmp(lvl, "debug") == 0)   level = ESP_LOG_DEBUG;
    else if (strcmp(lvl, "verbose") == 0) level = ESP_LOG_VERBOSE;
    else {
        printf("Unknown level: %s\n", lvl);
        return 1;
    }
    esp_log_level_set(tag, level);
    printf("Set %s to %s\n", tag, lvl);
    return 0;
}

static int cmd_info(int argc, char **argv)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    int64_t uptime_us = esp_timer_get_time();
    printf("Chip:      ESP32-S3 rev %d.%d, %d core(s)\n",
           chip.revision / 100, chip.revision % 100, chip.cores);
    printf("Heap free: %lu bytes\n", (unsigned long)esp_get_free_heap_size());
    printf("Heap min:  %lu bytes\n", (unsigned long)esp_get_minimum_free_heap_size());
    printf("Uptime:    %lld s\n", uptime_us / 1000000);
    return 0;
}

static int cmd_touch_cal(void *ctx, int argc, char **argv)
{
    app_context_t *app = (app_context_t *)ctx;
    if (argc == 1) {
        bool swap, mx, my;
        esp_lcd_touch_get_swap_xy(app->touch, &swap);
        esp_lcd_touch_get_mirror_x(app->touch, &mx);
        esp_lcd_touch_get_mirror_y(app->touch, &my);
        printf("swap_xy=%d  mirror_x=%d  mirror_y=%d\n", swap, mx, my);
        return 0;
    }
    if (argc != 3) {
        printf("Usage: touch_cal [swap_xy|mirror_x|mirror_y] [0|1]\n");
        return 1;
    }
    bool val = atoi(argv[2]) != 0;
    if      (strcmp(argv[1], "swap_xy") == 0)  esp_lcd_touch_set_swap_xy(app->touch, val);
    else if (strcmp(argv[1], "mirror_x") == 0) esp_lcd_touch_set_mirror_x(app->touch, val);
    else if (strcmp(argv[1], "mirror_y") == 0) esp_lcd_touch_set_mirror_y(app->touch, val);
    else {
        printf("Unknown flag: %s\n", argv[1]);
        return 1;
    }
    printf("Set %s=%d\n", argv[1], val);
    return 0;
}

static int cmd_rotation(void *ctx, int argc, char **argv)
{
    app_context_t *app = (app_context_t *)ctx;
    if (argc == 1) {
        printf("swap_xy=%d  mirror_x=%d  mirror_y=%d\n",
               app->swap_xy, app->mirror_x, app->mirror_y);
        return 0;
    }
    if (argc != 3) {
        printf("Usage: rotation [swap_xy|mirror_x|mirror_y] [0|1]\n");
        return 1;
    }
    bool val = atoi(argv[2]) != 0;
    if (strcmp(argv[1], "swap_xy") == 0) {
        app->swap_xy = val;
        esp_lcd_panel_swap_xy(app->panel, val);
    } else if (strcmp(argv[1], "mirror_x") == 0) {
        app->mirror_x = val;
        esp_lcd_panel_mirror(app->panel, app->mirror_x, app->mirror_y);
    } else if (strcmp(argv[1], "mirror_y") == 0) {
        app->mirror_y = val;
        esp_lcd_panel_mirror(app->panel, app->mirror_x, app->mirror_y);
    } else {
        printf("Unknown flag: %s\n", argv[1]);
        return 1;
    }
    printf("Set %s=%d\n", argv[1], val);
    return 0;
}

static int cmd_debug(int argc, char **argv)
{
    static bool debug_on = false;
    debug_on = !debug_on;
    esp_log_level_t lvl = debug_on ? ESP_LOG_DEBUG : ESP_LOG_INFO;
    esp_log_level_set("app", lvl);
    esp_log_level_set("ili9488", lvl);
    esp_log_level_set("xpt2046", lvl);
    printf("Debug %s\n", debug_on ? "ON" : "OFF");
    return 0;
}

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

    // Coordinate display
    lv_obj_t *coord_label = lv_label_create(scr);
    lv_label_set_text(coord_label, "");
    lv_obj_set_style_text_font(coord_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(coord_label, lv_color_make(0x99, 0x99, 0x99), 0);
    lv_obj_align(coord_label, LV_ALIGN_BOTTOM_MID, 0, -20);

    // Tap counter label
    lv_obj_t *counter = lv_label_create(scr);
    lv_label_set_text(counter, "Tapped: 0");
    lv_obj_set_style_text_font(counter, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(counter, lv_color_black(), 0);
    lv_obj_align(counter, LV_ALIGN_CENTER, 0, 60);

    // Button
    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 200, 80);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, -30);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, counter);

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Tap me!");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_28, 0);
    lv_obj_center(btn_label);

    // Screen press handler for coordinate display
    lv_obj_add_event_cb(scr, scr_press_cb, LV_EVENT_PRESSED, coord_label);
}

/* -- Main ---------------------------------------------------------- */

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
        .miso_io_num = CONFIG_LCD_SPI_MISO_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = buf_sz,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    // Display panel IO
    ESP_LOGI(TAG, "installing display panel IO");
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

    // Display panel
    ESP_LOGI(TAG, "installing ILI9488 panel driver");
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = CONFIG_LCD_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 24,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9488(io_handle, &panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, true, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    // Touch SPI bus (SPI3, dedicated for touch)
    ESP_LOGI(TAG, "initializing touch SPI bus (SPI3)");
    spi_bus_config_t touch_bus_cfg = {
        .sclk_io_num = CONFIG_TOUCH_SPI_SCLK_GPIO,
        .mosi_io_num = CONFIG_TOUCH_SPI_MOSI_GPIO,
        .miso_io_num = CONFIG_TOUCH_SPI_MISO_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &touch_bus_cfg, SPI_DMA_CH_AUTO));

    // Touch panel IO (SPI3, dedicated bus)
    ESP_LOGI(TAG, "installing touch panel IO");
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_spi_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(CONFIG_TOUCH_CS_GPIO);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &tp_io_cfg, &tp_io));

    // Touch driver
    ESP_LOGI(TAG, "installing XPT2046 touch driver");
    esp_lcd_touch_handle_t touch = NULL;
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = CONFIG_TOUCH_IRQ_GPIO,
        .levels = { .interrupt = 0 },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(tp_io, &tp_cfg, &touch));

    // LVGL init
    ESP_LOGI(TAG, "initializing LVGL");
    lv_init();

    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB888);
    lv_display_set_user_data(disp, panel);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);

    // Allocate draw buffers from PSRAM
    void *buf1 = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM);
    assert(buf1);
    void *buf2 = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM);
    assert(buf2);
    lv_display_set_buffers(disp, buf1, buf2, buf_sz,
                            LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Register SPI transfer-done callback to notify LVGL
    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = flush_ready_cb,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(
        io_handle, &cbs, disp));

    // Touch input device
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_user_data(indev, touch);
    lv_indev_set_display(indev, disp);

    // LVGL tick timer
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

    // Console REPL
    app_ctx.panel = panel;
    app_ctx.touch = touch;
    app_ctx.swap_xy = false;
    app_ctx.mirror_x = true;
    app_ctx.mirror_y = true;

    ESP_LOGI(TAG, "starting console");
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "tft> ";
    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl));

    esp_console_register_help_command();

    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "log_level", .help = "Set log level: log_level <tag> <level>",
        .hint = "<tag> <none|error|warn|info|debug|verbose>", .func = cmd_log_level });
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "info", .help = "Show system info", .func = cmd_info });
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "touch_cal", .help = "Get/set touch coordinate flags",
        .hint = "[swap_xy|mirror_x|mirror_y] [0|1]",
        .func_w_context = cmd_touch_cal, .context = &app_ctx });
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "rotation", .help = "Get/set display rotation flags",
        .hint = "[swap_xy|mirror_x|mirror_y] [0|1]",
        .func_w_context = cmd_rotation, .context = &app_ctx });
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "debug", .help = "Toggle debug logging", .func = cmd_debug });

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
