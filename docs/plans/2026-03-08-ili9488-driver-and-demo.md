# ILI9488 SPI Driver + Color Fill Demo Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build an ILI9488 esp_lcd panel driver component and a demo app that fills the screen red → green → blue in a loop.

**Architecture:** Local ESP-IDF component (`components/esp_lcd_ili9488/`) implements `esp_lcd_panel_t`. App in `main/` configures SPI, initializes driver, and runs color fill loop. Pin config via Kconfig + sdkconfig.defaults.

**Tech Stack:** ESP-IDF v5.5.3, esp_lcd framework, SPI, C

**References:**
- Spec: `iteration/spec.md`
- Research: `iteration/research.md`
- Reference driver: `~/src/zenith/zenith_components/esp_lcd_ili9488/`
- Board pinout: `docs/nanoesp32-c6.md`
- Display pinout: `docs/msp3520.md`

---

### Task 1: Component Build Config

**Files:**
- Modify: `components/esp_lcd_ili9488/CMakeLists.txt`

**Step 1: Update CMakeLists.txt with proper dependencies**

```cmake
idf_component_register(
    SRCS "esp_lcd_ili9488.c"
    INCLUDE_DIRS "include"
    REQUIRES "esp_lcd"
    PRIV_REQUIRES "esp_driver_gpio"
)
```

