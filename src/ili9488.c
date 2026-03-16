#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ili9488.h"

static const char *TAG = "ili9488";

// ILI9488 specific registers
#define ILI9488_INTRFC_MODE_CTL      0xB0
#define ILI9488_FRAME_RATE_NORMAL_CTL 0xB1
#define ILI9488_INVERSION_CTL        0xB4
#define ILI9488_FUNCTION_CTL         0xB6
#define ILI9488_ENTRY_MODE_CTL       0xB7
#define ILI9488_POWER_CTL_ONE        0xC0
#define ILI9488_POWER_CTL_TWO        0xC1
#define ILI9488_VCOM_CTL             0xC5
#define ILI9488_ADJUST_CTL_THREE     0xF7

// Color mode values for COLMOD register
#define ILI9488_COLOR_MODE_16BIT     0x55
#define ILI9488_COLOR_MODE_18BIT     0x66

typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t len;  // 0xFF = end marker
} ili9488_init_cmd_t;

typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    bool reset_level;
    int x_gap;
    int y_gap;
    uint8_t madctl;         // Current MADCTL register value
    uint8_t color_mode;     // 0x66 for SPI (RGB666)
    uint8_t bytes_per_pixel; // 3 for RGB666
} ili9488_panel_t;

static esp_err_t panel_ili9488_del(esp_lcd_panel_t *panel)
{
    ili9488_panel_t *ili9488 = __containerof(panel, ili9488_panel_t, base);
    if (ili9488->reset_gpio_num >= 0) {
        gpio_reset_pin(ili9488->reset_gpio_num);
    }
    ESP_LOGI(TAG, "del ili9488 panel @%p", ili9488);
    free(ili9488);
    return ESP_OK;
}

