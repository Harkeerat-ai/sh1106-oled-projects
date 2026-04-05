# 🖥️ SH1106 OLED Projects

A collection of STM32 projects driving an **SH1106 128×64 OLED** over I2C. Each project is self-contained and shares the same base hardware setup.

---

## 📋 Table of Contents

- [Projects](#projects)
- [Shared Hardware](#shared-hardware)
- [Shared Wiring](#shared-wiring)
- [Dependencies](#dependencies)
- [Project 1 — Interactive Display](#project-1--interactive-display)
- [Project 2 — Text Animation Engine](#project-2--text-animation-engine)
- [License](#license)

---

## Projects

| # | Folder | Description |
|---|---|---|
| 1 | `/interactive-display` | Button-controlled text sizing and display inversion |
| 2 | `/text-animation` | 6-mode text animation engine with a single button |

---

## Shared Hardware

| Component | Details |
|---|---|
| Microcontroller | STM32 (or Arduino-compatible board) |
| Display | SH1106 128×64 OLED (I2C) |
| Buttons | Tactile push buttons on PA14 / PA13 |
| Pull-down resistors | Required on button pins when using `INPUT` mode |

---

## Shared Wiring

```
MCU           SH1106 OLED
─────────────────────────
3.3V    →     VCC
GND     →     GND
SDA     →     SDA
SCL     →     SCL
```

> ⚠️ Buttons are read as active-HIGH. Use external pull-down resistors to GND, or switch to `INPUT_PULLUP` and invert the trigger logic.

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

## Project 1 — Interactive Display

📁 `/interactive-display`

### Overview
Two push buttons control the OLED in real time — one cycles through three text sizes, the other toggles display inversion (swaps black and white pixels).

### Extra Wiring

```
MCU           Buttons
─────────────────────────
PA14    →     Left Switch  (LSW) — cycles text size
PA13    →     Right Switch (RSW) — toggles inversion
```

### Features

- Text size cycling: 1 → 2 → 3 → 1
- Display inversion toggle (normal ↔ inverted)
- Edge-detection button handling (fires once per press)
- 200 ms software debounce

### How It Works

**Button Edge Detection** — Actions only trigger on the rising edge (LOW → HIGH transition), so holding a button down doesn't repeatedly fire.

```cpp
if (currentLSW == 1 && lastStateLSW == 0) {
    // Fires once when the button is first pressed
}
```

**Text Size Cycling** — `textSize` increments on each press and wraps back to 1 after 3.

| Text Size | Y Position |
|---|---|
| 1 | 0 px |
| 2 | 10 px |
| 3 | 20 px |

**Display Inversion** — `display.invertDisplay()` flips all pixel values on the OLED. A boolean flag tracks the current state.

### Configuration

| Setting | Default | Description |
|---|---|---|
| `lsw` | `PA14` | Text size button pin |
| `rsw` | `PA13` | Inversion button pin |
| `textSize` | `1` | Starting text size (1–3) |
| I2C Address | `0x3C` | Change to `0x3D` if needed |
| Debounce delay | `200 ms` | Adjust for your button quality |

---

## Project 2 — Text Animation Engine

📁 `/text-animation`

### Overview
A single push button cycles through 6 real-time animation modes for a text object on screen. Built with non-blocking `millis()` timing so the display never freezes between frames.

### Extra Wiring

```
MCU           Button
─────────────────────────
PA14    →     Left Switch (LSW) — cycles animation mode
```

### Animation Modes

| Mode | Name | Description |
|---|---|---|
| 1 | Left → Right | Horizontal scroll, wraps at edge |
| 2 | Right → Left | Horizontal scroll reversed, wraps at edge |
| 3 | Top → Bottom | Vertical scroll downward, wraps at edge |
| 4 | Bottom → Top | Vertical scroll upward, wraps at edge |
| 5 | Diagonal | Linear diagonal flight, resets at edge |
| 6 | Bounce | Bounces off all four walls |

### Features

- 6 animation modes, 1 button
- Non-blocking animation via `millis()` — no `delay()` used
- Edge-detection button handling
- Persistent mode indicator (bottom-right HUD)
- Clean 3-step render pipeline: `handleInput → animate → render`

### How It Works

**Non-Blocking Timing** — Animation ticks are gated by a `millis()` check so the loop stays responsive at all times.

```cpp
if (millis() - lastUpdate < interval) return;
lastUpdate = millis();
```

**Bounce Collision** — Direction vectors are flipped when the text hits any screen wall.

```cpp
if (x <= 0 || x >= SCREEN_WIDTH - textWidth())     dx = -dx;
if (y <= 0 || y >= SCREEN_HEIGHT - (8 * textSize)) dy = -dy;
```

### Configuration

| Setting | Default | Description |
|---|---|---|
| `LSW` | `PA14` | Mode-cycle button pin |
| `message` | `"*"` | Text to animate (any string) |
| `textSize` | `1` | Text render scale (1–3) |
| `interval` | `40 ms` | Animation tick rate (~25 fps) |
| `dx` / `dy` | `2` / `1` | Movement speed per tick (pixels) |
| I2C Address | `0x3C` | Change to `0x3D` if needed |

---

## License

This project is open source and available under the [MIT License](LICENSE).
