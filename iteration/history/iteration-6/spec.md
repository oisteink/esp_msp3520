# Spec: MSP3520 ESP-IDF Component

**Date:** 2026-03-10
**Builds on:** [research.md](research.md)

## What We're Building

A single ESP-IDF component (`msp3520`) that gives you a working LVGL display+touch from a config struct and one function call. It absorbs the ILI9488 driver, XPT2046 driver, touch calibration, and LVGL integration into one package. Manages LVGL lifecycle directly (task, tick timer, mutex locking, display/indev registration).

The current project restructures: component in `components/msp3520/`, current app becomes `examples/basic/`.

## Requirements

### R1: Single-call initialization
```c
msp3520_config_t cfg = MSP3520_CONFIG_DEFAULT();  // picks up Kconfig values
msp3520_handle_t handle;
msp3520_create(&cfg, &handle);
// LVGL display and touch input are now running
```

After `msp3520_create()` returns, the app can lock LVGL and build UI. Everything underneath (SPI buses, panel driver, touch driver, LVGL task, tick timer, calibration) is set up.

### R2: All pins and SPI hosts configurable via menuconfig

Kconfig exposes:

**Display:**
- SPI host (SPI2 / SPI3, chip-conditional)
- GPIO pins: SCLK, MOSI, MISO, CS, DC, RST, backlight
- SPI clock speed (MHz)

**Touch:**
- SPI host (SPI2 / SPI3, chip-conditional)
- GPIO pins: SCLK, MOSI, MISO, CS, IRQ
- Z-threshold default

**LVGL:**
- Task core affinity (no affinity / core 0 / core 1, hidden on single-core chips)
- Task priority
- Task stack size
- Draw buffer height (lines)

All values flow into `MSP3520_CONFIG_DEFAULT()`. App can override any field in the struct before calling `create`.

### R3: SPI bus sharing handled transparently

If display and touch are configured on the same SPI host, the component initializes the bus once and adds both devices. If on different hosts, it initializes each bus separately. The app doesn't need to know or care.

### R4: Touch calibration built-in

- 3-point affine calibration (existing algorithm)
- Persisted to NVS automatically
- Loaded on init if available
- Applied via `esp_lcd_touch` `process_coordinates` callback (compatible with `esp_lvgl_port`)
- Calibration screen triggered via REPL command or programmatic API

### R5: Optional REPL commands

```c
msp3520_register_console_commands(handle);  // app calls if it wants them
```

Registers:
- `touch` — status, `z <val>`, `cal start/show/clear`, `swap_xy/mirror_x/mirror_y`
- `display` — rotation get/set, backlight on/off

App owns the REPL lifecycle (UART init, `esp_console_start_repl()`). Component just registers commands.

### R6: Thread-safe LVGL access

Component provides recursive mutex locking for LVGL access:
```c
msp3520_lvgl_lock(handle, 0);  // 0 = wait forever
// LVGL API calls
msp3520_lvgl_unlock(handle);
```

### R7: Backlight control

Component owns the backlight GPIO via LEDC PWM. Configurable pin and active level via Kconfig. Full brightness on init. Exposed for runtime control:
```c
msp3520_set_backlight(handle, uint8_t brightness);  // 0-100
```

### R8: Display is 320x480, RGB888, portrait

Hardcoded to MSP3520 specs:
- 320x480 resolution
- RGB888 color format (ILI9488 SPI uses RGB666, driver converts)
- Portrait orientation by default

Rotation available at runtime via display commands or API, using MADCTL + LVGL coordinate swap.

### R9: Double-buffered drawing from internal RAM

Always two draw buffers allocated from internal RAM (not PSRAM — not all boards have it). Buffer height defaults to 1/10th of vertical resolution (48 lines for 480). 1/10th is also the minimum — Kconfig enforces this. App can increase but not decrease below the minimum.

## Public API

```c
// --- Config ---
typedef struct {
    // Display SPI
    int display_spi_host;
    int display_sclk, display_mosi, display_miso, display_cs, display_dc;
    int display_rst;            // -1 = not connected
    int display_bkl;            // -1 = not connected
    bool display_bkl_active_high;
    int display_spi_clock_mhz;

    // Touch SPI
    int touch_spi_host;
    int touch_sclk, touch_mosi, touch_miso, touch_cs;
    int touch_irq;              // -1 = not connected
    int touch_z_threshold;

    // LVGL
    int lvgl_task_core;
    int lvgl_task_priority;
    int lvgl_task_stack_size;
    int lvgl_draw_buf_lines;    // minimum and default: v_res / 10
} msp3520_config_t;

#define MSP3520_CONFIG_DEFAULT() { /* all from Kconfig */ }

// --- Lifecycle ---
typedef struct msp3520_t *msp3520_handle_t;

esp_err_t msp3520_create(const msp3520_config_t *config, msp3520_handle_t *out_handle);
esp_err_t msp3520_destroy(msp3520_handle_t handle);

// --- LVGL access ---
lv_display_t *msp3520_get_display(msp3520_handle_t handle);
lv_indev_t *msp3520_get_indev(msp3520_handle_t handle);
bool msp3520_lvgl_lock(msp3520_handle_t handle, uint32_t timeout_ms);
void msp3520_lvgl_unlock(msp3520_handle_t handle);

// --- Hardware control ---
esp_err_t msp3520_set_backlight(msp3520_handle_t handle, uint8_t brightness);  // 0-100

// --- Touch calibration ---
esp_err_t msp3520_start_calibration(msp3520_handle_t handle);
esp_err_t msp3520_clear_calibration(msp3520_handle_t handle);
bool msp3520_is_calibrated(msp3520_handle_t handle);

// --- Console commands (optional) ---
esp_err_t msp3520_register_console_commands(msp3520_handle_t handle);
```

## Component Structure

```
components/msp3520/
  include/
    msp3520.h                 — public API
  src/
    msp3520.c                 — create/destroy, SPI bus init, wiring
    ili9488.c                 — ILI9488 panel driver (from existing component)
    ili9488.h                 — internal header
    xpt2046.c                — XPT2046 touch driver (from existing component)
    xpt2046.h                — internal header
    touch_calibration.c       — affine calibration + NVS (from existing main/)
    touch_calibration.h       — internal header
    console_commands.c        — REPL commands (from existing console.c, touch/display parts)
  Kconfig                     — all menuconfig options
  CMakeLists.txt
  idf_component.yml           — declares deps on esp_lcd_touch, lvgl

examples/basic/
  main/
    main.c                    — simple app: create msp3520, register commands, build UI
    Kconfig.projbuild         — app-level config (if any)
  CMakeLists.txt
  sdkconfig.defaults          — pin assignments for the dev setup
```

## Dependencies

```
msp3520/
  REQUIRES: esp_lcd, lvgl                           — in public headers
  PRIV_REQUIRES: esp_driver_spi, esp_driver_gpio, esp_driver_ledc,
                 esp_timer, nvs_flash, console
  MANAGED: espressif/esp_lcd_touch, lvgl/lvgl
```

## Acceptance Criteria

1. `examples/basic/` builds and runs, producing the same display+touch behavior as current project
2. All pin assignments configurable via `idf.py menuconfig`
3. Touch calibration persists across reboots
4. REPL commands work when registered
5. Shared SPI bus works (both devices on SPI2)
6. Separate SPI buses work (display SPI2, touch SPI3)
7. Core pinning option works on ESP32-S3
8. Component builds for at least ESP32-S3 and ESP32-C3 (single SPI host, single core)
