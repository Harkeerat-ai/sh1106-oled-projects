/**
 * @file    oled_display.ino
 * @brief   SH1106 OLED Interactive Display with Button Controls
 *
 * Drives a 128x64 SH1106 OLED display over I2C.
 * Two push buttons allow the user to:
 *   - Cycle through text sizes 1, 2, and 3 (Left Switch - PA14)
 *   - Toggle display color inversion       (Right Switch - PA13)
 *
 * The display always renders "Hello World" as demo content,
 * repositioned vertically to suit the active text size.
 *
 * @hardware  STM32 (or Arduino-compatible), SH1106 OLED (I2C, 0x3C)
 * @author    Harkeerat Bhasin
 */

#include <Wire.h>           // Arduino built-in I2C library (handles SDA/SCL communication)
#include <Adafruit_SH1106.h> // Third-party driver for SH1106-based OLED displays

/**
 * Instantiate the OLED display object.
 * Passing -1 as the reset pin means no dedicated hardware reset pin is used;
 * the library handles reset internally over I2C.
 */
Adafruit_SH1106 display(-1);

// ─── Pin Definitions ──────────────────────────────────────────────────────────

#define lsw PA14   // Left Switch  → cycles text size (1 → 2 → 3 → 1)
#define rsw PA13   // Right Switch → toggles display inversion

// ─── State Variables ──────────────────────────────────────────────────────────

int  textSize    = 1;     // Current text size level (1, 2, or 3)
bool lastStateLSW = 0;    // Previous read of the left switch (for edge detection)
bool lastStateRSW = 0;    // Previous read of the right switch (for edge detection)
bool inverted    = false; // Tracks whether the display is currently inverted

// ─── Forward Declaration ──────────────────────────────────────────────────────
void updateDisplay(); // Defined below; called whenever text size changes

// ─── Setup ────────────────────────────────────────────────────────────────────

void setup() {
  /**
   * Initialize the OLED display.
   * - SH1106_SWITCHCAPVCC: use the internal charge pump to generate the display voltage
   * - 0x3C: the I2C address of the display (some modules use 0x3D instead)
   */
  display.begin(SH1106_SWITCHCAPVCC, 0x3C);

  display.clearDisplay();          // Wipe any residual content from the display buffer
  display.setTextSize(1);          // Set initial text size to 1 (smallest)
  display.setTextColor(WHITE);     // Render text as white pixels on a black background

  // Configure button pins as digital inputs.
  // NOTE: These are set to INPUT (floating), so external pull-down resistors
  //       must be connected to GND to avoid false triggers when the button
  //       is not pressed. Alternatively, use INPUT_PULLUP and invert the logic.
  pinMode(lsw, INPUT);
  pinMode(rsw, INPUT);

  // Enable display inversion on startup so the screen starts with
  // a dark background and light text (aesthetic choice; remove if unwanted).
  display.invertDisplay(1);
}

// ─── Main Loop ────────────────────────────────────────────────────────────────

void loop() {
  // Sample both buttons on every iteration
  bool currentLSW = digitalRead(lsw);
  bool currentRSW = digitalRead(rsw);

  // ── Left Switch: Cycle Text Size ────────────────────────────────────────────
  /**
   * Edge detection: only act when the button transitions LOW → HIGH.
   * This ensures the action fires exactly once per physical press,
   * even if the button is held down across multiple loop iterations.
   */
  if (currentLSW == 1 && lastStateLSW == 0) {
    textSize++;                       // Advance to the next text size level
    if (textSize > 3) textSize = 1;   // Wrap around: after size 3, go back to size 1

    updateDisplay();   // Re-render the display with the new text size
    delay(200);        // Software debounce: ignore any bounce noise for 200 ms
  }

  // ── Right Switch: Toggle Display Inversion ──────────────────────────────────
  /**
   * Same edge-detection pattern as above.
   * Flips the 'inverted' flag and updates the display accordingly.
   * Inverted mode swaps all pixel values (white ↔ black).
   */
  if (currentRSW == 1 && lastStateRSW == 0) {
    inverted = !inverted; // Toggle the inversion state

    if (inverted) {
      display.invertDisplay(1); // Enable inversion: dark background, light content
    } else {
      display.invertDisplay(0); // Disable inversion: light background, dark content
    }

    delay(200); // Software debounce
  }

  // ── Save Current States for Next Iteration ──────────────────────────────────
  // These become the "last state" on the next loop cycle, enabling edge detection.
  lastStateLSW = currentLSW;
  lastStateRSW = currentRSW;
}

// ─── Display Update Helper ────────────────────────────────────────────────────

/**
 * @brief Clears and redraws the display with the current textSize setting.
 *
 * Called every time the text size changes. The vertical cursor position (yPos)
 * shifts down slightly as text size increases to keep the content visually
 * balanced within the 64-pixel display height.
 *
 * Text size to pixel height reference (Adafruit GFX font):
 *   Size 1 →  8 px tall
 *   Size 2 → 16 px tall
 *   Size 3 → 24 px tall
 */
void updateDisplay() {
  display.clearDisplay();          // Clear the internal frame buffer
  display.setTextSize(textSize);   // Apply the new size before drawing
  display.setTextColor(WHITE);     // Ensure text color is set (resets after clearDisplay)

  /**
   * Compute a vertical offset so the text doesn't hug the very top edge
   * as it gets larger. Adjust these values to reposition text as desired.
   *
   *   textSize == 1 → yPos =  0
   *   textSize == 2 → yPos = 10
   *   textSize == 3 → yPos = 20
   */
  int yPos = (textSize == 1) ? 0 : (textSize == 2) ? 10 : 20;

  display.setCursor(0, yPos);      // Set the text origin (x=0, y=yPos)
  display.println("Hello World");  // Write the string to the buffer (with newline)

  display.display();               // Push the buffer to the physical display over I2C
}
