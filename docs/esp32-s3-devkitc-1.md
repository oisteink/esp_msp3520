# Espressif ESP32-S3-DevKitC-1

Source: https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/

## Specs

- **SoC**: ESP32-S3 (Xtensa LX7, dual core, up to 240 MHz)
- **Wireless**: Wi-Fi (2.4 GHz), Bluetooth 5.0 LE
- **USB**: USB-OTG + USB Serial/JTAG
- **Interfaces**: 3x UART, 2x I2C, 4x SPI (SPI2/SPI3 general purpose), 2x I2S

## Variants

| Module | Flash | PSRAM | SPI Voltage | Notes |
|--------|-------|-------|-------------|-------|
| WROOM-1 N8 | 8 MB (Quad) | -- | 3.3 V | |
| WROOM-1 N8R8 | 8 MB (Quad) | 8 MB (Octal) | 3.3 V | GPIO35/36/37 unavailable |
| WROOM-1 N16R8 | 16 MB (Quad) | 8 MB (Octal) | 3.3 V | GPIO35/36/37 unavailable |
| WROOM-1 N32R8V | 32 MB (Quad) | 8 MB (Octal) | 3.3 V | GPIO35/36/37 unavailable |
| WROOM-2 N32R16V | 32 MB (Octal) | 16 MB (Octal) | 1.8 V | We use this version. GPIO33-37 unavailable |

## Constraints

- **WROOM-1 R8 variants** (N8R8, N16R8, N32R8V): GPIO35, GPIO36, GPIO37 are used by the Octal SPI PSRAM interface and cannot be used as general-purpose GPIOs.
- **WROOM-2 N32R16V**: GPIO33, GPIO34, GPIO35, GPIO36, GPIO37 are all used by the Octal SPI flash/PSRAM interface and cannot be used as general-purpose GPIOs.
- **GPIO26-GPIO32** are connected to the SPI flash on all variants with PSRAM and are not available.
- **RGB LED**: GPIO38 on board revision v1.1; GPIO48 on v1.0. The N32R16V (WROOM-2) ships on v1.1 boards.
- The board has 2x USB ports: USB-OTG (directly connected to the S3 USB peripheral) and USB Serial/JTAG (via the built-in controller).
- 45 GPIOs total, but many are used internally by flash/PSRAM depending on variant. Zenith wiring must avoid the variant-specific restricted pins listed above.
