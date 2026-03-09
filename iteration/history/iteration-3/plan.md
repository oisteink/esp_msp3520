# XPT2046 Touch Input — Implementation Plan

**Ref:** `iteration/spec.md`, `iteration/research.md`

**Goal:** Add touch input to the existing LVGL app. Replace static UI with a tappable button demo.

---

### Task 1: Add touch pin config and build dependencies

**Files:**
- Modify: `main/Kconfig.projbuild`
- Modify: `main/CMakeLists.txt`

**Step 1: Add touch GPIO entries to Kconfig.projbuild**

Append before `endmenu`:

```kconfig
    config TOUCH_CS_GPIO
        int "Touch CS GPIO"
        range ENV_GPIO_RANGE_MIN ENV_GPIO_OUT_RANGE_MAX
        default 4
        help
            GPIO number for the XPT2046 touch chip select.

    config TOUCH_IRQ_GPIO
        int "Touch IRQ GPIO"
        range -1 ENV_GPIO_OUT_RANGE_MAX
        default 5
        help
            GPIO number for the XPT2046 touch interrupt. Set to -1 to disable.
```

**Step 2: Add touch components to CMakeLists.txt REQUIRES**

```cmake
idf_component_register(SRCS "ili9488-test.c"
                    INCLUDE_DIRS "."
                    REQUIRES "esp_lcd_ili9488" "esp_driver_spi" "lvgl" "esp_timer"
                             "atanisoft__esp_lcd_touch_xpt2046" "espressif__esp_lcd_touch")
```

**Step 3: Add pin defaults to sdkconfig.defaults**

```
# Touch
CONFIG_TOUCH_CS_GPIO=4
CONFIG_TOUCH_IRQ_GPIO=5
```

**Step 4: Build to verify components link**

---

### Task 2: Integrate touch into app

**Files:**
- Modify: `main/ili9488-test.c`

**Changes to app_main (after display init, before LVGL init):**

```c
#include "esp_lcd_touch_xpt2046.h"

// Touch read callback for LVGL
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

Insert in app_main after display panel init, before LVGL init:

```c
// Touch IO (same SPI bus, separate CS)
ESP_LOGI(TAG, "installing touch panel IO");
esp_lcd_panel_io_handle_t tp_io = NULL;
esp_lcd_panel_io_spi_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(CONFIG_TOUCH_CS_GPIO);
ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &tp_io_cfg, &tp_io));

// Touch driver
ESP_LOGI(TAG, "installing XPT2046 touch driver");
esp_lcd_touch_handle_t touch = NULL;
esp_lcd_touch_config_t tp_cfg = {
    .x_max = LCD_H_RES,
    .y_max = LCD_V_RES,
    .rst_gpio_num = GPIO_NUM_NC,
    .int_gpio_num = CONFIG_TOUCH_IRQ_GPIO,
    .levels = { .interrupt = 0 },
    .flags = {
        .swap_xy = 0,
        .mirror_x = 0,
        .mirror_y = 0,
    },
};
ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(tp_io, &tp_cfg, &touch));
```

Insert after LVGL display setup:

```c
// Touch input device
lv_indev_t *indev = lv_indev_create();
lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
lv_indev_set_read_cb(indev, touch_read_cb);
lv_indev_set_user_data(indev, touch);
lv_indev_set_display(indev, disp);
```

**Replace create_ui with interactive demo:**

```c
static void btn_event_cb(lv_event_t *e)
{
    static int count = 0;
    lv_obj_t *label = lv_event_get_user_data(e);
    lv_label_set_text_fmt(label, "Tapped: %d", ++count);
}

static void create_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);

    // Tap counter label
    lv_obj_t *counter = lv_label_create(scr);
    lv_label_set_text(counter, "Tapped: 0");
    lv_obj_set_style_text_font(counter, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(counter, lv_color_black(), 0);
    lv_obj_align(counter, LV_ALIGN_CENTER, 0, 60);

    // Button
    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 200, 80);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, -30);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, counter);

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Tap me!");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_28, 0);
    lv_obj_center(btn_label);
}
```

---

### Task 3: Flash and test

**Step 1:** Flash and verify display still works with new code.

**Step 2:** Tap the button — counter should increment.

**Step 3:** If touch coordinates are wrong (tapping button doesn't hit it), adjust `tp_cfg.flags`:
- Try `mirror_x = 1` and/or `mirror_y = 1`
- Try `swap_xy = 1` if axes are swapped
- The display uses `esp_lcd_panel_mirror(panel, true, true)` so touch may need matching flags

**Step 4:** Commit fixups.

---

### Task 4: Final commit and archive

```bash
git add -A
git commit -m "feat: add XPT2046 touch input with LVGL integration"
git push
```
