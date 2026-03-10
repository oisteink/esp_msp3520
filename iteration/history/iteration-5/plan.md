# Touch Interface Improvement — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make resistive touch usable with fingers by forking the XPT2046 driver, improving sensitivity, adding IRQ support, and implementing calibration.

**Architecture:** Fork the atanisoft XPT2046 component as `components/xpt2046/`, keeping the `esp_lcd_touch` interface. Modify the driver internals for better filtering, IRQ-based detection, and affine calibration with NVS persistence. Add REPL commands and a calibration UI screen in main.

**Tech Stack:** ESP-IDF v5.5.3, esp_lcd_touch API, NVS, LVGL v9.5, GPIO ISR

---

## Task 1: Fork XPT2046 Driver Component

**Files:**
- Create: `components/xpt2046/esp_lcd_touch_xpt2046.c` (copy from `managed_components/atanisoft__esp_lcd_touch_xpt2046/esp_lcd_touch_xpt2046.c`)
- Create: `components/xpt2046/include/esp_lcd_touch_xpt2046.h` (copy from `managed_components/atanisoft__esp_lcd_touch_xpt2046/include/esp_lcd_touch_xpt2046.h`)
- Create: `components/xpt2046/Kconfig.projbuild` (copy from `managed_components/atanisoft__esp_lcd_touch_xpt2046/Kconfig.projbuild`)
- Create: `components/xpt2046/CMakeLists.txt` (copy from `managed_components/atanisoft__esp_lcd_touch_xpt2046/CMakeLists.txt`)
- Create: `components/xpt2046/idf_component.yml` (new, depends on esp_lcd_touch)
- Modify: `main/idf_component.yml` — remove `atanisoft/esp_lcd_touch_xpt2046` dependency
- Modify: `main/CMakeLists.txt` — replace `atanisoft__esp_lcd_touch_xpt2046` with `xpt2046`

**Step 1: Copy driver files**

```bash
mkdir -p components/xpt2046/include
cp managed_components/atanisoft__esp_lcd_touch_xpt2046/esp_lcd_touch_xpt2046.c components/xpt2046/
cp managed_components/atanisoft__esp_lcd_touch_xpt2046/include/esp_lcd_touch_xpt2046.h components/xpt2046/include/
cp managed_components/atanisoft__esp_lcd_touch_xpt2046/Kconfig.projbuild components/xpt2046/
cp managed_components/atanisoft__esp_lcd_touch_xpt2046/CMakeLists.txt components/xpt2046/
```

**Step 2: Create `components/xpt2046/idf_component.yml`**

```yaml
dependencies:
  espressif/esp_lcd_touch: ">=1.0.4"
  idf: ">=5.0.0"
description: Forked XPT2046 touch driver (from atanisoft v1.0.6)
version: 1.1.0
```

**Step 3: Update `main/idf_component.yml`**

Remove the `atanisoft/esp_lcd_touch_xpt2046: ^1.0.0` line. The `espressif/esp_lcd_touch` dependency comes transitively from `components/xpt2046/idf_component.yml`.

```yaml
dependencies:
  idf:
    version: '>=4.1.0'
  lvgl/lvgl: ^9.5.0
```

**Step 4: Update `main/CMakeLists.txt`**

Replace `"atanisoft__esp_lcd_touch_xpt2046"` with `"xpt2046"` in the REQUIRES list:

```cmake
idf_component_register(SRCS "ili9488-test.c"
                   INCLUDE_DIRS "."
                   REQUIRES "esp_lcd_ili9488" "esp_driver_spi" "lvgl" "esp_timer"
                            "xpt2046" "espressif__esp_lcd_touch"
                            "console")
```

**Step 5: Update `components/xpt2046/CMakeLists.txt`**

The `REQUIRES` needs `esp_driver_spi` instead of bare `driver` for ESP-IDF v5.5:

```cmake
idf_component_register(SRCS "esp_lcd_touch_xpt2046.c"
                       INCLUDE_DIRS "include"
                       REQUIRES "esp_driver_spi" "esp_lcd_touch")
```

Note: `esp_lcd_touch` here refers to the managed component `espressif__esp_lcd_touch` — ESP-IDF resolves this automatically since the component declares it in `idf_component.yml`.

**Step 6: Clean old managed component**

```bash
rm -rf managed_components/atanisoft__esp_lcd_touch_xpt2046
```

**Step 7: Build to verify fork compiles cleanly**

```bash
idf.py build
```

Expected: Clean build, no errors. Warnings about deprecated `esp_lcd_touch_get_coordinates` are fine (existing code).

**Step 8: Commit**

