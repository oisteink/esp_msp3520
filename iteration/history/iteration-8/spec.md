# Spec: Finger Paint Example

## What
A new example project (`examples/finger-paint/`) that lets you draw on the screen with your finger. Doubles as a touch edge-reach diagnostic.

## Requirements

1. **Scaffolding**: Use `idf.py create-project` to generate the project skeleton, then edit from there.
2. **Drawing**: Touch down starts a stroke, dragging leaves a visible trail, lift ends the stroke.
3. **Edge visualization**: Draw border markers or a reference grid so the user can see how close to each edge touch reaches.
4. **Minimal UI**: A clear button and a few color choices — nothing fancy.
5. **Component reuse**: Uses the `msp3520` component for display, touch, and LVGL initialization.
6. **Standalone example**: Builds and flashes independently, lives at `examples/finger-paint/`.

## Component enhancement
7. **LVGL perf monitor**: Add a CLI command to the `msp3520` component that toggles the built-in LVGL performance monitor on/off. Available to all component consumers.

## Non-goals
- Pressure sensitivity, brush sizes, undo/redo.
- Touch calibration tuning (that's the component's job).

## Acceptance criteria
- Can draw continuous strokes across the screen.
- Edge dead zones are visually obvious against the border markers/grid.
- Clear button resets the canvas.
- LVGL perf monitor can be toggled on/off from the console.
- Builds cleanly with `idf.py build`.
