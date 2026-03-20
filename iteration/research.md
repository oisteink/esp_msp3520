# Research: Paint Example Renaming

Ref: [spec.md](spec.md)

## Current State Analysis

### Source Code References
Found 2 hardcoded "finger-paint" references in the paint example:

1. **`examples/paint/main/finger-paint.c:9`** — Log tag
   ```c
   static const char *TAG = "finger-paint";
   ```

2. **`examples/paint/main/CMakeLists.txt:2`** — Source file reference
   ```cmake
   SRCS "finger-paint.c"
   ```

### Documentation References
Multiple references in main documentation files:

**`README.md`:**
- Line 22: Directory listing shows `examples/finger-paint/`
- Line 30: Quick start command mentions `examples/finger-paint`
- Lines 40-41: Example description

**`docs/project-overview.md`:**
- Lines 21-23: Structure diagram shows `finger-paint/` with `main/finger-paint.c`
- Line 39: Example description

### Code Algorithm Understanding
The paint example uses **Bresenham line algorithm with round brush**:
- Draws filled circles at each point along the line
- BRUSH_W = 3 pixels
- Uses direct buffer drawing with partial invalidation

**Why fingers create "straws":** Large touch area and tracking jitter cause gaps between connected line segments, making it look like straw-like texture instead of smooth lines.

### What NOT to Change
- Iteration history files (already committed, should remain as historical record)
- VS Code workspace configuration
- Any other past iterations' artifacts

## Implementation Plan

### Files to Update
1. `examples/paint/main/finger-paint.c` — Update TAG string
2. `examples/paint/main/CMakeLists.txt` — Update source file name
3. `examples/paint/README.md` — Create new file with usage instructions and stylus requirement note
4. `README.md` — Update example section
5. `docs/project-overview.md` — Update structure diagram and description

### Changes Summary
- Rename `finger-paint.c` → `paint.c`
- Change TAG from `"finger-paint"` to `"paint"`
- Add documentation explaining stylus requirement
- Update all references in main documentation
