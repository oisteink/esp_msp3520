# Iteration 1: Research

Builds on: `iteration/spec.md`

## 1. esp_lcd Framework (ESP-IDF v5.5.3)

### Panel Interface

`esp_lcd_panel_t` (`components/esp_lcd/interface/esp_lcd_panel_interface.h`):

```c
struct esp_lcd_panel_t {
    esp_err_t (*reset)(esp_lcd_panel_t *panel);
    esp_err_t (*init)(esp_lcd_panel_t *panel);
    esp_err_t (*del)(esp_lcd_panel_t *panel);
    esp_err_t (*draw_bitmap)(esp_lcd_panel_t *panel, int x_start, int y_start,
                             int x_end, int y_end, const void *color_data);
    esp_err_t (*mirror)(esp_lcd_panel_t *panel, bool x_axis, bool y_axis);
    esp_err_t (*swap_xy)(esp_lcd_panel_t *panel, bool swap_axes);
    esp_err_t (*set_gap)(esp_lcd_panel_t *panel, int x_gap, int y_gap);
    esp_err_t (*invert_color)(esp_lcd_panel_t *panel, bool invert_color_data);
    esp_err_t (*disp_on_off)(esp_lcd_panel_t *panel, bool on_off);
    esp_err_t (*disp_sleep)(esp_lcd_panel_t *panel, bool sleep);
    void *user_data;
};
```

Note: `disp_sleep` exists in v5.5.3 but is not implemented in the reference driver. We can leave it NULL for now.

### Panel Device Config

`esp_lcd_panel_dev_config_t` (`components/esp_lcd/include/esp_lcd_panel_dev.h`):

```c
typedef struct {
    int reset_gpio_num;
    union {
        esp_lcd_color_space_t color_space;     // DEPRECATED
        lcd_color_rgb_endian_t rgb_endian;     // DEPRECATED
        lcd_rgb_element_order_t rgb_ele_order; // USE THIS
    };
    lcd_rgb_data_endian_t data_endian;
    uint32_t bits_per_pixel;
    struct {
        uint32_t reset_active_high: 1;
    } flags;
    void *vendor_config;
} esp_lcd_panel_dev_config_t;
```

`vendor_config` can carry driver-specific options. The reference driver doesn't use it.

### SPI Panel IO Config

`esp_lcd_panel_io_spi_config_t` (`components/esp_lcd/include/esp_lcd_io_spi.h`):

Key fields: `cs_gpio_num`, `dc_gpio_num`, `pclk_hz`, `spi_mode`, `trans_queue_depth`, `lcd_cmd_bits` (8), `lcd_param_bits` (8), `on_color_trans_done` callback, `user_ctx`.

The `on_color_trans_done` callback fires when DMA completes a color transfer. Signature:
```c
bool (*)(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *user_ctx);
```
Returns true if a high-priority task was woken. This is how LVGL gets notified that a flush is complete.

### SPI Transfer Internals

- `tx_param` (commands/params): synchronous, polling, no DMA
- `tx_color` (pixel data): async, queued, DMA-enabled
- Large color transfers are automatically chunked to `max_transfer_sz`
- `on_color_trans_done` fires only on the final chunk

### LCD Command Definitions

From `esp_lcd_panel_commands.h`:

| Define | Value | Purpose |
|--------|-------|---------|
| `LCD_CMD_SWRESET` | 0x01 | Software reset |
| `LCD_CMD_SLPOUT` | 0x11 | Sleep out |
| `LCD_CMD_INVOFF/INVON` | 0x20/0x21 | Color inversion |
| `LCD_CMD_DISPOFF/DISPON` | 0x28/0x29 | Display on/off |
| `LCD_CMD_CASET` | 0x2A | Column address set |
| `LCD_CMD_RASET` | 0x2B | Row address set |
| `LCD_CMD_RAMWR` | 0x2C | Memory write |
| `LCD_CMD_MADCTL` | 0x36 | Memory access control |
| `LCD_CMD_COLMOD` | 0x3A | Pixel format |

MADCTL bits: `LCD_CMD_MX_BIT` (bit 6), `LCD_CMD_MY_BIT` (bit 7), `LCD_CMD_MV_BIT` (bit 5), `LCD_CMD_BGR_BIT` (bit 3).

## 2. Reference Driver Analysis

Source: `~/src/zenith/zenith_components/esp_lcd_ili9488/`

### Architecture

Standard esp_lcd pattern: private `ili9488_panel_t` struct embeds `esp_lcd_panel_t base`, uses `__containerof()` to get private data from base pointer. Single public constructor `esp_lcd_new_panel_ili9488()` sets up function pointers.

### Private State