```bash
git add components/xpt2046/ main/idf_component.yml main/CMakeLists.txt
git commit -m "feat: fork XPT2046 driver as in-tree component

Verbatim copy of atanisoft/esp_lcd_touch_xpt2046 v1.0.6.
Removes managed component dependency, keeps esp_lcd_touch interface."
```

---

## Task 2: Lower Z-Threshold and Add Runtime Configuration

**Files:**
- Modify: `components/xpt2046/Kconfig.projbuild` — change default Z-threshold
- Modify: `main/ili9488-test.c` — add `touch_cfg` REPL command

**Step 1: Change Z-threshold default in `components/xpt2046/Kconfig.projbuild`**

Change the `XPT2046_Z_THRESHOLD` default from `400` to `100`:

```
    config XPT2046_Z_THRESHOLD
        int "Minimum Z pressure threshold"
        default 100
        help
            Touch pressure less than this value will be discarded as invalid
            and no touch position data collected.
            Lower values (50-150) suit finger use on resistive panels.
            Higher values (300-500) suit stylus use.
```

**Step 2: Add runtime Z-threshold to the driver**

We need a way to change the threshold at runtime without rebuilding. Add a global variable in the driver that defaults to `CONFIG_XPT2046_Z_THRESHOLD` and can be set externally.

In `components/xpt2046/esp_lcd_touch_xpt2046.c`, add after the existing constants (after line 64):

```c
static uint16_t xpt2046_z_threshold = CONFIG_XPT2046_Z_THRESHOLD;
```

And replace line 199 (`if (z >= CONFIG_XPT2046_Z_THRESHOLD)`) with:

```c
    if (z >= xpt2046_z_threshold)
```

In `components/xpt2046/include/esp_lcd_touch_xpt2046.h`, add before the closing `#ifdef __cplusplus`:

```c
/**
 * @brief Set the Z pressure threshold at runtime
 * @param threshold New threshold value (0-4096)
 */
void esp_lcd_touch_xpt2046_set_z_threshold(uint16_t threshold);

/**
 * @brief Get the current Z pressure threshold
 * @return Current threshold value
 */
uint16_t esp_lcd_touch_xpt2046_get_z_threshold(void);
```

In `components/xpt2046/esp_lcd_touch_xpt2046.c`, add the implementations (after `xpt2046_z_threshold`):

```c
void esp_lcd_touch_xpt2046_set_z_threshold(uint16_t threshold)
{
    xpt2046_z_threshold = threshold;
}

uint16_t esp_lcd_touch_xpt2046_get_z_threshold(void)
{
    return xpt2046_z_threshold;
}
```

**Step 3: Add `touch_cfg` REPL command in `main/ili9488-test.c`**

Add new command function (after `cmd_debug`, around line 216):

```c
static int cmd_touch_cfg(int argc, char **argv)
{
    if (argc == 1) {
        printf("z_threshold=%u\n", esp_lcd_touch_xpt2046_get_z_threshold());
        return 0;
    }
    if (argc != 3) {
        printf("Usage: touch_cfg z_threshold <value>\n");
        return 1;
    }
    if (strcmp(argv[1], "z_threshold") == 0) {
        uint16_t val = (uint16_t)atoi(argv[2]);
        esp_lcd_touch_xpt2046_set_z_threshold(val);
        printf("Set z_threshold=%u\n", val);
    } else {
        printf("Unknown param: %s\n", argv[1]);
        return 1;
    }
    return 0;
}
```

Register the command in `app_main` (after the existing command registrations):

```c
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "touch_cfg", .help = "Get/set touch config",
        .hint = "[z_threshold] [value]", .func = cmd_touch_cfg });
```

**Step 4: Reconfigure and build**

```bash
idf.py reconfigure && idf.py build
```

Expected: Clean build. The new default Z-threshold of 100 should appear in sdkconfig.

**Step 5: Flash and test**

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Verify:
- Touch responds with lighter pressure than before
- `touch_cfg` command shows current threshold
- `touch_cfg z_threshold 50` changes sensitivity in real-time
- `touch_cfg z_threshold 500` makes it harder to register (confirms it works)

**Step 6: Commit**

```bash
git add components/xpt2046/ main/ili9488-test.c
git commit -m "feat: lower Z-threshold default to 100 for finger use

Add runtime z_threshold adjustment via touch_cfg REPL command."
```

---

## Task 3: Improve Multi-Sample Filtering

**Files:**
- Modify: `components/xpt2046/esp_lcd_touch_xpt2046.c` — replace sampling loop in `xpt2046_read_data`

**Step 1: Replace the sampling loop in `xpt2046_read_data`**

The current code takes `CONFIG_ESP_LCD_TOUCH_MAX_POINTS` (5) samples and averages all valid ones. Replace with: take 5 samples, sort, discard min/max, average middle 3.

