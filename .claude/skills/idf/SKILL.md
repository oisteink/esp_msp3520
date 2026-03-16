---
name: idf
description: >
  Use this skill for any ESP-IDF project operation via idf.py. Triggers when the user wants to build, flash, monitor, create projects or components, manage dependencies, run diagnostics, analyze binary size, check code with clang-tidy, decode coredumps, or interact with device serial output in any way. Also use when the user wants Claude to react to serial output — e.g. "flash and check if it boots", "monitor and tell me when it crashes", "run the monitor and read the logs".
---

# ESP-IDF Project Operations

All `idf.py` commands run through tmux sessions — one per firmware, with ESP-IDF pre-sourced and the working directory set to the firmware root. This gives Claude the same workflow as having a terminal open in each project.

## tmux Sessions

Each firmware gets its own session, created on demand.

| Firmware | Session | Working directory |
|----------|---------|-------------------|
| `basic-example` | `basic` | `examples/basic/` |
| `paint-example` | `paint` | `examples/finger-paint/` |
| `tests` | `tests` | not implemented yet |

If a new firmware target is added, follow the same pattern.

### Create or reuse a session

```bash
tmux has-session -t basic 2>/dev/null || tmux new-session -d -s basic -c /home/ok/src/esp-msp3520/examples/basic -x 220 -y 50
```

Always reuse if it exists. Source the IDF environment on creation:

```bash
tmux send-keys -t basic "source ~/esp/v5.5.3/esp-idf/export.sh" Enter
```

### Running commands

```bash
tmux send-keys -t basic "idf.py build" Enter
```

### Reading output

Use the helper scripts to capture or poll pane content:

```bash
python3 /home/ok/src/esp-msp3520/.claude/skills/idf/scripts/capture_pane.py basic 500
python3 /home/ok/src/esp-msp3520/.claude/skills/idf/scripts/poll_pane.py basic "error\|Build complete" 120
```

---

## Command Reference

### Project Scaffolding

```bash
idf.py create-project NAME              # new project in current dir
idf.py create-component NAME            # new component in components/
idf.py create-manifest                   # create idf_component.yml
idf.py add-dependency DEPENDENCY         # add to manifest
idf.py update-dependencies               # fetch/update all dependencies
```

Use `-C path` to target a different directory:

```bash
idf.py create-component -C ../extra_components extra_foo
```

### Configuration

```bash
idf.py set-target <chip>                 # esp32s3, esp32c6, etc.
idf.py menuconfig                        # interactive Kconfig editor
idf.py reconfigure                       # re-run CMake (generates compile_commands.json)
idf.py save-defconfig                    # write sdkconfig.defaults from current config
```

### Build

```bash
idf.py build                             # full build
idf.py app                               # app only (skip bootloader + partition table)
idf.py clean                             # delete build outputs (keeps CMake cache)
idf.py fullclean                         # delete entire build directory
```

**Important**: `fullclean` does NOT delete `sdkconfig`. If you changed `sdkconfig.defaults`, also `rm sdkconfig` for defaults to take effect.

### Flash

```bash
idf.py flash                             # flash all
idf.py app-flash                         # flash app only (faster)
idf.py flash monitor                     # flash then start monitor
idf.py build flash monitor               # build + flash + monitor
```

Always specify port if ambiguous: `idf.py -p /dev/ttyACM0 flash`

### Monitor

```bash
idf.py monitor                           # start serial monitor
idf.py -p /dev/ttyACM0 monitor           # explicit port
```

#### Monitor keyboard shortcuts (Ctrl-T menu)

| Keys | Action |
|------|--------|
| `Ctrl-T Ctrl-X` | **Exit monitor** (preferred) |
| `Ctrl-T Ctrl-R` | **Reboot device** (software reset via RTS) |
| `Ctrl-T Ctrl-F` | **Rebuild + flash + resume monitor** (all-in-one) |
| `Ctrl-T Ctrl-P` | **Reset into bootloader** (halts app, ready for reflash) |
| `Ctrl-T Ctrl-Y` | **Pause/resume output** |
| `Ctrl-T Ctrl-S` | **Toggle output logging** to file |
| `Ctrl-T Ctrl-H` | **Show all shortcuts** |

Via tmux:

```bash
tmux send-keys -t basic C-t C-x    # exit monitor
tmux send-keys -t paint C-t C-r    # reboot device
tmux send-keys -t paint C-t C-f    # rebuild + flash + resume
```

#### Exiting monitor before flashing

If monitor is running from a previous invocation, exit cleanly first:

```bash
tmux send-keys -t basic C-t C-x
sleep 2
```

### Testing

Testing is not implemented yet

### Diagnostics & Analysis

```bash
idf.py size                              # binary size summary
idf.py size-components                   # per-component size breakdown
idf.py size-files                        # per-source-file size
idf.py clang-check                       # run clang-tidy, output to warnings.txt
idf.py diag                              # generate diagnostic report
idf.py coredump-info -p PORT             # decode coredump from device
```

---

## REPL Interaction

If the firmware includes `esp_console`, send commands through the monitor:

```bash
tmux send-keys -t paint "help" Enter
sleep 1
python3 /home/ok/src/esp-msp3520/.claude/skills/idf/scripts/capture_pane.py basic 30
```

Only send commands when the REPL prompt is visible. Check with `capture_pane.py` first.

---

## Serial Output Patterns

When analyzing captured output, look for:

- `Guru Meditation Error` / `panic` — crash, report the backtrace
- `ESP_ERROR_CHECK failed` — runtime assertion with file/line info
- `rst:0xc (RTC_SW_CPU_RST)` — software reset (crash reboot loop)
- `Restarting...` — watchdog or intentional reset
- `TEST_PASS`, `TEST_FAIL` — Unity test results
- App-specific markers (boot complete, data received, etc.)

---

## Do's

- **Use the correct session name** for the target device
- **Check pane state** before sending commands — shell prompt, mid-flash, or monitor?
- **Use `poll_pane.py`** to wait for completion rather than arbitrary sleeps
- **Use `Ctrl-T Ctrl-X`** to exit monitor (not `Ctrl-C` or `Ctrl-]`)
- **Delete `sdkconfig`** when changing `sdkconfig.defaults`
- **Poll for new content** after flash+monitor — pane buffer may contain old output

## Don'ts

- **Don't send `Ctrl-C` during flash** — corrupts firmware image
- **Don't send REPL commands during flash** — keystrokes interfere with serial protocol
- **Don't trust first `poll_pane.py` match after rebuild** — buffer contains previous boot output
- **Don't assume `fullclean` resets config** — must also `rm sdkconfig`
- **Don't mix up session names** — wrong session = wrong device

---

## Helper Scripts

Located in this skill's `scripts/` directory:

- `capture_pane.py <session> [lines]` — snapshot last N lines from a tmux pane
- `poll_pane.py <session> <pattern> [timeout] [interval]` — wait for a regex pattern, exit 0 on match, 1 on timeout

---

## Notes

- `idf.py flash monitor` chains commands — splitting them gives better error handling control
- If port is wrong, pass `-p /dev/ttyUSB0` explicitly
- tmux `capture-pane` returns plain text — ANSI codes stripped by default
- When `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`, ALL output goes to USB JTAG port, not UART. Use `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` to ensure output on UART.