```c
typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    bool reset_level;
    int x_gap, y_gap;
    uint8_t memory_access_control;  // MADCTL register value, modified by mirror/swap
    uint8_t color_mode;             // 0x55 (16-bit) or 0x66 (18-bit)
    uint8_t bits_per_pixel;         // 16 or 24 (internal)
} ili9488_panel_t;
```

### Color Mode Mapping

| Input `bits_per_pixel` | Internal `bits_per_pixel` | Register value | Wire format |
|------------------------|--------------------------|----------------|-------------|
| 16 | 16 | 0x55 (RGB565) | 2 bytes/px (parallel only) |
| 18 or 24 | 24 | 0x66 (RGB666) | 3 bytes/px |

For SPI, must use 18 or 24. The driver normalizes both to 24 internally.

### Init Sequence

ILI9488-specific registers configured during `panel_ili9488_init()`:

| Register | Value | Purpose |
|----------|-------|---------|
| 0xC0 (Power Ctl 1) | 0x17, 0x15 | Gamma voltages: +5V, -4.875V |
| 0xC1 (Power Ctl 2) | 0x41 | VGH = VCI×6, VGL = -(VCI×4) |
| 0xC5 (VCOM Ctl) | 0x00, 0x12, 0x80 | VCOM = -1.71875V |
| 0x36 (MADCTL) | dynamic | Mirror/swap/BGR from config |
| 0x3A (COLMOD) | 0x55 or 0x66 | Color mode from config |
| 0xB0 (Interface Mode) | 0x00 | Use SDO (but see MISO note) |
| 0xB1 (Frame Rate) | 0xA0 | 60 Hz |
| 0xB4 (Inversion) | 0x02 | 2-dot inversion |
| 0xB6 (Function Ctl) | 0x02, 0x02, 0x3B | 480 lines, 5-frame scan |
| 0xB7 (Entry Mode) | 0xC6 | 16→18 bit conversion, low voltage detect |
| 0xF7 (Adjust Ctl 3) | 0xA9, 0x51, 0x2C, 0x02 | RGB666 stream packet |

Then: SLPOUT (100ms delay), DISPON (100ms delay).

Gamma correction is commented out — "Initial gamma is OK".

### draw_bitmap

Sets column/row address window (CASET/RASET), then sends pixel data via `esp_lcd_panel_io_tx_color()`. End coordinates are exclusive (decremented by 1 in the SEND_COORDS macro). Data length = `width × height × (bits_per_pixel / 8)`.

### Mirror Logic (Gotcha)

The mirror_x logic is inverted from what you'd expect:
- `mirror_x=true` → CLEARS MX_BIT
- `mirror_x=false` → SETS MX_BIT

This compensates for the ILI9488's default column order. mirror_y follows the intuitive direction.

### MISO Warning

Comment in init: "MISO is not proper tri-state on my screen: do not wire". The display's SDO pin may cause SPI bus contention. Write-only SPI is recommended.

### Usage in Zenith

From `zenith_display` and `zenith_ui_core`:
- SPI clock: 20 MHz
- SPI mode: 0
- DMA: `SPI_DMA_CH_AUTO`
- `bits_per_pixel`: 24
- `rgb_ele_order`: `LCD_RGB_ELEMENT_ORDER_RGB`
- `trans_queue_depth`: 10
- `max_transfer_sz`: 320 × 48 × 3 = 46,080 bytes (matches LVGL draw buffer)
- Backlight: LEDC PWM, 5 kHz, 10-bit resolution

## 3. ESP-IDF Build System

### Component Auto-Discovery

The `components/` directory in the project root is automatically scanned by ESP-IDF's build system (`project.cmake` line 479). No explicit registration needed — just place the component there with a `CMakeLists.txt`.

### Component CMakeLists.txt

```cmake
idf_component_register(
    SRCS "esp_lcd_ili9488.c"
    INCLUDE_DIRS "include"
    REQUIRES "esp_lcd"               # Public dependency
    PRIV_REQUIRES "esp_driver_gpio"  # Private dependency
)
```

`REQUIRES` = public deps exposed to consumers. `PRIV_REQUIRES` = internal deps.

For ESP-IDF v5.3+, GPIO and SPI drivers are in separate components (`esp_driver_gpio`, `esp_driver_spi`), not the monolithic `driver`.

### Kconfig: Component vs Application

- **`Kconfig`** (in component dir): appears under Component config in menuconfig
- **`Kconfig.projbuild`** (in main/ or component dir): appears at the root level of menuconfig

For pin configuration, `Kconfig.projbuild` in `main/` is the right place.

### GPIO Kconfig Pattern

From ESP-IDF examples:

