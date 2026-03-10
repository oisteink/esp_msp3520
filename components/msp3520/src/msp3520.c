#include "msp3520_priv.h"
#include "ili9488.h"
#include "xpt2046.h"
#include "backlight.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#define LVGL_TICK_PERIOD_MS 2

static const char *TAG = "msp3520";

/* Singleton for process_coordinates callback (esp_lcd_touch doesn't pass user_data to it) */
static msp3520_handle_t s_handle = NULL;

static void process_coordinates_cb(esp_lcd_touch_handle_t tp,
                                   uint16_t *x, uint16_t *y,
                                   uint16_t *strength,
                                   uint8_t *point_num,
                                   uint8_t max_point_num)
{
    if (!s_handle || *point_num == 0) return;

    /* Store raw ADC values before any mapping (used by calibration) */
    s_handle->last_raw_x = x[0];
    s_handle->last_raw_y = y[0];

    for (uint8_t i = 0; i < *point_num; i++) {
        if (s_handle->cal.valid) {
            uint16_t sx, sy;
            touch_cal_apply(&s_handle->cal, x[i], y[i], &sx, &sy,
                            MSP3520_H_RES, MSP3520_V_RES);
            x[i] = sx;
            y[i] = sy;
        } else {
            /* Rough linear map from ADC range to screen coordinates */
            x[i] = (uint16_t)((uint32_t)x[i] * MSP3520_H_RES / 4096);
            y[i] = (uint16_t)((uint32_t)y[i] * MSP3520_V_RES / 4096);
        }
    }
}

static void IRAM_ATTR touch_isr_handler(void *arg)
{
    esp_lcd_touch_xpt2046_notify_touch();
}

/* -- LVGL callbacks ------------------------------------------------ */

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

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t touch = lv_indev_get_user_data(indev);
    esp_lcd_touch_read_data(touch);

    uint16_t x, y;
    uint8_t count = 0;
    esp_lcd_touch_get_coordinates(touch, &x, &y, NULL, &count, 1);

    if (count > 0) {
        ESP_LOGD(TAG, "touch: x=%u y=%u count=%u", x, y, count);
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void lvgl_task(void *arg)
{
    msp3520_handle_t h = (msp3520_handle_t)arg;
    ESP_LOGI(TAG, "LVGL task started");
    uint32_t time_threshold_ms = 1000 / CONFIG_FREERTOS_HZ;
    while (1) {
        xSemaphoreTakeRecursive(h->lvgl_mutex, portMAX_DELAY);
        uint32_t next_ms = lv_timer_handler();
        xSemaphoreGiveRecursive(h->lvgl_mutex);
        if (next_ms < time_threshold_ms) {
            next_ms = time_threshold_ms;
        }
        vTaskDelay(pdMS_TO_TICKS(next_ms));
    }
}

/* -- Init helpers -------------------------------------------------- */

static esp_err_t init_display_spi(msp3520_handle_t h)
{
    const msp3520_config_t *c = &h->config;
    size_t max_transfer = MSP3520_H_RES * c->lvgl_draw_buf_lines * 3; /* RGB888 */

    spi_bus_config_t bus_cfg = {
        .sclk_io_num = c->display_sclk,
        .mosi_io_num = c->display_mosi,
        .miso_io_num = c->display_miso,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = max_transfer,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(c->display_spi_host, &bus_cfg, SPI_DMA_CH_AUTO),
                        TAG, "display SPI bus init failed");
    h->display_bus_initialized = true;

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = c->display_dc,
        .cs_gpio_num = c->display_cs,
        .pclk_hz = c->display_spi_clock_mhz * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(c->display_spi_host, &io_cfg, &h->panel_io),
                        TAG, "display panel IO init failed");
    return ESP_OK;
}

static esp_err_t init_display_panel(msp3520_handle_t h)
{
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = h->config.display_rst,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 24,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9488(h->panel_io, &panel_cfg, &h->panel),
                        TAG, "ILI9488 panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(h->panel), TAG, "panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(h->panel), TAG, "panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(h->panel, true, true), TAG, "panel mirror failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(h->panel, true), TAG, "panel on failed");
    return ESP_OK;
}

