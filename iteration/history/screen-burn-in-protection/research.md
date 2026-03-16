# Research: Screen Burn-in Protection

Builds on [spec.md](spec.md).

## LEDC Hardware Fade

### API Pattern

```c
// Install once after ledc_channel_config()
ledc_fade_func_install(0);

// Thread-safe atomic set+start (preferred):
ledc_set_fade_time_and_start(LEDC_LOW_SPEED_MODE, ch, target_duty, fade_ms, LEDC_FADE_NO_WAIT);
```

### Key Findings

- **Thread-safe API**: `ledc_set_fade_time_and_start()` combines configure+start atomically under both mutex and semaphore. Use this instead of the bare `ledc_set_fade_with_time` + `ledc_fade_start` pair.
- **`LEDC_FADE_NO_WAIT`**: Returns immediately, fade runs in hardware. The semaphore stays held until ISR fires at completion. A subsequent fade call blocks until the current one finishes — no cancellation needed.
- **`LEDC_FADE_WAIT_DONE`**: Blocks the caller until fade completes. Simpler but ties up the calling task.
- **Cannot mix with bare `ledc_set_duty`/`ledc_update_duty`**: These don't acquire the fade semaphore and will corrupt state mid-fade. Use `ledc_set_duty_and_update()` (thread-safe) instead, or just use `ledc_set_fade_time_and_start` with `fade_ms=0` for instant changes.
- **Fade-complete callback**: `ledc_cb_register()` provides an ISR-context callback on fade end. Could be used to send DISPOFF after fade-to-zero completes.
- **`ledc_fade_stop()`**: Available on ESP32-S3 (`SOC_LEDC_SUPPORT_FADE_STOP`). Useful if we need to interrupt a dim-down on wake touch.
- **Timing accuracy**: Can vary up to 2x due to integer rounding. Acceptable for UI transitions.
- **Cleanup**: `ledc_fade_func_uninstall()` for teardown.

### Impact on backlight.c

Current `backlight_set()` uses `ledc_set_duty()` + `ledc_update_duty()`. Must be reworked:
- Init: add `ledc_fade_func_install(0)` after channel config.
- New function `backlight_fade(brightness, time_ms)` using `ledc_set_fade_time_and_start()`.
- Keep `backlight_set()` for instant changes but switch to `ledc_set_duty_and_update()` (fade-safe).
- Active-high/low inversion logic stays the same.

## LVGL v9 Inactivity Detection

### Available APIs

- **`lv_display_get_inactive_time(disp)`**: Returns ms since last touch press. Pass NULL for shortest across all displays. This is the primary idle detection mechanism.
- **No per-indev equivalent** in v9 — activity tracking lives on the display object.
- Activity resets on **press only** (not release). Timer starts counting from last press event.
- **`lv_display_trigger_activity(disp)`**: Manually reset the inactivity timer.

### Touch Event Suppression (Wake Touch)

**`lv_indev_stop_processing(indev)`** — call from an indev event callback to prevent the event from reaching widgets. The flag auto-clears after being checked.

Pattern:
```c
// Register on the touch indev
lv_indev_add_event_cb(indev, wake_cb, LV_EVENT_PRESSED, ctx);

void wake_cb(lv_event_t *e) {
    if (screen_is_dimmed_or_off) {
        lv_indev_stop_processing(lv_event_get_indev(e));
        // restore screen...
    }
}
```

The indev callback fires **before** the event reaches any widget, so this cleanly swallows the wake touch.

## ILI9488 Display On/Off

- `esp_lcd_panel_disp_on_off(panel, true/false)` sends `LCD_CMD_DISPON` / `LCD_CMD_DISPOFF`.
- 100ms `vTaskDelay` after the command (in ili9488.c).
- DISPOFF stops pixel updates but doesn't cut power. Combined with backlight=0 this is full "off".
- Panel handle accessible via `h->panel` on the `msp3520_t` struct.

## Timer Strategy

Two approaches considered:

| Approach | Pros | Cons |
|----------|------|------|
| `esp_timer` periodic check | Independent of LVGL, fires even if LVGL stalls | Extra timer, needs to query `lv_display_get_inactive_time` |
| LVGL timer (`lv_timer_create`) | Runs in LVGL task context, direct access to display state | Tied to LVGL task scheduling, needs LVGL mutex from outside |

**Decision: LVGL timer.** The idle check naturally belongs in the LVGL context — it reads display inactivity time and controls backlight. No mutex issues since it runs inside `lv_timer_handler()`. The wake path (indev callback) also runs in LVGL context.

The LVGL timer can check every ~1 second. State machine:

```
ACTIVE --[inactive >= dim_timeout]--> DIMMED --[inactive >= dim_timeout + off_timeout]--> OFF
   ^                                    |                                                  |
   |          (touch press)             |                (touch press)                     |
   +------------------------------------+--------------------------------------------------+
```

## Wake Sequence

1. Indev PRESSED event fires (wake_cb).
2. Call `lv_indev_stop_processing()` to swallow the touch.
3. If state was OFF: `esp_lcd_panel_disp_on_off(panel, true)` — but this has a 100ms vTaskDelay which is fine in LVGL task context.
4. Start fade-up: `backlight_fade(saved_brightness, fade_up_ms)` with `LEDC_FADE_NO_WAIT`.
5. Reset state to ACTIVE, reset inactivity via `lv_display_trigger_activity()`.

## Dim/Off Sequence

1. LVGL timer fires, checks `lv_display_get_inactive_time()`.
2. If inactive >= dim threshold and state is ACTIVE: `backlight_fade(dim_level, fade_down_ms)`, set state to DIMMED.
3. If inactive >= dim + off threshold and state is DIMMED: `backlight_fade(0, fade_down_ms)`. Register fade-complete callback to send DISPOFF. Set state to OFF.
4. If dim=0: skip DIMMED, go directly ACTIVE→OFF after off threshold.

## Interrupting a Fade on Wake

If user touches during a fade-down, we need to reverse it. `ledc_set_fade_time_and_start()` with a new target will block until the current fade finishes (holds semaphore). Two options:

1. **Use `ledc_fade_stop()` first** (ESP32-S3 supports it), then start fade-up. Clean and immediate.
2. **Let it finish** — fade-down is ~1s max, acceptable latency.

**Decision**: Use `ledc_fade_stop()` + immediate fade-up for responsive wake.
