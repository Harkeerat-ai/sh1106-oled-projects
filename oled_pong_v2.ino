/**
 * @file    oled_pong_v2.ino
 * @brief   Pong Game v2 — SH1106 OLED, Menu, PvC & PvP, Live Scores, Win at 10
 *
 * A full-featured Pong game on a 128×64 SH1106 OLED with three game states:
 *   MENU  → mode selection screen (P1 v CPU  /  P1 v P2)
 *   GAME  → active play with live score HUD and dashed center line
 *   WIN   → winner announcement with final score; button returns to menu
 *
 * Improvements over v1:
 *   - Score tracking with live HUD strip at the top of the display
 *   - First to WIN_SCORE (10) points triggers the WIN screen
 *   - WIN screen shows winner name, final score, and prompts for replay
 *   - Angle deflection on paddle hit: contact point determines rebound angle
 *     (edge hit → steep; center hit → shallow), making rallies more dynamic
 *   - Ball speeds up ~5 % on each paddle hit, capped at MAX_BALL_SPEED
 *   - Velocity-direction gating on collision prevents the "sticky paddle" bug
 *   - Playfield bounded below the HUD (y = HUD_H to 63); paddles and ball
 *     respect this boundary so the score text is never overwritten
 *   - Dashed center divider for classic Pong aesthetics
 *   - millis()-based frame pacing instead of blocking delay() — frame rate is
 *     now consistent even when I2C transfers vary in duration
 *   - Ball served toward the player who just lost a point (fair start)
 *
 * Classes:
 *   Joystick — reads and normalises analog Y input with deadzone filtering
 *   Button   — edge-detecting pull-up button; arms only after first release
 *              to prevent boot-time false triggers
 *   Paddle   — player or AI paddle; supports reset(), update(), aiMove(), draw()
 *   Ball     — position, velocity, directional reset, wall bounce, and render
 *
 * State machine transitions:
 *   MENU → GAME  (select button confirms mode)
 *   GAME → WIN   (a player reaches WIN_SCORE)
 *   WIN  → MENU  (select button pressed on win screen)
 *
 * Frame loop (FRAME_MS = 8 ms ≈ 125 fps target):
 *   MENU → drawMenu()  + updateMenu()
 *   GAME → runGame()   [input → paddles → ball → collide/score → render]
 *   WIN  → drawWin()   + updateWin()
 *
 * @hardware  STM32, SH1106 128×64 OLED (I2C 0x3C),
 *            Joystick P1 (PA4=X, PA5=Y),  Joystick P2 (PA2=X, PA3=Y),
 *            Select Button (PA14, INPUT_PULLUP, active LOW)
 * @author    Harkeerat
 * @version   2.0
 */

#include <Adafruit_SH1106.h>   // SH1106 OLED driver
#include <Adafruit_GFX.h>      // Core graphics primitives
#include <Wire.h>              // I2C bus

// ─────────────────────────────────────────────────────────────────────────────
// DISPLAY
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Global display object.
 * Passing -1 as the reset pin tells the library to manage reset internally;
 * no external RST wire is needed.
 */
Adafruit_SH1106 display(-1);

// ─────────────────────────────────────────────────────────────────────────────
// CONSTANTS
// ─────────────────────────────────────────────────────────────────────────────

const int   WIN_SCORE      = 10;    ///< Points required to win the match
const int   HUD_H          = 9;     ///< Pixel rows reserved for score HUD (y 0–8)
const int   FIELD_TOP      = HUD_H; ///< First playable y pixel (below HUD line)
const int   FIELD_BOT      = 63;    ///< Last playable y pixel
const int   FRAME_MS       = 8;     ///< Target milliseconds per frame (~125 fps)
const float MAX_BALL_SPEED = 5.0f;  ///< Maximum |vx| after rally speedups

// ─────────────────────────────────────────────────────────────────────────────
// STATE MACHINE
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @enum GameState
 * @brief The three operating states of the game.
 *
 *   MENU — mode selection screen
 *   GAME — active gameplay
 *   WIN  — winner announcement screen
 */
enum GameState { MENU, GAME, WIN };
GameState gameState = MENU; ///< Current state; begins at MENU on power-up

bool isPvP  = false; ///< false = P1 v CPU,  true = P1 v P2
int  winner = 0;     ///< Winning side: 1 = P1,  2 = P2 / CPU

// ─────────────────────────────────────────────────────────────────────────────
// SCORES
// ─────────────────────────────────────────────────────────────────────────────

