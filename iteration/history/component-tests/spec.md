# Spec: MSP3520 Component Integration Tests + Timeout Unit Change

## Goal

1. Change screen protection timeout internals from minutes to seconds. Kconfig stays in minutes (user-friendly), console commands and API use seconds (test-friendly).
2. On-target integration tests for the MSP3520 component that catch behavioral drift — especially screen burn-in protection.

## Part 1: Timeout Unit Change

### Changes

- `dim_timeout_min` / `off_timeout_min` → `dim_timeout_s` / `off_timeout_s` in struct and throughout code.
- `screen_protect_init()`: multiplies Kconfig values by 60 when storing.
- `screen_protect_set_dim_timeout()` / `set_off_timeout()`: accept seconds directly.
- Console commands `display dim <seconds>` / `display off <seconds>`: accept and display seconds.
- `display status`: show timeout in seconds and idle in seconds.
- `idle_check_cb`: compare against seconds × 1000 instead of minutes × 60000.

### Kconfig

No change — stays in minutes with same defaults (10, 50).

## Part 2: Integration Tests

### Approach

- Unity test framework via `idf.py -T msp3520`
- Tests run on the ESP32-S3 with actual hardware
- Automated tests use LVGL `lv_test_indev` for simulated touch
- Manual/interactive tests for physical touch verification (instruct user via serial, read feedback from monitor)
- Focus on drift-catching, not exhaustive

### Automated Test Cases

#### Screen Protection State Machine

1. **Dim after timeout**: Set dim=3s, wait, verify state is DIMMED.
2. **Off after dim+off**: Set dim=2s, off=2s, wait ~4s, verify state is OFF.
3. **Skip dim when dim=0**: Set dim=0, off=3s, wait, verify goes straight to OFF.
4. **No action when both=0**: Set both to 0, wait, verify state stays ACTIVE.
5. **Wake from dimmed**: Force dimmed state, inject touch via test_indev, verify state transitions to WAKING then ACTIVE.
6. **Wake from off**: Force off state, inject touch, verify DISPON + state restore.

#### Touch Suppression

7. **Wake touch consumed**: Create a test button, force dimmed, inject touch on button, verify button callback did NOT fire.
8. **Touch passes after wake**: After wake + 250ms, inject touch on button, verify button callback fires.

#### Backlight

9. **Manual backlight updates state**: Call `msp3520_set_backlight(h, 50)`, verify saved_brightness=50 and state=ACTIVE.

### Interactive Test Cases (User-Driven)

These print instructions to serial and wait for user confirmation:

10. **Physical touch wake**: "Screen will dim in 5s. Touch to wake. Did the screen restore? (y/n)"
11. **Fade-in visible on boot**: "Reboot the device. Did you see a smooth fade-in? (y/n)"

### Test Infrastructure

- Test app at `components/msp3520/test_apps/` (ESP-IDF component test pattern)
- Full component init in test setup (display, touch, LVGL all running)
- Helper to set timeouts in seconds and wait for state transitions
- State query via `screen_protect_get_status()`

### Constraints

- Tests need the LVGL task running — full component must be initialized.
- Automated time-dependent tests use seconds-level timeouts (2-5s) for fast runs.
- Tests must clean up state between runs.
- Test app needs its own `sdkconfig.defaults` with appropriate settings.