Replace the sampling section (from "read and discard" through the averaging, lines 201-254) with:

```c
        uint16_t discard_buf = 0;
        // read and discard a value as it is usually not reliable.
        ESP_RETURN_ON_ERROR(xpt2046_read_register(tp, X_POSITION, &discard_buf),
                            TAG, "XPT2046 read error!");

        // Take 5 samples for median filtering
        #define NUM_SAMPLES 5
        uint16_t x_samples[NUM_SAMPLES];
        uint16_t y_samples[NUM_SAMPLES];
        uint8_t valid = 0;

        for (uint8_t idx = 0; idx < NUM_SAMPLES; idx++)
        {
            uint16_t x_temp = 0;
            uint16_t y_temp = 0;
            ESP_RETURN_ON_ERROR(xpt2046_read_register(tp, X_POSITION, &x_temp),
                                TAG, "XPT2046 read error!");
            x_temp >>= 3;

            ESP_RETURN_ON_ERROR(xpt2046_read_register(tp, Y_POSITION, &y_temp),
                                TAG, "XPT2046 read error!");
            y_temp >>= 3;

            if ((x_temp >= 50) && (x_temp <= XPT2046_ADC_LIMIT - 50) &&
                (y_temp >= 50) && (y_temp <= XPT2046_ADC_LIMIT - 50))
            {
                x_samples[valid] = x_temp;
                y_samples[valid] = y_temp;
                valid++;
            }
        }

        // Need at least 3 valid samples for outlier rejection
        if (valid >= 3)
        {
            // Simple insertion sort for small arrays
            for (uint8_t i = 1; i < valid; i++) {
                uint16_t kx = x_samples[i], ky = y_samples[i];
                int8_t j = i - 1;
                while (j >= 0 && x_samples[j] > kx) {
                    x_samples[j + 1] = x_samples[j];
                    y_samples[j + 1] = y_samples[j];
                    j--;
                }
                x_samples[j + 1] = kx;
                y_samples[j + 1] = ky;
            }

            // Discard first and last (outliers), average the middle
            uint32_t x_sum = 0, y_sum = 0;
            uint8_t mid_count = valid - 2;
            for (uint8_t i = 1; i <= mid_count; i++) {
                x_sum += x_samples[i];
                y_sum += y_samples[i];
            }

#if CONFIG_XPT2046_CONVERT_ADC_TO_COORDS
            x = (uint32_t)(((double)x_sum / mid_count / (double)XPT2046_ADC_LIMIT) * tp->config.x_max);
            y = (uint32_t)(((double)y_sum / mid_count / (double)XPT2046_ADC_LIMIT) * tp->config.y_max);
#else
            x = x_sum / mid_count;
            y = y_sum / mid_count;
#endif
            point_count = 1;
        }
        else
        {
            z = 0;
            point_count = 0;
        }
```

**Step 2: Build**

```bash
idf.py build
```

Expected: Clean build.

**Step 3: Flash and test**

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Verify:
- Touch coordinates are more stable (less jitter when holding finger)
- Coordinate display updates smoothly instead of jumping around
- Enable debug logging (`debug` REPL command) and observe touch coordinate output

**Step 4: Commit**

```bash
git add components/xpt2046/esp_lcd_touch_xpt2046.c
git commit -m "feat: add median filtering with outlier rejection

Take 5 ADC samples, discard min/max, average middle 3.
Significantly reduces jitter on resistive touch."
```

---

## Task 4: IRQ-Driven Touch Detection

**Files:**
- Modify: `components/xpt2046/Kconfig.projbuild` — change INTERRUPT_MODE default
- Modify: `components/xpt2046/esp_lcd_touch_xpt2046.c` — add IRQ flag logic
- Modify: `main/ili9488-test.c` — add IRQ callback setup

**Step 1: Enable INTERRUPT_MODE by default**

In `components/xpt2046/Kconfig.projbuild`, change:

```
    config XPT2046_INTERRUPT_MODE
        bool "Enable Interrupt (PENIRQ) output"
        default y
        help
            Enable PENIRQ output from XPT2046. When enabled, the IRQ pin
            goes low when the screen is touched. Required for IRQ-driven
            touch detection to avoid continuous SPI polling.
```

**Step 2: Add atomic touch_pending flag to the driver**

In `components/xpt2046/esp_lcd_touch_xpt2046.c`, add after the includes:

```c
#include <stdatomic.h>
```

Add after the `xpt2046_z_threshold` variable:

```c
static atomic_bool xpt2046_touch_pending = false;
```

Add a public function to set the flag (to be called from ISR):