**Step 2: Verify build compiles (will have empty driver, that's OK for now)**

Run: `source ~/esp/v5.5.3/esp-idf/export.sh && idf.py set-target esp32c6 && idf.py build`
Expected: Build succeeds (stub files still present)

**Step 3: Commit**

```bash
git add components/esp_lcd_ili9488/CMakeLists.txt
git commit -m "chore: add esp_lcd and gpio dependencies to ili9488 component"
```

---

### Task 2: Driver Header

**Files:**
- Modify: `components/esp_lcd_ili9488/include/esp_lcd_ili9488.h`

**Step 1: Write the public header**

```c
#pragma once

#include "esp_lcd_panel_vendor.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create LCD panel for ILI9488 (SPI interface)
 *
 * @param[in] io LCD panel IO handle (from esp_lcd_new_panel_io_spi)
 * @param[in] panel_dev_config General panel device configuration
 *            - bits_per_pixel: must be 18 or 24 (both use RGB666, 3 bytes/pixel)
 *            - rgb_ele_order: LCD_RGB_ELEMENT_ORDER_RGB or _BGR
 *            - reset_gpio_num: GPIO for hardware reset, or -1 for software reset
 * @param[out] ret_panel Returned LCD panel handle
 * @return
 *      - ESP_ERR_INVALID_ARG   if parameter is invalid
 *      - ESP_ERR_NO_MEM        if out of memory
 *      - ESP_OK                on success
 */
esp_err_t esp_lcd_new_panel_ili9488(const esp_lcd_panel_io_handle_t io,
                                     const esp_lcd_panel_dev_config_t *panel_dev_config,
                                     esp_lcd_panel_handle_t *ret_panel);

#ifdef __cplusplus
}
#endif
```

**Step 2: Build to verify header compiles**

Run: `idf.py build`
Expected: Build succeeds (driver .c still has stub, that's fine)

**Step 3: Commit**

```bash
git add components/esp_lcd_ili9488/include/esp_lcd_ili9488.h
git commit -m "feat: add ili9488 driver public header"
```

---

### Task 3: Driver Implementation

**Files:**
- Modify: `components/esp_lcd_ili9488/esp_lcd_ili9488.c`

**Step 1: Write the full driver**

Replace the entire file. The driver follows the reference driver pattern from `~/src/zenith/zenith_components/esp_lcd_ili9488/esp_lcd_ili9488.c` but drops IDF v4 compat.

```c
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

#include "esp_lcd_ili9488.h"

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
```

**Step 2: Build to verify driver compiles**

Run: `idf.py build`
Expected: Build succeeds (main still has empty app_main, that's fine — driver is a separate component)

**Step 3: Commit**

```bash
git add components/esp_lcd_ili9488/esp_lcd_ili9488.c
git commit -m "feat: implement ili9488 esp_lcd panel driver"
```

---

### Task 4: Kconfig + sdkconfig.defaults

**Files:**
- Create: `main/Kconfig.projbuild`
- Create: `sdkconfig.defaults`

**Step 1: Create Kconfig.projbuild for pin and SPI configuration**

```kconfig
menu "ILI9488 Display Configuration"

    config LCD_SPI_SCLK_GPIO
        int "SPI SCLK GPIO"
        range ENV_GPIO_RANGE_MIN ENV_GPIO_OUT_RANGE_MAX
        default 6
        help
            GPIO number for the SPI clock line.

    config LCD_SPI_MOSI_GPIO
        int "SPI MOSI GPIO"
        range ENV_GPIO_RANGE_MIN ENV_GPIO_OUT_RANGE_MAX
        default 7
        help
            GPIO number for the SPI MOSI line.

    config LCD_CS_GPIO
        int "LCD CS GPIO"
        range ENV_GPIO_RANGE_MIN ENV_GPIO_OUT_RANGE_MAX
        default 20
        help
            GPIO number for the LCD chip select line.

    config LCD_DC_GPIO
        int "LCD DC GPIO"
        range ENV_GPIO_RANGE_MIN ENV_GPIO_OUT_RANGE_MAX
        default 21
        help
            GPIO number for the LCD data/command line.

    config LCD_RST_GPIO
        int "LCD RST GPIO"
        range -1 ENV_GPIO_OUT_RANGE_MAX
        default 22
        help
            GPIO number for the LCD reset line. Set to -1 for software reset.

    config LCD_BKL_GPIO
        int "LCD backlight GPIO"
        range -1 ENV_GPIO_OUT_RANGE_MAX
        default 23
        help
            GPIO number for the LCD backlight. Set to -1 to disable.

    config LCD_SPI_CLOCK_MHZ
        int "SPI clock speed (MHz)"
        range 1 40
        default 20
        help
            SPI clock frequency in MHz for the LCD.

endmenu
```

**Step 2: Create sdkconfig.defaults with NanoESP32-C6 defaults**

```
# Target
CONFIG_IDF_TARGET="esp32c6"

# Pin assignments for NanoESP32-C6
CONFIG_LCD_SPI_SCLK_GPIO=6
CONFIG_LCD_SPI_MOSI_GPIO=7
CONFIG_LCD_CS_GPIO=20
CONFIG_LCD_DC_GPIO=21
CONFIG_LCD_RST_GPIO=22
CONFIG_LCD_BKL_GPIO=23
CONFIG_LCD_SPI_CLOCK_MHZ=20
```

**Step 3: Delete existing sdkconfig so defaults take effect, then build**

Run: `rm sdkconfig && idf.py build`
Expected: Build succeeds, sdkconfig regenerated from defaults

**Step 4: Commit**

```bash
git add main/Kconfig.projbuild sdkconfig.defaults
git commit -m "feat: add Kconfig for LCD pin assignments and SPI config"
```

---

### Task 5: Demo App

**Files:**
- Modify: `main/ili9488-test.c`
- Modify: `main/CMakeLists.txt`

**Step 1: Update main CMakeLists.txt to depend on the driver**

```cmake
idf_component_register(SRCS "ili9488-test.c"
                    INCLUDE_DIRS "."
                    REQUIRES "esp_lcd_ili9488" "esp_driver_spi")
```

**Step 2: Write the demo app**

```c
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_ili9488.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app";

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

    // Draw in bands
    for (int y = 0; y < LCD_V_RES; y += BAND_LINES) {
        int y_end = y + BAND_LINES;
        if (y_end > LCD_V_RES) {
            y_end = LCD_V_RES;
        }
        esp_lcd_panel_draw_bitmap(panel, 0, y, LCD_H_RES, y_end, buf);
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
```

**Step 3: Build**

Run: `rm sdkconfig && idf.py build`
Expected: Build succeeds with no warnings

**Step 4: Commit**

```bash
git add main/CMakeLists.txt main/ili9488-test.c
git commit -m "feat: add color fill demo app with SPI and Kconfig wiring"
```

---

### Task 6: Flash and Test

**Step 1: Flash to NanoESP32-C6**

Run: `idf.py -p /dev/ttyUSB0 flash monitor`
Expected: Display shows solid red, then green, then blue, cycling every second

**Step 2: Verify acceptance criteria**

- [ ] Build succeeds with no warnings for esp32c6
- [ ] Display cycles red → green → blue
- [ ] Driver component has no board-specific code (pin config all in main/)
- [ ] Driver implements all `esp_lcd_panel_t` callbacks from spec

**Step 3: Commit any fixups if needed, then final commit**

```bash
git add -A
git commit -m "chore: iteration 1 complete - ili9488 driver + color fill demo"
```