static esp_err_t init_touch_spi(msp3520_handle_t h)
{
    const msp3520_config_t *c = &h->config;

    if (c->touch_spi_host != c->display_spi_host) {
        spi_bus_config_t bus_cfg = {
            .sclk_io_num = c->touch_sclk,
            .mosi_io_num = c->touch_mosi,
            .miso_io_num = c->touch_miso,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 0,
        };
        ESP_RETURN_ON_ERROR(spi_bus_initialize(c->touch_spi_host, &bus_cfg, SPI_DMA_CH_AUTO),
                            TAG, "touch SPI bus init failed");
        h->touch_bus_initialized = true;
    } else {
        ESP_LOGI(TAG, "touch shares SPI bus with display (host %d)", c->touch_spi_host);
    }

    esp_lcd_panel_io_spi_config_t tp_io_cfg = XPT2046_SPI_IO_CONFIG(c->touch_cs);
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(c->touch_spi_host, &tp_io_cfg, &h->touch_io),
                        TAG, "touch panel IO init failed");
    return ESP_OK;
}

static esp_err_t init_touch_driver(msp3520_handle_t h)
{
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = MSP3520_H_RES,
        .y_max = MSP3520_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = h->config.touch_irq >= 0 ? h->config.touch_irq : GPIO_NUM_NC,
        .levels = { .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
        .process_coordinates = process_coordinates_cb,
        .user_data = h,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_spi_xpt2046(h->touch_io, &tp_cfg, &h->touch),
                        TAG, "XPT2046 init failed");

    if (h->config.touch_irq >= 0) {
        ESP_RETURN_ON_ERROR(gpio_install_isr_service(0), TAG, "GPIO ISR service failed");
        ESP_RETURN_ON_ERROR(gpio_isr_handler_add(h->config.touch_irq, touch_isr_handler, NULL),
                            TAG, "touch IRQ handler failed");
        ESP_LOGI(TAG, "touch IRQ on GPIO %d", h->config.touch_irq);
    }

    return ESP_OK;
}

static esp_err_t init_lvgl(msp3520_handle_t h)
{
    const msp3520_config_t *c = &h->config;

    h->lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    ESP_RETURN_ON_FALSE(h->lvgl_mutex, ESP_ERR_NO_MEM, TAG, "LVGL mutex create failed");

    lv_init();

    /* Display */
    size_t buf_sz = MSP3520_H_RES * c->lvgl_draw_buf_lines * 3; /* RGB888 */
    h->display = lv_display_create(MSP3520_H_RES, MSP3520_V_RES);
    ESP_RETURN_ON_FALSE(h->display, ESP_FAIL, TAG, "LVGL display create failed");

    lv_display_set_color_format(h->display, LV_COLOR_FORMAT_RGB888);
    lv_display_set_user_data(h->display, h->panel);
    lv_display_set_flush_cb(h->display, lvgl_flush_cb);

    void *buf1 = heap_caps_malloc(buf_sz, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *buf2 = heap_caps_malloc(buf_sz, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(buf1 && buf2, ESP_ERR_NO_MEM, TAG, "LVGL buffer alloc failed");
    lv_display_set_buffers(h->display, buf1, buf2, buf_sz,
                            LV_DISPLAY_RENDER_MODE_PARTIAL);

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = flush_ready_cb,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(
        h->panel_io, &cbs, h->display), TAG, "panel IO callback register failed");

    /* Touch input device */
    h->indev = lv_indev_create();
    ESP_RETURN_ON_FALSE(h->indev, ESP_FAIL, TAG, "LVGL indev create failed");
    lv_indev_set_type(h->indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(h->indev, touch_read_cb);
    lv_indev_set_user_data(h->indev, h->touch);
    lv_indev_set_display(h->indev, h->display);

    /* Tick timer */
    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick_args, &h->lvgl_tick_timer),
                        TAG, "LVGL tick timer create failed");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(h->lvgl_tick_timer,
                        LVGL_TICK_PERIOD_MS * 1000),
                        TAG, "LVGL tick timer start failed");

    /* LVGL task */
    BaseType_t xret = xTaskCreatePinnedToCore(lvgl_task, "lvgl",
                        c->lvgl_task_stack_size, h,
                        c->lvgl_task_priority, &h->lvgl_task_handle,
                        c->lvgl_task_core < 0 ? tskNO_AFFINITY : c->lvgl_task_core);
    ESP_RETURN_ON_FALSE(xret == pdPASS, ESP_FAIL, TAG, "LVGL task create failed");

    return ESP_OK;
}

/* -- Public API ---------------------------------------------------- */

