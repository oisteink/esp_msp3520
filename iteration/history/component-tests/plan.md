# Plan: MSP3520 Component Integration Tests + Timeout Unit Change

Builds on [spec.md](spec.md) and [research.md](research.md).

## Overview

Two parts: (1) refactor timeout internals from minutes to seconds, (2) create a test app with automated and interactive Unity tests.

## Part 1: Timeout Unit Change

### `msp3520_priv.h`

Rename fields:
- `uint8_t dim_timeout_min` → `uint16_t dim_timeout_s`
- `uint8_t off_timeout_min` → `uint16_t off_timeout_s`

Use `uint16_t` since 60 minutes = 3600 seconds exceeds `uint8_t` range.

### `screen_protect.h`

Update API signatures:
```c
void screen_protect_set_dim_timeout(msp3520_handle_t h, uint16_t seconds);
void screen_protect_set_off_timeout(msp3520_handle_t h, uint16_t seconds);
void screen_protect_get_status(msp3520_handle_t h, const char **state,
                                uint16_t *dim_s, uint16_t *off_s,
                                uint32_t *idle_ms);
```

### `screen_protect.c`

- `screen_protect_init()`: `h->dim_timeout_s = CONFIG_MSP3520_SCREEN_DIM_TIMEOUT * 60`
- `idle_check_cb()`: `dim_ms = (uint32_t)h->dim_timeout_s * 1000` (was `* 60000`)
- `screen_protect_set_dim_timeout()` / `set_off_timeout()`: accept seconds
- `screen_protect_get_status()`: return `dim_s`, `off_s`
- Log messages: show seconds

### `console_commands.c`

- `display dim <seconds>` / `display off <seconds>`: accept seconds
- Range check: 0–3600 (up to 1 hour)
- `display status`: show timeouts in seconds, idle in seconds

### `msp3520.h` help text / Kconfig

No change — Kconfig stays in minutes.

## Part 2: Test App

### Directory Structure

```
components/msp3520/test_apps/
    main/
        CMakeLists.txt
        test_app_main.c
        test_screen_protect.c
        test_indev_sim.h
        test_indev_sim.c
    CMakeLists.txt
    sdkconfig.defaults
```

### `CMakeLists.txt` (project level)

```cmake
cmake_minimum_required(VERSION 3.16)
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(msp3520_test)
```

### `main/CMakeLists.txt`

```cmake
idf_component_register(
    SRCS "test_app_main.c" "test_screen_protect.c" "test_indev_sim.c"
    PRIV_REQUIRES msp3520 unity
    WHOLE_ARCHIVE)
```

### `sdkconfig.defaults`

```
# Disable task watchdog so unity_run_menu() doesn't trigger it
# CONFIG_ESP_TASK_WDT_INIT is not set
CONFIG_FREERTOS_HZ=1000

# Copy relevant pin/SPI config from the basic example
# (or inherit from the component Kconfig defaults)
```

### `test_indev_sim.h` / `test_indev_sim.c` — Minimal test touch indev

DIY test indev (~30 lines). Avoids `LV_USE_TEST` dependency.

```c
// test_indev_sim.h
#pragma once
#include "lvgl.h"

lv_indev_t *test_indev_sim_create(void);
void test_indev_sim_press(int16_t x, int16_t y);
void test_indev_sim_release(void);
```

Implementation: static x/y/pressed state, read callback returns them. `lv_indev_create()` + `lv_indev_set_type(LV_INDEV_TYPE_POINTER)` + `lv_indev_set_read_cb()`.

### `test_app_main.c`

```c
#include "unity.h"
#include "unity_test_utils.h"
#include "msp3520.h"

static msp3520_handle_t h;

void setUp(void) { unity_utils_record_free_mem(); }
void tearDown(void) { unity_utils_evaluate_leaks_direct(450); }

void app_main(void)
{
    msp3520_config_t cfg = MSP3520_CONFIG_DEFAULT();
    msp3520_create(&cfg, &h);

    unity_run_menu();
}
```

The handle `h` needs to be accessible from test files. Use an extern or a getter.

### `test_screen_protect.c` — Test Cases

#### Automated (tag: `[screen_protect]`)

**1. "dim after timeout"**
- Set dim=3s, off=0. Wait 4s. Assert state == "dimmed".

**2. "off after dim+off timeout"**
- Set dim=2s, off=2s. Wait 5s. Assert state == "off".

**3. "skip dim when dim=0"**
- Set dim=0, off=3s. Wait 4s. Assert state == "off".

**4. "no action when both=0"**
- Set dim=0, off=0. Wait 3s. Assert state == "active".

**5. "wake from dimmed"**
- Set dim=2s, off=0. Wait 3s (dimmed).
- Press test indev. Wait 500ms (LVGL processes + wake window).
- Assert state == "active".

**6. "wake from off"**
- Set dim=0, off=2s. Wait 3s (off).
- Press test indev. Wait 500ms.
- Assert state == "active".

**7. "wake touch consumed"**
- Create test button with click counter.
- Set dim=2s, off=0. Wait 3s (dimmed).
- Press+release test indev on button coords. Wait 500ms.
- Assert click_count == 0 (consumed).
- Assert state == "active" (woke up).

**8. "touch passes after wake"**
- After test 7 setup, once in active state:
- Press+release test indev on button. Wait 100ms.
- Assert click_count == 1 (passed through).

**9. "manual backlight updates state"**
- Set dim=2s. Wait 3s (dimmed).
- Call `msp3520_set_backlight(h, 50)`.
- Assert state == "active", saved_brightness == 50.

#### Interactive (tag: `[interactive]`)

**10. "physical touch wake"**
- Set dim=5s, off=0.
- Print: "Wait for screen to dim, then touch. Did the screen restore? (y/n)"
- Read UART input. Assert 'y'.

**11. "fade-in visible on boot"**
- Print: "Reboot device now. Did you see a smooth fade-in from dark? (y/n)"
- Read UART input. Assert 'y'.

### Test Helper Pattern

Each automated test:
1. Lock LVGL mutex.
2. Set timeouts via `screen_protect_set_dim_timeout()` / `set_off_timeout()`.
3. Trigger activity to reset idle timer: `lv_display_trigger_activity(display)`.
4. Unlock mutex.
5. `vTaskDelay()` for the required idle period + margin.
6. Lock mutex, query status, assert, unlock.

For wake tests, inject touch via `test_indev_sim_press()` between steps 5 and 6.

### Build Sequence

1. Timeout refactor (Part 1) — all in existing component files
2. `test_indev_sim.h/c` — standalone, no dependencies beyond LVGL
3. `test_app_main.c` — init + unity runner
4. `test_screen_protect.c` — test cases
5. `CMakeLists.txt` + `sdkconfig.defaults` — build config
6. Build from `test_apps/` directory, flash, run

### Verification

- `idf.py build` from `test_apps/` succeeds
- Flash to device, open monitor
- `*` runs all automated tests — expect all pass
- `[interactive]` runs interactive tests — user confirms visually