int score1 = 0; ///< P1 (left paddle) cumulative score
int score2 = 0; ///< P2 / CPU (right paddle) cumulative score

// ─────────────────────────────────────────────────────────────────────────────
// CLASS: Joystick
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @class Joystick
 * @brief Reads and normalises the analog X/Y axes with deadzone filtering.
 *
 * The STM32 ADC is 12-bit (0–4095). At mechanical rest the axes sit near
 * 2050. Readings within ±deadZone of center are clamped to zero to eliminate
 * stick drift. The remaining range is normalised to [-1.0, 1.0].
 *
 * Only getDY() is used for paddle movement; getDX() is provided for
 * potential future use (e.g., menu left/right navigation).
 */
class Joystick {
  public:
    int pinX, pinY;
    int centerX  = 2050; ///< ADC center value for X axis
    int centerY  = 2050; ///< ADC center value for Y axis
    int deadZone = 60;   ///< ±60 ADC counts (~3 % of full range) treated as zero

    /**
     * @brief Constructs the joystick with specified analog pins.
     * @param xPin  Analog pin for X axis
     * @param yPin  Analog pin for Y axis
     */
    Joystick(int xPin, int yPin) : pinX(xPin), pinY(yPin) {}

    /**
     * @brief Returns normalised X displacement in [-1.0, 1.0], 0 in deadzone.
     */
    float getDX() {
      int x = analogRead(pinX);
      if (abs(x - centerX) < deadZone) return 0.0f;
      return (float)(x - centerX) / 4096.0f;
    }

