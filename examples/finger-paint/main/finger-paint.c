#include "msp3520.h"
#include "nvs_flash.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include <stdlib.h>

static const char *TAG = "finger-paint";

/* -- Config -------------------------------------------------------- */

#define TOOLBAR_H   40
#define CANVAS_W    MSP3520_H_RES                       /* 320 */
#define CANVAS_H    (MSP3520_V_RES - TOOLBAR_H)         /* 440 */
#define BRUSH_W     3
#define GRID_STEP   40

/* -- State --------------------------------------------------------- */

static lv_obj_t *canvas;
static uint8_t *canvas_buf;
static lv_color_t current_color;
static bool has_prev;
static int32_t prev_x, prev_y;

/* -- Direct buffer drawing ----------------------------------------- */

static inline void buf_set_px(int32_t x, int32_t y, lv_color_t c)
{
    if (x < 0 || x >= CANVAS_W || y < 0 || y >= CANVAS_H) return;
    uint8_t *p = canvas_buf + (y * CANVAS_W + x) * 3;
    p[0] = c.blue;
    p[1] = c.green;
    p[2] = c.red;
}

/* Draw a filled circle of radius r at (cx, cy) directly into the buffer */
static void buf_fill_circle(int32_t cx, int32_t cy, int32_t r, lv_color_t c)
{
    for (int32_t dy = -r; dy <= r; dy++) {
        for (int32_t dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r) {
                buf_set_px(cx + dx, cy + dy, c);
            }
        }
    }
}

/* Bresenham line with round brush (filled circle at each point) */
static void buf_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                          int32_t w, lv_color_t c)
{
    int32_t r = w / 2;
    int32_t dx = abs(x1 - x0);
    int32_t dy = -abs(y1 - y0);
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy;

    while (1) {
        buf_fill_circle(x0, y0, r, c);
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* Invalidate only the bounding box of a line segment (canvas-local coords) */
static void invalidate_line_area(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w)
{
    int32_t r = w / 2 + 1;
    int32_t min_x = (x0 < x1 ? x0 : x1) - r;
    int32_t min_y = (y0 < y1 ? y0 : y1) - r;
    int32_t max_x = (x0 > x1 ? x0 : x1) + r;
    int32_t max_y = (y0 > y1 ? y0 : y1) + r;

    /* Clamp to canvas bounds */
    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= CANVAS_W) max_x = CANVAS_W - 1;
    if (max_y >= CANVAS_H) max_y = CANVAS_H - 1;

    /* Area is in canvas-local coords; canvas is at (0, TOOLBAR_H) on screen */
    lv_area_t area = {min_x, min_y + TOOLBAR_H, max_x, max_y + TOOLBAR_H};
    lv_obj_invalidate_area(canvas, &area);
}

/* -- Drawing helpers (use layer API only for init/clear) ------------ */

static void draw_border_and_grid(void)
{
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    /* Border: 1px rectangle at canvas edges */
    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_opa = LV_OPA_TRANSP;
    rect.border_color = lv_color_make(0xCC, 0xCC, 0xCC);
    rect.border_width = 1;
    rect.border_opa = LV_OPA_COVER;

    lv_area_t area = {0, 0, CANVAS_W - 1, CANVAS_H - 1};
    lv_draw_rect(&layer, &rect, &area);

    /* Grid lines */
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_make(0xEE, 0xEE, 0xEE);
    line.width = 1;

    for (int x = GRID_STEP; x < CANVAS_W; x += GRID_STEP) {
        line.p1 = (lv_point_precise_t){x, 0};
        line.p2 = (lv_point_precise_t){x, CANVAS_H - 1};
        lv_draw_line(&layer, &line);
    }
    for (int y = GRID_STEP; y < CANVAS_H; y += GRID_STEP) {
        line.p1 = (lv_point_precise_t){0, y};
        line.p2 = (lv_point_precise_t){CANVAS_W - 1, y};
        lv_draw_line(&layer, &line);
    }

    lv_canvas_finish_layer(canvas, &layer);
}

static void clear_canvas(void)
{
    lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);
    draw_border_and_grid();
}

/* -- Event handlers ------------------------------------------------ */

