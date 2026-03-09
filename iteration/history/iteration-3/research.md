# Iteration 3 Research: XPT2046 Touch Input

**Ref:** `iteration/spec.md`

## Component: atanisoft/esp_lcd_touch_xpt2046 v1.0.6

### API

```c
// Create touch panel IO on same SPI bus as display
esp_lcd_panel_io_spi_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(touch_cs_pin);
// Macro expands to: cs_gpio_num=touch_cs, dc_gpio_num=NC, spi_mode=0, pclk_hz=1MHz,
//                   trans_queue_depth=3, lcd_cmd/param_bits=8
esp_lcd_panel_io_handle_t tp_io;
esp_lcd_new_panel_io_spi(SPI2_HOST, &tp_io_cfg, &tp_io);

// Create touch driver
esp_lcd_touch_config_t tp_cfg = {
    .x_max = LCD_H_RES,
    .y_max = LCD_V_RES,
    .rst_gpio_num = GPIO_NUM_NC,
    .int_gpio_num = irq_pin,       // or GPIO_NUM_NC to disable
    .levels = { .interrupt = 0 },  // XPT2046 IRQ is active-low
    .flags = {
        .swap_xy = 0,
        .mirror_x = 0,
        .mirror_y = 0,
    },
};
esp_lcd_touch_handle_t touch;
esp_lcd_touch_new_spi_xpt2046(tp_io, &tp_cfg, &touch);
```

### Kconfig options (Kconfig.projbuild in component)

- `XPT2046_Z_THRESHOLD` (default 400): min pressure to register touch
- `XPT2046_INTERRUPT_MODE` (default n): enable PENIRQ output (uses more power)
- `XPT2046_CONVERT_ADC_TO_COORDS` (default y): auto-convert ADC 0-4096 to screen coords
- `XPT2046_ENABLE_LOCKING` (default n): thread-safety locking (warning: may crash)
- `XPT2046_VREF_ON_MODE` (default n): keep internal Vref enabled

### Reading touch data

```c
// Two-step: read hardware, then get coordinates
esp_lcd_touch_read_data(touch);  // SPI transaction to XPT2046

// New API (v1.2.1 esp_lcd_touch):
esp_lcd_touch_point_data_t point;
uint8_t count = 0;
esp_lcd_touch_get_data(touch, &point, &count, 1);
// point.x, point.y, point.strength, point.track_id

// Old API (deprecated but still works):
uint16_t x, y;
uint8_t count = 0;
bool touched = esp_lcd_touch_get_coordinates(touch, &x, &y, NULL, &count, 1);
```

### Coordinate mapping

The `flags.swap_xy`, `flags.mirror_x`, `flags.mirror_y` in `esp_lcd_touch_config_t` are applied automatically by the base `esp_lcd_touch` layer after reading. The `x_max` and `y_max` values are used for mirroring calculations. With `XPT2046_CONVERT_ADC_TO_COORDS=y`, raw ADC values (0-4096) are scaled to (0-x_max) and (0-y_max).

## LVGL v9.5 Input Device API

LVGL v9 replaced `lv_indev_drv_t` with a create/configure pattern:

```c
lv_indev_t *indev = lv_indev_create();
lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
lv_indev_set_read_cb(indev, touch_read_cb);
lv_indev_set_user_data(indev, touch_handle);
lv_indev_set_display(indev, disp);  // optional, auto-assigns to default
```

### Read callback signature

```c
// typedef void (*lv_indev_read_cb_t)(lv_indev_t *indev, lv_indev_data_t *data);

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t touch = lv_indev_get_user_data(indev);
    esp_lcd_touch_read_data(touch);

    uint16_t x, y;
    uint8_t count = 0;
    esp_lcd_touch_get_coordinates(touch, &x, &y, NULL, &count, 1);

    if (count > 0) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
```

## SPI bus sharing

- Display and touch share SPI2_HOST, each with separate CS pin
- ESP-IDF SPI driver handles CS arbitration automatically -- only one device active at a time
- Display runs at 40 MHz, touch at 1 MHz (set by `ESP_LCD_TOUCH_SPI_CLOCK_HZ`)
- The SPI driver reconfigures clock speed per-device transparently
- Touch IO uses `dc_gpio_num = GPIO_NUM_NC` (XPT2046 doesn't have a DC pin)

## IRQ pin

- XPT2046 PENIRQ output goes low when screen is touched
- With `XPT2046_INTERRUPT_MODE=y`: PENIRQ stays active between conversions (more power)
- With `XPT2046_INTERRUPT_MODE=n` (default): PENIRQ also goes low during SPI reads
- The `int_gpio_num` in config sets up a GPIO ISR that calls `interrupt_callback`
- For LVGL polling, IRQ isn't strictly needed -- we poll in the read callback
- IRQ can optimize by skipping SPI reads when no touch detected

## Key decisions for plan

1. **Polling vs IRQ**: Use polling in LVGL read callback (simpler). Wire IRQ but don't enable interrupt mode initially.
2. **Coordinate flags**: Will need testing on hardware -- depends on touch panel orientation relative to display. Start with all flags at 0 and adjust.
3. **Thread safety**: Touch reads happen in LVGL task (under lvgl_lock), so `XPT2046_ENABLE_LOCKING=n` is fine.
4. **ADC conversion**: Keep `XPT2046_CONVERT_ADC_TO_COORDS=y` (default) for automatic scaling.
5. **CMakeLists**: Add `"atanisoft__esp_lcd_touch_xpt2046"` and `"espressif__esp_lcd_touch"` to REQUIRES.
