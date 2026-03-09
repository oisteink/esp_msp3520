/*
 * SPDX-FileCopyrightText: 2022 atanisoft (github.com/atanisoft)
 *
 * SPDX-License-Identifier: MIT
 */

#include <driver/gpio.h>
#include <esp_check.h>
#include <esp_err.h>
#include <esp_lcd_panel_io.h>
#include <esp_rom_gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
// This must be included after FreeRTOS includes due to missing include
// for portMUX_TYPE
#include <esp_lcd_touch.h>
#include <memory.h>
#include <stdatomic.h>

#include "sdkconfig.h"

static const char *TAG = "xpt2046";

#ifdef CONFIG_XPT2046_INTERRUPT_MODE
    #define XPT2046_PD0_BIT       (0x00)
#else
    #define XPT2046_PD0_BIT       (0x01)
#endif

#ifdef CONFIG_XPT2046_VREF_ON_MODE
    #define XPT2046_PD1_BIT   (0x02)
#else
    #define XPT2046_PD1_BIT   (0x00)
#endif

#define XPT2046_PD_BITS       (XPT2046_PD1_BIT | XPT2046_PD0_BIT)

enum xpt2046_registers
{
                                          // START  ADDR  MODE    SER/  VREF    ADC (PENIRQ)
                                          //              12/8b   DFR   INT/EXT ENA
    Z_VALUE_1   = 0xB0 | XPT2046_PD_BITS, // 1      011   0       0     X       X
    Z_VALUE_2   = 0xC0 | XPT2046_PD_BITS, // 1      100   0       0     X       X
    Y_POSITION  = 0x90 | XPT2046_PD_BITS, // 1      001   0       0     X       X
    X_POSITION  = 0xD0 | XPT2046_PD_BITS, // 1      101   0       0     X       X
    BATTERY     = 0xA6 | XPT2046_PD_BITS, // 1      010   0       1     1       X
    AUX_IN      = 0xE6 | XPT2046_PD_BITS, // 1      110   0       1     1       X
    TEMP0       = 0x86 | XPT2046_PD_BITS, // 1      000   0       1     1       X
    TEMP1       = 0xF6 | XPT2046_PD_BITS, // 1      111   0       1     1       X
};

#if CONFIG_XPT2046_ENABLE_LOCKING
#define XPT2046_LOCK(lock) portENTER_CRITICAL(lock)
#define XPT2046_UNLOCK(lock) portEXIT_CRITICAL(lock)
#else
#define XPT2046_LOCK(lock)
#define XPT2046_UNLOCK(lock)
#endif

static const uint16_t XPT2046_ADC_LIMIT = 4096;
// refer the TSC2046 datasheet https://www.ti.com/lit/ds/symlink/tsc2046.pdf rev F 2008
// TEMP0 reads approx 599.5 mV at 25C (Refer p8 TEMP0 diode voltage vs Vcc chart)
// Vref is approx 2.507V = 2507mV at moderate temperatures (refer p8 Vref vs Temperature chart)
// counts@25C = TEMP0_mV / Vref_mv * XPT2046_ADC_LIMIT
static const float XPT2046_TEMP0_COUNTS_AT_25C = (599.5 / 2507 * XPT2046_ADC_LIMIT);
static uint16_t xpt2046_z_threshold = CONFIG_XPT2046_Z_THRESHOLD;
static atomic_bool xpt2046_touch_pending = false;
static esp_err_t xpt2046_read_data(esp_lcd_touch_handle_t tp);
static bool xpt2046_get_xy(esp_lcd_touch_handle_t tp,
                           uint16_t *x, uint16_t *y,
                           uint16_t *strength,
                           uint8_t *point_num,
                           uint8_t max_point_num);
static esp_err_t xpt2046_del(esp_lcd_touch_handle_t tp);

void IRAM_ATTR esp_lcd_touch_xpt2046_notify_touch(void)
{
    atomic_store(&xpt2046_touch_pending, true);
}

