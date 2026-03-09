# Iteration 3: XPT2046 Touch Input

**Goal:** Add resistive touch input to the existing LVGL UI so the user can interact with on-screen elements.

**What we're building:**
- XPT2046 touch driver on shared SPI2 bus (T_CS=GPIO 4, T_IRQ=GPIO 5)
- Integration with LVGL v9.5 input device system
- Simple interactive demo UI (e.g. button that responds to taps) replacing the static "Hello LVGL" screen

**What we're NOT building:**
- Landscape orientation (deferred -- touch calibration depends on orientation)
- Touch calibration UI (use coordinate flags: swap_xy, mirror_x, mirror_y to get mapping right)
- Gestures beyond basic tap/press

**Approach:**
- Use `atanisoft/esp_lcd_touch_xpt2046` managed component
- Share SPI2 bus with display, separate CS pin
- Use IRQ pin for efficient touch detection
- Register as LVGL pointer input device

**Acceptance criteria:**
- [ ] Touch events register in LVGL (tap a button, something happens)
- [ ] Touch coordinates map correctly to display in portrait mode
- [ ] No SPI bus conflicts between display and touch
- [ ] No crashes or watchdog triggers
