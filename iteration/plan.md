# Plan: Paint Example Renaming

Ref: [spec.md](spec.md), [research.md](research.md)

## Overview
Rename the paint example from "finger-paint" to "paint" and add documentation explaining it requires a pointer/stylus for optimal use.

## File Changes

### 1. Rename Source File
**File**: `examples/paint/main/finger-paint.c`
**→**: `examples/paint/main/paint.c`
**Change**: Rename file
**Also update**: `examples/paint/main/CMakeLists.txt` to reference `paint.c` instead of `finger-paint.c`

### 2. Update Log Tag
**File**: `examples/paint/main/paint.c` (after rename)
**Line 9**: `static const char *TAG = "finger-paint";`
**→**: `static const char *TAG = "paint";`

### 3. Create Paint Example README
**File**: `examples/paint/README.md` (new)
**Content**: Usage instructions, stylus requirement explanation, basic operation guide

### 4. Update Main README
**File**: `README.md`
**Line 22**: `examples/finger-paint/` → `examples/paint/`
**Line 30**: `cd examples/finger-paint` → `cd examples/paint`
**Lines 40-41**: Update description to mention stylus pointer requirement

### 5. Update Project Overview
**File**: `docs/project-overview.md`
**Lines 21-23**: Update structure diagram to show `paint/` instead of `finger-paint/`
**Line 39**: Update description to mention stylus requirement

## Implementation Order
1. Rename `finger-paint.c` → `paint.c` (keeping file open for next edit)
2. Update `TAG` string in paint.c
3. Update `CMakeLists.txt` to reference `paint.c`
4. Create `examples/paint/README.md`
5. Update `README.md`
6. Update `docs/project-overview.md`

## What Changes vs What Stays

| Changes | Stays the same |
|---------|---------------|
| `finger-paint.c` → `paint.c` | Git history (no rewrites) |
| TAG `"finger-paint"` → `"paint"` | Touch algorithm and performance characteristics |
| Example name in docs | Directory structure and file locations |
| Add README with stylus note | Component functionality and LVGL integration |