esp_err_t esp_lcd_touch_new_spi_xpt2046(const esp_lcd_panel_io_handle_t io,
                                        const esp_lcd_touch_config_t *config,
                                        esp_lcd_touch_handle_t *out_touch)
{
    esp_err_t ret = ESP_OK;
    esp_lcd_touch_handle_t handle = NULL;

    ESP_GOTO_ON_FALSE(io, ESP_ERR_INVALID_ARG, err, TAG,
                      "esp_lcd_panel_io_handle_t must not be NULL");
    ESP_GOTO_ON_FALSE(config, ESP_ERR_INVALID_ARG, err, TAG,
                      "esp_lcd_touch_config_t must not be NULL");

    handle = (esp_lcd_touch_handle_t)calloc(1, sizeof(esp_lcd_touch_t));
    ESP_GOTO_ON_FALSE(handle, ESP_ERR_NO_MEM, err, TAG,
                      "No memory available for XPT2046 state");
    handle->io = io;
    handle->read_data = xpt2046_read_data;
    handle->get_xy = xpt2046_get_xy;
    handle->del = xpt2046_del;
    handle->data.lock.owner = portMUX_FREE_VAL;
    memcpy(&handle->config, config, sizeof(esp_lcd_touch_config_t));

    if (config->int_gpio_num != GPIO_NUM_NC)
    {
        ESP_GOTO_ON_FALSE(GPIO_IS_VALID_GPIO(config->int_gpio_num),
            ESP_ERR_INVALID_ARG, err, TAG, "Invalid GPIO Interrupt Pin");
        gpio_config_t cfg;
        memset(&cfg, 0, sizeof(gpio_config_t));
        esp_rom_gpio_pad_select_gpio(config->int_gpio_num);
        cfg.pin_bit_mask = BIT64(config->int_gpio_num);
        cfg.mode = GPIO_MODE_INPUT;
        cfg.intr_type = (config->levels.interrupt ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE);

        // If the user has provided a callback routine for the interrupt enable
        // the interrupt mode on the negative edge.
        if (config->interrupt_callback)
        {
            cfg.intr_type = GPIO_INTR_NEGEDGE;
        }

        ESP_GOTO_ON_ERROR(gpio_config(&cfg), err, TAG,
                          "Configure GPIO for Interrupt failed");

        // Connect the user interrupt callback routine.
        if (config->interrupt_callback)
        {
            esp_lcd_touch_register_interrupt_callback(handle, config->interrupt_callback);
        }

#if CONFIG_XPT2046_INTERRUPT_MODE
        // Read a register to enable Low Power mode, which is required for interrupt to work.
        uint8_t battery = 0;
        ESP_GOTO_ON_ERROR(esp_lcd_panel_io_rx_param(handle->io, BATTERY, &battery, 1), err, TAG, "XPT2046 read error!");
#endif

    }

err:
    if (ret != ESP_OK)
    {
        if (handle)
        {
            xpt2046_del(handle);
            handle = NULL;
        }
    }

    *out_touch = handle;

    return ret;
}

static esp_err_t xpt2046_del(esp_lcd_touch_handle_t tp)
{
    if (tp != NULL)
    {
        if (tp->config.int_gpio_num != GPIO_NUM_NC)
        {
            gpio_reset_pin(tp->config.int_gpio_num);
        }
    }
    free(tp);

    return ESP_OK;
}

static inline esp_err_t xpt2046_read_register(esp_lcd_touch_handle_t tp, uint8_t reg, uint16_t *value)
{
    uint8_t buf[2] = {0, 0};
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_rx_param(tp->io, reg, buf, 2), TAG, "XPT2046 read error!");
    *value = ((buf[0] << 8) | (buf[1]));
    return ESP_OK;
}

static esp_err_t xpt2046_read_data(esp_lcd_touch_handle_t tp)
{
    uint16_t z1 = 0, z2 = 0, z = 0;
    uint32_t x = 0, y = 0;
    uint8_t point_count = 0;

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

    ESP_RETURN_ON_ERROR(xpt2046_read_register(tp, Z_VALUE_1, &z1), TAG, "XPT2046 read error!");
    ESP_RETURN_ON_ERROR(xpt2046_read_register(tp, Z_VALUE_2, &z2), TAG, "XPT2046 read error!");

    // Convert the received values into a Z value.
    z = (z1 >> 3) + (XPT2046_ADC_LIMIT - (z2 >> 3));

    // If the Z (pressure) exceeds the threshold it is likely the user has
    // pressed the screen, read in and average the positions.
    if (z >= xpt2046_z_threshold)
    {
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
    }

    XPT2046_LOCK(&tp->data.lock);
    tp->data.coords[0].x = x;
    tp->data.coords[0].y = y;
    tp->data.coords[0].strength = z;
    tp->data.points = point_count;
    XPT2046_UNLOCK(&tp->data.lock);

    return ESP_OK;
}

static bool xpt2046_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y,
                           uint16_t *strength, uint8_t *point_num,
                           uint8_t max_point_num)
{
    XPT2046_LOCK(&tp->data.lock);

    // Determine how many touch points that are available.
    if (tp->data.points > max_point_num)
    {
        *point_num = max_point_num;
    }
    else
    {
        *point_num = tp->data.points;
    }

    for (size_t i = 0; i < *point_num; i++)
    {
        x[i] = tp->data.coords[i].x;
        y[i] = tp->data.coords[i].y;

        if (strength)
        {
            strength[i] = tp->data.coords[i].strength;
        }
    }

    // Invalidate stored touch data.
    tp->data.points = 0;

    XPT2046_UNLOCK(&tp->data.lock);

    if (*point_num)
    {
        ESP_LOGD(TAG, "Touch point: %dx%d", x[0], y[0]);
    }
    else
    {
        ESP_LOGV(TAG, "No touch points");
    }

    return (*point_num > 0);
}

void esp_lcd_touch_xpt2046_set_z_threshold(uint16_t threshold)
{
    xpt2046_z_threshold = threshold;
}

uint16_t esp_lcd_touch_xpt2046_get_z_threshold(void)
{
    return xpt2046_z_threshold;
}

