# Plan: MSP3520 ESP-IDF Component

**Date:** 2026-03-10
**Builds on:** [spec.md](spec.md), [research.md](research.md)

## Overview

Restructure the project into a reusable `msp3520` component + example app. Existing code moves into the component; `main/` becomes a thin example.

## Build Order

The work is sequenced so we can compile and test incrementally:

1. **Scaffold component** — directory structure, CMakeLists, Kconfig, idf_component.yml
2. **Move drivers** — ILI9488 + XPT2046 sources into component, internal headers
3. **Move calibration** — touch_calibration into component
4. **Write msp3520.c** — the orchestrator: SPI init, driver init, esp_lvgl_port setup, backlight
5. **Write msp3520.h** — public API header
6. **Write console_commands.c** — extract touch/display commands from console.c
7. **Write Kconfig** — all menuconfig options (SPI hosts, pins, LVGL, backlight)
8. **Create example** — thin app_main using the component
9. **Remove old structure** — delete old components/, slim down main/

## Step 1: Scaffold Component

Create directory structure:

```
components/msp3520/
  include/
    msp3520.h
  src/
    msp3520.c
    ili9488.c
    ili9488.h
    xpt2046.c
    xpt2046.h
    touch_calibration.c
    touch_calibration.h
    console_commands.c
    backlight.c
    backlight.h
  Kconfig
  CMakeLists.txt
  idf_component.yml
```

**CMakeLists.txt:**
```cmake
idf_component_register(
    SRCS "src/msp3520.c"
         "src/ili9488.c"
         "src/xpt2046.c"
         "src/touch_calibration.c"
         "src/console_commands.c"
         "src/backlight.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_lcd lvgl
    PRIV_REQUIRES esp_driver_spi esp_driver_gpio esp_driver_ledc
                  esp_timer nvs_flash esp_console
)
```

**idf_component.yml:**
```yaml
version: "0.1.0"
description: "MSP3520 display module driver (ILI9488 + XPT2046 + LVGL)"
dependencies:
  idf: ">=5.0.0"
  espressif/esp_lcd_touch: ">=1.0.4"
  espressif/esp_lvgl_port: ">=2.0.0"
  lvgl/lvgl: "^9.5.0"
```

## Step 2: Move Drivers

### ILI9488

- Copy `components/esp_lcd_ili9488/esp_lcd_ili9488.c` → `components/msp3520/src/ili9488.c`
- Create `components/msp3520/src/ili9488.h` — internal header exposing `esp_lcd_new_panel_ili9488()`
- No changes to driver code itself

### XPT2046

- Copy `components/xpt2046/esp_lcd_touch_xpt2046.c` → `components/msp3520/src/xpt2046.c`
- Create `components/msp3520/src/xpt2046.h` — internal header exposing:
  - `esp_lcd_touch_new_spi_xpt2046()`
  - `esp_lcd_touch_xpt2046_set_z_threshold()` / `get_z_threshold()`
  - `esp_lcd_touch_xpt2046_notify_touch()`
  - `ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG()` macro
- The XPT2046 Kconfig options (`Z_THRESHOLD`, `INTERRUPT_MODE`, `VREF_ON_MODE`, etc.) move to the component's Kconfig under an "XPT2046 Advanced" submenu, renamed with `MSP3520_` prefix

## Step 3: Move Calibration

- Copy `main/touch_calibration.{c,h}` → `components/msp3520/src/touch_calibration.{c,h}`
- No API changes. Internal to component.
- NVS namespace stays `"touch_cal"`

## Step 4: Write msp3520.c

This is the orchestrator. The `msp3520_create()` function does what `app_main()` currently does lines 172-330:

```c
struct msp3520_t {
    // Handles
    esp_lcd_panel_handle_t panel;
    esp_lcd_touch_handle_t touch;
    lv_display_t *display;
    lv_indev_t *indev;

    // State
    touch_cal_t cal;
    msp3520_config_t config;

    // Backlight
    bool bkl_active_high;
    int bkl_gpio;
};
```

**`msp3520_create()` sequence:**

