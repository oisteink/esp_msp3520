# Touch Interface Improvement Design

**Date**: 2026-03-09
**Iteration**: 5

## Problem

Resistive touch (XPT2046) is hard to use with fingers. Must press very hard for taps to register, and taps get missed frequently. Touch is polled every LVGL tick even when screen is not being touched. The third-party driver (atanisoft) limits our ability to customize behavior.

## Design

### 1. Driver Fork

Copy `managed_components/atanisoft__esp_lcd_touch_xpt2046/` verbatim into `components/xpt2046/`. Remove the atanisoft managed component dependency from `idf_component.yml`. Keep the `espressif/esp_lcd_touch` base dependency — our driver continues to implement the `esp_lcd_touch` interface so LVGL integration and main.c stay unchanged.

### 2. Z-Threshold & Filtering

**Z-Threshold**: Lower default from 400 to ~100-150 for finger use (fingers spread pressure over a larger area than a stylus, producing lower Z readings). Make it adjustable at runtime via REPL command (`touch_cfg z_threshold <value>`).

**Filtering**: Add multi-sample averaging in the SPI read path:
- Take 5 ADC samples per read cycle
- Discard highest and lowest values (outlier rejection)
- Average the remaining 3

5 SPI reads at 1 MHz completes in under 1ms — negligible latency impact.

### 3. IRQ-Driven Touch Detection

Replace continuous polling with interrupt-driven detection:
- Register GPIO ISR on IRQ pin (GPIO 5, active-low)
- ISR sets an atomic flag (`touch_pending`)
- `touch_read_cb`: if flag not set, return RELEASED immediately (no SPI transaction)
- If flag set, perform SPI read, clear flag
- IRQ pin stays low while touched, so reads continue during a press

Simple flag-based approach — no queues, no separate tasks.

### 4. Touch Calibration

**Interactive routine**: 3-point crosshair calibration triggered via REPL (`touch_cal start`). Crosshairs displayed at known screen positions. User taps each, raw ADC coordinates recorded. Computes 6-coefficient affine transform mapping raw touch to screen coordinates.

**Storage**: Coefficients saved to NVS, loaded on boot. Falls back to default linear ADC-to-pixel mapping if no calibration stored.

**REPL commands**:
- `touch_cal start` — launch interactive crosshair calibration
- `touch_cal show` — display current coefficients
- `touch_cal clear` — delete calibration, revert to default

Transform applied inside the driver's coordinate conversion path.

## Changes Summary

| Area | Change |
|------|--------|
| `components/xpt2046/` | New in-tree component (fork of atanisoft driver) |
| `managed_components/` | Remove atanisoft__esp_lcd_touch_xpt2046 |
| `idf_component.yml` | Remove atanisoft entry |
| Driver internals | Z-threshold, multi-sample filtering, IRQ flag, affine calibration |
| `main/ili9488-test.c` | IRQ GPIO ISR, calibration UI, NVS load/store, new REPL commands |
| NVS | 6 affine calibration coefficients |

## Out of Scope

Gesture recognition, multi-touch, UI redesign, landscape support.
