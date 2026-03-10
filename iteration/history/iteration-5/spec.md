# Iteration 5: Touch Interface Improvement

## Problem

Resistive touch (XPT2046) requires excessive pressure for finger use. Taps are frequently missed. Touch is polled continuously even when screen is idle. The third-party driver limits customization.

## Requirements

1. **Fork XPT2046 driver** — Copy atanisoft component in-tree as `components/xpt2046/`. Keep `esp_lcd_touch` interface. Remove managed component dependency.

2. **Improve touch sensitivity** — Lower Z-threshold from 400 to ~100-150 for finger use. Make threshold adjustable at runtime via REPL.

3. **Add sample filtering** — Multi-sample averaging with outlier rejection (5 samples, discard min/max, average remaining 3).

4. **IRQ-driven touch detection** — Use GPIO ISR on IRQ pin to avoid polling SPI bus when screen is not touched.

5. **Touch calibration** — 3-point crosshair calibration routine triggered from REPL. Affine transform (6 coefficients) stored in NVS. REPL commands: `touch_cal start`, `touch_cal show`, `touch_cal clear`.

## Out of Scope

Gesture recognition, multi-touch, UI redesign, landscape support.

## References

- Design: `docs/plans/2026-03-09-touch-improvement-design.md`
