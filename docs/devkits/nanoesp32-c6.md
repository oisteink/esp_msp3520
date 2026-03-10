# NanoESP32-C6

Source: https://github.com/wuxx/nanoESP32-C6/blob/master/README_en.md

## Specs

- **Module**: ESP32-C6-WROOM-1 (RISC-V, single core, 160 MHz)
- **Flash**: 4/8/16 MB variants (N4/N8/N16); no PSRAM
- **Wireless**: Wi-Fi 6 (2.4 GHz), Bluetooth 5.3 LE, 802.15.4 (Thread/Zigbee)
- **USB**: Dual USB-C -- CH343 USB-to-UART bridge + native ESP32-C6 USB
- **On-board**: RGB LED (GPIO8), BOOT button, RST button
- **Headers**: 13-pin top + 13-pin bottom (26 total pins including power and ground)

## Pinout

Pinout from board silkscreen. GPIO numbers are native ESP32-C6 numbers (no remapping).

### Top header (left to right)

| Pin | GPIO | Notes |
|-----|------|-------|
| 1 | -- | 5V |
| 2 | GPIO13 | USB D+ (native USB) |
| 3 | GPIO12 | USB D- (native USB) |
| 4 | GPIO11 | |
| 5 | GPIO10 | |
| 6 | GPIO8 | On-board RGB LED |
| 7 | GPIO7 | JTAG MTDO |
| 8 | GPIO6 | JTAG MTCK |
| 9 | GPIO5 | Strapping; JTAG MTDI |
| 10 | GPIO4 | Strapping; JTAG MTMS |
| 11 | -- | RST |
| 12 | -- | 3V3 |
| 13 | -- | GND |

### Bottom header (left to right)

| Pin | GPIO | Notes |
|-----|------|-------|
| 1 | -- | GND |
| 2 | GPIO9 | Strapping (boot mode select) |
| 3 | GPIO18 | |
| 4 | GPIO19 | |
| 5 | GPIO20 | |
| 6 | GPIO21 | |
| 7 | GPIO22 | |
| 8 | GPIO23 | |
| 9 | GPIO15 | Strapping; JTAG TDI |
| 10 | GPIO17 | RX (CH343 UART) |
| 11 | GPIO16 | TX (CH343 UART) |
| 12 | GPIO3 | ADC1_CH3 |
| 13 | GPIO2 | ADC1_CH2 |

## Constraints

- **GPIO12/GPIO13** connected to native ESP32-C6 USB port (the second USB-C connector).
- **GPIO16/GPIO17** connected to the CH343 USB-to-UART bridge (the primary flashing port).
- **GPIO8** drives the on-board RGB LED.
- **Strapping pins** (GPIO4, GPIO5, GPIO8, GPIO9, GPIO15) affect boot behavior if driven at reset.
- **No PSRAM** in any variant.

## Comparison with other ESP32-C6 boards

| Feature | NanoESP32-C6 | XIAO ESP32-C6 | ESP32-C6 Super Mini |
|---------|-------------|---------------|---------------------|
| Module | WROOM-1 | Integrated | Integrated |
| USB ports | 2 (CH343 + native) | 1 (native) | 1 (native) |
| Headers | DIP 26-pin | Stamp-hole 14-pin | Stamp-hole |
| GPIO labeling | Native numbers | D0-D10 silkscreen | Native numbers |
| RGB LED | GPIO8 | GPIO8 | GPIO8 |
| Status LED | -- | -- | GPIO15 |
| UART bridge | CH343 (GPIO16/17) | -- | -- |
