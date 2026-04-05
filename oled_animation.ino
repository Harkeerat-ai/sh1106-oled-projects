/**
 * @file    oled_animation.ino
 * @brief   SH1106 OLED Text Animation Engine — 6 Modes, 1 Button
 *
 * Renders a text object on a 128x64 SH1106 OLED display and animates
 * it across the screen in one of 6 motion modes. A single push button
 * cycles between modes. Animation is driven by non-blocking millis()
 * timing so the main loop never stalls.
 *
 * Modes:
 *   1 - Left  → Right  (horizontal scroll)
 *   2 - Right → Left   (horizontal scroll, reversed)
 *   3 - Top   → Bottom (vertical scroll, downward)
 *   4 - Bottom → Top   (vertical scroll, upward)
 *   5 - Diagonal       (linear diagonal flight, resets at edge)
 *   6 - Bounce         (bounces off all four screen walls)
 *
 * @hardware  STM32 (or Arduino-compatible), SH1106 OLED (I2C, 0x3C)
 * @author    Your Name
 */

#include <Adafruit_SH1106.h>  // SH1106 OLED display driver
#include <Adafruit_GFX.h>     // Core graphics library (fonts, drawing primitives)
#include <Wire.h>             // I2C communication (SDA/SCL)

// ─── Display Dimensions ───────────────────────────────────────────────────────

#define SCREEN_WIDTH  128   // OLED pixel width
#define SCREEN_HEIGHT  64   // OLED pixel height

/**
 * Create the display object.
 * -1 = no dedicated hardware reset pin; reset is managed by the library internally.
 */
Adafruit_SH1106 display(-1);

// ─── Pin Definitions ──────────────────────────────────────────────────────────

#define LSW PA14  // Left Switch — cycles through animation modes on each press

// ─── Position & Velocity ──────────────────────────────────────────────────────

int x = 0,  y = 20;  // Current on-screen position of the text object (pixels)
int dx = 2, dy = 1;  // Movement delta per animation tick (pixels per update)
                     //   dx > 0 → moving right  | dx < 0 → moving left
                     //   dy > 0 → moving down   | dy < 0 → moving up

// ─── Mode Control ─────────────────────────────────────────────────────────────

int  mode    = 1;  // Active animation mode (1–6); starts at mode 1 on power-up
bool lastLSW = 0;  // Previous button state — used for rising-edge detection

// ─── Text / Content Settings ──────────────────────────────────────────────────

int    textSize = 1;    // Adafruit GFX text scale factor (1 = 8px tall, 2 = 16px, 3 = 24px)
String message  = "*";  // The string to animate; change to any text or symbol

// ─── Non-Blocking Timing ──────────────────────────────────────────────────────

unsigned long lastUpdate = 0;  // Timestamp (ms) of the last animation tick
int           interval   = 40; // Minimum ms between animation ticks (~25 fps)
                               // Lower value = faster animation

// ─── Helper: Approximate Text Width ──────────────────────────────────────────

/**
 * @brief Returns the approximate pixel width of `message` at the current textSize.
 *
 * Adafruit GFX default font characters are 6 pixels wide each at scale 1.
 * Width scales linearly with textSize, so: width = chars * 6 * textSize.
 * Used for boundary calculations in scroll and bounce modes.
 */
int textWidth() {
  return message.length() * 6 * textSize;
}

// ─── Forward Declarations ─────────────────────────────────────────────────────

void handleInput();
void animate();
void render();
void drawModeIndicator();
void resetMotion();

// ─── Setup ────────────────────────────────────────────────────────────────────

void setup() {
  pinMode(LSW, INPUT); // Button pin as digital input (needs external pull-down to GND)

  // Initialize the OLED display over I2C
  // SH1106_SWITCHCAPVCC → use internal charge pump to generate display voltage
  // 0x3C              → I2C address (some displays use 0x3D)
  display.begin(SH1106_SWITCHCAPVCC, 0x3C);

  display.setTextSize(2);      // Default text size (overridden per-frame in render())
  display.clearDisplay();      // Clear the frame buffer
  display.setTextColor(WHITE); // White pixels on black background
}

// ─── Main Loop ────────────────────────────────────────────────────────────────

/**
 * The loop follows a strict three-step pipeline every iteration:
 *
 *   1. handleInput() — read the button and update mode if pressed
 *   2. animate()     — advance the text position based on the active mode
 *   3. render()      — clear the buffer, draw the frame, push to display
 *
 * Separating these concerns prevents input from affecting mid-render state
 * and makes each step independently readable and testable.
 */
void loop() {
  handleInput();
  animate();
  render();
}

// ─── Input Handling ───────────────────────────────────────────────────────────

/**
 * @brief Reads the mode-cycle button and advances the mode on a rising edge.
 *
 * Rising edge detection (current == HIGH, last == LOW) ensures the mode
 * increments exactly once per physical button press, regardless of how
 * long the button is held down. After mode 6, wraps back to mode 1.
 * resetMotion() is called so the text starts from a clean position in
 * the new mode rather than inheriting leftover coordinates.
 */