1. Allocate `msp3520_t` struct
2. **Backlight** — LEDC PWM init on backlight GPIO, set 100%
3. **Display SPI bus** — `spi_bus_initialize(config->display_spi_host, ...)`
4. **Display panel IO** — `esp_lcd_new_panel_io_spi()` with CS, DC, clock
5. **Display panel** — `esp_lcd_new_panel_ili9488()` → reset → init → mirror for MSP3520 orientation
6. **Touch SPI bus** — if `config->touch_spi_host != config->display_spi_host`, init separate bus. Otherwise skip (already initialized).
7. **Touch panel IO** — `esp_lcd_new_panel_io_spi()` with touch CS, 1 MHz
8. **Touch driver** — `esp_lcd_touch_new_spi_xpt2046()` with `process_coordinates` callback pointing to calibration transform
9. **Touch IRQ** — if `config->touch_irq >= 0`, configure GPIO ISR
10. **Load calibration** — `touch_cal_load(&handle->cal)`
11. **Load z_threshold** — `touch_z_threshold_load()` → `esp_lcd_touch_xpt2046_set_z_threshold()`
12. **LVGL via esp_lvgl_port:**
    - `lvgl_port_init()` with task core, priority, stack size from config
    - `lvgl_port_add_disp()` with panel handle, buffer config (internal RAM, double-buffered, `draw_buf_lines` height), RGB888
    - `lvgl_port_add_touch()` with touch handle
13. Store display/indev handles in struct
14. Return handle

**Key detail for SPI bus sharing (R3):**

```c
// Init display bus
spi_bus_initialize(config->display_spi_host, &display_bus_cfg, SPI_DMA_CH_AUTO);

// Init touch bus only if different host
if (config->touch_spi_host != config->display_spi_host) {
    spi_bus_initialize(config->touch_spi_host, &touch_bus_cfg, SPI_DMA_CH_AUTO);
}
// Touch panel IO always uses config->touch_spi_host (works either way)
```

When same host: bus is initialized once with display pins (SCLK/MOSI/MISO). Touch SCLK/MOSI/MISO config values are ignored — they must match the display pins (or be wired to the same lines). This is the "shared bus" scenario on MSP3520 where display and touch share MOSI/MISO/CLK.

**`msp3520_destroy()`:** Reverse order — delete LVGL port, delete touch, delete panel, free SPI buses, free struct.

**Calibration `process_coordinates` callback:**

```c
static void calibration_process_coords(esp_lcd_touch_handle_t tp,
                                       uint16_t *x, uint16_t *y,
                                       uint16_t *strength, uint8_t *count,
                                       uint8_t max_count) {
    msp3520_handle_t handle = /* retrieved via tp->config.user_data or static */;
    if (handle->cal.valid && *count > 0) {
        for (int i = 0; i < *count; i++) {
            touch_cal_apply(&handle->cal, x[i], y[i], &x[i], &y[i],
                           MSP3520_H_RES, MSP3520_V_RES);
        }
    }
}
```

## Step 5: Write msp3520.h

Public API as defined in spec. Key additions:

```c
#define MSP3520_H_RES  320
#define MSP3520_V_RES  480

#define MSP3520_CONFIG_DEFAULT() {                                     \
    .display_spi_host       = CONFIG_MSP3520_DISPLAY_SPI_HOST_ID,      \
    .display_sclk           = CONFIG_MSP3520_DISPLAY_SCLK_GPIO,        \
    .display_mosi           = CONFIG_MSP3520_DISPLAY_MOSI_GPIO,        \
    .display_miso           = CONFIG_MSP3520_DISPLAY_MISO_GPIO,        \
    .display_cs             = CONFIG_MSP3520_DISPLAY_CS_GPIO,          \
    .display_dc             = CONFIG_MSP3520_DISPLAY_DC_GPIO,          \
    .display_rst            = CONFIG_MSP3520_DISPLAY_RST_GPIO,         \
    .display_bkl            = CONFIG_MSP3520_DISPLAY_BKL_GPIO,         \
    .display_bkl_active_high = CONFIG_MSP3520_DISPLAY_BKL_ACTIVE_HIGH, \
    .display_spi_clock_mhz  = CONFIG_MSP3520_DISPLAY_SPI_CLOCK_MHZ,   \
    .touch_spi_host         = CONFIG_MSP3520_TOUCH_SPI_HOST_ID,        \
    .touch_sclk             = CONFIG_MSP3520_TOUCH_SCLK_GPIO,          \
    .touch_mosi             = CONFIG_MSP3520_TOUCH_MOSI_GPIO,          \
    .touch_miso             = CONFIG_MSP3520_TOUCH_MISO_GPIO,          \
    .touch_cs               = CONFIG_MSP3520_TOUCH_CS_GPIO,            \
    .touch_irq              = CONFIG_MSP3520_TOUCH_IRQ_GPIO,           \
    .touch_z_threshold      = CONFIG_MSP3520_TOUCH_Z_THRESHOLD,        \
    .lvgl_task_core         = CONFIG_MSP3520_LVGL_TASK_CORE_ID,        \
    .lvgl_task_priority     = CONFIG_MSP3520_LVGL_TASK_PRIORITY,       \
    .lvgl_task_stack_size   = CONFIG_MSP3520_LVGL_TASK_STACK_SIZE,     \
    .lvgl_draw_buf_lines    = CONFIG_MSP3520_LVGL_DRAW_BUF_LINES,     \
}
```

