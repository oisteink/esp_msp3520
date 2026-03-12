# Spec: Screen Burn-in Protection

## Goal

Protect the ILI9488 display from burn-in by dimming and/or turning off the backlight after periods of touch inactivity. Provide a smooth startup experience with a fade-in from dark.

## Settings

Two new Kconfig settings under the existing MSP3520 menu:

| Setting | Type | Default | Unit | Range |
|---------|------|---------|------|-------|
| Screen dim timeout | int | 0 | minutes | 0–60 |
| Screen off timeout | int | 0 | minutes | 0–60 |
| Fade out time | int | 1000 | ms | 0–5000 |
| Fade in time | int | 1000 | ms | 0–5000 |

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

- **Startup**: Backlight initializes at 0%. After display and LVGL are ready, fade in to configured brightness over the fade-in time.
- **Dim**: Fade backlight down to a low brightness level (e.g. 10%) over the configured fade-out time. The display panel stays on.
- **Turn off**: Fade backlight to 0% over the configured fade-out time. Once fade completes, send display-off command (`DISPOFF`) to the ILI9488.
- **Wake**: Any touch restores the screen. If display was off, send `DISPON` first. Then fade backlight up to previous brightness over the configured fade-in time. Timers are reset.

## Touch Handling on Wake

When the screen is dimmed, off, or fading in (startup or wake):
- The touch wakes the screen (restores backlight, resets timers).
- Touch events are **consumed** for the entire duration of the fade-in — they should NOT pass through to the LVGL UI as clicks/presses. This prevents accidental button presses when waking or during startup.

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
- State transitions include a FADING_IN state during which touch events are consumed: `startup/wake → fading_in → active → dimmed → off`.

## Out of Scope

- Pixel shifting / screensaver animations.
- Configurable dim brightness level (hardcode ~10% for now).
- NVS persistence of timeout settings (Kconfig defaults only for now).
