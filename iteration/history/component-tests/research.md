# Research: MSP3520 Component Integration Tests

Builds on [spec.md](spec.md).

## ESP-IDF Component Test Infrastructure

### `test_apps/` Pattern (Current Standard)

Each test app is a standalone ESP-IDF project. **Not** discovered by `idf.py -T` — built independently from its own directory with `idf.py build`.

```
components/msp3520/test_apps/
    screen_protect/
        CMakeLists.txt
        sdkconfig.defaults
        main/
            CMakeLists.txt
            test_app_main.c
            test_screen_protect.c
```

**Project-level CMakeLists.txt:**
```cmake
cmake_minimum_required(VERSION 3.16)
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(msp3520_test)
```

**main/CMakeLists.txt — critical detail:**
```cmake
idf_component_register(
    SRCS "test_app_main.c" "test_screen_protect.c"
    PRIV_REQUIRES msp3520 unity
    WHOLE_ARCHIVE)    # Required! Without it, TEST_CASE constructors get dead-stripped
```

### Unity Framework

**Test registration:** Uses `__attribute__((constructor))` — tests auto-register, no manual listing.

```c
TEST_CASE("descriptive name", "[tag]")
{
    TEST_ASSERT_EQUAL(expected, actual);
    TEST_ESP_OK(some_esp_function());
}
```

**Test runner boilerplate:**
```c
void setUp(void) { unity_utils_record_free_mem(); }
void tearDown(void) { unity_utils_evaluate_leaks_direct(450); }
void app_main(void) { unity_run_menu(); }
```

`unity_run_menu()` presents interactive UART menu. Type number for specific test, `*` for all, `[tag]` for tagged group. Requires task watchdog disabled.

**sdkconfig.defaults:**
```
# CONFIG_ESP_TASK_WDT_INIT is not set
CONFIG_FREERTOS_HZ=1000
```

## LVGL Test Indev

### Available via `LV_USE_TEST`

Gated by `CONFIG_LV_USE_TEST`. Located in `lvgl/src/debugging/test/`.

**Key APIs:**

| Function | What it does |
|----------|-------------|
| `lv_test_mouse_move_to(x, y)` | Set cursor position |
| `lv_test_mouse_move_to_obj(obj)` | Move to widget center |
| `lv_test_mouse_press()` | Set pressed state |
| `lv_test_mouse_release()` | Set released state |
| `lv_test_mouse_click_at(x, y)` | Full click: release→wait→move+press→wait→release→wait |

### How It Works

Creates a **separate indev** alongside the real touch — both coexist. The test indev has its own read callback that returns static state variables. Cannot inject into the existing XPT2046 indev.

### FreeRTOS Consideration

`lv_test_mouse_click_at()` uses `lv_test_wait()` which calls `lv_tick_inc()` + `lv_timer_handler()` internally. This **conflicts** with the LVGL task that's already doing the same.

**Two approaches:**

1. **Run from LVGL task context** — safest but requires hooking into the task.
2. **Set state variables and let the existing LVGL task process them** — simpler, slightly async. Set pressed state, wait for a real tick cycle, then check results.

For our tests, approach 2 is simpler: set test indev state, sleep briefly for LVGL task to process, then assert. We already have `msp3520_lvgl_lock/unlock` for thread-safe LVGL access.

### Alternative: DIY Test Indev

If `LV_USE_TEST` pulls in too much or conflicts, we can create a minimal test indev (~30 lines): create indev, set type pointer, provide read callback that reads from static vars. Same pattern as lv_test_indev but without the full test framework.

### Verifying Touch Was Consumed

LVGL test pattern: attach event callback with counter, simulate click, assert counter.

```c
static uint32_t click_count = 0;
void btn_cb(lv_event_t *e) { click_count++; }
// Register, click, assert click_count == 0 (consumed) or == 1 (passed through)
```

## Timeout Unit Change

Straightforward refactor. Key touchpoints:

- `msp3520_priv.h`: `dim_timeout_min` → `dim_timeout_s`, `off_timeout_min` → `off_timeout_s`
- `screen_protect.c`: init multiplies Kconfig by 60, idle_check uses `timeout_s * 1000`
- `screen_protect.h`: API signatures update to seconds
- `console_commands.c`: display/accept seconds
- All internal comparisons: `minutes * 60000` → `seconds * 1000`

## Test Strategy Decision

**Use DIY test indev** rather than `LV_USE_TEST`:
- Avoids pulling in the full LVGL test framework (display, fs, screenshots)
- Avoids `lv_test_wait` conflicts with our FreeRTOS LVGL task
- Minimal code: create indev, read callback, press/release helpers
- Set state + `vTaskDelay` + assert — works naturally with our existing task architecture

**Test execution:**
- Flash test app, connect via monitor
- `*` runs all automated tests
- Interactive tests prompt user for action + confirmation