```kconfig
config EXAMPLE_SPI_SCLK_GPIO
    int "SPI SCLK GPIO number"
    range ENV_GPIO_RANGE_MIN ENV_GPIO_OUT_RANGE_MAX
    default 7
    help
        GPIO number for SPI clock.
```

`ENV_GPIO_RANGE_MIN` and `ENV_GPIO_OUT_RANGE_MAX` are target-dependent environment variables set by ESP-IDF, ensuring valid GPIO ranges per chip.

### sdkconfig.defaults

- Placed in project root
- Simple `KEY=value` format
- Loaded before menuconfig; `sdkconfig` takes precedence if it exists
- Target-specific variants: `sdkconfig.defaults.esp32c6`, `sdkconfig.defaults.esp32s3`

### SPI on ESP32-C6

- 2 SPI peripherals: SPI1 (flash, unavailable) and SPI2 (general purpose)
- **Only SPI2_HOST is available** for display/touch
- Supports DMA, up to 6 CS lines per host
- Both display and touch can share SPI2_HOST with separate CS lines (for future iteration)

## 4. LVGL Integration (Future Reference)

Not in scope for this iteration, but critical to get right now so the driver is compatible.

### Component

We use **`lvgl/lvgl^9.5.0`** from [components.espressif.com](https://components.espressif.com/components/lvgl/lvgl/versions/9.5.0). Already added to the project. No port layer — we wire LVGL to esp_lcd manually.

### Integration Pattern

From the porting template (`managed_components/lvgl__lvgl/examples/porting/lv_port_disp_template.c`):

```c
// 1. Create display
lv_display_t *disp = lv_display_create(480, 320);

// 2. Set flush callback (calls esp_lcd_panel_draw_bitmap internally)
lv_display_set_flush_cb(disp, my_flush_cb);

// 3. Set color format — RGB888 maps directly to ILI9488's RGB666 over SPI
lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB888);

// 4. Allocate and set draw buffers (partial rendering)
static uint8_t buf[480 * 32 * 3];  // 32 lines at 3 bytes/pixel
lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
```

The flush callback:
```c
static void my_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    // lv_display_flush_ready() called from on_color_trans_done DMA callback
}
```

DMA completion signaling: the `on_color_trans_done` callback in `esp_lcd_panel_io_spi_config_t` calls `lv_display_flush_ready(disp)` when the SPI transfer finishes. This is set up by the app, not the driver.

### Color Format

LVGL v9.5 supports `LV_COLOR_FORMAT_RGB888` (24-bit, 3 bytes/pixel). The ILI9488 in RGB666 mode uses 3 bytes/pixel where the upper 6 bits of each byte are significant. RGB888 output from LVGL maps directly — no conversion layer needed. The display simply ignores the lowest 2 bits of each byte.

This eliminates the need for the RGB565→RGB888 conversion that the atanisoft driver does. Simpler driver, no extra conversion buffer.

### Buffer Sizing

480 × 320 × 3 = 460,800 bytes — far exceeds available RAM. Must use partial rendering.

| Lines | Buffer size | % of screen |
|-------|------------|-------------|
| 16 | 23 KB | 5% |
| 32 | 46 KB | 10% |
| 48 | 69 KB | 15% |

LVGL recommends at least 1/10 screen. Double-buffering (two 32-line buffers = 92 KB) allows LVGL to render the next chunk while DMA sends the current one.

### What Our Driver Needs for LVGL Compatibility

1. Implement `esp_lcd_panel_t` — this is the contract LVGL's flush callback expects
2. `draw_bitmap` must work with async DMA (`esp_lcd_panel_io_tx_color`)
3. The `on_color_trans_done` callback is set by the app, not the driver
4. Driver accepts 24-bit pixel data and sends it as-is — no color conversion needed

## 5. Key Decisions for Implementation

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Color mode | 18-bit (RGB666), `bits_per_pixel=24` | Only mode supported over SPI |
| SPI host | SPI2_HOST | Only GP SPI available on C6 |
| MISO | Not wired / -1 | Tri-state issue, write-only sufficient |
| Pin config | Kconfig.projbuild + sdkconfig.defaults | Easy board switching |
| IDF v4 compat | Drop it | We target v5.5.3 only |
| Gamma correction | Use defaults | Reference driver confirms defaults are fine |
| `disp_sleep` callback | NULL (not implemented) | Not needed for demo |
| Color conversion | None needed | LVGL RGB888 maps directly to ILI9488 RGB666 |
| LVGL integration | Manual wiring to `lvgl/lvgl^9.5.0` | No port layer, we control flush/tick/task |
| `vendor_config` | Not used | No driver-specific config needed yet |
