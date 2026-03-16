# Plan: Screen Burn-in Protection

Builds on [spec.md](spec.md) and [research.md](research.md).

## Overview

New file `screen_protect.c` implements a state machine that monitors LVGL inactivity and controls backlight fade transitions. Backlight module gets fade support via LEDC hardware fade service. Four new Kconfig entries. Three new console subcommands.

## File Changes

### 1. `Kconfig` — Add screen protection menu

Add new menu "Screen Protection" after "XPT2046 Advanced":

```
menu "Screen Protection"
    config MSP3520_SCREEN_DIM_TIMEOUT
        int "Dim timeout (minutes, 0=disable)"
        range 0 60
        default 10

    config MSP3520_SCREEN_OFF_TIMEOUT
        int "Off timeout after dim (minutes, 0=disable)"
        range 0 60
        default 50

    config MSP3520_SCREEN_FADE_OUT_MS
        int "Fade out time (ms)"
        range 0 5000
        default 1000

    config MSP3520_SCREEN_FADE_IN_MS
        int "Fade in time (ms)"
        range 0 5000
        default 1000
endmenu
```

### 2. `backlight.h` / `backlight.c` — Add fade support

**New API:**

```c
esp_err_t backlight_fade(uint8_t brightness, uint32_t fade_ms);
esp_err_t backlight_fade_stop(void);
```

**Changes to `backlight_init()`:**

- Set initial duty to 0 (dark on startup) regardless of active_high polarity.
- Call `ledc_fade_func_install(0)` after channel config.

**Changes to `backlight_set()`:**

- Replace `ledc_set_duty()` + `ledc_update_duty()` with `ledc_set_duty_and_update()` (fade-safe, no semaphore conflict).

**New `backlight_fade()`:**

- Convert brightness 0–100 to duty 0–255, apply active_high/low inversion.
- Call `ledc_set_fade_time_and_start(LEDC_LOW_SPEED_MODE, BKL_LEDC_CHANNEL, duty, fade_ms, LEDC_FADE_NO_WAIT)`.

**New `backlight_fade_stop()`:**

- Call `ledc_fade_stop(LEDC_LOW_SPEED_MODE, BKL_LEDC_CHANNEL)`. ESP32-S3 supports this (`SOC_LEDC_SUPPORT_FADE_STOP`).

### 3. New file: `screen_protect.h`

```c
#pragma once
#include "msp3520_priv.h"

esp_err_t screen_protect_init(msp3520_handle_t h);
void screen_protect_deinit(msp3520_handle_t h);
void screen_protect_set_dim_timeout(msp3520_handle_t h, uint8_t minutes);
void screen_protect_set_off_timeout(msp3520_handle_t h, uint8_t minutes);
void screen_protect_get_status(msp3520_handle_t h, const char **state,
                                uint8_t *dim_min, uint8_t *off_min,
                                uint32_t *idle_ms);
```

### 4. New file: `screen_protect.c`

**State machine:**

```
enum screen_state { ACTIVE, DIMMED, OFF, WAKING };
```

WAKING is a transient state lasting ~250ms after wake/startup where touch events are consumed.

**Struct additions to `msp3520_t`** (in `msp3520_priv.h`):

```c
/* Screen protection */
uint8_t screen_state;           /* enum screen_state */
uint8_t saved_brightness;       /* brightness before dim, default 100 */
uint8_t dim_timeout_min;        /* from Kconfig, changeable at runtime */
uint8_t off_timeout_min;        /* from Kconfig, changeable at runtime */
int64_t wake_timestamp_us;      /* esp_timer_get_time() when entering WAKING */
lv_timer_t *screen_protect_timer;  /* LVGL timer, ~1s period */
```

**`screen_protect_init(msp3520_handle_t h)`:**

1. Set `h->saved_brightness = 100`.
2. Set `h->dim_timeout_min = CONFIG_MSP3520_SCREEN_DIM_TIMEOUT`.
3. Set `h->off_timeout_min = CONFIG_MSP3520_SCREEN_OFF_TIMEOUT`.
4. Register indev event callback: `lv_indev_add_event_cb(h->indev, wake_touch_cb, LV_EVENT_PRESSED, h)`.
5. Create LVGL timer: `h->screen_protect_timer = lv_timer_create(idle_check_cb, 1000, h)`.
6. Enter WAKING state (set `h->wake_timestamp_us = esp_timer_get_time()`).
7. Start fade-in: `backlight_fade(h->saved_brightness, CONFIG_MSP3520_SCREEN_FADE_IN_MS)`.

**`idle_check_cb(lv_timer_t *timer)` — runs every ~1s in LVGL task:**

