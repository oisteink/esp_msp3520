#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_ili9488.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "app";
static SemaphoreHandle_t trans_done_sem;

static bool on_color_trans_done(esp_lcd_panel_io_handle_t io,
                                 esp_lcd_panel_io_event_data_t *edata,
                                 void *user_ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(trans_done_sem, &woken);
    return woken;
}

#define LCD_H_RES 320
#define LCD_V_RES 480
#define LCD_BPP   3  // bytes per pixel (RGB666)

// Band height for partial screen fills (must fit in RAM)
// 320 * 32 * 3 = 30720 bytes
#define BAND_LINES 32

static void fill_screen(esp_lcd_panel_handle_t panel, uint8_t r, uint8_t g, uint8_t b)
{
    const size_t band_size = LCD_H_RES * BAND_LINES * LCD_BPP;
    uint8_t *buf = heap_caps_malloc(band_size, MALLOC_CAP_DMA);
    assert(buf);

    // Fill buffer with the color pattern (R, G, B per pixel)
    for (int i = 0; i < LCD_H_RES * BAND_LINES; i++) {
        buf[i * 3 + 0] = r;
        buf[i * 3 + 1] = g;
        buf[i * 3 + 2] = b;
    }

    // Draw in bands, waiting for each DMA transfer to complete
    for (int y = 0; y < LCD_V_RES; y += BAND_LINES) {
        int y_end = y + BAND_LINES;
        if (y_end > LCD_V_RES) {
            y_end = LCD_V_RES;
        }
        esp_lcd_panel_draw_bitmap(panel, 0, y, LCD_H_RES, y_end, buf);
        xSemaphoreTake(trans_done_sem, portMAX_DELAY);
    }

    free(buf);
}

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
    spi_bus_config_t bus_cfg = {
        .sclk_io_num = CONFIG_LCD_SPI_SCLK_GPIO,
        .mosi_io_num = CONFIG_LCD_SPI_MOSI_GPIO,
        .miso_io_num = -1,  // write-only
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * BAND_LINES * LCD_BPP,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    // Semaphore for waiting on DMA completion
    trans_done_sem = xSemaphoreCreateBinary();
    assert(trans_done_sem);

    // Panel IO
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
        .on_color_trans_done = on_color_trans_done,
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
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    ESP_LOGI(TAG, "starting color fill loop");
    while (1) {
        fill_screen(panel, 0xFF, 0x00, 0x00);  // Red
        vTaskDelay(pdMS_TO_TICKS(1000));
        fill_screen(panel, 0x00, 0xFF, 0x00);  // Green
        vTaskDelay(pdMS_TO_TICKS(1000));
        fill_screen(panel, 0x00, 0x00, 0xFF);  // Blue
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
