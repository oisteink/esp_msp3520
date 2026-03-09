## CRITICAL: Do not modify anything without permission

Never change, add, or remove code, files, or content unless the user explicitly asks you to. This applies to all agents and sub-agents. If you see a problem, report it and ask for permission — do not fix it on your own. The user decides what gets changed.

# Project: explore-tft-spi-touch

ESP-IDF project exploring SPI TFT displays with touch input.

**Current state:** ESP32-S3 driving ILI9488 display + XPT2046 touch over shared SPI bus, with LVGL v9.5 UI. Portrait mode (320x480).

## Environment

- **Framework**: ESP-IDF v5.5.3
- **Target**: ESP32-S3 (DevKitC-1)
- **Setup**: `source ~/esp/v5.5.3/esp-idf/export.sh`
- **Hardware docs**: `docs/` (one markdown file per component)
- **Wiring**: `docs/project-overview.md` (full pinout table)

## Development Process

Work follows a 4-stage gated process. Each stage requires explicit user approval before moving to the next. All artifacts live in the `iteration/` directory and each stage builds on the previous.

### Stage 1: Spec

Collaborate with the user to define what we're building. Capture requirements, constraints, and acceptance criteria.

**Output**: `iteration/spec.md`

### Stage 2: Research

Deep-dive into the spec. Investigate APIs, hardware details, existing patterns, and potential pitfalls.

**Output**: `iteration/research.md`

### Stage 3: Plan

Design the implementation based on spec + research. Include file changes, architecture decisions, and tests. Show how the plan satisfies the spec.

**Output**: `iteration/plan.md`

### Stage 4: Implementation

Execute the plan. Write code, run tests, verify nothing is broken.

### Rules

- Never skip a stage or start the next without approval.
- Keep stage documents concise and focused.
- Each stage document references the previous ones -- don't repeat, build on.