## Step 6: Write console_commands.c

Extract from current `console.c`:

**Moves into component:**
- `cmd_touch()` — z_threshold, calibration, swap/mirror flags
- `cmd_rotation()` — display rotation (swap_xy, mirror_x, mirror_y via panel handle)
- Calibration screen UI: `draw_crosshair()`, `cal_touch_cb()`, `cal_release_cb()`, `cal_return_timer_cb()`, related statics
- `msp3520_register_console_commands()` — registers touch + display commands

**Stays in example app (or is dropped):**
- `cmd_log_level()` — generic, app-level
- `cmd_info()` — generic, app-level
- `cmd_debug()` — app-level
- REPL lifecycle (`esp_console_new_repl_uart`, `esp_console_start_repl`) — app-level

**Adaptation needed:**
- Current commands use `app_context_t` for panel/touch/cal/flags. In the component, they use `msp3520_handle_t` instead.
- Calibration screen callbacks currently access statics and `cal_lvgl_lock`. In the component, they use `lvgl_port_lock()`/`unlock()` via the handle.
- The `display` command for backlight adds `backlight <0-100>` subcommand calling `msp3520_set_backlight()`.

## Step 7: Write Kconfig

Single `Kconfig` file in `components/msp3520/`. Uses `menu "MSP3520 Display Module"`.

