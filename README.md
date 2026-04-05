# 📡 STM32 ESP OLED Terminal

An STM32 project that bridges an **ESP Wi-Fi module** and an **SH1106 OLED display**, handling a structured serial handshake to configure the ESP, then switching into a live terminal display mode once the system is ready.

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

This project connects an STM32 microcontroller to an ESP module over UART (`Serial3`) and drives an SH1106 OLED display. On boot, the STM32 waits for the ESP to request credentials and path configuration via a structured request protocol (`REQ:SSID`, `REQ:PASS`, `REQ:PATH`, `REQ:TYPE`). Once all four handshake steps complete, the system marks itself as ready and the OLED switches into terminal mode — displaying any incoming serial data from the ESP in real time.

Two push buttons allow the user to adjust text size and toggle display inversion at any time.

---

## Hardware Requirements

| Component | Details |
|---|---|
| Microcontroller | STM32 (with Serial3 available) |
| Wi-Fi Module | ESP8266 / ESP32 (communicating over UART) |
| Display | SH1106 128×64 OLED (I2C, 0x3C) |
| Left Switch (LSW) | Tactile button on **PA14** — cycles text size |
| Right Switch (RSW) | Tactile button on **PA13** — toggles inversion |
| Pull-down resistors | Required on PA14 and PA13 |

---

## Wiring Diagram

```
STM32         SH1106 OLED
─────────────────────────
3.3V    →     VCC
GND     →     GND
SDA     →     SDA
SCL     →     SCL

STM32         ESP Module
─────────────────────────
Serial3 TX →  ESP RX
Serial3 RX →  ESP TX
GND        →  GND

STM32         Buttons
─────────────────────────
PA14    →     Left Switch  (LSW) → GND
PA13    →     Right Switch (RSW) → GND
```

> ⚠️ Buttons are wired as active-HIGH. Use external pull-down resistors, or switch to `INPUT_PULLUP` and invert the trigger logic.

---

## Dependencies

| Library | Purpose |
|---|---|
| `Wire.h` | Built-in I2C communication |
| `Adafruit_GFX` | Core graphics primitives |
| `Adafruit_SH1106` | SH1106 OLED display driver |

```bash
# PlatformIO
pio lib install "Adafruit SH1106"
pio lib install "Adafruit GFX Library"
```

---

## How It Works

### Two-Phase Operation

The system operates in two distinct phases separated by a `systemReady` flag:

```
Phase 1 — Init Handshake
  ESP sends REQ:SSID → STM32 replies SSID:<value>
  ESP sends REQ:PASS → STM32 replies PASS:<value>
  ESP sends REQ:PATH → STM32 replies PATH:<value>
  ESP sends REQ:TYPE → STM32 replies TYPE:<value>
                     → systemReady = true

Phase 2 — Terminal Mode
  Any incoming Serial3 data → displayed on OLED as terminal output
```

### Request Parser (Enum-Based)

Incoming requests are parsed into a typed enum rather than raw string comparisons scattered across the code. This makes the handler clean and easy to extend.

```cpp
enum RequestType { REQ_SSID, REQ_PASS, REQ_PATH, REQ_TYPE, REQ_UNKNOWN };
```

Each case in `espInit()` responds with the appropriate prefixed value and updates the OLED to confirm what was sent.

### Firebase Path Construction

The Firebase path is assembled at runtime in `setup()` from the user ID and page name:

```cpp
Path = "commands/" + userID + "/" + page;
// Result: "commands/P44J1/terminal"
```

This keeps the path dynamic — change `userID` or `page` and the path updates automatically.

### Button Controls

Both buttons use edge detection (LOW → HIGH transition) to fire exactly once per press:

| Button | Pin | Action |
|---|---|---|
| LSW | PA14 | Cycle text size: 1 → 2 → 3 → 1 |
| RSW | PA13 | Toggle display inversion |

---

## Configuration

| Variable | Default | Description |
|---|---|---|
| `wifiName` | `"YOUR_SSID"` | Wi-Fi network name sent to ESP |
| `wifiP` | `"YOUR_PASSWORD"` | Wi-Fi password sent to ESP |
| `userID` | `"P44J1"` | User identifier for Firebase path |
| `page` | `"terminal"` | Page/node name for Firebase path |
| `datatype` | `"STRING"` | Data type sent to ESP on REQ:TYPE |
| `LSW` | `PA14` | Text size button pin |
| `RSW` | `PA13` | Inversion button pin |
| I2C Address | `0x3C` | OLED address (try `0x3D` if blank) |
| Baud rate | `9600` | Used for both Serial and Serial3 |

---

## Project Structure

```
.
├── oled_terminal.ino   # Full sketch
└── README.md           # This file
```

---

## Security Notice

> 🔐 **Never commit Wi-Fi credentials to a public repository.**

This sketch requires you to fill in your Wi-Fi SSID and password. Before pushing to GitHub, replace the credential values with placeholders:

```cpp
String wifiName = "YOUR_SSID";
String wifiP    = "YOUR_PASSWORD";
```

Consider adding a `config.h` file (added to `.gitignore`) to store credentials separately from the main sketch.

---

## Usage

1. Fill in your Wi-Fi credentials and Firebase path details in the configuration variables.
2. Wire up hardware per the [Wiring Diagram](#wiring-diagram).
3. Flash the sketch to your STM32.
4. On boot, the OLED shows `"System Booting..."`.
5. Power on the ESP module — it will begin the `REQ:SSID → REQ:PASS → REQ:PATH → REQ:TYPE` handshake automatically.
6. Once all four steps complete, the OLED switches to terminal mode and displays any data the ESP sends.
7. Use **LSW** to cycle text size and **RSW** to toggle display inversion at any time.

---

## License

This project is open source and available under the [MIT License](LICENSE).