```c
void IRAM_ATTR esp_lcd_touch_xpt2046_notify_touch(void)
{
    atomic_store(&xpt2046_touch_pending, true);
}
```

In `components/xpt2046/include/esp_lcd_touch_xpt2046.h`, add:

```c
/**
 * @brief Notify the driver that a touch event is pending (call from ISR)
 */
void esp_lcd_touch_xpt2046_notify_touch(void);
```

**Step 3: Use the flag in xpt2046_read_data**

The existing `CONFIG_XPT2046_INTERRUPT_MODE` block (lines 173-188) already checks GPIO level. Replace it to use the atomic flag instead:

```c
#ifdef CONFIG_XPT2046_INTERRUPT_MODE
    if (!atomic_exchange(&xpt2046_touch_pending, false))
    {
        XPT2046_LOCK(&tp->data.lock);
        tp->data.coords[0].x = 0;
        tp->data.coords[0].y = 0;
        tp->data.coords[0].strength = 0;
        tp->data.points = 0;
        XPT2046_UNLOCK(&tp->data.lock);
        return ESP_OK;
    }
#endif
```

**Step 4: Set up ISR in `main/ili9488-test.c`**

Add a GPIO ISR handler before `touch_read_cb`:

```c
static void IRAM_ATTR touch_isr_handler(void *arg)
{
    esp_lcd_touch_xpt2046_notify_touch();
}
```

In `app_main`, after the touch driver is created (after `esp_lcd_touch_new_spi_xpt2046` call, around line 347), add ISR setup:

```c
    // Install GPIO ISR for touch interrupt
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(CONFIG_TOUCH_IRQ_GPIO, touch_isr_handler, NULL));
    ESP_LOGI(TAG, "touch IRQ enabled on GPIO %d", CONFIG_TOUCH_IRQ_GPIO);
```

Note: The XPT2046 driver init already configures the GPIO pin as input with NEGEDGE interrupt type. We just need to install the ISR service and add our handler.

**Step 5: Reconfigure and build**

```bash
idf.py reconfigure && idf.py build
```

Expected: Clean build. `CONFIG_XPT2046_INTERRUPT_MODE=y` in sdkconfig.

**Step 6: Flash and test**

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Verify:
- Touch still works correctly
- With debug logging enabled, observe that `xpt2046_read_data` log messages only appear when touching the screen (not continuously)
- Add a log line temporarily if needed to confirm the ISR fires

**Step 7: Commit**

```bash
git add components/xpt2046/ main/ili9488-test.c
git commit -m "feat: IRQ-driven touch detection

Use GPIO ISR on PENIRQ pin to set atomic flag. Driver skips
SPI transactions when no touch is pending. Eliminates
continuous polling of the touch controller."
```

---

## Task 5: Affine Calibration — Data Structure and NVS Storage

**Files:**
- Create: `main/touch_calibration.h` — calibration data types and API
- Create: `main/touch_calibration.c` — NVS load/save, affine transform math
- Modify: `main/CMakeLists.txt` — add new source file
- Modify: `main/CMakeLists.txt` — add `nvs_flash` to REQUIRES

**Step 1: Create `main/touch_calibration.h`**

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// 3-point affine calibration: maps raw touch (x,y) to screen (sx,sy)
//   sx = a*x + b*y + c
//   sy = d*x + e*y + f
typedef struct {
    float a, b, c;
    float d, e, f;
    bool valid;
} touch_cal_t;

// Compute affine coefficients from 3 reference point pairs
// raw[3] = raw touch coordinates, screen[3] = expected screen coordinates
// Returns ESP_OK on success, ESP_ERR_INVALID_ARG if points are degenerate
esp_err_t touch_cal_compute(const uint16_t raw_x[3], const uint16_t raw_y[3],
                            const uint16_t scr_x[3], const uint16_t scr_y[3],
                            touch_cal_t *cal);

// Apply calibration transform to raw coordinates
void touch_cal_apply(const touch_cal_t *cal, uint16_t raw_x, uint16_t raw_y,
                     uint16_t *scr_x, uint16_t *scr_y, uint16_t x_max, uint16_t y_max);

// Save calibration to NVS
esp_err_t touch_cal_save(const touch_cal_t *cal);

// Load calibration from NVS (sets cal->valid = false if not found)
esp_err_t touch_cal_load(touch_cal_t *cal);

// Clear calibration from NVS
esp_err_t touch_cal_clear(void);
```

**Step 2: Create `main/touch_calibration.c`**

```c
#include "touch_calibration.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "touch_cal";
static const char *NVS_NAMESPACE = "touch_cal";