```
menu "MSP3520 Display Module"

    orsource "$IDF_PATH/examples/common_components/env_caps/$IDF_TARGET/Kconfig.env_caps"

    menu "Display SPI"
        choice MSP3520_DISPLAY_SPI_HOST
            prompt "SPI Host"
            default MSP3520_DISPLAY_SPI2
            config MSP3520_DISPLAY_SPI2
                bool "SPI2"
            config MSP3520_DISPLAY_SPI3
                bool "SPI3"
                depends on SOC_SPI_PERIPH_NUM > 2
        endchoice
        config MSP3520_DISPLAY_SPI_HOST_ID
            int
            default 1 if MSP3520_DISPLAY_SPI2
            default 2 if MSP3520_DISPLAY_SPI3

        config MSP3520_DISPLAY_SCLK_GPIO
            int "SCLK GPIO"
            default 11
            range ENV_GPIO_RANGE_MIN ENV_GPIO_OUT_RANGE_MAX
        # ... MOSI, MISO, CS, DC, RST (same pattern)

        config MSP3520_DISPLAY_BKL_GPIO
            int "Backlight GPIO (-1 = none)"
            default 12
            range -1 ENV_GPIO_OUT_RANGE_MAX
        config MSP3520_DISPLAY_BKL_ACTIVE_HIGH
            bool "Backlight active high"
            default y

        config MSP3520_DISPLAY_SPI_CLOCK_MHZ
            int "SPI clock (MHz)"
            default 40
            range 1 80
    endmenu

    menu "Touch SPI"
        choice MSP3520_TOUCH_SPI_HOST
            prompt "SPI Host"
            default MSP3520_TOUCH_SPI3 if SOC_SPI_PERIPH_NUM > 2
            default MSP3520_TOUCH_SPI2
            config MSP3520_TOUCH_SPI2
                bool "SPI2"
            config MSP3520_TOUCH_SPI3
                bool "SPI3"
                depends on SOC_SPI_PERIPH_NUM > 2
        endchoice
        config MSP3520_TOUCH_SPI_HOST_ID
            int
            default 1 if MSP3520_TOUCH_SPI2
            default 2 if MSP3520_TOUCH_SPI3

        # ... SCLK, MOSI, MISO, CS, IRQ GPIOs

        config MSP3520_TOUCH_Z_THRESHOLD
            int "Touch pressure threshold"
            default 100
            range 10 1000
    endmenu

    menu "LVGL"
        choice MSP3520_LVGL_TASK_CORE
            prompt "LVGL task core affinity"
            default MSP3520_LVGL_CORE_NO_AFFINITY
            depends on !FREERTOS_UNICORE
            config MSP3520_LVGL_CORE_NO_AFFINITY
                bool "No affinity"
            config MSP3520_LVGL_CORE_0
                bool "Core 0"
            config MSP3520_LVGL_CORE_1
                bool "Core 1"
        endchoice
        config MSP3520_LVGL_TASK_CORE_ID
            int
            default -1 if MSP3520_LVGL_CORE_NO_AFFINITY || FREERTOS_UNICORE
            default 0 if MSP3520_LVGL_CORE_0
            default 1 if MSP3520_LVGL_CORE_1

        config MSP3520_LVGL_TASK_PRIORITY
            int "LVGL task priority"
            default 2
            range 1 25

        config MSP3520_LVGL_TASK_STACK_SIZE
            int "LVGL task stack size (bytes)"
            default 8192
            range 4096 32768

        config MSP3520_LVGL_DRAW_BUF_LINES
            int "Draw buffer height (lines)"
            default 48
            range 48 480
            help
                Height of each LVGL draw buffer in lines.
                Two buffers of this size are allocated from internal RAM.
                Minimum is 1/10th of vertical resolution (48 lines).
    endmenu

    menu "XPT2046 Advanced"
        config MSP3520_XPT2046_INTERRUPT_MODE
            bool "Use PENIRQ interrupt"
            default y
        config MSP3520_XPT2046_VREF_ON_MODE
            bool "Keep internal Vref on between conversions"
            default n
    endmenu

endmenu
```

Note: `XPT2046_CONVERT_ADC_TO_COORDS` and `XPT2046_ENABLE_LOCKING` are dropped — the component always uses calibration for coord conversion and doesn't need the locking option (LVGL port handles thread safety).

## Step 8: Create Example

**`examples/basic/CMakeLists.txt`:**
```cmake
cmake_minimum_required(VERSION 3.16)

# Point to the component in parent directory
set(EXTRA_COMPONENT_DIRS ../../components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(msp3520-basic-example)
```

**`examples/basic/main/CMakeLists.txt`:**
```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES msp3520 nvs_flash esp_console
)
```

**`examples/basic/main/main.c`:**
```c
#include "msp3520.h"
#include "nvs_flash.h"
#include "esp_console.h"

static void create_ui(void) {
    // Button with counter + coordinate label (from current create_ui)
}

void app_main(void) {
    // NVS init
    nvs_flash_init();

    // Create display
    msp3520_config_t cfg = MSP3520_CONFIG_DEFAULT();
    msp3520_handle_t display;
    ESP_ERROR_CHECK(msp3520_create(&cfg, &display));

    // Build UI
    msp3520_lvgl_lock(display, 0);
    create_ui();
    msp3520_lvgl_unlock(display);

    // Console (app-level)
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "msp3520>";
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_console_new_repl_uart(&uart_config, &repl_config, &repl);

    // Register component commands + app commands
    msp3520_register_console_commands(display);
    // register_app_commands();  // log_level, info, debug if wanted

    esp_console_start_repl(repl);
}
```

