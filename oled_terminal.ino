/**
 * @file    oled_terminal.ino
 * @brief   STM32 ESP Wi-Fi Handshake + OLED Terminal Display
 *
 * This sketch connects an STM32 to an ESP Wi-Fi module over UART (Serial3)
 * and drives an SH1106 128x64 OLED display over I2C.
 *
 * Operation is split into two phases:
 *
 *   Phase 1 — Init Handshake:
 *     The ESP sends structured requests (REQ:SSID, REQ:PASS, REQ:PATH, REQ:TYPE).
 *     The STM32 replies with the corresponding credential or config value.
 *     Once REQ:TYPE is handled, systemReady is set to true.
 *
 *   Phase 2 — Terminal Mode:
 *     Any data received from the ESP over Serial3 is displayed on the OLED
 *     as a live terminal readout.
 *
 * Two push buttons allow text size cycling and display inversion at any time.
 *
 * @hardware  STM32, ESP8266/ESP32 (UART), SH1106 OLED (I2C 0x3C)
 * @author    Your Name
 *
 * ⚠️  SECURITY: Never commit real credentials to a public repo.
 *     Replace wifiName and wifiP with placeholders before pushing to GitHub.
 */

#include <Adafruit_SH1106.h>  // SH1106 OLED display driver
#include <Adafruit_GFX.h>     // Core graphics library (fonts, drawing primitives)
#include <Wire.h>             // I2C communication for the display

// ─── Credentials & Configuration ─────────────────────────────────────────────
// ⚠️  Replace these with your actual values before flashing.
//     DO NOT push real credentials to a public GitHub repository.
//     Consider moving these to a config.h file added to .gitignore.

String wifiName = "YOUR_SSID";       // Wi-Fi SSID sent to the ESP on REQ:SSID
String wifiP    = "YOUR_PASSWORD";   // Wi-Fi password sent to the ESP on REQ:PASS

// Firebase / backend path components
String userID   = "P44J1";           // Unique user identifier
String page     = "terminal";        // Firebase node / page name
String datatype = "STRING";          // Data type sent to the ESP on REQ:TYPE

// Path is assembled at runtime in setup():  "commands/<userID>/<page>"
String Path     = "";

// ─── Runtime State ────────────────────────────────────────────────────────────

String request  = "";    // Stores the latest string received from the ESP over Serial3

// ─── Pin Definitions ──────────────────────────────────────────────────────────

#define LSW PA14   // Left Switch  — cycles text size (1 → 2 → 3 → 1)
#define RSW PA13   // Right Switch — toggles display inversion

// ─── Display Settings ─────────────────────────────────────────────────────────

int  textSize     = 1;      // Current text scale factor (1 = 8px, 2 = 16px, 3 = 24px)
bool inverted     = false;  // Tracks current display inversion state

// ─── Button Edge Detection ────────────────────────────────────────────────────

bool lastStateLSW = 0;   // Previous read of LSW (for rising-edge detection)
bool lastStateRSW = 0;   // Previous read of RSW (for rising-edge detection)

// ─── System State ─────────────────────────────────────────────────────────────

bool systemReady = false;
/**
 * systemReady gates two operating modes:
 *   false → incoming Serial3 data is treated as an init handshake request
 *   true  → incoming Serial3 data is displayed directly on the OLED terminal
 *
 * Set to true after the ESP sends REQ:TYPE (the final handshake step).
 */

// ─── Display Object ───────────────────────────────────────────────────────────

/**
 * Instantiate the SH1106 display.
 * -1 = no dedicated hardware reset pin; reset is managed internally by the library.
 */
Adafruit_SH1106 display(-1);

// ─── Request Type Enum ────────────────────────────────────────────────────────

/**
 * Typed representation of the ESP's init request commands.
 * Using an enum instead of raw string comparisons keeps the handler
 * clean, compiler-checkable, and easy to extend with new request types.
 */
enum RequestType {
  REQ_SSID,     // ESP is requesting the Wi-Fi SSID
  REQ_PASS,     // ESP is requesting the Wi-Fi password
  REQ_PATH,     // ESP is requesting the Firebase path
  REQ_TYPE,     // ESP is requesting the data type (final handshake step)
  REQ_UNKNOWN   // Anything that doesn't match a known request string
};

// ─── Request Parser ───────────────────────────────────────────────────────────

/**
 * @brief Maps a raw request string received from the ESP to a RequestType enum value.
 *
 * Expected request format: "REQ:<KEYWORD>"
 * Examples: "REQ:SSID", "REQ:PASS", "REQ:PATH", "REQ:TYPE"
 *
 * @param  req  The trimmed string received from Serial3
 * @return      The matching RequestType, or REQ_UNKNOWN if unrecognized
 */
RequestType getRequestType(String req) {
  if      (req == "REQ:SSID") return REQ_SSID;
  else if (req == "REQ:PASS") return REQ_PASS;
  else if (req == "REQ:PATH") return REQ_PATH;
  else if (req == "REQ:TYPE") return REQ_TYPE;
  else                        return REQ_UNKNOWN;
}

// ─── OLED Helpers ─────────────────────────────────────────────────────────────

/**
 * @brief Clears the display and prints a single message string from the top-left.
 *
 * This is the primary display utility used throughout the sketch.
 * Text size is read from the global `textSize` so button changes apply immediately.
 *
 * @param msg  The string to display on the OLED
 */
void oledDisplay(String msg) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(textSize);
  display.setTextColor(WHITE);
  display.println(msg);
  display.display();       // Flush buffer → push frame to the OLED over I2C
}