    /**
     * @brief Returns normalised Y displacement in [-1.0, 1.0], 0 in deadzone.
     *
     * Positive values mean joystick pushed down (ADC above center).
     * The Paddle::update() method inverts this so pushing down moves the
     * paddle downward on screen.
     */
    float getDY() {
      int y = analogRead(pinY);
      if (abs(y - centerY) < deadZone) return 0.0f;
      return (float)(y - centerY) / 4096.0f;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// CLASS: Button
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @class Button
 * @brief Edge-detecting digital button using INPUT_PULLUP (active LOW).
 *
 * Reports true on the falling edge (HIGH → LOW) only.
 * The `armed` flag ensures the button cannot trigger at boot — it waits
 * until the pin is observed HIGH at least once before registering any press.
 * This prevents a held-down button during power-on from immediately
 * transitioning out of the MENU state.
 */
class Button {
  public:
    int  pin;
    bool lastState = HIGH;
    bool armed     = false; ///< Set true once pin is observed HIGH after boot

    /**
     * @brief Constructs the button and configures the pin as INPUT_PULLUP.
     * @param p  Digital pin number
     */
    Button(int p) : pin(p) {
      pinMode(pin, INPUT_PULLUP);
    }

    /**
     * @brief Returns true exactly once per physical button press.
     *
     * Arm-then-detect pattern:
     *   1. Before first HIGH reading — return false (button is not yet armed)
     *   2. After arming — return true only on the HIGH→LOW edge
     *
     * @return true on the falling edge (button pressed), false otherwise
     */
    bool wasPressed() {
      bool current = digitalRead(pin);

      // Wait until pin goes HIGH at least once before arming
      if (!armed) {
        if (current == HIGH) armed = true;
        lastState = current;
        return false;
      }

      // Detect falling edge: HIGH → LOW
      if (lastState == HIGH && current == LOW) {
        lastState = current;
        return true;
      }

      lastState = current;
      return false;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// CLASS: Paddle
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @class Paddle
 * @brief Represents a Pong paddle — shared by player and AI control paths.
 *
 * The paddle is a 2×height white rectangle at a fixed X position.
 * Vertical position (y) is stored as a float for sub-pixel precision so
 * movement at fractional speeds accumulates correctly over many frames.
 * Rendering uses (int)y, and position is clamped to [FIELD_TOP, FIELD_BOT -
 * height] so the paddle always stays within the playfield.
 *
 * The AI path (aiMove) tracks the ball center at 65 % of player speed.
 * This slight handicap gives a human player a fair fighting chance.
 */
class Paddle {
  public:
    float y;            ///< Top-edge Y position (float for smooth movement)
    int   x;            ///< Fixed horizontal pixel position
    int   height = 12;  ///< Paddle height in pixels
    float speed  = 4.5f;///< Movement speed in pixels per frame

    /**
     * @brief Constructs a paddle at posX, vertically centred in the playfield.
     * @param posX  Fixed horizontal position
     */
    Paddle(int posX) : x(posX) {
      reset();
    }

    /**
     * @brief Resets the paddle to the vertical centre of the playfield.
     *
     * Called at game start and after each point is scored so both paddles
     * return to a neutral starting position before the next serve.
     */
    void reset() {
      y = FIELD_TOP + (FIELD_BOT - FIELD_TOP - height) / 2.0f;
    }

    /**
     * @brief Moves the paddle based on joystick input.
     *
     * Multiplies normalised joystick delta by speed, then subtracts (inverts
     * the axis) so that joystick-up maps to on-screen-up.
     * constrain() prevents the paddle from leaving the playfield.
     *
     * @param dy  Normalised joystick Y axis value in [-1.0, 1.0]
     */
    void update(float dy) {
      y -= dy * speed;
      y  = constrain(y, (float)FIELD_TOP, (float)(FIELD_BOT - height));
    }

    /**
     * @brief Moves the paddle toward a target Y coordinate (AI control).
     *
     * Computes the signed error between the paddle centre and targetY,
     * then steps toward the target at 65 % of player speed.
     * abs()/sign maths are avoided: simple comparisons keep the step
     * bounded to ±speed automatically.
     *
     * @param targetY  Target Y coordinate — pass ball.y + 1 to track ball centre
     */
    void aiMove(float targetY) {
      float centre = y + height / 2.0f;
      if      (centre < targetY) y += speed * 0.65f;
      else if (centre > targetY) y -= speed * 0.65f;
      y = constrain(y, (float)FIELD_TOP, (float)(FIELD_BOT - height));
    }

    /**
     * @brief Renders the paddle as a 2×height white rectangle at (x, y).
     */
    void draw() {
      display.fillRect(x, (int)y, 2, height, WHITE);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// CLASS: Ball
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @class Ball
 * @brief Represents the Pong ball — position, velocity, wall bounce, render.
 *
 * All values are floats for smooth diagonal motion.
 * Wall bouncing (top/bottom of the playfield) is handled inside update().
 * Paddle collision, speed changes, and out-of-bounds scoring are handled
 * externally in checkCollision().
 */
class Ball {
  public:
    float x,  y;   ///< Position of the top-left corner of the 2×2 ball
    float vx, vy;  ///< Velocity components in pixels per frame

    /**
     * @brief Default constructor — calls reset(-1) to initialise.
     */
    Ball() { reset(-1); }

    /**
     * @brief Resets ball to playfield centre with a fresh velocity.
     *
     * Called at match start and after each point is scored.
     * The ball is served toward the player who just lost the point.
     *
     * @param direction  -1 = move left (toward P1),  +1 = move right (toward P2)
     */
    void reset(int direction) {
      x  = 64.0f;
      y  = FIELD_TOP + (FIELD_BOT - FIELD_TOP) / 2.0f;
      vx = 2.2f * direction; ///< Slightly faster than v1 (was 2.0) for better feel
      vy = 1.5f;
    }

    /**
     * @brief Advances ball position one frame and bounces off playfield walls.
     *
     * Top wall (y <= FIELD_TOP) and bottom wall (y >= FIELD_BOT - 1) clamp
     * position and flip vy sign. Clamping before sign flip prevents the ball
     * getting "stuck" inside a wall when vx is high.
     */
    void update() {
      x += vx;
      y += vy;

      if (y <= FIELD_TOP) {
        y  = FIELD_TOP;
        vy = abs(vy);    // Force downward after top-wall hit
      }
      if (y >= FIELD_BOT - 1) {
        y  = FIELD_BOT - 1;
        vy = -abs(vy);   // Force upward after bottom-wall hit
      }
    }

    /**
     * @brief Renders the ball as a 2×2 white square at (x, y).
     */
    void draw() {
      display.fillRect((int)x, (int)y, 2, 2, WHITE);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// GLOBALS
// ─────────────────────────────────────────────────────────────────────────────

Joystick joy1(PA4, PA5); ///< P1 joystick — left paddle + menu navigation
Joystick joy2(PA2, PA3); ///< P2 joystick — right paddle in PvP mode
Button   selectBtn(PA14);///< Confirm / select button (INPUT_PULLUP)

Paddle player1(2);       ///< Left  paddle at x=2   (always P1-controlled)
Paddle player2(124);     ///< Right paddle at x=124  (AI in PvC, P2 in PvP)
Ball   ball;             ///< The game ball

unsigned long lastFrameTime = 0; ///< Timestamp of the last rendered frame (ms)

// ─────────────────────────────────────────────────────────────────────────────
// HELPER: HUD + CENTER LINE
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Draws the score HUD strip at the top of the display.
 *
 * Renders the current score as "P1 - P2" centred horizontally in the top
 * HUD_H pixel rows, then draws a solid horizontal separator line at y = HUD_H - 1
 * to visually divide the score area from the playfield.
 *
 * The layout reserves y 0–8 for the HUD so game objects (ball, paddles)
 * are constrained to FIELD_TOP and below, preventing score overwriting.
 */
void drawHUD() {
  char buf[8];
  snprintf(buf, sizeof(buf), "%d - %d", score1, score2);
  int len = strlen(buf);
  display.setTextSize(1);
  display.setCursor((128 - len * 6) / 2, 1); // centre horizontally; 6px per char at size 1
  display.print(buf);
  display.drawFastHLine(0, HUD_H - 1, 128, WHITE); // separator line
}

/**
 * @brief Draws a dashed vertical centre line across the playfield.
 *
 * Renders 3-pixel dashes with 3-pixel gaps from FIELD_TOP to FIELD_BOT,
 * centred at x = 63 (middle of 128px display).
 * This visual element mirrors classic arcade Pong and helps players judge
 * ball distance without adding gameplay complexity.
 */
void drawCenterLine() {
  for (int y = FIELD_TOP + 2; y < FIELD_BOT; y += 6) {
    display.drawFastVLine(63, y, 3, WHITE);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// HELPER: GAME RESET
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Resets all game state to initial values for a fresh match.
 *
 * Clears both scores, resets both paddles to centre, and resets the ball
 * (served toward P1 by default). Called when transitioning from MENU → GAME
 * and from WIN → MENU to ensure each match starts clean.
 */
void resetGame() {
  score1 = 0;
  score2 = 0;
  player1.reset();
  player2.reset();
  ball.reset(-1); // Initial serve toward P1
}

// ─────────────────────────────────────────────────────────────────────────────
// COLLISION DETECTION + SCORING
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Handles paddle collisions, angle deflection, and out-of-bounds scoring.
 *
 * ── Paddle Collision ────────────────────────────────────────────────────────
 * AABB test is velocity-direction gated:
 *   - P1 paddle is only tested when ball.vx < 0 (ball moving left)
 *   - P2 paddle is only tested when ball.vx > 0 (ball moving right)
 * Gating prevents multiple reflections in the same frame ("sticky paddle"
 * bug that occurred in v1 when vx was large enough to overlap two frames).
 *
 * On a valid hit two things happen:
 *   1. Angle deflection — relative contact point on the paddle face sets vy:
 *        relHit = (ball_centre_y - paddle_centre_y) / (paddle_height / 2)
 *        vy = relHit × 3.5   (range ≈ -3.5 to +3.5 px/frame)
 *      Hitting the paddle edge produces steep angles; hitting centre produces
 *      flat trajectories, adding skill depth.
 *   2. Speed increase — |vx| grows by 5 % per rally, capped at MAX_BALL_SPEED.
 *      This creates natural rally tension.
 *
 * ── Scoring ─────────────────────────────────────────────────────────────────
 * If ball.x < 0  → P2 / CPU scores  (P1 missed)
 * If ball.x > 127 → P1 scores (P2 / CPU missed)
 *
 * After a point:
 *   1. Score is incremented and checked against WIN_SCORE.
 *   2. If WIN_SCORE is reached → gameState = WIN; function returns early.
 *   3. Otherwise a 700 ms pause renders the updated score before the next serve.
 *   4. Both paddles reset to centre; ball resets served toward the loser.
 */
void checkCollision() {

  // ── P1 left paddle (x=2) ────────────────────────────────────────────────────
  if (ball.vx < 0 &&
      ball.x     <= player1.x + 2  &&
      ball.x     >= player1.x - 1  &&
      ball.y + 2 >= player1.y      &&
      ball.y     <= player1.y + player1.height) {

    float relHit = (ball.y + 1.0f) - (player1.y + player1.height / 2.0f);
    ball.vy = (relHit / (player1.height / 2.0f)) * 3.5f;
    ball.vx =  abs(ball.vx) * 1.05f;                        // reflect + speed up
    if (ball.vx > MAX_BALL_SPEED) ball.vx = MAX_BALL_SPEED; // cap
  }

  // ── P2 / AI right paddle (x=124) ────────────────────────────────────────────
  if (ball.vx > 0 &&
      ball.x + 2 >= player2.x      &&
      ball.x     <= player2.x + 1  &&
      ball.y + 2 >= player2.y      &&
      ball.y     <= player2.y + player2.height) {

    float relHit = (ball.y + 1.0f) - (player2.y + player2.height / 2.0f);
    ball.vy = (relHit / (player2.height / 2.0f)) * 3.5f;
    ball.vx = -abs(ball.vx) * 1.05f;                         // reflect + speed up
    if (ball.vx < -MAX_BALL_SPEED) ball.vx = -MAX_BALL_SPEED;
  }

  // ── Ball exited left → P2 / CPU scores ───────────────────────────────────────
  if (ball.x < 0) {
    score2++;
    if (score2 >= WIN_SCORE) {
      winner    = 2;
      gameState = WIN;
      return;
    }
    // Show updated score briefly before next serve
    display.clearDisplay();
    drawHUD();
    drawCenterLine();
    player1.draw();
    player2.draw();
    display.display();
    delay(700);
    player1.reset();
    player2.reset();
    ball.reset(-1); // Serve toward P1 (loser)
  }

  // ── Ball exited right → P1 scores ─────────────────────────────────────────────
  else if (ball.x > 127) {
    score1++;
    if (score1 >= WIN_SCORE) {
      winner    = 1;
      gameState = WIN;
      return;
    }
    display.clearDisplay();
    drawHUD();
    drawCenterLine();
    player1.draw();
    player2.draw();
    display.display();
    delay(700);
    player1.reset();
    player2.reset();
    ball.reset(1); // Serve toward P2 (loser)
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// MENU
// ─────────────────────────────────────────────────────────────────────────────

int menuOption = 0; ///< Currently highlighted menu option: 0 = PvC, 1 = PvP

/**
 * @brief Renders the mode selection menu on the OLED.
 *
 * Displays the game title (size 2) and two selectable options (size 1).
 * A `>` cursor prefix marks the currently highlighted option.
 *
 * Layout:
 *   y =  8, size 2 : "PONG"          (title)
 *   y = 35, size 1 : "> P1 v CPU"   or "  P1 v CPU"
 *   y = 50, size 1 : "> P1 v P2 "   or "  P1 v P2 "
 */
void drawMenu() {
  display.clearDisplay();

  // Title
  display.setTextSize(2);
  display.setCursor(40, 8);
  display.print("PONG");

  // Options
  display.setTextSize(1);
  display.setCursor(20, 35);
  display.print(menuOption == 0 ? "> P1 v CPU" : "  P1 v CPU");
  display.setCursor(20, 50);
  display.print(menuOption == 1 ? "> P1 v P2 " : "  P1 v P2 ");

  display.display();
}

/**
 * @brief Handles joystick navigation and button confirmation in the menu.
 *
 * Reads the raw ADC value of the P1 joystick Y axis (PA3) directly.
 * Thresholds of 2500 / 1500 (out of 4095) correspond to roughly ±25 %
 * deflection — responsive to a gentle push without jitter.
 *
 * On button press:
 *   - isPvP is set from the highlighted option
 *   - resetGame() clears scores and centres paddles/ball
 *   - gameState transitions to GAME
 */
void updateMenu() {
  int y = analogRead(PA3);
  if (y > 2500) menuOption = 1;
  if (y < 1500) menuOption = 0;

  if (selectBtn.wasPressed()) {
    isPvP = (menuOption == 1);
    resetGame();
    gameState = GAME;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// WIN SCREEN
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Renders the winner announcement screen.
 *
 * Displays:
 *   Row 1 (size 2, ~y=8) : "P1 WINS!" / "P2 WINS!" / "CPU WINS"
 *   Row 2 (size 1, y=38) : final score in "[ P1 - P2 ]" format, centred
 *   Row 3 (size 1, y=54) : "Press BTN to replay"
 *
 * Winner text is centred horizontally. At text size 2 each character is
 * 12 px wide; the formula (128 - len×12) / 2 gives the left-edge X.
 */
void drawWin() {
  display.clearDisplay();
  display.setTextSize(2);

  // Build centred winner string
  const char* winStr;
  if      (winner == 1) winStr = "P1 WINS!";
  else if (isPvP)       winStr = "P2 WINS!";
  else                  winStr = "CPU WINS";

  int titleLen = strlen(winStr);
  display.setCursor((128 - titleLen * 12) / 2, 8);
  display.print(winStr);

  // Final score
  char scoreBuf[14];
  snprintf(scoreBuf, sizeof(scoreBuf), "[ %d  -  %d ]", score1, score2);
  int scoreLen = strlen(scoreBuf);
  display.setTextSize(1);
  display.setCursor((128 - scoreLen * 6) / 2, 38);
  display.print(scoreBuf);

  // Replay prompt
  display.setCursor(14, 54);
  display.print("Press BTN to replay");

  display.display();
}

/**
 * @brief Handles input on the win screen.
 *
 * Pressing the select button calls resetGame() (clears scores, recentres
 * paddles and ball) and transitions back to MENU for mode re-selection.
 */
void updateWin() {
  if (selectBtn.wasPressed()) {
    resetGame();
    gameState = MENU;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// GAME LOOP
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Executes one full game frame: input → update → collide → render.
 *
 * Frame steps:
 *   1. Read joystick deltas for both axes
 *   2. Update P1 paddle from joy1; update P2 from joy2 (PvP) or AI (PvC)
 *      — AI target is ball.y + 1 to track the ball's vertical centre
 *   3. Advance ball position; handle top/bottom wall bouncing
 *   4. Run collision detection and scoring; return early if WIN triggered
 *   5. Clear buffer, draw HUD + center line + paddles + ball, flush to OLED
 *
 * In PvC mode  : joy1 → player1,  aiMove(ball.y+1) → player2
 * In PvP mode  : joy1 → player1,  joy2 → player2
 *
 * The early return after checkCollision() prevents a stale frame from being
 * drawn on top of the WIN screen when the winning point is scored.
 */
void runGame() {
  // ── 1. Input ───────────────────────────────────────────────────────────────
  float dy1 = joy1.getDY();
  float dy2 = joy2.getDY();

  // ── 2. Paddles ─────────────────────────────────────────────────────────────
  player1.update(dy1);
  if (isPvP) player2.update(dy2);
  else       player2.aiMove(ball.y + 1.0f); // Track ball centre (ball is 2px tall)

  // ── 3. Ball ────────────────────────────────────────────────────────────────
  ball.update();

  // ── 4. Collision + Scoring ──────────────────────────────────────────────────
  checkCollision();
  if (gameState != GAME) return; // Win condition triggered — skip this render

  // ── 5. Render ──────────────────────────────────────────────────────────────
  display.clearDisplay();
  drawHUD();
  drawCenterLine();
  player1.draw();
  player2.draw();
  ball.draw();
  display.display(); // Flush full frame buffer to OLED over I2C
}

// ─────────────────────────────────────────────────────────────────────────────
// SETUP
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief One-time initialisation — runs once on power-up or hardware reset.
 *
 * Initialises the SH1106 display with the internal charge pump voltage source
 * at I2C address 0x3C (try 0x3D if the display stays blank).
 * Clears any residual buffer content left from a previous session.
 * A short 200 ms delay allows the display power supply to stabilise before
 * the first drawMenu() call.
 */
void setup() {
  display.begin(SH1106_SWITCHCAPVCC, 0x3C); // Internal charge pump; I2C addr 0x3C
  display.clearDisplay();
  display.setTextColor(WHITE);
  delay(200); // Supply stabilisation
}

// ─────────────────────────────────────────────────────────────────────────────
// MAIN LOOP
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Main loop — frame-rate limited dispatcher for MENU / GAME / WIN states.
 *
 * Uses millis()-based timing instead of a blocking delay() call.
 * This decouples frame pacing from variable I2C transfer time: an I2C flush
 * to the SH1106 can take 3–5 ms, so a naive delay(8) would produce 11–13 ms
 * frames. The millis() gate ensures each frame starts exactly FRAME_MS ms
 * after the previous one started, giving ~125 fps on the logic side.
 *
 * State dispatch:
 *   MENU → drawMenu()  + updateMenu()   (navigation + mode selection)
 *   GAME → runGame()                    (full game frame)
 *   WIN  → drawWin()   + updateWin()    (winner screen + replay prompt)
 *
 * State transitions are triggered inside updateMenu(), checkCollision(),
 * and updateWin() respectively.
 */
void loop() {
  unsigned long now = millis();
  if (now - lastFrameTime < FRAME_MS) return; // Yield until next frame slot
  lastFrameTime = now;

  if      (gameState == MENU) { drawMenu();  updateMenu(); }
  else if (gameState == GAME) { runGame();                 }
  else if (gameState == WIN)  { drawWin();   updateWin();  }
}
