# explore-tft-spi-touch

Learning project: drive an ILI9488 3.5" SPI TFT display with touch input from ESP32, then explore LVGL.

## Goals

1. Get the ILI9488 (MSP3520) displaying something over SPI
2. Get touch input working
3. Integrate LVGL and build simple UIs

## Hardware

- **Display**: 3.5" ILI9488 SPI TFT (MSP3520) with resistive touch
- **Boards** (on hand):
  - ESP32-S3-DevKitC-1
  - ESP32-C6 Super Mini
  - NanoESP32-C6
  - XIAO ESP32-C6

See `docs/` for per-component details.

## Stack

- ESP-IDF v5.5.3
- LVGL (later)