esp_err_t touch_cal_compute(const uint16_t raw_x[3], const uint16_t raw_y[3],
                            const uint16_t scr_x[3], const uint16_t scr_y[3],
                            touch_cal_t *cal)
{
    // Solve affine transform from 3 point pairs using Cramer's rule
    // | x0 y0 1 |       | sx0 |       | sy0 |
    // | x1 y1 1 | * A = | sx1 |,  B = | sy1 |
    // | x2 y2 1 |       | sx2 |       | sy2 |

    float x0 = raw_x[0], y0 = raw_y[0];
    float x1 = raw_x[1], y1 = raw_y[1];
    float x2 = raw_x[2], y2 = raw_y[2];

    float det = x0 * (y1 - y2) - y0 * (x1 - x2) + (x1 * y2 - x2 * y1);
    if (det == 0.0f || (det > -0.001f && det < 0.001f)) {
        ESP_LOGE(TAG, "Degenerate calibration points (det=%.4f)", det);
        return ESP_ERR_INVALID_ARG;
    }

    float inv_det = 1.0f / det;

    // Solve for screen X coefficients (a, b, c)
    float sx0 = scr_x[0], sx1 = scr_x[1], sx2 = scr_x[2];
    cal->a = (sx0 * (y1 - y2) - y0 * (sx1 - sx2) + (sx1 * y2 - sx2 * y1)) * inv_det;
    cal->b = (x0 * (sx1 - sx2) - sx0 * (x1 - x2) + (x1 * sx2 - x2 * sx1)) * inv_det;
    cal->c = (x0 * (y1 * sx2 - y2 * sx1) - y0 * (x1 * sx2 - x2 * sx1) + sx0 * (x1 * y2 - x2 * y1)) * inv_det;

    // Solve for screen Y coefficients (d, e, f)
    float sy0 = scr_y[0], sy1 = scr_y[1], sy2 = scr_y[2];
    cal->d = (sy0 * (y1 - y2) - y0 * (sy1 - sy2) + (sy1 * y2 - sy2 * y1)) * inv_det;
    cal->e = (x0 * (sy1 - sy2) - sy0 * (x1 - x2) + (x1 * sy2 - x2 * sy1)) * inv_det;
    cal->f = (x0 * (y1 * sy2 - y2 * sy1) - y0 * (x1 * sy2 - x2 * sy1) + sy0 * (x1 * y2 - x2 * y1)) * inv_det;

    cal->valid = true;
    ESP_LOGI(TAG, "Calibration computed: a=%.4f b=%.4f c=%.1f d=%.4f e=%.4f f=%.1f",
             cal->a, cal->b, cal->c, cal->d, cal->e, cal->f);
    return ESP_OK;
}

void touch_cal_apply(const touch_cal_t *cal, uint16_t raw_x, uint16_t raw_y,
                     uint16_t *scr_x, uint16_t *scr_y, uint16_t x_max, uint16_t y_max)
{
    float sx = cal->a * raw_x + cal->b * raw_y + cal->c;
    float sy = cal->d * raw_x + cal->e * raw_y + cal->f;

    // Clamp to screen bounds
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sx >= x_max) sx = x_max - 1;
    if (sy >= y_max) sy = y_max - 1;

    *scr_x = (uint16_t)sx;
    *scr_y = (uint16_t)sy;
}

esp_err_t touch_cal_save(const touch_cal_t *cal)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    // Store as a blob
    float coeffs[6] = { cal->a, cal->b, cal->c, cal->d, cal->e, cal->f };
    err = nvs_set_blob(nvs, "coeffs", coeffs, sizeof(coeffs));
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Calibration saved to NVS");
    }
    return err;
}

esp_err_t touch_cal_load(touch_cal_t *cal)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        cal->valid = false;
        return (err == ESP_ERR_NVS_NOT_FOUND) ? ESP_OK : err;
    }

    float coeffs[6];
    size_t len = sizeof(coeffs);
    err = nvs_get_blob(nvs, "coeffs", coeffs, &len);
    nvs_close(nvs);

    if (err == ESP_OK && len == sizeof(coeffs)) {
        cal->a = coeffs[0]; cal->b = coeffs[1]; cal->c = coeffs[2];
        cal->d = coeffs[3]; cal->e = coeffs[4]; cal->f = coeffs[5];
        cal->valid = true;
        ESP_LOGI(TAG, "Calibration loaded from NVS");
    } else {
        cal->valid = false;
    }
    return ESP_OK;
}

esp_err_t touch_cal_clear(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    err = nvs_erase_key(nvs, "coeffs");
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_commit(nvs);
        err = ESP_OK;
    }
    nvs_close(nvs);
    ESP_LOGI(TAG, "Calibration cleared");
    return err;
}
```

**Step 3: Update `main/CMakeLists.txt`**

```cmake
idf_component_register(SRCS "ili9488-test.c" "touch_calibration.c"
                   INCLUDE_DIRS "."
                   REQUIRES "esp_lcd_ili9488" "esp_driver_spi" "lvgl" "esp_timer"
                            "xpt2046" "espressif__esp_lcd_touch"
                            "console" "nvs_flash")
