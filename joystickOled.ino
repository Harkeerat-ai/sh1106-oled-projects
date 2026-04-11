#include <Adafruit_SH1106.h>
#include <Adafruit_GFX.h>
#include <Wire.h>

Adafruit_SH1106 display(-1);

// Screen position
int posX = 64;
int posY = 32;

// Joystick center (approx from your data)
int centerX = 2050;
int centerY = 2050;

// Dead zone
int deadZone = 100;

void setup() {
  Serial.begin(9600);
  display.begin(SH1106_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextSize(1);
  display.invertDisplay(1);
  display.setTextColor(WHITE);
  display.display();
}

void loop() {
  int x = analogRead(PA2);
  int y = analogRead(PA3);

  // Map joystick to screen
  int mappedX = map(x, 0, 4096, 0, 127);
  int mappedY = map(y, 0, 4096, 0, 63);

  // Apply dead zone (to avoid jitter)
  if (abs(x - centerX) > deadZone) {
    posX = mappedX;
  }

  if (abs(y - centerY) > deadZone) {
    posY = mappedY;
  }

  // Clamp values (just in case)
  posX = constrain(posX, 0, 127);
  posY = constrain(posY, 0, 63);

  // Display
  display.clearDisplay();
  display.setCursor(posX, posY);
  display.print("*");
  display.display();

  delay(50);
}