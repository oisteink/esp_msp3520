# REPL Console — Implementation Plan

**Ref:** `iteration/spec.md`, `iteration/research.md`

**Goal:** Add UART REPL with diagnostic and config commands.

---

### Task 1: Add console dependency

**Files:**
- Modify: `main/CMakeLists.txt`

Add `"console"` to REQUIRES:

```cmake
idf_component_register(SRCS "ili9488-test.c"
                    INCLUDE_DIRS "."
                    REQUIRES "esp_lcd_ili9488" "esp_driver_spi" "lvgl" "esp_timer"
                             "atanisoft__esp_lcd_touch_xpt2046" "espressif__esp_lcd_touch"
                             "console")
```

Build to verify.

---

### Task 2: Add console commands and wire into app

**Files:**
- Modify: `main/ili9488-test.c`

**Context struct for commands that need handles:**

```c
typedef struct {
    esp_lcd_panel_handle_t panel;
    esp_lcd_touch_handle_t touch;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
} app_context_t;

static app_context_t app_ctx;
```

**Command: `log_level <tag> <level>`**

```c
static int cmd_log_level(int argc, char **argv)
{
    if (argc != 3) {
        printf("Usage: log_level <tag> <none|error|warn|info|debug|verbose>\n");
        return 1;
    }
    const char *tag = argv[1];
    const char *lvl = argv[2];
    esp_log_level_t level;
    if      (strcmp(lvl, "none") == 0)    level = ESP_LOG_NONE;
    else if (strcmp(lvl, "error") == 0)   level = ESP_LOG_ERROR;
    else if (strcmp(lvl, "warn") == 0)    level = ESP_LOG_WARN;
    else if (strcmp(lvl, "info") == 0)    level = ESP_LOG_INFO;
    else if (strcmp(lvl, "debug") == 0)   level = ESP_LOG_DEBUG;
    else if (strcmp(lvl, "verbose") == 0) level = ESP_LOG_VERBOSE;
    else {
        printf("Unknown level: %s\n", lvl);
        return 1;
    }
    esp_log_level_set(tag, level);
    printf("Set %s to %s\n", tag, lvl);
    return 0;
}
```

**Command: `info`**

```c
static int cmd_info(int argc, char **argv)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    int64_t uptime_us = esp_timer_get_time();
    printf("Chip:      ESP32-S3 rev %d.%d, %d core(s)\n",
           chip.revision / 100, chip.revision % 100, chip.cores);
    printf("Heap free: %lu bytes\n", (unsigned long)esp_get_free_heap_size());
    printf("Heap min:  %lu bytes\n", (unsigned long)esp_get_minimum_free_heap_size());
    printf("Uptime:    %lld s\n", uptime_us / 1000000);
    return 0;
}
```

**Command: `touch_cal [flag] [0|1]`**

```c
static int cmd_touch_cal(void *ctx, int argc, char **argv)
{
    app_context_t *app = (app_context_t *)ctx;
    if (argc == 1) {
        bool swap, mx, my;
        esp_lcd_touch_get_swap_xy(app->touch, &swap);
        esp_lcd_touch_get_mirror_x(app->touch, &mx);
        esp_lcd_touch_get_mirror_y(app->touch, &my);
        printf("swap_xy=%d  mirror_x=%d  mirror_y=%d\n", swap, mx, my);
        return 0;
    }
    if (argc != 3) {
        printf("Usage: touch_cal [swap_xy|mirror_x|mirror_y] [0|1]\n");
        return 1;
    }
    bool val = atoi(argv[2]) != 0;
    if      (strcmp(argv[1], "swap_xy") == 0)  esp_lcd_touch_set_swap_xy(app->touch, val);
    else if (strcmp(argv[1], "mirror_x") == 0) esp_lcd_touch_set_mirror_x(app->touch, val);
    else if (strcmp(argv[1], "mirror_y") == 0) esp_lcd_touch_set_mirror_y(app->touch, val);
    else {
        printf("Unknown flag: %s\n", argv[1]);
        return 1;
    }
    printf("Set %s=%d\n", argv[1], val);
    return 0;
}
```

