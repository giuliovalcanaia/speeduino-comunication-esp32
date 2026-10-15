# Agent Notes

This is a hardware Arduino/ESP32 project, not a conventional software repo. There is no package manager, test suite, CI, or build script.

## Project layout

- `set_rtc_time/set_rtc_time.ino` — standalone utility to set/check the DS3231 RTC.
- `speeduino_logger/` — main logger sketch and its modules.
  - `speeduino_logger.ino` — `setup()`/`loop()` and FreeRTOS task creation.
  - `config.h` — all constants and `#define USE_WIFI`.
  - `channels.h/.cpp` — realtime-data channel table `CH[]`.
  - `rtc_time.h/.cpp` — DS3231 wrapper and ISO 8601 formatting.
  - `frame_queue.h/.cpp` — FreeRTOS queue of raw frames.
  - `log_store.h/.cpp` — SD card: open/rotate/cleanup/CSV write.
  - `transport.h/.cpp` — Wi-Fi AP + TCP 2000 or Bluetooth SPP.
  - `ecu_bridge.h/.cpp` — Serial2, polling command `n`, bridge + sniffer.
  - `web_ui.h/.cpp` — read-only HTTP log browser (`USE_WIFI = 1`).
  - `led_status.h/.cpp` — LED blink patterns.

The two sketches are independent and must be compiled/flashed separately. All `.h/.cpp` files in `speeduino_logger/` are compiled automatically by the Arduino IDE / `arduino-cli`.

## Build requirements

- Board: **ESP32 Dev Module** (`esp32:esp32:esp32`).
- Partition scheme: **Huge APP** (the `BluetoothSerial` stack is large).
- Only the **classic ESP32** works. ESP32-S2/S3/C3 are unsupported because Bluetooth Classic is required for the non-Wi-Fi mode.
- Required libraries:
  - `RTClib` by Adafruit (DS3231)
  - `SD`, `SPI`, `Wire` (ESP32 built-ins)
  - `WiFi`, `WebServer` (ESP32 built-ins, used by `set_rtc_time` and by logger when `USE_WIFI` is set)
  - `BluetoothSerial` (ESP32 built-in, used when `USE_WIFI` is `0`)

Use the Arduino IDE or `arduino-cli` to compile and upload.

## Critical configuration

### RTC timezone (`set_rtc_time`)

`HOURS_TO_SUBTRACT_FROM_UTC` in `set_rtc_time/set_rtc_time.ino` must be set before each flash:

- `0` if the build PC is already in local time (e.g. BRT).
- `3` if the build PC is in UTC and you want BRT (UTC-3) stored in the RTC.

The RTC is set from `__DATE__` / `__TIME__` at compile time, so flashing near the target time matters.

### Transport mode (`speeduino_logger`)

`#define USE_WIFI 1` at the top of `speeduino_logger/config.h` selects the link:

- `1` — ESP32 creates AP `Speeduino` (password `qK95xvFC`). It listens for TunerStudio on TCP port `2000` and serves a log browser on HTTP port `80` at `192.168.4.1`.
- `0` — Bluetooth SPP with name `Speeduino-ESP32` and PIN `1234`. The web log browser is not available in this mode.

## Hardware wiring (do not change pin numbers without updating both sketches)

### Speeduino ↔ ESP32

| Signal | Speeduino | ESP32 |
|---|---|---|
| ECU TX (5 V) | TX | GPIO16 (RX2) via 5 V → 3.3 V divider |
| ECU RX | RX | GPIO17 (TX2) |
| GND | GND | GND |
| Power | 5 V (HC-06 VCC pin) | VIN |

**Important:** the Speeduino TX line is 5 V and must be level-shifted to 3.3 V before GPIO16.

### DS3231 RTC

| DS3231 | ESP32 |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| SDA | GPIO21 |
| SCL | GPIO22 |

### Micro SD module

| SD | ESP32 |
|---|---|
| 3.3 V | 3.3 V |
| GND | GND |
| CS | GPIO5 |
| MOSI | GPIO23 |
| CLK/SCK | GPIO18 |
| MISO | GPIO19 |

## Runtime behavior

- When no client is connected, the logger polls the ECU with command `n` at `POLL_HZ` (30 Hz) and logs replies.
- When a TunerStudio/client is connected, the ESP32 acts as a transparent bridge and sniffs realtime-data replies.
- CSV logs are stored under `/YYYY-MM-DD/YYYY-MM-DDTHH-MM-SS.csv` when the RTC is valid.
- RTC failure fallback: `/NO_RTC/BOOT_<millis>.csv` with empty `datetime` column.
- Auto-cleanup removes oldest `.csv` files (and empty date folders) if free SD space drops below `SD_FREE_MARGIN_MB` (100 MB).
- LED patterns:
  - 100 ms blink: SD error
  - 200 ms blink: RTC failed / fallback active
  - 500 ms blink: logging normally
  - 1500 ms blink: waiting for connection / polling

## Web log browser

In `USE_WIFI` mode the ESP32 also runs a small read-only HTTP file browser at:

- URL: `http://192.168.4.1/`

It lists the SD card folders and files and lets you download any closed `.csv` log. The file currently being written is shown as *“gravando agora”* and cannot be downloaded until the logger rotates to a new file.

## TunerStudio connection

For `USE_WIFI` mode, use the **TCP/IP – WiFi driver** with:

- IP: `192.168.4.1`
- Port: `2000`

## Gotchas

- There is no automated test, lint, or typecheck. Verification is flashing and observing serial output/LED.
- `R_REPLY_HEADER` in `speeduino_logger/config.h` may need adjustment (`0` for primary port, `2` for Serial3 secondary port). Use `SNIFF_DEBUG 1` to verify.
- Channel scales in `CH[]` in `speeduino_logger/channels.cpp` (especially `PW1_ms`, `TPS_pct`, `AFR_target`) must be confirmed against the `speeduino.ini` of the target firmware version.
