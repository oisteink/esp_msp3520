# Iteration 2 Research: LVGL Integration

## Sources

- LVGL v9.5.0 managed component (`managed_components/lvgl__lvgl/`)
- ESP-IDF v5.5.3 esp_lcd and SPI APIs
- Reference implementation: `~/src/zenith/zenith_components/zenith_display/zenith_display.c`

## LVGL v9.5 API (verified)

### Display creation and configuration

```c
lv_display_t *lv_display_create(int32_t hor_res, int32_t ver_res);
void lv_display_set_color_format(lv_display_t *disp, lv_color_format_t color_format);
void lv_display_set_buffers(lv_display_t *disp, void *buf1, void *buf2, uint32_t buf_size,
                            lv_display_render_mode_t render_mode);
void lv_display_set_flush_cb(lv_display_t *disp, lv_display_flush_cb_t flush_cb);
void lv_display_set_user_data(lv_display_t *disp, void *user_data);
void *lv_display_get_user_data(lv_display_t *disp);
```

### Flush completion (ISR-safe)

```c
LV_ATTRIBUTE_FLUSH_READY void lv_display_flush_ready(lv_display_t *disp);
```

### Tick and timer

```c
LV_ATTRIBUTE_TICK_INC void lv_tick_inc(uint32_t tick_period);  // call from timer ISR
uint32_t lv_timer_handler(void);  // returns ms until next run needed
```

No built-in ESP timer integration — must create `esp_timer` manually.

### Render modes

```c
LV_DISPLAY_RENDER_MODE_PARTIAL  // buffer smaller than screen, required for RAM-constrained
LV_DISPLAY_RENDER_MODE_DIRECT   // screen-sized buffer, only dirty areas updated
LV_DISPLAY_RENDER_MODE_FULL     // screen-sized buffer, always full redraw
```

### Color format

`LV_COLOR_FORMAT_RGB888` (0x0F) — 3 bytes/pixel. Matches ILI9488's RGB666 over SPI (upper 6 bits of each byte used).

## Configuration: Kconfig, not lv_conf.h

LVGL v9.5 on ESP-IDF uses **Kconfig exclusively** — no `lv_conf.h` needed. Configuration via `CONFIG_LV_*` symbols from sdkconfig. Default `LV_CONF_SKIP=y` skips the custom header.

Key default: `LV_MEM_SIZE_KILOBYTES=64` — LVGL's internal heap. May need tuning if we're tight on RAM.

## ESP-IDF APIs (verified)

### SPI DMA buffer allocation

```c
void *spi_bus_dma_memory_alloc(spi_host_device_t host_id, size_t size, uint32_t extra_heap_caps);
```

Allocates from DMA-capable memory pool. Better than `heap_caps_malloc(MALLOC_CAP_DMA)` because it handles SPI-specific alignment requirements.

### LCD panel IO callback registration

```c
typedef bool (*esp_lcd_panel_io_color_trans_done_cb_t)(
    esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_io_event_data_t *edata,
    void *user_ctx);

typedef struct {
    esp_lcd_panel_io_color_trans_done_cb_t on_color_trans_done;
} esp_lcd_panel_io_callbacks_t;

esp_err_t esp_lcd_panel_io_register_event_callbacks(
    esp_lcd_panel_io_handle_t io,
    const esp_lcd_panel_io_callbacks_t *cbs,
    void *user_ctx);
```

Use this instead of setting `on_color_trans_done` in the SPI config. Pass the `lv_display_t*` as `user_ctx` so the callback can call `lv_display_flush_ready()`.

## Memory budget (ESP32-C6)

- Total SRAM: 320 KB (shared instruction/data)
- Typical free heap after boot: ~200-250 KB
- LVGL internal heap: 64 KB (default)
- Full framebuffer (480x320x3): 460 KB — **impossible**

### Buffer sizing

Using PARTIAL mode with 32-line buffers:
- Per buffer: 320 × 32 × 3 = 30,720 bytes (~30 KB)
- Dual buffers: ~60 KB total
- Combined with LVGL heap (64 KB): ~124 KB — fits within available heap

The reference project uses 48-line dual buffers (~92 KB) on ESP32-S3 which has PSRAM. We use 32 lines to be safe on the C6.

Single buffer is also viable if memory is tight — dual just allows LVGL to render the next frame while the current one is being DMA'd.

## Landscape orientation

The ILI9488 native orientation is portrait (320x480). For landscape (480x320):

```c
esp_lcd_panel_swap_xy(panel, true);
esp_lcd_panel_mirror(panel, false, false);  // may need adjustment based on connector orientation
```

Handle rotation at the **hardware level** (panel driver MADCTL) not LVGL software rotation, to avoid double-rotation. Create the LVGL display as `lv_display_create(480, 320)` — already in landscape coordinates.

The exact mirror flags may need trial-and-error on the physical hardware depending on how the display is mounted.

## Flush callback pattern

From the reference implementation — the flush callback is minimal:

```c
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = lv_display_get_user_data(disp);
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, px_map);
}
```

No color conversion needed — LVGL outputs RGB888, ILI9488 accepts it as RGB666 (ignoring lower 2 bits of each byte).

Completion signaled by the SPI DMA done callback:

```c
static bool flush_ready_cb(esp_lcd_panel_io_handle_t panel_io,
                            esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_display_flush_ready((lv_display_t *)user_ctx);
    return false;
}
```

## LVGL task pattern

```c
static void lvgl_task(void *arg)
{
    while (1) {
        lock_acquire(&lvgl_lock);
        uint32_t next_ms = lv_timer_handler();
        lock_release(&lvgl_lock);
        vTaskDelay(pdMS_TO_TICKS(MAX(next_ms, 1)));
    }
}
```

Task priority 2, stack 4KB. Mutex needed if any other task touches LVGL APIs.

## Gotchas

| Issue | Mitigation |
|-------|------------|
| No `lv_conf.h` needed | Kconfig handles everything |
| DMA alignment | Use `spi_bus_dma_memory_alloc()` not `heap_caps_malloc()` |
| Double rotation | Rotate via panel driver only, not LVGL |
| Callback returns bool | Return `false` from flush_ready_cb (no high-priority task woken) |
| LVGL internal heap (64KB) | Monitor with `lv_mem_monitor()` if issues arise |
| Buffer width is 320 (short axis) in landscape | After swap_xy, the "horizontal" SPI transfer width is the physical 320px side |