**`examples/basic/sdkconfig.defaults`:**
```
CONFIG_IDF_TARGET="esp32s3"
# Pin assignments matching current dev setup
CONFIG_MSP3520_DISPLAY_SCLK_GPIO=11
CONFIG_MSP3520_DISPLAY_MOSI_GPIO=10
CONFIG_MSP3520_DISPLAY_MISO_GPIO=13
CONFIG_MSP3520_DISPLAY_CS_GPIO=3
CONFIG_MSP3520_DISPLAY_DC_GPIO=9
CONFIG_MSP3520_DISPLAY_RST_GPIO=46
CONFIG_MSP3520_DISPLAY_BKL_GPIO=12
CONFIG_MSP3520_DISPLAY_SPI_CLOCK_MHZ=40
CONFIG_MSP3520_TOUCH_SCLK_GPIO=6
CONFIG_MSP3520_TOUCH_MOSI_GPIO=7
CONFIG_MSP3520_TOUCH_MISO_GPIO=8
CONFIG_MSP3520_TOUCH_CS_GPIO=4
CONFIG_MSP3520_TOUCH_IRQ_GPIO=5
# LVGL
CONFIG_LV_COLOR_DEPTH_24=y
CONFIG_LV_FONT_MONTSERRAT_28=y
```

## Step 9: Remove Old Structure

- Delete `components/esp_lcd_ili9488/` (absorbed)
- Delete `components/xpt2046/` (absorbed)
- Delete `main/` (replaced by example)
- Move `docs/` to project root (unchanged)
- Update top-level `CMakeLists.txt` if needed

## File Change Summary

| Action | File | Source |
|--------|------|--------|
| **Create** | `components/msp3520/CMakeLists.txt` | New |
| **Create** | `components/msp3520/Kconfig` | New, based on main/Kconfig.projbuild + xpt2046/Kconfig.projbuild |
| **Create** | `components/msp3520/idf_component.yml` | New |
| **Create** | `components/msp3520/include/msp3520.h` | New (public API) |
| **Create** | `components/msp3520/src/msp3520.c` | New (orchestrator, from app_main init logic) |
| **Move** | `components/msp3520/src/ili9488.c` | From `components/esp_lcd_ili9488/esp_lcd_ili9488.c` |
| **Create** | `components/msp3520/src/ili9488.h` | New (internal header) |
| **Move** | `components/msp3520/src/xpt2046.c` | From `components/xpt2046/esp_lcd_touch_xpt2046.c` |
| **Create** | `components/msp3520/src/xpt2046.h` | New (internal header) |
| **Move** | `components/msp3520/src/touch_calibration.c` | From `main/touch_calibration.c` |
| **Move** | `components/msp3520/src/touch_calibration.h` | From `main/touch_calibration.h` |
| **Create** | `components/msp3520/src/console_commands.c` | New, extracted from `main/console.c` |
| **Create** | `components/msp3520/src/backlight.c` | New (LEDC PWM) |
| **Create** | `components/msp3520/src/backlight.h` | New (internal header) |
| **Create** | `examples/basic/CMakeLists.txt` | New |
| **Create** | `examples/basic/main/CMakeLists.txt` | New |
| **Create** | `examples/basic/main/main.c` | New, simplified from `main/ili9488-test.c` |
| **Create** | `examples/basic/sdkconfig.defaults` | From current `sdkconfig.defaults` |
| **Delete** | `components/esp_lcd_ili9488/` | Absorbed into msp3520 |
| **Delete** | `components/xpt2046/` | Absorbed into msp3520 |
| **Delete** | `main/` | Replaced by examples/basic |

## Risk Areas

1. **`esp_lvgl_port` buffer allocation** — need to confirm it supports internal-RAM-only double buffers at our size. The API has `flags.buff_dma` and `flags.buff_spiram` — we'd set both false for internal RAM, or `buff_dma = true` for DMA-capable internal RAM.

2. **`process_coordinates` callback signature** — need to verify exact signature matches what `esp_lcd_touch` v1.0.4+ expects. If it doesn't support user_data, we may need a static handle pointer.

3. **XPT2046 Kconfig rename** — existing `CONFIG_XPT2046_*` references in xpt2046.c need updating to `CONFIG_MSP3520_XPT2046_*`.

4. **Managed component resolution** — `esp_lvgl_port` depends on `lvgl`. Our component also depends on `lvgl`. Need to verify the component manager resolves this cleanly without version conflicts.
