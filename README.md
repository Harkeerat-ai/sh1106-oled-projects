# 🖥️ SH1106 OLED Interactive Display

A lightweight Arduino/STM32 project that drives an **SH1106-based 128×64 OLED display** with two push-button controls — one to cycle through text sizes and another to toggle display inversion.

---

## 📋 Table of Contents

- [Overview](#overview)
- [Hardware Requirements](#hardware-requirements)
- [Wiring Diagram](#wiring-diagram)
- [Dependencies](#dependencies)
- [Features](#features)
- [How It Works](#how-it-works)
- [Configuration](#configuration)
- [Usage](#usage)
- [License](#license)

---

## Overview

This project demonstrates basic I2C OLED control on an STM32 (or compatible) microcontroller. Two tactile switches are used to interact with the display in real time:

- **Left Switch (PA14)** — Cycles the displayed text through three size levels (1 → 2 → 3 → back to 1)
- **Right Switch (PA13)** — Toggles the display between normal and inverted color modes

The display always shows `"Hello World"` as the demo text, repositioned vertically based on the selected text size.

---

## Hardware Requirements

| Component | Details |
|---|---|
| Microcontroller | STM32 (or Arduino-compatible board) |
| Display | SH1106 128×64 OLED (I2C) |
| Switch 1 (LSW) | Tactile push button on pin **PA14** |
| Switch 2 (RSW) | Tactile push button on pin **PA13** |
| Pull-down resistors | Required if not using `INPUT_PULLUP` (see [Configuration](#configuration)) |

---

## Wiring Diagram

```
MCU           SH1106 OLED
─────────────────────────
3.3V    →     VCC
GND     →     GND
SDA     →     SDA  (I2C Data)
SCL     →     SCL  (I2C Clock)

MCU           Buttons
─────────────────────────
PA14    →     Left Switch  (LSW) → GND
PA13    →     Right Switch (RSW) → GND
```

> ⚠️ The buttons are read as active-HIGH (`digitalRead == 1` triggers action). Make sure the pins are properly pulled down to GND when the button is not pressed, or change `INPUT` to `INPUT_PULLUP` and invert the logic.

---

## Dependencies

Install these libraries via the Arduino Library Manager or PlatformIO:

| Library | Purpose |
|---|---|
| `Wire.h` | Built-in I2C communication library |
| `Adafruit_SH1106` | OLED driver for SH1106-based displays |

```bash
# PlatformIO example
pio lib install "Adafruit SH1106"
```

---

## Features

- ✅ I2C OLED display initialization and rendering
- ✅ Real-time text size cycling (3 levels)
- ✅ Display inversion toggle
- ✅ Software debouncing for both buttons (200 ms delay)
- ✅ Edge-detection based button handling (fires once per press, not per held state)

---

## How It Works

### Button Edge Detection

Rather than triggering on the raw button state, the code compares the **current state** to the **last recorded state**. An action fires only on the **rising edge** (button transitions from not-pressed → pressed), preventing repeated triggers from a single long press.

```cpp
if (currentLSW == 1 && lastStateLSW == 0) {
    // Fires once when the button is first pressed
}
```

### Text Size Cycling

`textSize` is an integer that increments on each left-button press and wraps back to `1` after reaching `3`.

```
textSize: 1 → 2 → 3 → 1 → ...
```

The vertical cursor position (`yPos`) is adjusted per size to keep text visually centered:

| Text Size | Y Position |
|---|---|
| 1 | 0 px |
| 2 | 10 px |
| 3 | 20 px |

### Display Inversion

The `display.invertDisplay()` call flips all pixel values on the OLED — white becomes black and vice versa. The `inverted` boolean flag tracks the current state and toggles it on each right-button press.

---

## Configuration

| Constant / Variable | Default | Description |
|---|---|---|
| `lsw` | `PA14` | Pin for the left (text size) button |
| `rsw` | `PA13` | Pin for the right (invert) button |
| `textSize` | `1` | Starting text size (1–3) |
| `inverted` | `false` | Starting display inversion state |
| I2C Address | `0x3C` | OLED I2C address (change if your display uses `0x3D`) |
| Debounce delay | `200 ms` | Adjust lower for faster response, higher for noisier buttons |

---

## Usage

1. Wire up the hardware as described in the [Wiring Diagram](#wiring-diagram).
2. Install the required [Dependencies](#dependencies).
3. Flash the sketch to your board.
4. On power-up, the display will initialize and show **"Hello World"** with display inversion enabled by default.
5. Press the **Left Switch** to cycle through text sizes.
6. Press the **Right Switch** to toggle the inverted display mode.

---

## License

This project is open source and available under the [MIT License](LICENSE).