```

**Step 4: Build**

```bash
idf.py build
```

Expected: Clean build.

**Step 5: Commit**

```bash
git add main/touch_calibration.h main/touch_calibration.c main/CMakeLists.txt
git commit -m "feat: add touch calibration module with NVS persistence

3-point affine transform calibration. Coefficients computed via
Cramer's rule, stored as NVS blob, loaded on boot."
```

---

## Task 6: Integrate Calibration Into Touch Pipeline

**Files:**
- Modify: `components/xpt2046/Kconfig.projbuild` — disable ADC-to-coords conversion by default (calibration replaces it)
- Modify: `main/ili9488-test.c` — apply calibration in `touch_read_cb`, load calibration on boot, init NVS

**Step 1: Disable built-in ADC-to-coords conversion**

Since our affine calibration maps raw ADC values directly to screen coordinates, we should disable the driver's built-in linear conversion. In `components/xpt2046/Kconfig.projbuild`, change:

```
    config XPT2046_CONVERT_ADC_TO_COORDS
        bool "Convert touch coordinates to screen coordinates"
        default n
        help
            When this option is enabled the raw ADC values will be converted from
            0-4096 to 0-{screen width} or 0-{screen height}.
            Disable this when using external calibration (e.g. affine transform).
```

**Step 2: Add calibration state and NVS init to `main/ili9488-test.c`**

Add include at the top:

```c
#include "touch_calibration.h"
#include "nvs_flash.h"
```

Add calibration state to `app_context_t`:

```c
typedef struct {
    esp_lcd_panel_handle_t panel;
    esp_lcd_touch_handle_t touch;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    touch_cal_t cal;
} app_context_t;
```

**Step 3: Apply calibration in `touch_read_cb`**

Replace the `touch_read_cb` function:

```c
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t touch = lv_indev_get_user_data(indev);
    esp_lcd_touch_read_data(touch);

    uint16_t x, y;
    uint8_t count = 0;
    esp_lcd_touch_get_coordinates(touch, &x, &y, NULL, &count, 1);

    if (count > 0) {
        if (app_ctx.cal.valid) {
            uint16_t sx, sy;
            touch_cal_apply(&app_ctx.cal, x, y, &sx, &sy, LCD_H_RES, LCD_V_RES);
            data->point.x = sx;
            data->point.y = sy;
            ESP_LOGD(TAG, "touch: raw=%d,%d cal=%d,%d", x, y, sx, sy);
        } else {
            data->point.x = x;
            data->point.y = y;
            ESP_LOGD(TAG, "touch: raw=%d,%d (uncalibrated)", x, y);
        }
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
```

**Step 4: Initialize NVS and load calibration in `app_main`**

Add at the very start of `app_main`, before backlight setup:

```c
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
```

After `app_ctx.mirror_y = true;`, add:

```c
    // Load touch calibration from NVS
    touch_cal_load(&app_ctx.cal);
    if (app_ctx.cal.valid) {
        ESP_LOGI(TAG, "Touch calibration loaded");
    } else {
        ESP_LOGI(TAG, "No touch calibration found, using raw coordinates");
    }
```

**Step 5: Reconfigure and build**

```bash
idf.py reconfigure && idf.py build
```

Expected: Clean build. `CONFIG_XPT2046_CONVERT_ADC_TO_COORDS` is now `n`.

**Step 6: Flash and test**

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Verify:
- Boot message shows "No touch calibration found, using raw coordinates"
- Touch still works but coordinates are raw ADC values (0-4096 range) — this is expected since calibration hasn't been performed yet
- The coordinate display should show large numbers (raw ADC) instead of 0-320/0-480 range

**Step 7: Commit**

```bash
git add components/xpt2046/Kconfig.projbuild main/ili9488-test.c
git commit -m "feat: integrate affine calibration into touch pipeline

Calibration loaded from NVS on boot and applied in touch_read_cb.
Disabled built-in linear ADC conversion in favor of affine transform."
```

---

## Task 7: Calibration UI Screen and REPL Commands

**Files:**
- Modify: `main/ili9488-test.c` — add calibration screen, update `touch_cal` REPL command

**Step 1: Add calibration screen function**

This is the interactive 3-point crosshair routine. Add before `create_ui`:

```c
// Calibration crosshair target positions (screen coordinates)
static const uint16_t cal_screen_x[3] = { 40, 280, 160 };  // top-left, top-right, bottom-center
static const uint16_t cal_screen_y[3] = { 40, 40, 440 };

static lv_obj_t *cal_label = NULL;
static lv_obj_t *cal_crosshair = NULL;
static uint8_t cal_point_idx = 0;
static uint16_t cal_raw_x[3];
static uint16_t cal_raw_y[3];
static bool cal_active = false;

static void draw_crosshair(lv_obj_t *parent, uint16_t x, uint16_t y)
{
    if (cal_crosshair) {
        lv_obj_del(cal_crosshair);
    }
    // Use a small "+" label as crosshair
    cal_crosshair = lv_label_create(parent);
    lv_label_set_text(cal_crosshair, "+");
    lv_obj_set_style_text_font(cal_crosshair, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(cal_crosshair, lv_color_make(0xFF, 0x00, 0x00), 0);
    lv_obj_set_pos(cal_crosshair, x - 8, y - 14);
}

static void cal_touch_cb(lv_event_t *e)
{
    if (!cal_active) return;

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    // Read raw coordinates directly from the touch driver
    uint16_t x, y;
    uint8_t count = 0;
    esp_lcd_touch_get_coordinates(app_ctx.touch, &x, &y, NULL, &count, 1);
    if (count == 0) return;

    cal_raw_x[cal_point_idx] = x;
    cal_raw_y[cal_point_idx] = y;

    ESP_LOGI(TAG, "Cal point %d: raw=(%u, %u) screen=(%u, %u)",
             cal_point_idx, x, y,
             cal_screen_x[cal_point_idx], cal_screen_y[cal_point_idx]);

    cal_point_idx++;

    if (cal_point_idx >= 3) {
        // All 3 points collected — compute calibration
        cal_active = false;

        touch_cal_t cal;
        esp_err_t err = touch_cal_compute(cal_raw_x, cal_raw_y,
                                          cal_screen_x, cal_screen_y, &cal);
        if (err == ESP_OK) {
            app_ctx.cal = cal;
            touch_cal_save(&cal);
            lv_label_set_text(cal_label, "Calibration OK!\nSaved to NVS.");
        } else {
            lv_label_set_text(cal_label, "Calibration FAILED.\nTry again.");
        }

        if (cal_crosshair) {
            lv_obj_del(cal_crosshair);
            cal_crosshair = NULL;
        }

        // Return to main UI after 2 seconds
        lv_obj_add_flag(lv_obj_get_parent(cal_label), LV_OBJ_FLAG_HIDDEN);
    } else {
        // Show next crosshair
        lv_obj_t *scr = lv_obj_get_parent(cal_label);
        draw_crosshair(scr, cal_screen_x[cal_point_idx], cal_screen_y[cal_point_idx]);
        lv_label_set_text_fmt(cal_label, "Tap crosshair %d/3", cal_point_idx + 1);
    }
}

static lv_obj_t *cal_screen = NULL;

static void start_calibration(void)
{
    _lock_acquire(&lvgl_lock);

    // Temporarily disable calibration so we get raw coordinates
    app_ctx.cal.valid = false;

    if (!cal_screen) {
        cal_screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(cal_screen, lv_color_white(), 0);
        lv_obj_set_size(cal_screen, LCD_H_RES, LCD_V_RES);

        cal_label = lv_label_create(cal_screen);
        lv_obj_set_style_text_font(cal_label, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(cal_label, lv_color_black(), 0);
        lv_obj_set_style_text_align(cal_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(cal_label, LV_ALIGN_CENTER, 0, 0);

        lv_obj_add_event_cb(cal_screen, cal_touch_cb, LV_EVENT_PRESSED, NULL);
    }

    cal_point_idx = 0;
    cal_active = true;
    lv_label_set_text(cal_label, "Tap crosshair 1/3");
    lv_obj_clear_flag(cal_screen, LV_OBJ_FLAG_HIDDEN);

    draw_crosshair(cal_screen, cal_screen_x[0], cal_screen_y[0]);

    lv_screen_load(cal_screen);

    _lock_release(&lvgl_lock);
}
```

**Step 2: Update `cmd_touch_cal` to handle new subcommands**

Replace the existing `cmd_touch_cal` function:

```c
static int cmd_touch_cal(void *ctx, int argc, char **argv)
{
    app_context_t *app = (app_context_t *)ctx;

    if (argc == 1) {
        // Show current state
        if (app->cal.valid) {
            printf("Calibration: active\n");
            printf("  a=%.6f b=%.6f c=%.1f\n", app->cal.a, app->cal.b, app->cal.c);
            printf("  d=%.6f e=%.6f f=%.1f\n", app->cal.d, app->cal.e, app->cal.f);
        } else {
            printf("Calibration: none (using raw coordinates)\n");
        }
        // Also show mirror/swap flags
        bool swap, mx, my;
        esp_lcd_touch_get_swap_xy(app->touch, &swap);
        esp_lcd_touch_get_mirror_x(app->touch, &mx);
        esp_lcd_touch_get_mirror_y(app->touch, &my);
        printf("Flags: swap_xy=%d mirror_x=%d mirror_y=%d\n", swap, mx, my);
        return 0;
    }

    const char *sub = argv[1];

    if (strcmp(sub, "start") == 0) {
        printf("Starting calibration...\n");
        start_calibration();
        return 0;
    }

    if (strcmp(sub, "show") == 0) {
        if (app->cal.valid) {
            printf("a=%.6f b=%.6f c=%.1f\n", app->cal.a, app->cal.b, app->cal.c);
            printf("d=%.6f e=%.6f f=%.1f\n", app->cal.d, app->cal.e, app->cal.f);
        } else {
            printf("No calibration data\n");
        }
        return 0;
    }

    if (strcmp(sub, "clear") == 0) {
        app->cal.valid = false;
        touch_cal_clear();
        printf("Calibration cleared\n");
        return 0;
    }

    // Legacy: swap_xy/mirror_x/mirror_y flags
    if (argc == 3) {
        bool val = atoi(argv[2]) != 0;
        if      (strcmp(sub, "swap_xy") == 0)  esp_lcd_touch_set_swap_xy(app->touch, val);
        else if (strcmp(sub, "mirror_x") == 0) esp_lcd_touch_set_mirror_x(app->touch, val);
        else if (strcmp(sub, "mirror_y") == 0) esp_lcd_touch_set_mirror_y(app->touch, val);
        else {
            printf("Unknown: %s\n", sub);
            return 1;
        }
        printf("Set %s=%d\n", sub, val);
        return 0;
    }

    printf("Usage: touch_cal [start|show|clear|swap_xy|mirror_x|mirror_y] [0|1]\n");
    return 1;
}
```

**Step 3: Update the touch_cal command registration hint**

In `app_main`, update the hint for the `touch_cal` command registration:

```c
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "touch_cal", .help = "Touch calibration and coordinate flags",
        .hint = "[start|show|clear|swap_xy|mirror_x|mirror_y] [0|1]",
        .func_w_context = cmd_touch_cal, .context = &app_ctx });
```

**Step 4: Build**

```bash
idf.py build
```

Expected: Clean build.

**Step 5: Flash and test**

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Verify:
- `touch_cal` shows "Calibration: none"
- `touch_cal start` switches to calibration screen with red "+" crosshair
- Tap each of the 3 crosshairs — coordinates are captured
- After 3rd tap: "Calibration OK! Saved to NVS." appears
- Touch now maps accurately to screen coordinates
- `touch_cal show` displays the 6 coefficients
- Reboot — calibration persists (loaded from NVS)
- `touch_cal clear` removes calibration, reverts to raw

**Step 6: Commit**

```bash
git add main/ili9488-test.c
git commit -m "feat: add 3-point touch calibration with REPL commands

Interactive crosshair calibration screen triggered via 'touch_cal start'.
Coefficients displayed with 'touch_cal show', cleared with 'touch_cal clear'.
Calibration persists across reboots via NVS."
```

---

## Task 8: Final Integration and Cleanup

**Files:**
- Modify: `main/ili9488-test.c` — minor cleanup
- Create: `iteration/spec.md` — already done
- Create: `iteration/plan.md` — this file

**Step 1: Flash and run full integration test**

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Full test checklist:
- [ ] Boot shows NVS init, touch IRQ enabled
- [ ] Light finger touch registers (Z-threshold = 100)
- [ ] Touch coordinates are stable (median filtering)
- [ ] No SPI polling when screen is idle (IRQ-driven)
- [ ] `touch_cfg z_threshold 50` makes it even more sensitive
- [ ] `touch_cfg z_threshold 300` reduces sensitivity
- [ ] `touch_cal start` launches crosshair calibration
- [ ] After calibrating, button taps work accurately
- [ ] `touch_cal show` displays coefficients
- [ ] `touch_cal clear` removes calibration
- [ ] Reboot preserves calibration
- [ ] `debug` toggle still works
- [ ] `info` command still works
- [ ] Existing `rotation` command still works

**Step 2: Commit final state if any tweaks were needed**

```bash
git add -A
git commit -m "feat: iteration 5 complete — improved touch interface

Forked XPT2046 driver, lowered Z-threshold for finger use,
added median filtering, IRQ-driven detection, and 3-point
affine calibration with NVS persistence."
```
