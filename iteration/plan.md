# Plan: Restructure repo to standard IDF standalone component layout

Ref: [spec.md](spec.md), [research.md](research.md)

## Key insight from failed build

IDF derives the component name from the directory name. The repo directory `esp-msp3520` doesn't match the component name `msp3520`. Solution: rename the repo directory to `esp_msp3520` and update build-system references to use `esp_msp3520` as the component name.

The public API surface (header name `msp3520.h`, functions `msp3520_create()`, Kconfig symbols `CONFIG_MSP3520_*`) stays unchanged — these are independent of the IDF component name.

## Step 1: Move component files to repo root using `git mv`

```bash
git mv components/msp3520/CMakeLists.txt .
git mv components/msp3520/Kconfig .
git mv components/msp3520/idf_component.yml .
git mv components/msp3520/include .
git mv components/msp3520/src .
git mv components/msp3520/test_apps .
```

## Step 2: Update examples to use `path` in idf_component.yml

Each example's `CMakeLists.txt` — remove `EXTRA_COMPONENT_DIRS`, add `set(COMPONENTS main)`.

Each example's `main/idf_component.yml` — add `esp_msp3520` as a `path` dependency.

Each example's `main/CMakeLists.txt` — change `REQUIRES msp3520` to `REQUIRES esp_msp3520`.

## Step 3: Update test_apps similarly

`test_apps/CMakeLists.txt` — remove `EXTRA_COMPONENT_DIRS`.

Create `test_apps/main/idf_component.yml` with `esp_msp3520` path dependency.

`test_apps/main/CMakeLists.txt` — change `PRIV_REQUIRES msp3520` to `PRIV_REQUIRES esp_msp3520`.

## Step 4: Delete stale dependencies.lock files

## Step 5: Rename repo directory

User renames `esp-msp3520/` → `esp_msp3520/` (outside git — `mv` at parent level). Update GitHub remote if needed.

## Step 6: Verify builds

## What changes vs what stays

| Changes | Stays the same |
|---------|---------------|
| Repo directory name → `esp_msp3520` | Public header `msp3520.h` |
| Component name in REQUIRES → `esp_msp3520` | API functions `msp3520_*()` |
| `idf_component.yml` dependency key → `esp_msp3520` | Kconfig symbols `CONFIG_MSP3520_*` |
| Folder structure (flattened) | All source code |
