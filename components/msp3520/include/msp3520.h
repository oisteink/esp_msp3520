#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MSP3520_H_RES 320
#define MSP3520_V_RES 480

typedef struct {
    /* Display SPI */
    int display_spi_host;
    int display_sclk;
    int display_mosi;
    int display_miso;
    int display_cs;
    int display_dc;
    int display_rst;              /* -1 = software reset */
    int display_bkl;              /* -1 = no backlight control */
    bool display_bkl_active_high;
    int display_spi_clock_mhz;

    /* Touch SPI */
    int touch_spi_host;
    int touch_sclk;
    int touch_mosi;
    int touch_miso;
    int touch_cs;
    int touch_irq;                /* -1 = polling mode */
    int touch_z_threshold;

    /* LVGL */
    int lvgl_task_core;           /* -1 = no affinity */
    int lvgl_task_priority;
    int lvgl_task_stack_size;
    int lvgl_draw_buf_lines;      /* min: MSP3520_V_RES / 10 */
} msp3520_config_t;

#define MSP3520_CONFIG_DEFAULT() {                                         \
    .display_spi_host       = CONFIG_MSP3520_DISPLAY_SPI_HOST_ID,          \
    .display_sclk           = CONFIG_MSP3520_DISPLAY_SCLK_GPIO,            \
    .display_mosi           = CONFIG_MSP3520_DISPLAY_MOSI_GPIO,            \
    .display_miso           = CONFIG_MSP3520_DISPLAY_MISO_GPIO,            \
    .display_cs             = CONFIG_MSP3520_DISPLAY_CS_GPIO,              \
    .display_dc             = CONFIG_MSP3520_DISPLAY_DC_GPIO,              \
    .display_rst            = CONFIG_MSP3520_DISPLAY_RST_GPIO,             \
    .display_bkl            = CONFIG_MSP3520_DISPLAY_BKL_GPIO,             \
    .display_bkl_active_high = CONFIG_MSP3520_DISPLAY_BKL_ACTIVE_HIGH,     \
    .display_spi_clock_mhz  = CONFIG_MSP3520_DISPLAY_SPI_CLOCK_MHZ,       \
    .touch_spi_host         = CONFIG_MSP3520_TOUCH_SPI_HOST_ID,            \
    .touch_sclk             = CONFIG_MSP3520_TOUCH_SCLK_GPIO,              \
    .touch_mosi             = CONFIG_MSP3520_TOUCH_MOSI_GPIO,              \
    .touch_miso             = CONFIG_MSP3520_TOUCH_MISO_GPIO,              \
    .touch_cs               = CONFIG_MSP3520_TOUCH_CS_GPIO,                \
    .touch_irq              = CONFIG_MSP3520_TOUCH_IRQ_GPIO,               \
    .touch_z_threshold      = CONFIG_MSP3520_TOUCH_Z_THRESHOLD,            \
    .lvgl_task_core         = CONFIG_MSP3520_LVGL_TASK_CORE_ID,            \
    .lvgl_task_priority     = CONFIG_MSP3520_LVGL_TASK_PRIORITY,           \
    .lvgl_task_stack_size   = CONFIG_MSP3520_LVGL_TASK_STACK_SIZE,         \
    .lvgl_draw_buf_lines    = CONFIG_MSP3520_LVGL_DRAW_BUF_LINES,         \
}

typedef struct msp3520_t *msp3520_handle_t;

/* Lifecycle */
esp_err_t msp3520_create(const msp3520_config_t *config, msp3520_handle_t *out_handle);
esp_err_t msp3520_destroy(msp3520_handle_t handle);

/* LVGL access */
lv_display_t *msp3520_get_display(msp3520_handle_t handle);
lv_indev_t *msp3520_get_indev(msp3520_handle_t handle);
bool msp3520_lvgl_lock(msp3520_handle_t handle, uint32_t timeout_ms);
void msp3520_lvgl_unlock(msp3520_handle_t handle);

/* Hardware control */
esp_err_t msp3520_set_backlight(msp3520_handle_t handle, uint8_t brightness);

/* Touch calibration */
esp_err_t msp3520_start_calibration(msp3520_handle_t handle);
esp_err_t msp3520_clear_calibration(msp3520_handle_t handle);
bool msp3520_is_calibrated(msp3520_handle_t handle);

/* Console commands (optional) */
esp_err_t msp3520_register_console_commands(msp3520_handle_t handle);

#ifdef __cplusplus
}
#endif