```
idle_ms = lv_display_get_inactive_time(h->display)

if state == WAKING:
    if now - wake_timestamp_us >= 250000:   // 250ms elapsed
        state = ACTIVE

if state == ACTIVE:
    dim_ms = dim_timeout_min * 60000
    off_ms = off_timeout_min * 60000

    if dim_ms > 0 and idle_ms >= dim_ms:
        backlight_fade(10, CONFIG_MSP3520_SCREEN_FADE_OUT_MS)
        state = DIMMED

    elif dim_ms == 0 and off_ms > 0 and idle_ms >= off_ms:
        backlight_fade(0, CONFIG_MSP3520_SCREEN_FADE_OUT_MS)
        // DISPOFF after fade — use esp_timer one-shot delayed by fade_out_ms
        state = OFF

if state == DIMMED:
    off_ms = off_timeout_min * 60000
    total_ms = (dim_timeout_min * 60000) + off_ms
    if off_ms > 0 and idle_ms >= total_ms:
        backlight_fade(0, CONFIG_MSP3520_SCREEN_FADE_OUT_MS)
        // DISPOFF after fade
        state = OFF
```

**DISPOFF timing:** Schedule a one-shot `esp_timer` callback delayed by `CONFIG_MSP3520_SCREEN_FADE_OUT_MS` to call `esp_lcd_panel_disp_on_off(h->panel, false)`. This avoids blocking the LVGL task. Store the timer handle in the struct for cleanup/cancellation.

**`wake_touch_cb(lv_event_t *e)` — indev callback, fires before widgets:**

```
if state == WAKING:
    lv_indev_stop_processing(indev)   // consume touch during wake window
    return

if state == ACTIVE:
    return   // normal operation

// state is DIMMED or OFF — wake the screen
backlight_fade_stop()   // interrupt any ongoing fade-down
if state == OFF:
    cancel DISPOFF timer if pending
    esp_lcd_panel_disp_on_off(h->panel, true)
backlight_fade(saved_brightness, CONFIG_MSP3520_SCREEN_FADE_IN_MS)
wake_timestamp_us = esp_timer_get_time()
state = WAKING
lv_indev_stop_processing(indev)
lv_display_trigger_activity(h->display)
```

**`screen_protect_deinit()`:**

- Delete LVGL timer.
- Cancel and delete DISPOFF esp_timer if active.

### 5. `msp3520_priv.h` — Add struct fields

Add the fields listed above to `struct msp3520_t`. Add `#include "esp_lcd_panel_ops.h"` if not already present (needed for `esp_lcd_panel_disp_on_off` in screen_protect.c — actually already included).

### 6. `msp3520.c` — Integration

**In `backlight_init()` call (line 283):**

No change — backlight already inits first. It now starts at duty=0 (dark).

**After `init_lvgl(h)` succeeds (line 304), add:**

```c
ESP_GOTO_ON_ERROR(screen_protect_init(h), err, TAG, "screen protect init failed");
```

This starts the fade-in from dark and begins monitoring inactivity.

**In `msp3520_destroy()`:**

Add `screen_protect_deinit(h)` before the LVGL task deletion.

**`msp3520_set_backlight()`:**

Update to also store `h->saved_brightness = brightness` so the screen protection knows what to restore to. Reset state to ACTIVE and trigger activity (so manual backlight set via console acts as user activity).

### 7. `console_commands.c` — New subcommands

Add to `cmd_display()`:

- **`display dim <minutes>`**: Calls `screen_protect_set_dim_timeout(h, val)`. Prints confirmation.
- **`display off <minutes>`**: Calls `screen_protect_set_off_timeout(h, val)`. Prints confirmation.
- **`display status`**: Calls `screen_protect_get_status()`, prints state/timeouts/idle time.

Update the help text in both the `argc == 1` block and the command registration hint.

### 8. `CMakeLists.txt` — Add source

Add `"src/screen_protect.c"` to the SRCS list.

## Build Sequence

1. `Kconfig` — new settings available
2. `backlight.h` + `backlight.c` — fade API (no callers yet, existing behavior preserved)
3. `msp3520_priv.h` — new struct fields
4. `screen_protect.h` + `screen_protect.c` — state machine
5. `msp3520.c` — wire up init/deinit, update `msp3520_set_backlight`
6. `console_commands.c` — new subcommands
7. `CMakeLists.txt` — add source file

## Verification

- Build with `idf.py build`.
- Both timeouts at 0: no dimming or shutdown, backlight stays on. Only startup fade-in visible.
- Dim only (dim=1, off=0): screen dims after 1 min, stays dimmed. Touch restores.
- Off only (dim=0, off=1): screen turns off after 1 min. Touch restores.
- Both (dim=1, off=1): dims at 1 min, off at 2 min. Touch restores from either state.
- Console: `display dim 0` / `display off 0` disables at runtime. `display status` shows correct state.
- Wake touch consumed — first tap on dimmed/off screen doesn't trigger UI elements.
- `display backlight 50` from console updates saved brightness, resets to ACTIVE.
