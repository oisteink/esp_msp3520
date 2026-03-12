# Spec: Screen Burn-in Protection

## Goal

Protect the ILI9488 display from burn-in by dimming and/or turning off the backlight after periods of touch inactivity.

## Settings

Two new Kconfig settings under the existing MSP3520 menu:

| Setting | Type | Default | Unit | Range |
|---------|------|---------|------|-------|
| Screen dim timeout | int | 0 | minutes | 0–60 |
| Screen off timeout | int | 0 | minutes | 0–60 |
| Fade down time | int | 1000 | ms | 0–5000 |
| Fade up time | int | 500 | ms | 0–5000 |

## Behavior Matrix

| Dim | Off | Behavior |
|-----|-----|----------|
| 0 | 0 | No power saving. Screen stays on. |
| 0 | N | After N minutes idle → turn off backlight. |
| D | 0 | After D minutes idle → dim backlight. Stays dimmed. |
| D | N | After D minutes idle → dim. After N more minutes idle → turn off. |

"Idle" = no touch input detected by LVGL indev.

## Actions

All backlight transitions use **LEDC hardware fade** for smooth, CPU-free brightness changes.

- **Dim**: Fade backlight down to a low brightness level (e.g. 10%) over the configured fade-down time. The display panel stays on.
- **Turn off**: Fade backlight to 0% over the configured fade-down time. Once fade completes, send display-off command (`DISPOFF`) to the ILI9488.
- **Wake**: Any touch restores the screen. If display was off, send `DISPON` first. Then fade backlight up to previous brightness over the configured fade-up time. Timers are reset.

## Touch Handling on Wake

When the screen is dimmed or off and the user touches:
- The touch wakes the screen (restores backlight, resets timers).
- The touch event that woke the screen is **consumed** — it should NOT pass through to the LVGL UI as a click/press. This prevents accidental button presses when waking.

## Console Commands

Extend existing REPL:
- `display dim <minutes>` — set dim timeout at runtime (0 = disable)
- `display off <minutes>` — set off timeout at runtime (0 = disable)
- `display status` — show current state (active/dimmed/off), timeouts, idle time

## Constraints

- Timers must not interfere with the LVGL task or touch polling.
- Must work with both IRQ and polling touch modes.
- Backlight brightness before dimming must be remembered so it can be restored.
- No new FreeRTOS tasks — use `esp_timer` or LVGL timer callbacks.
- State transitions: `active → dimmed → off` (or `active → off` when dim=0).
- Backlight transitions use LEDC hardware fade (`ledc_set_fade_with_time` + `ledc_fade_start`). The existing `backlight_set()` must be reworked to install the fade service and support fading.

## Out of Scope

- Pixel shifting / screensaver animations.
- Configurable dim brightness level (hardcode ~10% for now).
- NVS persistence of timeout settings (Kconfig defaults only for now).