**Command: `rotation [swap_xy|mirror_x|mirror_y] [0|1]`**

```c
static int cmd_rotation(void *ctx, int argc, char **argv)
{
    app_context_t *app = (app_context_t *)ctx;
    if (argc == 1) {
        printf("swap_xy=%d  mirror_x=%d  mirror_y=%d\n",
               app->swap_xy, app->mirror_x, app->mirror_y);
        return 0;
    }
    if (argc != 3) {
        printf("Usage: rotation [swap_xy|mirror_x|mirror_y] [0|1]\n");
        return 1;
    }
    bool val = atoi(argv[2]) != 0;
    if (strcmp(argv[1], "swap_xy") == 0) {
        app->swap_xy = val;
        esp_lcd_panel_swap_xy(app->panel, val);
    } else if (strcmp(argv[1], "mirror_x") == 0) {
        app->mirror_x = val;
        esp_lcd_panel_mirror(app->panel, app->mirror_x, app->mirror_y);
    } else if (strcmp(argv[1], "mirror_y") == 0) {
        app->mirror_y = val;
        esp_lcd_panel_mirror(app->panel, app->mirror_x, app->mirror_y);
    } else {
        printf("Unknown flag: %s\n", argv[1]);
        return 1;
    }
    printf("Set %s=%d\n", argv[1], val);
    return 0;
}
```

**Command: `debug`**

```c
static int cmd_debug(int argc, char **argv)
{
    static bool debug_on = false;
    debug_on = !debug_on;
    esp_log_level_t lvl = debug_on ? ESP_LOG_DEBUG : ESP_LOG_INFO;
    esp_log_level_set("app", lvl);
    esp_log_level_set("ili9488", lvl);
    esp_log_level_set("XPT2046", lvl);
    printf("Debug %s\n", debug_on ? "ON" : "OFF");
    return 0;
}
```

**REPL init in app_main (at the end, after LVGL task):**

```c
// Initialize context
app_ctx.panel = panel;
app_ctx.touch = touch;
app_ctx.swap_xy = false;
app_ctx.mirror_x = true;   // matches current esp_lcd_panel_mirror(panel, true, true)
app_ctx.mirror_y = true;

// Console REPL
ESP_LOGI(TAG, "starting console");
esp_console_repl_t *repl = NULL;
esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
repl_cfg.prompt = "tft> ";
esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl));

esp_console_register_help_command();

// Register commands
esp_console_cmd_register(&(esp_console_cmd_t){
    .command = "log_level", .help = "Set log level: log_level <tag> <level>",
    .hint = "<tag> <none|error|warn|info|debug|verbose>", .func = cmd_log_level });
esp_console_cmd_register(&(esp_console_cmd_t){
    .command = "info", .help = "Show system info", .func = cmd_info });
esp_console_cmd_register(&(esp_console_cmd_t){
    .command = "touch_cal", .help = "Get/set touch flags",
    .hint = "[swap_xy|mirror_x|mirror_y] [0|1]",
    .func_w_context = cmd_touch_cal, .context = &app_ctx });
esp_console_cmd_register(&(esp_console_cmd_t){
    .command = "rotation", .help = "Get/set display rotation flags",
    .hint = "[swap_xy|mirror_x|mirror_y] [0|1]",
    .func_w_context = cmd_rotation, .context = &app_ctx });
esp_console_cmd_register(&(esp_console_cmd_t){
    .command = "debug", .help = "Toggle debug logging", .func = cmd_debug });

ESP_ERROR_CHECK(esp_console_start_repl(repl));
```

---

### Task 3: Flash and test

- [ ] `help` lists all commands
- [ ] `info` prints heap/uptime
- [ ] `debug` toggles debug, touch coordinates appear in log
- [ ] `log_level app debug` / `log_level app info` works
- [ ] `touch_cal` prints flags, `touch_cal mirror_x 1` changes mapping
- [ ] `rotation` prints flags, changing them updates display
- [ ] No interference with display/touch during command execution

---

### Task 4: Commit and push

```bash
git add -A
git commit -m "feat: add REPL console with diagnostic commands"
git push
```
