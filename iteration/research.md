# Research: Standard IDF standalone component layout

Ref: [spec.md](spec.md)

## Standard component repo layout

From official docs and real Espressif repos, a standalone component has its component files at repo root — no `components/` wrapper.

**Minimal example** (`atanisoft/esp_lcd_touch_xpt2046`):
```
esp_lcd_touch_xpt2046/        (repo root = component root)
  CMakeLists.txt
  Kconfig.projbuild
  idf_component.yml
  include/
  esp_lcd_touch_xpt2046.c
```

**With examples and test_apps** (`espressif/idf-extra-components` — `led_strip`):
```
led_strip/                    (component root)
  CMakeLists.txt
  idf_component.yml
  include/
  src/
  examples/
    led_strip_rmt_ws2812/
      CMakeLists.txt
      main/
        CMakeLists.txt
        idf_component.yml    ← override_path here
        led_strip_rmt_ws2812_main.c
  test_apps/
    CMakeLists.txt
    main/
      CMakeLists.txt
      idf_component.yml      ← override_path here
```

Non-component dirs (`docs/`, `examples/`, `.github/`) coexist at root without issues — the component manager uses `idf_component.yml` and `CMakeLists.txt` to identify the component, not the directory contents.

## How examples/test_apps reference the parent component

Every Espressif repo uses the same pattern: **`override_path` in `main/idf_component.yml`**, not `EXTRA_COMPONENT_DIRS`.

### Example project CMakeLists.txt — minimal boilerplate, no extra dirs

```cmake
cmake_minimum_required(VERSION 3.16)
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(led_strip_rmt_ws2812)
```

### Example main/idf_component.yml — override_path to component root

```yaml
dependencies:
  espressif/led_strip:
    version: '^3'
    override_path: '../../../'
```

Path goes: `main/` → `example_name/` → `examples/` → component root.

### test_apps follow the same pattern

`onewire_bus/test_apps/main/idf_component.yml`:
```yaml
dependencies:
  espressif/onewire_bus:
    version: "*"
    override_path: "../.."
```

Path goes: `main/` → `test_apps/` → component root.

### override_path behavior

From official docs:
- When `override_path` is defined, it takes priority over the registry version
- When examples are downloaded individually from the registry, **`override_path` is automatically stripped** so the component is fetched from the registry instead
- The directory at the end of the override path should match the component name
- The dependency key uses the component's registry name (e.g. `espressif/led_strip`)

### EXTRA_COMPONENT_DIRS

The official packaging docs explicitly state:
> You shouldn't add your component's directory to `EXTRA_COMPONENT_DIRS` in example's CMakeLists.txt, because it will break the examples downloaded with the repository.

None of the examined repos use `EXTRA_COMPONENT_DIRS`.

## Dependency key naming

Some repos use namespace prefix (`espressif/led_strip`), some don't (`esp_lcd_ili9341`). This depends on whether they're published under a namespace. For an unpublished local component, the `path` field can be used instead of `override_path` — `path` is for components not in the registry, `override_path` is for overriding a registry component with a local version.

Since msp3520 is not published to the registry, `path` would be the correct field rather than `override_path`.

## Sources

- [Packaging ESP-IDF Components](https://espressif-docs.readthedocs-hosted.com/projects/idf-component-manager/en/v1.4.0/guides/packaging_components.html)
- [idf_component.yml Manifest Reference](https://docs.espressif.com/projects/idf-component-manager/en/latest/reference/manifest_file.html)
- [espressif/idf-extra-components](https://github.com/espressif/idf-extra-components) — led_strip, onewire_bus
- [espressif/esp-bsp](https://github.com/espressif/esp-bsp) — esp_lcd_ili9341, esp_lvgl_port
- [atanisoft/esp_lcd_touch_xpt2046](https://github.com/atanisoft/esp_lcd_touch_xpt2046)