esp_err_t msp3520_create(const msp3520_config_t *config, msp3520_handle_t *out_handle)
{
    esp_err_t ret = ESP_OK;
    ESP_RETURN_ON_FALSE(config && out_handle, ESP_ERR_INVALID_ARG, TAG, "null argument");

    msp3520_handle_t h = calloc(1, sizeof(struct msp3520_t));
    ESP_RETURN_ON_FALSE(h, ESP_ERR_NO_MEM, TAG, "no memory");
    h->config = *config;
    s_handle = h;

    ESP_LOGI(TAG, "initializing MSP3520 display module");

    /* Backlight */
    ESP_GOTO_ON_ERROR(backlight_init(config->display_bkl, config->display_bkl_active_high),
                      err, TAG, "backlight init failed");

    /* Display */
    ESP_GOTO_ON_ERROR(init_display_spi(h), err, TAG, "display SPI failed");
    ESP_GOTO_ON_ERROR(init_display_panel(h), err, TAG, "display panel failed");

    /* Touch */
    ESP_GOTO_ON_ERROR(init_touch_spi(h), err, TAG, "touch SPI failed");
    ESP_GOTO_ON_ERROR(init_touch_driver(h), err, TAG, "touch driver failed");

    /* Load calibration and z_threshold from NVS */
    touch_cal_load(&h->cal);
    if (h->cal.valid) {
        ESP_LOGI(TAG, "touch calibration loaded from NVS");
    }
    uint16_t z = touch_z_threshold_load(config->touch_z_threshold);
    esp_lcd_touch_xpt2046_set_z_threshold(z);
    ESP_LOGI(TAG, "touch z_threshold=%u", z);

    /* LVGL */
    ESP_GOTO_ON_ERROR(init_lvgl(h), err, TAG, "LVGL init failed");

    ESP_LOGI(TAG, "MSP3520 initialized successfully");
    *out_handle = h;
    return ESP_OK;

err:
    msp3520_destroy(h);
    return ret;
}

esp_err_t msp3520_destroy(msp3520_handle_t handle)
{
    if (!handle) return ESP_OK;

    if (handle->lvgl_task_handle) {
        vTaskDelete(handle->lvgl_task_handle);
    }
    if (handle->lvgl_tick_timer) {
        esp_timer_stop(handle->lvgl_tick_timer);
        esp_timer_delete(handle->lvgl_tick_timer);
    }
    if (handle->display) {
        /* Free draw buffers */
        lv_display_set_buffers(handle->display, NULL, NULL, 0,
                               LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_delete(handle->display);
    }
    if (handle->indev) {
        lv_indev_delete(handle->indev);
    }

    if (handle->touch) {
        esp_lcd_touch_del(handle->touch);
    }
    if (handle->touch_io) {
        esp_lcd_panel_io_del(handle->touch_io);
    }
    if (handle->panel) {
        esp_lcd_panel_del(handle->panel);
    }
    if (handle->panel_io) {
        esp_lcd_panel_io_del(handle->panel_io);
    }
    if (handle->touch_bus_initialized) {
        spi_bus_free(handle->config.touch_spi_host);
    }
    if (handle->display_bus_initialized) {
        spi_bus_free(handle->config.display_spi_host);
    }
    if (handle->lvgl_mutex) {
        vSemaphoreDelete(handle->lvgl_mutex);
    }

    if (s_handle == handle) {
        s_handle = NULL;
    }
    free(handle);
    return ESP_OK;
}

lv_display_t *msp3520_get_display(msp3520_handle_t handle)
{
    return handle ? handle->display : NULL;
}

lv_indev_t *msp3520_get_indev(msp3520_handle_t handle)
{
    return handle ? handle->indev : NULL;
}

bool msp3520_lvgl_lock(msp3520_handle_t handle, uint32_t timeout_ms)
{
    if (!handle || !handle->lvgl_mutex) return false;
    TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(handle->lvgl_mutex, ticks) == pdTRUE;
}

void msp3520_lvgl_unlock(msp3520_handle_t handle)
{
    if (handle && handle->lvgl_mutex) {
        xSemaphoreGiveRecursive(handle->lvgl_mutex);
    }
}

esp_err_t msp3520_set_backlight(msp3520_handle_t handle, uint8_t brightness)
{
    return backlight_set(brightness);
}

bool msp3520_is_calibrated(msp3520_handle_t handle)
{
    return handle ? handle->cal.valid : false;
}

esp_err_t msp3520_clear_calibration(msp3520_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    handle->cal.valid = false;
    return touch_cal_clear();
}