static esp_err_t panel_ili9488_reset(esp_lcd_panel_t *panel)
{
    ili9488_panel_t *ili9488 = __containerof(panel, ili9488_panel_t, base);
    esp_lcd_panel_io_handle_t io = ili9488->io;

    if (ili9488->reset_gpio_num >= 0) {
        gpio_set_level(ili9488->reset_gpio_num, ili9488->reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(ili9488->reset_gpio_num, !ili9488->reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
    } else {
        esp_lcd_panel_io_tx_param(io, LCD_CMD_SWRESET, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return ESP_OK;
}

static esp_err_t panel_ili9488_init(esp_lcd_panel_t *panel)
{
    ili9488_panel_t *ili9488 = __containerof(panel, ili9488_panel_t, base);
    esp_lcd_panel_io_handle_t io = ili9488->io;

    const ili9488_init_cmd_t init_cmds[] = {
        {ILI9488_POWER_CTL_ONE,        {0x17, 0x15},                   2},
        {ILI9488_POWER_CTL_TWO,        {0x41},                         1},
        {ILI9488_VCOM_CTL,             {0x00, 0x12, 0x80},             3},
        {LCD_CMD_MADCTL,               {ili9488->madctl},               1},
        {LCD_CMD_COLMOD,               {ili9488->color_mode},           1},
        {ILI9488_INTRFC_MODE_CTL,      {0x00},                         1},
        {ILI9488_FRAME_RATE_NORMAL_CTL, {0xA0},                        1},
        {ILI9488_INVERSION_CTL,        {0x02},                         1},
        {ILI9488_FUNCTION_CTL,         {0x02, 0x02, 0x3B},             3},
        {ILI9488_ENTRY_MODE_CTL,       {0xC6},                         1},
        {ILI9488_ADJUST_CTL_THREE,     {0xA9, 0x51, 0x2C, 0x02},      4},
        {LCD_CMD_NOP,                  {0},                             0xFF},
    };

    ESP_LOGI(TAG, "initializing ILI9488");
    for (int i = 0; init_cmds[i].len != 0xFF; i++) {
        esp_lcd_panel_io_tx_param(io, init_cmds[i].cmd,
                                  init_cmds[i].data, init_cmds[i].len);
    }

    esp_lcd_panel_io_tx_param(io, LCD_CMD_SLPOUT, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_lcd_panel_io_tx_param(io, LCD_CMD_DISPON, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "initialization complete");
    return ESP_OK;
}

static esp_err_t panel_ili9488_draw_bitmap(esp_lcd_panel_t *panel,
                                            int x_start, int y_start,
                                            int x_end, int y_end,
                                            const void *color_data)
{
    ili9488_panel_t *ili9488 = __containerof(panel, ili9488_panel_t, base);
    esp_lcd_panel_io_handle_t io = ili9488->io;

    assert((x_start < x_end) && (y_start < y_end));

    x_start += ili9488->x_gap;
    x_end += ili9488->x_gap;
    y_start += ili9488->y_gap;
    y_end += ili9488->y_gap;

    // Column address set (x_end is exclusive, display expects inclusive)
    uint8_t col_data[] = {
        (x_start >> 8) & 0xFF, x_start & 0xFF,
        ((x_end - 1) >> 8) & 0xFF, (x_end - 1) & 0xFF,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_io_tx_param(io, LCD_CMD_CASET, col_data, sizeof(col_data)),
        TAG, "CASET failed");

    // Row address set
    uint8_t row_data[] = {
        (y_start >> 8) & 0xFF, y_start & 0xFF,
        ((y_end - 1) >> 8) & 0xFF, (y_end - 1) & 0xFF,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_io_tx_param(io, LCD_CMD_RASET, row_data, sizeof(row_data)),
        TAG, "RASET failed");

    // Memory write
    size_t len = (x_end - x_start) * (y_end - y_start) * ili9488->bytes_per_pixel;
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_io_tx_color(io, LCD_CMD_RAMWR, color_data, len),
        TAG, "RAMWR failed");

    return ESP_OK;
}

static esp_err_t panel_ili9488_invert_color(esp_lcd_panel_t *panel, bool invert)
{
    ili9488_panel_t *ili9488 = __containerof(panel, ili9488_panel_t, base);
    int cmd = invert ? LCD_CMD_INVON : LCD_CMD_INVOFF;
    return esp_lcd_panel_io_tx_param(ili9488->io, cmd, NULL, 0);
}

static esp_err_t panel_ili9488_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
{
    ili9488_panel_t *ili9488 = __containerof(panel, ili9488_panel_t, base);

    // ILI9488 default column order is right-to-left, so MX logic is inverted
    if (mirror_x) {
        ili9488->madctl &= ~LCD_CMD_MX_BIT;
    } else {
        ili9488->madctl |= LCD_CMD_MX_BIT;
    }
    if (mirror_y) {
        ili9488->madctl |= LCD_CMD_MY_BIT;
    } else {
        ili9488->madctl &= ~LCD_CMD_MY_BIT;
    }
    return esp_lcd_panel_io_tx_param(ili9488->io, LCD_CMD_MADCTL, &ili9488->madctl, 1);
}

static esp_err_t panel_ili9488_swap_xy(esp_lcd_panel_t *panel, bool swap)
{
    ili9488_panel_t *ili9488 = __containerof(panel, ili9488_panel_t, base);
    if (swap) {
        ili9488->madctl |= LCD_CMD_MV_BIT;
    } else {
        ili9488->madctl &= ~LCD_CMD_MV_BIT;
    }
    return esp_lcd_panel_io_tx_param(ili9488->io, LCD_CMD_MADCTL, &ili9488->madctl, 1);
}

static esp_err_t panel_ili9488_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
{
    ili9488_panel_t *ili9488 = __containerof(panel, ili9488_panel_t, base);
    ili9488->x_gap = x_gap;
    ili9488->y_gap = y_gap;
    return ESP_OK;
}

static esp_err_t panel_ili9488_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    ili9488_panel_t *ili9488 = __containerof(panel, ili9488_panel_t, base);
    int cmd = on_off ? LCD_CMD_DISPON : LCD_CMD_DISPOFF;
    esp_lcd_panel_io_tx_param(ili9488->io, cmd, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

esp_err_t esp_lcd_new_panel_ili9488(const esp_lcd_panel_io_handle_t io,
                                     const esp_lcd_panel_dev_config_t *panel_dev_config,
                                     esp_lcd_panel_handle_t *ret_panel)
{
    esp_err_t ret = ESP_OK;
    ili9488_panel_t *ili9488 = NULL;

    ESP_GOTO_ON_FALSE(io && panel_dev_config && ret_panel,
                      ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");

    ili9488 = calloc(1, sizeof(ili9488_panel_t));
    ESP_GOTO_ON_FALSE(ili9488, ESP_ERR_NO_MEM, err, TAG, "no mem for ili9488 panel");

    if (panel_dev_config->reset_gpio_num >= 0) {
        gpio_config_t cfg = {
            .pin_bit_mask = BIT64(panel_dev_config->reset_gpio_num),
            .mode = GPIO_MODE_OUTPUT,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&cfg), err, TAG, "configure reset GPIO failed");
    }

    switch (panel_dev_config->bits_per_pixel) {
    case 18:
    case 24:
        ili9488->color_mode = ILI9488_COLOR_MODE_18BIT;
        ili9488->bytes_per_pixel = 3;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG,
                          "unsupported bits_per_pixel: %lu (use 18 or 24 for SPI)",
                          (unsigned long)panel_dev_config->bits_per_pixel);
    }

    switch (panel_dev_config->rgb_ele_order) {
    case LCD_RGB_ELEMENT_ORDER_RGB:
        ili9488->madctl = 0;
        break;
    case LCD_RGB_ELEMENT_ORDER_BGR:
        ili9488->madctl = LCD_CMD_BGR_BIT;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_INVALID_ARG, err, TAG,
                          "unsupported rgb_ele_order");
    }

    ili9488->io = io;
    ili9488->reset_gpio_num = panel_dev_config->reset_gpio_num;
    ili9488->reset_level = panel_dev_config->flags.reset_active_high;
    ili9488->base.del = panel_ili9488_del;
    ili9488->base.reset = panel_ili9488_reset;
    ili9488->base.init = panel_ili9488_init;
    ili9488->base.draw_bitmap = panel_ili9488_draw_bitmap;
    ili9488->base.invert_color = panel_ili9488_invert_color;
    ili9488->base.mirror = panel_ili9488_mirror;
    ili9488->base.swap_xy = panel_ili9488_swap_xy;
    ili9488->base.set_gap = panel_ili9488_set_gap;
    ili9488->base.disp_on_off = panel_ili9488_disp_on_off;
    *ret_panel = &ili9488->base;

    ESP_LOGI(TAG, "new ili9488 panel @%p", ili9488);
    return ESP_OK;

err:
    if (ili9488) {
        if (panel_dev_config->reset_gpio_num >= 0) {
            gpio_reset_pin(panel_dev_config->reset_gpio_num);
        }
        free(ili9488);
    }
    return ret;
}
