# Seeed Studio XIAO ESP32-C6

Source: https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/

## Specs

- **SoC**: ESP32-C6 (RISC-V, single core, 160 MHz)
- **Flash**: 4 MB (no PSRAM)
- **Wireless**: Wi-Fi 6 (2.4 GHz), Bluetooth 5.3 LE, 802.15.4 (Thread/Zigbee)
- **USB**: USB-C (USB Serial/JTAG)
- **Interfaces**: 2x UART, 2x I2C, 1x SPI, 12x GPIO, 1x I2S

## Silkscreen to GPIO mapping

| Silkscreen | ESP32-C6 GPIO | ADC | Notes |
|------------|---------------|-----|-------|
| D0 | GPIO2 | ADC1_CH2 | |
| D1 | GPIO3 | ADC1_CH3 | |
| D2 | GPIO4 | ADC1_CH4 | |
| D3 | GPIO5 | ADC1_CH5 | |
| D4 | GPIO6 | | |
| D5 | GPIO7 | | |
| D6 | GPIO21 | | |
| D7 | GPIO20 | | |
| D8 | GPIO22 | | IO matrix routed |
| D9 | GPIO18 | | |
| D10 | GPIO19 | | |
| -- | GPIO8 | | On-board WS2812 LED |
| -- | GPIO12 | | USB D- (shared) |
| -- | GPIO13 | | USB D+ (shared) |

## Constraints

- GPIO12/GPIO13 are shared with USB D-/D+. Using them as GPIOs disables USB.
- GPIO8 drives the on-board WS2812 RGB LED.
- GPIO22 is routed through the IO matrix (usable but not directly on a peripheral bus by default).
- No PSRAM -- memory-intensive operations (large frame buffers, etc.) must fit in internal SRAM.
