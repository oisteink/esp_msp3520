# Iteration 4: REPL Console

**Goal:** Add a UART console (REPL) for runtime diagnostics and configuration.

**What we're building:**
- ESP-IDF `esp_console` with UART REPL on the default UART
- Built-in commands:
  - `log_level <tag> <level>` — set log level per component at runtime
  - `info` — display free heap, minimum free heap, uptime, chip info
  - `touch_cal` — print current touch coordinate flags (swap_xy, mirror_x, mirror_y) and allow setting them
  - `rotation` — print current display orientation and allow setting mirror/swap_xy flags
  - `debug` — shortcut to set all app-relevant tags (app, ili9488, XPT2046) to DEBUG level

**What we're NOT building:**
- Persistent settings (flags reset on reboot)
- Scripting or batch commands

**Approach:**
- Use `esp_console_new_repl_uart()` for the REPL
- Register each command via `esp_console_cmd_register()`
- Touch and display handles passed via command context
- Console runs on its own REPL task (managed by esp_console)

**Acceptance criteria:**
- [ ] Typing `help` lists all available commands
- [ ] `log_level app DEBUG` enables debug output (e.g. touch coordinates)
- [ ] `info` prints heap and uptime
- [ ] `touch_cal` and `rotation` read and write flags
- [ ] `debug` toggles debug logging for app components
- [ ] No interference with display or touch operation