/**
 * @brief Refreshes the display to show the latest terminal input from the ESP.
 *
 * Called continuously in loop() once systemReady is true, so the OLED
 * always reflects the most recently received Serial3 message.
 */
void updateDisplay() {
  oledDisplay("Terminal: " + request);
}

// ─── ESP Init Handshake Handler ───────────────────────────────────────────────

/**
 * @brief Responds to ESP initialization requests before the system is ready.
 *
 * The ESP sends requests one at a time in this expected order:
 *   REQ:SSID  →  STM32 sends "SSID:<wifiName>"
 *   REQ:PASS  →  STM32 sends "PASS:<wifiP>"
 *   REQ:PATH  →  STM32 sends "PATH:<Path>"
 *   REQ:TYPE  →  STM32 sends "TYPE:<datatype>" and sets systemReady = true
 *
 * Each step also updates the OLED to confirm what was sent.
 * A 100 ms delay after each reply gives the ESP time to process before sending
 * the next request.
 *
 * If an unrecognized request arrives during init, it is ignored and logged
 * to the debug serial port.
 */
void espInit() {
  switch (getRequestType(request)) {

    case REQ_SSID:
      Serial3.println("SSID:" + wifiName);  // Send SSID to the ESP
      Serial.println("Sent SSID");           // Debug log to USB serial
      oledDisplay("Sent SSID");
      break;

    case REQ_PASS:
      Serial3.println("PASS:" + wifiP);
      Serial.println("Sent PASS");
      oledDisplay("Sent PASS");
      break;

    case REQ_PATH:
      Serial3.println("PATH:" + Path);
      Serial.println("Sent PATH");
      oledDisplay("Sent PATH");
      break;

    case REQ_TYPE:
      Serial3.println("TYPE:" + datatype);
      Serial.println("Sent TYPE");
      oledDisplay("System Ready");
      systemReady = true;  // Handshake complete — switch to terminal mode
      break;

    default:
      // Unrecognized request during init — ignore and log
      oledDisplay("Waiting for init...");
      Serial.println("Ignored (not ready): " + request);
      break;
  }

  delay(100); // Brief pause to let the ESP process the response before the next request
}

// ─── Setup ────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(9600);   // USB debug serial (monitor with Serial Monitor)
  Serial3.begin(9600);  // UART to ESP module (TX3/RX3 on STM32)

  Serial.println("STM32 Booted");

  // Build the Firebase database path at runtime:
  //   "commands/<userID>/<page>"  →  e.g. "commands/P44J1/terminal"
  // Keeping this dynamic means changing userID or page automatically updates the path.
  Path = "commands/" + userID + "/" + page;

  // Configure button pins as digital inputs
  // Needs external pull-down resistors to GND (or change to INPUT_PULLUP + invert logic)
  pinMode(LSW, INPUT);
  pinMode(RSW, INPUT);

  // Initialize the OLED over I2C
  // SH1106_SWITCHCAPVCC → internal charge pump generates display voltage
  // 0x3C               → I2C address (try 0x3D if display stays blank)
  display.begin(SH1106_SWITCHCAPVCC, 0x3C);

  display.setTextSize(1);       // Default text scale
  display.invertDisplay(1);     // Start inverted (aesthetic choice — remove if unwanted)

  oledDisplay("System Booting...");  // Show boot message while setup completes
}

// ─── Main Loop ────────────────────────────────────────────────────────────────

void loop() {

  // ── Serial3 Input (ESP → STM32) ─────────────────────────────────────────────
  if (Serial3.available()) {
    // Read until newline, then strip any trailing whitespace or \r
    request = Serial3.readStringUntil('\n');
    request.trim();

    Serial.println("RX: " + request);  // Log every received message for debugging

    if (!systemReady) {
      // Phase 1 — Handshake: only process init requests
      espInit();
    } else {
      // Phase 2 — Terminal Mode: display any incoming ESP data directly
      oledDisplay("Terminal: " + request);
    }
  }

  // ── Button Handling ──────────────────────────────────────────────────────────

  bool currentLSW = digitalRead(LSW);
  bool currentRSW = digitalRead(RSW);

  /**
   * Left Switch — Cycle Text Size
   * Edge detection: only fires on the rising edge (LOW → HIGH transition),
   * so holding the button doesn't repeatedly increment textSize.
   * Wraps: 1 → 2 → 3 → 1
   */
  if (currentLSW == 1 && lastStateLSW == 0) {
    textSize++;
    if (textSize > 3) textSize = 1;
    delay(200); // Software debounce — ignore bounce noise for 200 ms
  }

  /**
   * Right Switch — Toggle Display Inversion
   * Flips the 'inverted' flag and updates the display accordingly.
   * Inverted mode swaps all pixel values (white ↔ black).
   */
  if (currentRSW == 1 && lastStateRSW == 0) {
    inverted = !inverted;
    display.invertDisplay(inverted); // Pass the bool directly (true = inverted)
    delay(200); // Software debounce
  }

  // Save button states for edge detection on the next loop iteration
  lastStateLSW = currentLSW;
  lastStateRSW = currentRSW;

  // ── Continuous Terminal Refresh ───────────────────────────────────────────────
  /**
   * Once systemReady is true, keep the OLED updated with the latest
   * received message on every loop iteration. This ensures the display
   * stays current even if no new data arrives (e.g. after a text size change).
   */
  if (systemReady) {
    updateDisplay();
  }
}