void handleInput() {
  bool current = digitalRead(LSW);

  // Detect rising edge: button just went from not-pressed → pressed
  if (current == 1 && lastLSW == 0) {
    mode++;
    if (mode > 6) mode = 1; // Wrap: 6 → 1

    resetMotion(); // Reset position and velocity for the incoming mode
  }

  lastLSW = current; // Save state for comparison on the next loop iteration
}

// ─── Animation Logic ──────────────────────────────────────────────────────────

/**
 * @brief Advances the text object's position by one tick, if enough time has passed.
 *
 * The millis() gate makes animation non-blocking: the function returns
 * immediately if the interval hasn't elapsed, so handleInput() and render()
 * are never starved while waiting between frames.
 *
 * Each case handles one motion pattern:
 *   - Cases 1–4: linear scroll in one axis, wraps at screen edge
 *   - Case 5:    linear diagonal, resets to origin when either edge is hit
 *   - Case 6:    bounces by negating dx or dy on wall collision
 */
void animate() {
  // Non-blocking gate — only proceed if the frame interval has elapsed
  if (millis() - lastUpdate < interval) return;
  lastUpdate = millis(); // Record the time of this tick

  switch (mode) {

    case 1: // ── Left → Right scroll ──────────────────────────────────────────
      dx = 2; dy = 0;
      x += dx;
      // When the text fully exits the right edge, re-enter from the left
      if (x > SCREEN_WIDTH) x = -textWidth();
      break;

    case 2: // ── Right → Left scroll ──────────────────────────────────────────
      dx = -2; dy = 0;
      x += dx;
      // When the text fully exits the left edge, re-enter from the right
      if (x < -textWidth()) x = SCREEN_WIDTH;
      break;

    case 3: // ── Top → Bottom scroll ──────────────────────────────────────────
      dx = 0; dy = 2;
      y += dy;
      // When the text fully exits the bottom, re-enter from the top
      if (y > SCREEN_HEIGHT) y = -10;
      break;

    case 4: // ── Bottom → Top scroll ──────────────────────────────────────────
      dx = 0; dy = -2;
      y += dy;
      // When the text fully exits the top, re-enter from the bottom
      if (y < -10) y = SCREEN_HEIGHT;
      break;

    case 5: // ── Diagonal flight ──────────────────────────────────────────────
      dx = 2; dy = 1;
      x += dx;
      y += dy;
      // Reset to origin when the text reaches any edge
      // (no wrap — it teleports back to [0,0] for a clean restart)
      if (x > SCREEN_WIDTH || y > SCREEN_HEIGHT) {
        x = 0;
        y = 0;
      }
      break;

    case 6: // ── Bounce ───────────────────────────────────────────────────────
      x += dx;
      y += dy;

      /**
       * Wall collision: flip the direction component that caused the hit.
       *
       * Horizontal walls:
       *   Left  wall → x <= 0              (text hit or passed the left edge)
       *   Right wall → x >= WIDTH - width  (right side of text hit the right edge)
       *
       * Vertical walls:
       *   Top    wall → y <= 0
       *   Bottom wall → y >= HEIGHT - (8 * textSize)
       *                 8px is the base character height; scaled by textSize
       */
      if (x <= 0 || x >= SCREEN_WIDTH - textWidth())      dx = -dx;
      if (y <= 0 || y >= SCREEN_HEIGHT - (8 * textSize))  dy = -dy;
      break;
  }
}

// ─── Rendering ────────────────────────────────────────────────────────────────

/**
 * @brief Clears the frame buffer, draws the animated text and mode indicator,
 *        then pushes the buffer to the physical display.
 *
 * Called every loop iteration. display.display() is the expensive I2C transfer
 * that flushes the internal buffer to the screen — all drawing before it is
 * just writing to RAM.
 */
void render() {
  display.clearDisplay(); // Wipe the frame buffer before drawing the new frame

  display.setTextSize(textSize); // Apply current text scale
  display.setCursor(x, y);       // Position the cursor at the animated coordinate
  display.println(message);      // Write the text string to the buffer

  drawModeIndicator();   // Overlay the mode number in the bottom-right corner

  display.display();     // Flush buffer → send the complete frame to the OLED over I2C
}

// ─── Mode Indicator ───────────────────────────────────────────────────────────

/**
 * @brief Draws "Mode: N" in size-1 text at the bottom-right of the screen.
 *
 * Acts as a persistent HUD element so the user always knows which mode is active.
 * Rendered after the main text so it always appears on top.
 *
 * Position: x=64 (horizontal midpoint), y=54 (6 pixels from the bottom edge at 64px height)
 */
void drawModeIndicator() {
  display.setTextSize(1);   // Force size 1 regardless of the animation textSize
  display.setCursor(64, 54);
  display.print("Mode: ");
  display.print(mode);      // Print the integer mode number directly
}

// ─── Motion Reset ─────────────────────────────────────────────────────────────

/**
 * @brief Resets the text object to its default starting position and velocity.
 *
 * Called whenever the mode changes to avoid inheriting stale coordinates
 * from the previous mode (which could cause jumps or off-screen starts).
 *
 * Default state:
 *   Position → (0, 20)  — left edge, slightly below the top
 *   Velocity → (2,  1)  — moving right and slightly downward
 */
void resetMotion() {
  x = 0;
  y = 20;
  dx = 2;
  dy = 1;
}