static void canvas_press_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);

    /* Convert screen coords to canvas-local coords */
    int32_t cx = p.x;
    int32_t cy = p.y - TOOLBAR_H;

    if (cx < 0 || cx >= CANVAS_W || cy < 0 || cy >= CANVAS_H) return;

    if (has_prev) {
        buf_draw_line(prev_x, prev_y, cx, cy, BRUSH_W, current_color);
        invalidate_line_area(prev_x, prev_y, cx, cy, BRUSH_W);
    } else {
        /* First point of a new stroke: draw a dot */
        buf_fill_circle(cx, cy, BRUSH_W / 2, current_color);
        invalidate_line_area(cx, cy, cx, cy, BRUSH_W);
    }

    prev_x = cx;
    prev_y = cy;
    has_prev = true;
}

static void canvas_release_cb(lv_event_t *e)
{
    has_prev = false;
}

static void clear_btn_cb(lv_event_t *e)
{
    clear_canvas();
    has_prev = false;
}

static void color_btn_cb(lv_event_t *e)
{
    lv_color_t *color = lv_event_get_user_data(e);
    current_color = *color;
}

/* -- UI ------------------------------------------------------------ */

static lv_color_t swatch_colors[] = {
    {.red = 0x00, .green = 0x00, .blue = 0x00},  /* black */
    {.red = 0xE0, .green = 0x20, .blue = 0x20},  /* red */
    {.red = 0x20, .green = 0xA0, .blue = 0x20},  /* green */
    {.red = 0x20, .green = 0x40, .blue = 0xE0},  /* blue */
    {.red = 0xFF, .green = 0xFF, .blue = 0xFF},  /* white (eraser) */
};
#define NUM_SWATCHES (sizeof(swatch_colors) / sizeof(swatch_colors[0]))

static void create_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_make(0x30, 0x30, 0x30), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    /* -- Toolbar --------------------------------------------------- */

    lv_obj_t *toolbar = lv_obj_create(scr);
    lv_obj_set_size(toolbar, MSP3520_H_RES, TOOLBAR_H);
    lv_obj_align(toolbar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(toolbar, lv_color_make(0x30, 0x30, 0x30), 0);
    lv_obj_set_style_border_width(toolbar, 0, 0);
    lv_obj_set_style_radius(toolbar, 0, 0);
    lv_obj_set_style_pad_all(toolbar, 4, 0);
    lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toolbar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(toolbar, LV_SCROLLBAR_MODE_OFF);

    /* Clear button */
    lv_obj_t *clear_btn = lv_button_create(toolbar);
    lv_obj_set_size(clear_btn, 60, 32);
    lv_obj_add_event_cb(clear_btn, clear_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *clear_lbl = lv_label_create(clear_btn);
    lv_label_set_text(clear_lbl, "Clear");
    lv_obj_center(clear_lbl);

    /* Color swatches */
    for (int i = 0; i < NUM_SWATCHES; i++) {
        lv_obj_t *btn = lv_button_create(toolbar);
        lv_obj_set_size(btn, 32, 32);
        lv_obj_set_style_bg_color(btn, swatch_colors[i], 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_set_style_border_color(btn, lv_color_make(0x80, 0x80, 0x80), 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, color_btn_cb, LV_EVENT_CLICKED, &swatch_colors[i]);
    }

    /* -- Canvas ---------------------------------------------------- */

    canvas = lv_canvas_create(scr);
    lv_obj_align(canvas, LV_ALIGN_TOP_LEFT, 0, TOOLBAR_H);

    canvas_buf = heap_caps_malloc(CANVAS_W * CANVAS_H * 3, MALLOC_CAP_SPIRAM);
    assert(canvas_buf);
    lv_canvas_set_buffer(canvas, canvas_buf, CANVAS_W, CANVAS_H, LV_COLOR_FORMAT_RGB888);

    clear_canvas();

    lv_obj_add_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(canvas, canvas_press_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(canvas, canvas_release_cb, LV_EVENT_RELEASED, NULL);

    /* Default color: black */
    current_color = swatch_colors[0];
}

/* -- Main ---------------------------------------------------------- */

void app_main(void)
{
    /* NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* MSP3520 display + touch */
    msp3520_config_t cfg = MSP3520_CONFIG_DEFAULT();
    msp3520_handle_t display;
    ESP_ERROR_CHECK(msp3520_create(&cfg, &display));

    /* Build UI */
    msp3520_lvgl_lock(display, 0);
    create_ui();
    msp3520_lvgl_unlock(display);

    /* Console */
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "paint> ";
    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl));

    esp_console_register_help_command();
    msp3520_register_console_commands(display);

    ESP_LOGI(TAG, "starting REPL");
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
