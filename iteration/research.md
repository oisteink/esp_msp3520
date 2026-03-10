# Research: MSP3520 Reusable ESP-IDF Component

**Date:** 2026-03-10
**Goal:** Design a single ESP-IDF component (`msp3520`) that wraps ILI9488 display + XPT2046 touch + LVGL integration + touch calibration + optional REPL commands.

## 1. What Exists Today (This Project)

The current codebase has all the pieces working but spread across `main/` and two local components:

- **Display:** `components/esp_lcd_ili9488/` — ILI9488 panel driver implementing `esp_lcd` interface
- **Touch:** `components/xpt2046/` — XPT2046 driver (forked atanisoft v1.0.6) with median filtering, IRQ, runtime z_threshold, NVS persistence
- **Calibration:** `main/touch_calibration.{h,c}` — 3-point affine transform, NVS save/load
- **LVGL glue:** `main/ili9488-test.c` — ~120 lines of SPI bus init, panel IO, LVGL display/indev registration, flush callback, tick timer, LVGL task
- **REPL:** `main/console.{h,c}` — touch commands (z_threshold, calibration, swap/mirror), rotation, debug, info

The wiring: SPI2 for display (40 MHz), SPI3 for touch (1 MHz), both hardware SPI, separate buses.

## 2. What Espressif Provides (Gaps and Opportunities)

### Existing Components

| Component | What it does | Gap for us |
|-----------|-------------|------------|
| `esp_lcd` (IDF) | Panel IO + panel driver interface | Base layer, we build on this |
| `esp_lcd_touch` (registry) | Touch driver interface | Base layer, we build on this |
| `esp_lvgl_port` (registry, v2.7.2) | LVGL task, timer, lock, display/indev registration | Could use this instead of hand-rolling LVGL glue |
| `esp_bsp_generic` (registry) | Full BSP with Kconfig | No ILI9488, no SPI touch — can't use directly |
| `atanisoft/esp_lcd_ili9488` | ILI9488 panel driver | We already have a local fork/equivalent |
| `atanisoft/esp_lcd_touch_xpt2046` | XPT2046 touch driver | We have enhanced fork with better filtering |

### Key Insight: esp_lvgl_port

`esp_lvgl_port` handles exactly the LVGL plumbing we currently do manually: task creation, tick timer, thread-safe locking, display registration, input device registration. It takes already-created panel and touch handles.

**Decision point:** Do we use `esp_lvgl_port` inside our component, or roll our own LVGL glue?

- **Pro using it:** Less code to maintain, follows Espressif conventions, handles edge cases (buffer allocation strategies, rotation modes)
- **Con:** Another dependency, may not give us enough control over calibration hook in the touch read path
- **Key concern:** Our touch read callback applies affine calibration *before* passing coords to LVGL. `esp_lvgl_port` registers its own touch read callback that just calls `esp_lcd_touch_get_coordinates()`. We'd need to either: (a) hook calibration into the `esp_lcd_touch` pipeline via the `process_coordinates` callback, or (b) not use `esp_lvgl_port` for touch

The `esp_lcd_touch_config_t` has a `process_coordinates` callback that runs inside `esp_lcd_touch_get_coordinates()`. We could plug calibration there. This would let us use `esp_lvgl_port` cleanly.

## 3. SPI Host Configuration

### Hardware Facts
- ESP32-S3: SPI2_HOST and SPI3_HOST available, equivalent performance
- ESP32-C3/C6: SPI2_HOST only
- SPI1_HOST always reserved for flash
- ESP-IDF defines `SOC_SPI_PERIPH_NUM` per chip (3 = two user hosts, 2 = one user host)

### Bus Sharing
When display and touch are on the same SPI host:
- ESP-IDF handles device arbitration (per-device mutex)
- Clock speed switches automatically per-device (40 MHz display, 1 MHz touch)
- Both devices need different CS pins
- Only one call at a time — display DMA transfers block touch reads and vice versa
- The component calls `spi_bus_initialize()` once, then `esp_lcd_new_panel_io_spi()` twice

When on different hosts:
- Fully independent, no contention
- Component initializes each bus separately

### Kconfig Design
Use `choice` blocks with `depends on SOC_SPI_PERIPH_NUM > 2` for SPI3 option. Companion `int` config resolves to host ID (1 or 2). See memory file `esp-idf-component-patterns.md` for exact Kconfig syntax.

## 4. Core Pinning

LVGL task can be pinned to a specific core or left floating. On single-core chips (C3, S2), the option shouldn't appear.

Use `depends on !FREERTOS_UNICORE` on the choice block. Defaults to no affinity. On single-core chips, silently defaults to -1 (tskNO_AFFINITY). See memory file for Kconfig pattern.

## 5. Calibration Integration

Current approach: affine transform in LVGL touch_read_cb. Better approach for the component:

- Use `esp_lcd_touch_config_t.process_coordinates` callback
- This callback is called inside `esp_lcd_touch_get_coordinates()` and can modify coordinates in-place
- Calibration data stored in component-internal state, persisted to NVS
- This cleanly separates calibration from LVGL plumbing and works with or without `esp_lvgl_port`

## 6. REPL Commands Integration

Current commands: `touch` (z_threshold, calibration, swap/mirror), `rotation`, `debug`, `info`.

For the component, only touch-related and display-related commands belong inside it. `debug` and `info` are app-level.

