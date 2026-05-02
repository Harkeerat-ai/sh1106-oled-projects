# 📡 STM32 ESP OLED Terminal

An STM32 project that bridges an ESP Wi-Fi module and an SH1106 OLED display using a structured serial handshake. On boot the STM32 configures the ESP via a four-step credential exchange, then switches the display into a live terminal mode — streaming any incoming ESP data to the OLED in real time.

---

## 📋 Table of Contents

- [Overview](#overview)
- [Hardware Requirements](#hardware-requirements)
- [Wiring Diagram](#wiring-diagram)
- [Dependencies](#dependencies)
- [How It Works](#how-it-works)
- [Configuration](#configuration)
- [Project Structure](#project-structure)
- [Security Notice](#security-notice)
- [Usage](#usage)
- [License](#license)

---

## Overview

The system connects an STM32 microcontroller to an ESP8266 / ESP32 module over UART (`Serial3`) and drives an SH1106 128×64 OLED via I2C.

Operation is split into two phases controlled by a `systemReady` flag:

- **Phase 1 — Init Handshake:** The ESP requests credentials and a Firebase path sequentially (`REQ:SSID` → `REQ:PASS` → `REQ:PATH` → `REQ:TYPE`). The STM32 responds to each with the appropriate prefixed value and confirms on the OLED. Once all four steps complete, `systemReady` is set `true`.
- **Phase 2 — Terminal Mode:** Any data received over `Serial3` is rendered on the OLED as a scrolling terminal output.

Two push buttons let the user cycle text size and toggle display inversion at any point during operation.

---

## Hardware Requirements

| Component | Details |
|---|---|
| Microcontroller | STM32 (with `Serial3` available) |
| Wi-Fi Module | ESP8266 or ESP32 (UART communication) |
| Display | SH1106 128×64 OLED (I2C, address `0x3C`) |
| Left Switch (LSW) | Tactile button on `PA14` — cycles text size |
| Right Switch (RSW) | Tactile button on `PA13` — toggles display inversion |
| Pull-down resistors | Required on `PA14` and `PA13` |

---

## Wiring Diagram

```
STM32           SH1106 OLED
────────────────────────────
3.3V    →       VCC
GND     →       GND
SDA     →       SDA
SCL     →       SCL

STM32           ESP Module
────────────────────────────
Serial3 TX →    ESP RX
Serial3 RX →    ESP TX
GND        →    GND

STM32           Buttons
────────────────────────────
PA14    →       Left Switch  (LSW) → GND
PA13    →       Right Switch (RSW) → GND
```

> ⚠️ **Button wiring:** Buttons are configured as active-HIGH using external pull-down resistors. If you prefer to avoid external resistors, switch the `pinMode` to `INPUT_PULLUP` and invert the trigger logic in the edge-detection block.

---

## Dependencies

| Library | Purpose |
|---|---|
| `Wire.h` | Built-in I2C communication |
| `Adafruit_GFX` | Core graphics primitives |
| `Adafruit_SH1106` | SH1106 OLED display driver |

**Install via PlatformIO:**

```bash
pio lib install "Adafruit SH1106"
pio lib install "Adafruit GFX Library"
```

**Install via Arduino IDE:**
Library Manager → search `Adafruit SH1106` and `Adafruit GFX` → Install.

---

## How It Works

### Two-Phase Operation

```
Phase 1 — Init Handshake
─────────────────────────────────────────────
ESP sends  REQ:SSID  →  STM32 replies  SSID:<value>
ESP sends  REQ:PASS  →  STM32 replies  PASS:<value>
ESP sends  REQ:PATH  →  STM32 replies  PATH:<value>
ESP sends  REQ:TYPE  →  STM32 replies  TYPE:<value>
                                     → systemReady = true

Phase 2 — Terminal Mode
─────────────────────────────────────────────
Any incoming Serial3 data → rendered on OLED
```

### Request Parser — Enum-Based Design

Incoming ESP requests are resolved into a typed enum rather than scattered raw string comparisons. This keeps the `espInit()` handler clean and easy to extend with new request types.

```cpp
enum RequestType { REQ_SSID, REQ_PASS, REQ_PATH, REQ_TYPE, REQ_UNKNOWN };
```

Each case responds with the appropriate prefixed value and updates the OLED to confirm the exchange step.

### Firebase Path Construction

The Firebase path is assembled at runtime in `setup()` from the configured `userID` and `page` variables:

```cpp
Path = "commands/" + userID + "/" + page;
// Example result: "commands/P44J1/terminal"
```

Changing `userID` or `page` in the configuration block automatically propagates to the path — no hardcoding required.

### Button Controls

Both buttons use **edge detection** (LOW → HIGH transition) so each press fires exactly once regardless of how long the button is held.

| Button | Pin | Action |
|---|---|---|
| LSW | `PA14` | Cycle text size: `1 → 2 → 3 → 1` |
| RSW | `PA13` | Toggle display inversion |

---

## Configuration

All user-configurable values are declared at the top of `oled_terminal.ino`:

| Variable | Default | Description |
|---|---|---|
| `wifiName` | `"YOUR_SSID"` | Wi-Fi network name sent to ESP |
| `wifiP` | `"YOUR_PASSWORD"` | Wi-Fi password sent to ESP |
| `userID` | `"P44J1"` | User identifier for Firebase path |
| `page` | `"terminal"` | Page / node name for Firebase path |
| `datatype` | `"STRING"` | Data type string sent on `REQ:TYPE` |
| `LSW` | `PA14` | Text size button pin |
| `RSW` | `PA13` | Display inversion button pin |
| I2C Address | `0x3C` | OLED address (try `0x3D` if display stays blank) |
| Baud rate | `9600` | Applied to both `Serial` and `Serial3` |

---

## Project Structure

```
.
├── oled_terminal.ino   # Full sketch — handshake logic, terminal renderer, button handling
└── README.md           # This file
```

---

## Security Notice

> 🔐 **Never commit Wi-Fi credentials to a public repository.**

The sketch requires your Wi-Fi SSID and password. Before pushing to GitHub, replace real values with placeholders:

```cpp
String wifiName = "YOUR_SSID";
String wifiP    = "YOUR_PASSWORD";
```

**Recommended approach:** Create a `config.h` file to hold credentials separately from the main sketch, then add it to `.gitignore` so it is never tracked by Git.

```cpp
// config.h  (add this filename to .gitignore)
#define WIFI_SSID     "your_actual_ssid"
#define WIFI_PASSWORD "your_actual_password"
```

```cpp
// oled_terminal.ino
#include "config.h"
String wifiName = WIFI_SSID;
String wifiP    = WIFI_PASSWORD;
```

---

## Usage

1. Fill in your Wi-Fi credentials and Firebase path details in the configuration variables (or in `config.h` if using the separated approach).
2. Wire up hardware per the [Wiring Diagram](#wiring-diagram).
3. Flash `oled_terminal.ino` to your STM32.
4. On boot the OLED shows **"System Booting..."**
5. Power on the ESP module — it will initiate the `REQ:SSID → REQ:PASS → REQ:PATH → REQ:TYPE` handshake automatically.
6. Once all four steps complete, the OLED switches to **terminal mode** and renders any data the ESP sends over `Serial3`.
7. Use **LSW** (`PA14`) to cycle text size and **RSW** (`PA13`) to toggle display inversion at any time.

---

## License

This project is open source and available under the [MIT License](LICENSE).
