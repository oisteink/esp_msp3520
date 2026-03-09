# Iteration 4 Research: REPL Console

**Ref:** `iteration/spec.md`

## esp_console API

### REPL setup

Console is configured as `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` (UART0, 115200). Use UART REPL:

```c
#include "esp_console.h"

esp_console_repl_t *repl = NULL;
esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
repl_cfg.prompt = "tft> ";
esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();

esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl);
// register commands here...
esp_console_start_repl(repl);  // spawns REPL task, non-blocking
```

The REPL handles its own task, linenoise, history, and completion. `help` command is registered via `esp_console_register_help_command()`.

### Command registration

```c
esp_console_cmd_t cmd = {
    .command = "info",
    .help = "Show system info (heap, uptime)",
    .hint = NULL,
    .func = cmd_info,           // int (*)(int argc, char **argv)
};
esp_console_cmd_register(&cmd);
```

For commands needing context (touch/display handles), use `func_w_context`:

```c
esp_console_cmd_t cmd = {
    .command = "touch_cal",
    .help = "Get/set touch coordinate flags",
    .hint = "[swap_xy|mirror_x|mirror_y] [0|1]",
    .func_w_context = cmd_touch_cal,  // int (*)(void *ctx, int argc, char **argv)
    .context = touch_handle,
};
```

### CMakeLists dependency

Add `"console"` to REQUIRES. The `console` component includes linenoise and argtable3.

## Command implementations

### `log_level <tag> <level>`

```c
#include "esp_log.h"
// esp_log_level_set(tag, level) — levels: NONE=0, ERROR=1, WARN=2, INFO=3, DEBUG=4, VERBOSE=5
```

Parse level string to `esp_log_level_t`. Standard approach.

### `info`

```c
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_timer.h"

esp_get_free_heap_size();
esp_get_minimum_free_heap_size();
esp_timer_get_time();  // microseconds since boot
esp_chip_info_t info; esp_chip_info(&info);
```

### `touch_cal`

Runtime get/set via `esp_lcd_touch` API:
- `esp_lcd_touch_get_swap_xy(handle, &val)` / `esp_lcd_touch_set_swap_xy(handle, val)`
- `esp_lcd_touch_get_mirror_x(handle, &val)` / `esp_lcd_touch_set_mirror_x(handle, val)`
- `esp_lcd_touch_get_mirror_y(handle, &val)` / `esp_lcd_touch_set_mirror_y(handle, val)`

### `rotation`

Runtime get/set via our `esp_lcd_panel` driver:
- `esp_lcd_panel_swap_xy(panel, val)`
- `esp_lcd_panel_mirror(panel, mirror_x, mirror_y)`

Note: our driver doesn't have getters for current state. We'll need to track state in a struct or add getters. Simpler to track in a static struct in the app.

### `debug`

Shortcut to set multiple tags at once:
```c
esp_log_level_set("app", ESP_LOG_DEBUG);
esp_log_level_set("ili9488", ESP_LOG_DEBUG);
esp_log_level_set("XPT2046", ESP_LOG_DEBUG);
```

Toggle behavior: call once for DEBUG, call again for INFO.

## Key decisions

1. **UART REPL** — matches current console config, works with `idf.py monitor`
2. **Context passing** — use `func_w_context` for commands that need touch/display handles, pack into a struct
3. **Display state tracking** — track mirror/swap state in app since driver has no getters
4. **REPL prompt** — `"tft> "` to distinguish from ESP log output
5. **REPL stack** — default 4096 should be fine for simple commands