**Proposed API:**
```c
// App calls this if it wants REPL commands
esp_err_t msp3520_register_console_commands(msp3520_handle_t handle);
```

This registers:
- `touch` — z_threshold get/set, calibration start/show/clear, swap/mirror
- `display` — rotation, backlight on/off (maybe)

The component depends on `esp_console` only if commands are registered. The REPL itself (UART init, task) stays in the app.

## 7. Proposed Component API Shape

```c
// Config with defaults from Kconfig
typedef struct {
    // Display
    int display_spi_host;       // from CONFIG_MSP3520_DISPLAY_SPI_HOST_ID
    int display_sclk, display_mosi, display_miso, display_cs, display_dc;
    int display_rst;            // -1 if not connected
    int display_bkl;            // -1 if not connected
    int display_spi_clock_mhz;

    // Touch
    int touch_spi_host;         // from CONFIG_MSP3520_TOUCH_SPI_HOST_ID
    int touch_sclk, touch_mosi, touch_miso, touch_cs;
    int touch_irq;              // -1 if not connected

    // LVGL
    int lvgl_task_core;         // from CONFIG_MSP3520_LVGL_TASK_CORE_ID
    int lvgl_task_priority;
    int lvgl_task_stack_size;
    int lvgl_draw_buf_lines;    // height of draw buffer in lines
} msp3520_config_t;

#define MSP3520_CONFIG_DEFAULT() { \
    .display_spi_host = CONFIG_MSP3520_DISPLAY_SPI_HOST_ID, \
    /* ... all Kconfig defaults ... */ \
}

typedef struct msp3520_t *msp3520_handle_t;

// Create and initialize everything
esp_err_t msp3520_create(const msp3520_config_t *config, msp3520_handle_t *out_handle);

// Get LVGL objects for UI code
lv_display_t *msp3520_get_display(msp3520_handle_t handle);
lv_indev_t *msp3520_get_indev(msp3520_handle_t handle);

// Thread-safe LVGL access
bool msp3520_lvgl_lock(msp3520_handle_t handle, uint32_t timeout_ms);
void msp3520_lvgl_unlock(msp3520_handle_t handle);

// Optional REPL commands
esp_err_t msp3520_register_console_commands(msp3520_handle_t handle);

// Cleanup
esp_err_t msp3520_destroy(msp3520_handle_t handle);
```

**App usage:**
```c
void app_main(void) {
    msp3520_config_t cfg = MSP3520_CONFIG_DEFAULT();
    msp3520_handle_t display;
    msp3520_create(&cfg, &display);

    msp3520_register_console_commands(display);  // optional

    msp3520_lvgl_lock(display, 0);
    lv_obj_t *btn = lv_btn_create(lv_scr_act());
    // ... build UI ...
    msp3520_lvgl_unlock(display);
}
```

## 8. Component Dependencies

```
msp3520/
  REQUIRES: esp_lcd, lvgl
  PRIV_REQUIRES: esp_driver_spi, esp_driver_gpio, esp_timer, nvs_flash
  MANAGED: espressif/esp_lcd_touch (or bundled)
  INTERNAL: esp_lcd_ili9488 driver, xpt2046 driver, calibration module
```

The ILI9488 and XPT2046 drivers would be compiled as part of the component (not separate components), since they're tightly coupled to this module.

## 9. Decisions Made

These were resolved through discussion:

1. **Use `esp_lvgl_port`** — Yes. It handles LVGL task, timer, locking, display/indev registration. Less code to maintain, Espressif-maintained. Calibration hooks in via `esp_lcd_touch_config_t.process_coordinates` callback, so no conflict. This is *not* the BSP — it's a pure plumbing layer with no hardware opinion.

2. **Hardware SPI only** — No software/bit-bang SPI. ESP-IDF doesn't provide software SPI for ESP32-S3 (only RISC-V chips). Each device (display, touch) independently picks a hardware SPI host via menuconfig. Same host = shared bus (component handles it). Different hosts = independent.

3. **SPI host selection is chip-conditional** — Kconfig uses `SOC_SPI_PERIPH_NUM` to only show available hosts. ESP32-C3 gets SPI2 only; ESP32-S3 gets SPI2 + SPI3.

4. **Core pinning is chip-conditional** — Kconfig uses `FREERTOS_UNICORE` to hide the option on single-core chips. Choices: no affinity / core 0 / core 1.

5. **REPL commands via explicit registration** — App calls `msp3520_register_console_commands()` if it wants them. Component registers touch and display commands. App owns REPL lifecycle.

6. **Repo structure** — Same repo. Component lives in `components/msp3520/`. Current `main/` evolves into `examples/basic/`. Existing `esp_lcd_ili9488` and `xpt2046` components get absorbed into `msp3520` as internal sources.

7. **BSP is a future layer** — A thin `esp32s3_msp3520` BSP could wrap the component with board-specific pin config. But the component is the real work; BSP is trivial on top.

## 10. Open Questions (For Spec)

1. **Backlight control:** Include in component (with configurable GPIO and active level) or leave to app?

2. **Display resolution:** Hardcode 320x480 (it's the MSP3520) or make configurable?

3. **Color format:** Lock to RGB888 or expose as option? ILI9488 SPI requires RGB666 (18-bit); driver converts.

4. **Portrait vs landscape:** Expose rotation as Kconfig option, runtime function, or both?