esp_err_t esp_lcd_touch_xpt2046_read_battery_level(const esp_lcd_touch_handle_t handle, float *output)
{
    uint16_t level;
#ifndef CONFIG_XPT2046_VREF_ON_MODE
    // First read is to turn on the Vref, so it has extra time to stabilise before we read it for real
    ESP_RETURN_ON_ERROR(xpt2046_read_register(handle, BATTERY, &level), TAG, "XPT2046 read error!");
#endif
    // Read the battery voltage
    ESP_RETURN_ON_ERROR(xpt2046_read_register(handle, BATTERY, &level), TAG, "XPT2046 read error!");
    // drop lowest three bits to convert to 12-bit value
    level >>= 3;

    // battery voltage is reported as 1/4 the actual voltage due to logic in
    // the chip.
    *output = level * 4.0;

    // adjust for internal vref of 2.5v
    *output *= 2.507f;

    // adjust for ADC bit count
    *output /= 4096.0f;

#ifndef CONFIG_XPT2046_VREF_ON_MODE
    // Final read is to turn the Vref off
    ESP_RETURN_ON_ERROR(xpt2046_read_register(handle, Z_VALUE_1, &level), TAG, "XPT2046 read error!");
#endif

    return ESP_OK;
}

esp_err_t esp_lcd_touch_xpt2046_read_aux_level(const esp_lcd_touch_handle_t handle, float *output)
{
    uint16_t level;
#ifndef CONFIG_XPT2046_VREF_ON_MODE
    // First read is to turn on the Vref, so it has extra time to stabilise before we read it for real
    ESP_RETURN_ON_ERROR(xpt2046_read_register(handle, AUX_IN, &level), TAG, "XPT2046 read error!");
#endif
    // Read the aux input voltage
    ESP_RETURN_ON_ERROR(xpt2046_read_register(handle, AUX_IN, &level), TAG, "XPT2046 read error!");
    // drop lowest three bits to convert to 12-bit value
    level >>= 3;
    *output = level;

    // adjust for internal vref of 2.5v
    *output *= 2.507f;

    // adjust for ADC bit count
    *output /= 4096.0f;

#ifndef CONFIG_XPT2046_VREF_ON_MODE
    // Final read is to turn on the ADC and the Vref off
    ESP_RETURN_ON_ERROR(xpt2046_read_register(handle, Z_VALUE_1, &level), TAG, "XPT2046 read error!");
#endif

    return ESP_OK;
}

esp_err_t esp_lcd_touch_xpt2046_read_temp0_level(const esp_lcd_touch_handle_t handle, float *output)
{
    uint16_t temp0;
#ifndef CONFIG_XPT2046_VREF_ON_MODE
    // First read is to turn on the Vref, so it has extra time to stabilise before we read it for real
    ESP_RETURN_ON_ERROR(xpt2046_read_register(handle, TEMP0, &temp0), TAG, "XPT2046 read error!");
#endif
    // Read the temp0 value
    ESP_RETURN_ON_ERROR(xpt2046_read_register(handle, TEMP0, &temp0), TAG, "XPT2046 read error!");
    // drop lowest three bits to convert to 12-bit value
    temp0 >>= 3;
    *output = temp0;
    // Convert to temperature in degrees C
    *output = (XPT2046_TEMP0_COUNTS_AT_25C - *output) * (2.507 / 4096.0) / 0.0021 + 25.0;

#ifndef CONFIG_XPT2046_VREF_ON_MODE
    // Final read is to turn on the ADC and the Vref off
    ESP_RETURN_ON_ERROR(xpt2046_read_register(handle, Z_VALUE_1, &temp0), TAG, "XPT2046 read error!");
#endif

    return ESP_OK;
}

esp_err_t esp_lcd_touch_xpt2046_read_temp1_level(const esp_lcd_touch_handle_t handle, float *output)
{
    uint16_t temp0;
    uint16_t temp1;
#ifndef CONFIG_XPT2046_VREF_ON_MODE
    // First read is to turn on the Vref, so it has extra time to stabilise before we read it for real
    ESP_RETURN_ON_ERROR(xpt2046_read_register(handle, TEMP0, &temp0), TAG, "XPT2046 read error!");
#endif
    // Read the temp0 value
    ESP_RETURN_ON_ERROR(xpt2046_read_register(handle, TEMP0, &temp0), TAG, "XPT2046 read error!");
    // Read the temp1 value
    ESP_RETURN_ON_ERROR(xpt2046_read_register(handle, TEMP1, &temp1), TAG, "XPT2046 read error!");
    // drop lowest three bits to convert to 12-bit value
    temp0 >>= 3;
    temp1 >>= 3;
    *output = temp1 - temp0;
    *output = *output * 1000.0 * (2.507 / 4096.0) * 2.573 - 273.0;

#ifndef CONFIG_XPT2046_VREF_ON_MODE
    // Final read is to turn on the ADC and the Vref off
    ESP_RETURN_ON_ERROR(xpt2046_read_register(handle, Z_VALUE_1, &temp0), TAG, "XPT2046 read error!");
#endif

    return ESP_OK;
}
