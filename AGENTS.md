# Agent Notes

This is a hardware Arduino/ESP32 project, not a conventional software repo. There is no package manager, test suite, CI, or build script.

## Project layout

- `set_rtc_time/set_rtc_time.ino` — standalone utility to set/check the DS3231 RTC.
- `set_rtc_time/upload_set_rtc_time.py` — helper script that compiles, uploads, and sends the exact PC time to the sketch via Serial for fine-tuning.
- `speeduino_logger/` — main logger sketch and its modules.
  - `speeduino_logger.ino` — `setup()`/`loop()` and FreeRTOS task creation.
  - `config.h` — all constants and `#define USE_WIFI`.
  - `channels.h/.cpp` — realtime-data channel table `CH[]`.
  - `rtc_time.h/.cpp` — DS3231 wrapper and ISO 8601 formatting.
  - `frame_queue.h/.cpp` — FreeRTOS queue of raw frames.
  - `log_store.h/.cpp` — SD card: open/rotate/cleanup/CSV write, hierarchy `/Year/Month/Day/`.
  - `transport.h/.cpp` — Wi-Fi AP + TCP 2000 or Bluetooth SPP.
  - `speeduino_protocol.h/.cpp` — framed `[len][payload][CRC32]` protocol helpers.
  - `ecu_bridge.h/.cpp` — Serial2, framed `'r'` polling, bridge + passive sniffer.
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

The RTC is initially set from `__DATE__` / `__TIME__` at compile time, so flashing near the target time matters. The sketch also accepts a fine-tuning command via Serial in the format `SET:YYYY-MM-DDTHH:MM:SS` for a few seconds after boot; use `upload_set_rtc_time.py` to upload and send the exact PC time automatically.

### Transport mode (`speeduino_logger`)

`#define USE_WIFI 1` at the top of `speeduino_logger/config.h` selects the link:

- `1` — ESP32 creates AP `Speeduino` (password `qK95xvFC`). It listens for TunerStudio on TCP port `2000` and serves a log browser on HTTP port `80` at `192.168.4.1`.
- `0` — Bluetooth SPP with name `Speeduino-ESP32` and PIN `1234`. The web log browser is not available in this mode.

### Speeduino serial protocol

The ESP32 is wired to the HC-06 header, which is **Serial0 (D0/D1)** on the Mega, not Serial3. The primary Speeduino protocol is always active there:

```text
[2B BE length][payload][4B BE CRC32]
```

No secondary serial configuration is required. Both TunerStudio and ECU Manager use framed `'r'` 0x30 for live data. The autonomous poll sends the same request at `POLL_HZ` (default 10 Hz). The response payload starts with one `RC_OK` byte followed by the 130 OutputChannels bytes.

## Hardware wiring (do not change pin numbers without updating both sketches)

### Speeduino ↔ ESP32

| Signal | Speeduino | ESP32 |
|---|---|---|
| ECU TX (5 V) | TX | GPIO16 (RX2) via 5 V → 3.3 V divider |
| ECU RX | RX | GPIO17 (TX2) |
| GND | GND | GND |
| Power | 5 V (HC-06 VCC pin) | VIN |

**Important:** the Speeduino TX line is 5 V and must be level-shifted to 3.3 V before GPIO16.

The HC-06 header is wired to **Serial0 (D0/D1)**, not Serial3. The USB-serial 16U2 shares Serial0, so do not use USB and the ESP32 at the same time. Disconnect/power off the ESP32 when flashing the Speeduino.

### DS3231 RTC

| DS3231 | ESP32 |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| SDA | GPIO21 |
| SCL | GPIO19 |

The DS3231 accepts a supply voltage between **2.3 V and 5.5 V**, so 3.3 V is safe. Keep the I²C lines (SDA/SCL) at 3.3 V to match the ESP32 GPIO levels.

### Micro SD module

| SD | ESP32 |
|---|---|
| VCC (5 V)* | VIN |
| GND | GND |
| CS | GPIO25 |
| MOSI | GPIO27 |
| CLK/SCK | GPIO26 |
| MISO | GPIO14 |

\* *Use 3.3 V if your SD module lacks an internal 5 V → 3.3 V regulator.*

## Runtime behavior

- When no TCP client is connected, the logger polls the ECU with framed `'r'` 0x30 at `POLL_HZ` (default 10 Hz) and logs replies.
- When a TCP client is connected (TunerStudio or ECU Manager), the ESP32 acts as a transparent bridge and passively sniffs `'r'` live-data replies. It does not inject polling in this mode.
- The bridge is triggered by a TCP connection on port 2000, not merely by a device joining the Wi-Fi AP.
- After a client disconnects, the logger waits `MODE_SETTLE_MS` (500 ms) before resuming polling.
- CSV logs are stored under `/YYYY/Mês/DD/YYYY-MM-DDTHH-MM-SS.csv` when the RTC is valid.
- RTC failure fallback: `/NO_RTC/BOOT_<millis>.csv` with empty `datetime` column.
- Auto-cleanup removes oldest `.csv` files (and empty day/month/year folders) if free SD space drops below `SD_FREE_MARGIN_MB` (100 MB).
- LED blink codes (each code repeats every `LED_CODE_PAUSE_MS`, default 2 s):
  - 1 blink: SD error
  - 2 blinks: RTC failed / fallback active
  - 3 blinks: logging normally
  - 4 blinks: waiting for connection / polling
- LED timing configurable in `config.h` (`LED_BLINK_ON_MS`, `LED_BLINK_OFF_MS`, `LED_CODE_PAUSE_MS`).

## Web log browser

In `USE_WIFI` mode the ESP32 also runs a small read-only HTTP file browser at:

- URL: `http://192.168.4.1/`

It lists the SD card folders and files, lets you download any closed `.csv` log, and delete `.csv` files (with a confirmation prompt). The file currently being written is shown as *“gravando agora”* and cannot be downloaded or deleted until the logger rotates to a new file.

## TunerStudio / ECU Manager connection

For `USE_WIFI` mode, use the **TCP/IP** driver with:

- IP: `192.168.4.1`
- Port: `2000`

Both TunerStudio and ECU Manager use the framed Speeduino protocol with `'r'` for live data. Only one TCP client at a time is supported.

## Gotchas

- There is no automated test, lint, or typecheck. Verification is flashing and observing serial output/LED.
- Compile with:
  ```bash
  arduino-cli compile --fqbn esp32:esp32:esp32 \
    --board-options PartitionScheme=huge_app speeduino_logger
  ```
- The HC-06 header is **Serial0 (D0/D1)**, not Serial3. Do not enable/configure the Speeduino secondary serial for this bridge.
- USB and the ESP32 share Serial0. Do not use both at the same time; disconnect/power off the ESP32 when flashing the Speeduino.
- A framed live-data response payload is `[RC_OK][130 bytes]`; the sniffer must skip one byte (`R_REPLY_HEADER = 1`).
- Channel offsets in `CH[]` target firmware **202501.7** (130-byte OutputChannels). Review them if the firmware changes.
- The sniffer is passive: it only logs `'r'` replies that the connected app actually requests. Use `SNIFF_DEBUG 1` to confirm recognition.
