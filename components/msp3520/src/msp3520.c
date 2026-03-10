#include "msp3520_priv.h"
#include "ili9488.h"
#include "xpt2046.h"
#include "backlight.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

static const char *TAG = "msp3520";

/* Singleton for process_coordinates callback (esp_lcd_touch doesn't pass user_data to it) */
static msp3520_handle_t s_handle = NULL;

static void process_coordinates_cb(esp_lcd_touch_handle_t tp,
                                   uint16_t *x, uint16_t *y,
                                   uint16_t *strength,
                                   uint8_t *point_num,
                                   uint8_t max_point_num)
{
    if (!s_handle || !s_handle->cal.valid || *point_num == 0) return;

    for (uint8_t i = 0; i < *point_num; i++) {
        uint16_t sx, sy;
        touch_cal_apply(&s_handle->cal, x[i], y[i], &sx, &sy,
                        MSP3520_H_RES, MSP3520_V_RES);
        x[i] = sx;
        y[i] = sy;
    }
}

static void IRAM_ATTR touch_isr_handler(void *arg)
{
    esp_lcd_touch_xpt2046_notify_touch();
}

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

    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = c->lvgl_task_priority,
        .task_stack = c->lvgl_task_stack_size,
        .task_affinity = c->lvgl_task_core,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 2,
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL port init failed");

    /* Display */
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = h->panel_io,
        .panel_handle = h->panel,
        .buffer_size = MSP3520_H_RES * c->lvgl_draw_buf_lines * sizeof(lv_color_t),
        .double_buffer = true,
        .hres = MSP3520_H_RES,
        .vres = MSP3520_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB888,
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .swap_bytes = false,
        },
    };
    h->display = lvgl_port_add_disp(&disp_cfg);
    ESP_RETURN_ON_FALSE(h->display, ESP_FAIL, TAG, "LVGL display add failed");

    /* Touch */
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = h->display,
        .handle = h->touch,
    };
    h->indev = lvgl_port_add_touch(&touch_cfg);
    ESP_RETURN_ON_FALSE(h->indev, ESP_FAIL, TAG, "LVGL touch add failed");

    return ESP_OK;
}

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

    if (handle->display) {
        lvgl_port_remove_disp(handle->display);
    }
    lvgl_port_deinit();

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
    return lvgl_port_lock(timeout_ms);
}

void msp3520_lvgl_unlock(msp3520_handle_t handle)
{
    lvgl_port_unlock();
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
