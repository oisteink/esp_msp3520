# Spec: Restructure repo to standard IDF standalone component layout

## Problem

The msp3520 component is nested under `components/msp3520/` inside a repo that exists solely for this component. This adds an unnecessary directory level. Additionally, the repo name (`esp-msp3520`) doesn't match the component name (`msp3520`), which breaks the IDF convention that directory name = component name.

## Current layout

```
esp-msp3520/                    (repo root)
  components/msp3520/           (component lives here)
    CMakeLists.txt
    Kconfig
    idf_component.yml
    include/
    src/
    test_apps/
  examples/
  docs/
```

## Desired outcome

- Repo follows standard ESP-IDF standalone component layout (component files at root)
- Component renamed from `msp3520` to `esp_msp3520` to match repo directory name
- Repo directory renamed from `esp-msp3520` to `esp_msp3520` (underscore)
- All references updated: headers, Kconfig symbols, REQUIRES, includes, etc.
- Examples and test_apps wire up the component dependency the idiomatic way
- Build succeeds for examples and test_apps after restructure
- No functional code changes
