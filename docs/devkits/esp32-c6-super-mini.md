# ESP32-C6 Super Mini

Source: https://www.fibel.no/product/esp32-c6-super-mini/

## Specs

- **SoC**: ESP32-C6 (RISC-V, single core, 160 MHz)
- **Flash**: 4 MB (QIO, no PSRAM)
- **Wireless**: Wi-Fi 6 (2.4 GHz), Bluetooth 5.3 LE, 802.15.4 (Thread/Zigbee)
- **USB**: USB-C (USB Serial/JTAG via GPIO12/GPIO13)
- **On-board LEDs**: WS2812 RGB (GPIO8), green status LED (GPIO15)
- **Form factor**: compact "stamp hole" design, native GPIO numbering (no silkscreen remapping)

## Pinout

| GPIO | ADC | Notes |
|------|-----|-------|
| GPIO0 | ADC1_CH0 | |
| GPIO1 | ADC1_CH1 | |
| GPIO2 | ADC1_CH2 | |
| GPIO3 | ADC1_CH3 | |
| GPIO4 | ADC1_CH4 | Strapping; JTAG MTMS |
| GPIO5 | ADC1_CH5 | Strapping; JTAG MTDI |
| GPIO6 | | JTAG MTCK |
| GPIO7 | | JTAG MTDO |
| GPIO8 | | Strapping; on-board WS2812 RGB LED |
| GPIO9 | | Strapping (boot mode select) |
| GPIO12 | | USB D- (shared with USB Serial/JTAG) |
| GPIO13 | | USB D+ (shared with USB Serial/JTAG) |
| GPIO14 | | |
| GPIO15 | | Strapping; JTAG TDI; on-board green status LED |
| GPIO18 | | Labeled FSPIQ in some sources; available as GPIO |
| GPIO19 | | Labeled FSPID in some sources; available as GPIO |
| GPIO20 | | |
| GPIO21 | | |
| GPIO22 | | |
| GPIO23 | | |

## Constraints

- **GPIO12/GPIO13** shared with USB D-/D+. Using them as GPIOs disables USB Serial/JTAG.
- **GPIO8** drives the on-board WS2812 RGB LED.
- **GPIO15** drives the on-board green status LED.
- **Strapping pins** (GPIO4, GPIO5, GPIO8, GPIO9, GPIO15) affect boot behavior if driven at reset.
- **No PSRAM** -- all memory is internal SRAM (512 KB HP + 16 KB LP) plus flash.

