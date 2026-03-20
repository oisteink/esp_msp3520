# Spec: Paint Example Renaming

## Goal
Rename the paint example to properly reflect that it requires a pointer/stylus for optimal use.

## Current State
- Example directory: `examples/paint/`
- Main source: `examples/paint/main/finger-paint.c`
- Directory name already uses "paint"
- Code contains hardcoded "finger-paint" references

## Requirements

### 1. Rename Files and References
- Rename `finger-paint.c` to `paint.c`
- Remove or update any hardcoded "finger-paint" strings in the source code (check for tags, logs, etc.)

### 2. Add Documentation
- Create `examples/paint/README.md` with usage instructions
- Document that this example requires a pointer/stylus
- Explain the reason: drawing algorithm creates "straws" with fingers due to touch tracking jitter

### 3. What NOT to Change
- Git commit history (no git rewrites)
- No changes to past commits

## Acceptance Criteria
- ✅ `examples/paint/main/` contains `paint.c` instead of `finger-paint.c`
- ✅ No hardcoded "finger-paint" references remain in source code
- ✅ `examples/paint/README.md` exists and explains the stylus requirement
- ✅ Directory and project names are consistently "paint"
- ✅ Git history remains unchanged
