/*
  LEGO 4WD car - Arduino UNO R4 WiFi
  ============================================================
  The car joins your home WiFi and serves a small web page with
  driving buttons. Any phone, tablet or PC on the same network can
  open that page and drive the car. A game controller works too,
  through the separate page in controller/gamepad/.

  WiFi name and password live in arduino_secrets.h (NOT in git!).
  Copy arduino_secrets.h.example to arduino_secrets.h and fill it in.

  ADDRESS: the sketch prints its IP and MAC address over serial at
  startup. Reserve a fixed IP for that MAC in your router, and - if
  your router can do it - add a local DNS name, so you can type
  something like http://car.lan instead of an IP address.

  OVER-THE-AIR UPDATE (OTA): the car accepts new firmware over WiFi,
  no USB cable needed. Compile, then POST the .bin file. There is a
  ready-made script for this: tools/ota-upload.ps1
  NOTE: the very first upload must go over USB, and if you ever flash
  a build that cannot join the WiFi, only the cable will save you.

  CONTROLLER: a Bluetooth game controller can also drive the car directly,
  through an ESP32 on D0 (firmware/esp32-gamepad/). The ESP32 only
  reports - sticks and buttons arrive RAW, and everything they do on the
  car is decided HERE: dead zone, steering, which button switches which
  light. So a new button idea only needs this sketch updated; the ESP32
  stays as it is. See "What comes from the controller" below.

  LIGHTS: three LEDs can be switched from the controller buttons -
  red on D3 (button B), blue on D5 (button X), green on D6 (button A).
  See "Lights" below for how to wire them. To try them without a
  controller, just open these in a browser:
      http://<car>/lights?red=1&blue=1&green=1

  RGB LED: one LED that can do any colour, on D11/D12/A4. The RIGHT stick
  sets it: up/down = brightness (all the way down = off), left/right =
  turn through the rainbow. Without a controller:
      http://<car>/lights?bright=100&hue=120     (120 = green)

  SOUND: an M5Stack ATOM Echo on D1 plays the horn (button Y), a starter
  motor when the controller connects and a reversing beeper. The car only
  tells it what is going on; the sounds are made in firmware/atom-sound/.
  To try the horn without a controller:  http://<car>/horn

  SAFETY - dead man's switch:
  The car only drives while drive commands keep arriving. The web page
  sends them continuously while a button is held. If they stop (finger
  off the button, connection lost, tab closed) the motors cut out after
  a fraction of a second.
*/

#include "WiFiS3.h"
#define NO_OTA_PORT      // no mDNS needed - we upload with curl
#include <ArduinoOTA.h>
#include "Arduino_LED_Matrix.h"
#include "arduino_secrets.h"

char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;

WiFiServer server(80);

// --- Status on the built-in 12x8 LED matrix ---
// This shows what the car is doing RIGHT ON THE CAR - no laptop, no
// network needed. Priceless once it runs on battery, where you have
// neither a serial monitor nor a way to ping it.
ArduinoLEDMatrix matrix;

// Check mark = connected, all good
uint8_t ICON_OK[8][12] = {
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,1,0,0},
  {0,0,0,0,0,0,0,0,1,0,0,0},
  {0,0,0,1,0,0,0,1,0,0,0,0},
  {0,0,0,0,1,0,1,0,0,0,0,0},
  {0,0,0,0,0,1,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0}
};

// Cross = no WiFi
uint8_t ICON_ERROR[8][12] = {
  {0,0,1,0,0,0,0,0,0,1,0,0},
  {0,0,0,1,0,0,0,0,1,0,0,0},
  {0,0,0,0,1,0,0,1,0,0,0,0},
  {0,0,0,0,0,1,1,0,0,0,0,0},
  {0,0,0,0,0,1,1,0,0,0,0,0},
  {0,0,0,0,1,0,0,1,0,0,0,0},
  {0,0,0,1,0,0,0,0,1,0,0,0},
  {0,0,1,0,0,0,0,0,0,1,0,0}
};

// Dots = searching for the WiFi
uint8_t ICON_SEARCH[8][12] = {
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,1,1,0,0,0,0,0,0,0},
  {0,0,0,1,1,0,1,1,0,1,1,0},
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0}
};

// --- Heading as a thin arrow, free to rotate ---
// Instead of four fixed pictures (forward/back/left/right) we draw the
// arrow fresh every time: a thin line through the centre with a small V
// tip at the front. That way it can point in 32 directions and turns
// like the needle of an instrument. A gentle arc then looks different
// from a spin on the spot, which is exactly what you want to see while
// the car is on battery and out of reach.
uint8_t ICON_ARROW[8][12];

const int   ARROW_STEPS  = 32;      // how many directions it can point in
const float ARROW_RADIUS = 3.5f;    // half the arrow length, in dots
const float TIP_LENGTH   = 1.8f;    // length of the two barbs, in dots
// The barbs sit at 45 degrees behind the tip. Worked out once so we do
// not have to call sin/cos for them on every redraw.
const float TIP_COS = 0.7071f;
const float TIP_SIN = 0.7071f;

int lastStep = -1;   // last direction we drew

// Which picture is on the matrix right now? Without this marker we would
// rewrite the matrix 50 times a second even though it rarely changes.
uint8_t (*currentIcon)[12] = nullptr;

void showIcon(uint8_t icon[8][12]) {
  if (icon == currentIcon) return;
  currentIcon = icon;
  matrix.renderBitmap(icon, 8, 12);
}

void setDot(int x, int y) {
  if (x >= 0 && x < 12 && y >= 0 && y < 8) ICON_ARROW[y][x] = 1;
}

// A line from point to point. We walk in as many steps as the longer of
// the two distances has dots, so no gaps are left.
void drawLine(float x0, float y0, float x1, float y1) {
  float dx = x1 - x0, dy = y1 - y0;
  float far = (fabs(dx) > fabs(dy)) ? fabs(dx) : fabs(dy);
  int steps = (int)far + 1;
  for (int i = 0; i <= steps; i++) {
    float t = (float)i / steps;
    setDot(lroundf(x0 + dx * t), lroundf(y0 + dy * t));
  }
}

void drawArrow(int step) {
  memset(ICON_ARROW, 0, sizeof(ICON_ARROW));

  const float cx = 5.5f, cy = 3.5f;    // centre of the matrix
  float a  = step * 2.0f * PI / ARROW_STEPS;
  float dx = cos(a), dy = sin(a);

  // FIXED length: the arrow rotates inside the middle 8x8 field instead
  // of stretching depending on direction. It is then the same size in
  // every direction and turns cleanly like a needle. The two outer
  // columns stay dark, which is far less distracting than an arrow that
  // keeps growing and shrinking as it turns.
  const float L = ARROW_RADIUS;

  float sx = cx + dx * L, sy = cy + dy * L;      // the tip
  drawLine(cx - dx * L, cy - dy * L, sx, sy);    // the shaft

  // The two barbs start from where the tip ACTUALLY landed. Computed
  // from the unrounded value they sit askew, because the centre of the
  // matrix lies between two dots (5.5 / 3.5).
  float px = lroundf(sx), py = lroundf(sy);
  for (int s = -1; s <= 1; s += 2) {
    // rotate the backwards direction (-dx,-dy) by +/- 45 degrees
    float sa = s * TIP_SIN;
    float rx = (-dx) * TIP_COS - (-dy) * sa;
    float ry = (-dx) * sa      + (-dy) * TIP_COS;
    drawLine(px, py, px + rx * TIP_LENGTH, py + ry * TIP_LENGTH);
  }
}

// --- WiFi watchdog ---
// An earlier version waited for the WiFi in an endless loop inside
// setup(). When the connection failed the car was simply dead and gave
// no clue why. Now: one attempt with a time limit, then keep retrying
// while running.
const unsigned long WIFI_TIMEOUT_MS = 12000;  // how long to wait for a connection
const unsigned long WIFI_RETRY_MS   = 10000;  // pause between two attempts
unsigned long lastWifiAttempt = 0;
bool netStarted   = false;   // web server + OTA already started?
bool wasConnected = false;   // so we only report a dropped link once

// --- Motor pins (one TB6612, two motors wired in parallel per channel) ---
const int PWMA = 9,  AIN1 = 7, AIN2 = 8;   // left  side (channel A)
const int PWMB = 10, BIN1 = 4, BIN2 = 2;   // right side (channel B)

// SPEED CAP - protects the motors.
// The battery pack delivers ~9.6 V, but the little TT gear motors are
// rated for 3-6 V. So we never write a full 255. At 9.6 V a value of
// 150 is roughly 5.6 V at the motor (130 would be ~5 V).
// If the car is too slow, raise this in small steps and measure the
// motor voltage with a multimeter - never go above 6 V, so about 160 max.
const int SPEED_MAX = 150;

// Below this value the motor does not turn at all any more, it just
// hums. So when the analog throttle asks for something tiny, we lift it
// up to this minimum instead.
const int SPEED_MIN = 70;

// STEERING for the arrow buttons (the gamepad mixes steering smoothly,
// see /drive below):
//    0          -> inner side stops, wide gentle arc. Little tyre scrub.
//   -SPEED_MAX  -> spin on the spot. Powerful, but the tyres scrub
//                  sideways across the floor: lots of current, and very
//                  little grip on smooth floors.
// Currently -70. The inner side pushes back, but more weakly than the
// outer side pulls. That moves the pivot point out of the centre of the
// car, so the wheels partly roll instead of only scrubbing sideways --
// which needs far less force than a full -SPEED_MAX spin, where the
// wheels simply stalled on our floor.
const int TURN_INNER_SPEED = -70;

// --- The buttons on the controller ---
// The ESP32 sends all buttons as ONE number, one bit per button (that is
// how Bluepad32 counts): A=1, B=2, X=4, Y=8 ... "B and Y together" = 2+8 = 10.
// MIND THE NAMES: our controller reports itself as a "Switch Pro", and on
// Nintendo pads A/B and X/Y sit the other way round compared to Xbox. If the
// wrong button does something, just put a different BUTTON_... into the
// table below.
const uint16_t BUTTON_A  = 1,   BUTTON_B  = 2,   BUTTON_X  = 4,   BUTTON_Y  = 8;
const uint16_t BUTTON_LB = 16,  BUTTON_RB = 32,  BUTTON_LT = 64,  BUTTON_RT = 128;
const uint16_t BUTTON_STICK_L = 256, BUTTON_STICK_R = 512;   // stick pushed in
const uint8_t  DPAD_UP = 1, DPAD_DOWN = 2, DPAD_RIGHT = 4, DPAD_LEFT = 8;

const uint16_t BUTTON_HORN = BUTTON_Y;

// Everything the controller reports, unpacked from the ESP32's line.
struct Gamepad {
  bool     connected;
  int      lx, ly, rx, ry;    // sticks, -512..512 (y: MINUS = forward)
  int      throttle, brake;   // ZR/ZL analog 0..1023, always 0 in Switch mode
  uint16_t buttons;           // one bit per button, see BUTTON_...
  uint8_t  dpad;              // the D-pad, see DPAD_...
  uint8_t  misc;              // HOME=1, minus=2, plus=4, capture=8
};
Gamepad gp = {};   // what counts right now (filled in by handleGamepadLine)

// --- Lights ---
// Three LEDs, each with its own button. Three parts per LED, no soldering:
//
//     D3  ---[ resistor ]---  long leg (+) of the RED LED
//                             short leg (-)          ---  GND
//     D5  ---[ resistor ]---  long leg (+) of the BLUE LED
//                             short leg (-)          ---  GND
//     D6  ---[ resistor ]---  long leg (+) of the GREEN LED
//                             short leg (-)          ---  GND
//
// The LONG leg is plus. Put an LED in the wrong way round and it simply
// does not light up - it does not break, so let the kids try.
//
// WHICH SIDE THE RESISTOR GOES ON DOES NOT MATTER - before the LED or
// after it, both are correct. Current flows in a loop, so whatever goes
// through the LED also goes through the resistor: a narrow spot slows the
// whole hose down no matter where it sits. We still always put it at the
// pin, so all three chains look the same and every short leg ends up in
// the same ground rail.
//
// THE RESISTOR IS NOT OPTIONAL, not even "just for a moment". An LED does
// not limit current by itself; without a resistor it draws whatever the
// pin will give. And the pin is the expensive part here:
//
//   *** A pin of the UNO R4 may only source 8 mA. ***
//
// The old UNO allowed 20 mA, which is why nearly every tutorial online
// says "220 ohm". On an R4 that is more than twice the limit, and a pin
// killed that way stays dead.
//
// HOW MUCH RESISTANCE, THEN? Every LED eats part of the voltage itself,
// and how much depends on the colour. What is left over sits across the
// resistor and sets the current - (5 V - LED voltage) / resistance:
//
//   LED            eats     1 kOhm    500 Ohm    330 Ohm
//   red            ~2.0 V   3.0 mA    6.0 mA     9.1 mA  <- over the limit
//   green (pale)   ~2.1 V   2.9 mA    5.8 mA     8.8 mA  <- over the limit
//   green (bright) ~3.1 V   1.9 mA    3.8 mA     5.8 mA
//   blue           ~3.2 V   1.8 mA    3.6 mA     5.5 mA
//
// Note the two rows for green: there really are two kinds, the old pale
// (yellowish) one and the modern bright one, and you cannot tell them
// apart by looking. Measuring the voltage across the LED tells you in ten
// seconds - a nice little experiment, because the part gives nothing away
// and the multimeter does.
//
// We run 1 kOhm on red and blue, which is bright enough. Green at 1 kOhm
// turned out too dim, so it gets ~500 Ohm - made from TWO 1 kOhm resistors
// side by side (in parallel), because our kit has no 470 Ohm. That is a
// nice thing to show: two resistors next to each other resist LESS than
// one, the same way two open doors let twice as many people through.
// 500 Ohm is safe for both kinds of green; a single 330 Ohm would not be.
//
// TEST IT WITH NO WIRING AT ALL: the little "L" LED already on the board
// joins in whenever any light is on (see setLight) - it has its resistor
// built in. So the button can be checked before a single LED is plugged
// in, and later, on battery power, it is the simplest proof that the
// command arrives at all.
//
// Every light is one row in this table: the name it goes by in requests
// (?red=1), its pin, its button on the controller, and whether it is on
// right now. Another light needs just one more row here. Nothing else --
// not even an ESP32 update.
struct Light {
  const char* name;   // the name used in requests, e.g. ?red=1
  int         pin;
  uint16_t    button; // which controller button switches it, see BUTTON_...
  bool        on;
};

Light lights[] = {
  { "red",   3, BUTTON_B, false },  // red LED
  { "blue",  5, BUTTON_X, false },  // blue LED
  { "green", 6, BUTTON_A, false },  // green LED
};
const int LIGHT_COUNT = sizeof(lights) / sizeof(lights[0]);

// Still free for more lights: A0..A3 and A5 (A1 is a good spot for a
// battery monitor, see docs/next-steps.md). On/off only -- of these, only
// A5 can still dim, and it shares its timer with A4 (RGB blue).
// In use: D0/D1 (ESP32/ATOM), D2/D4/D7/D8/D9/D10 (motors),
// D3/D5/D6 (these LEDs), D11/D12/A4 (RGB LED).

// --- RGB LED ---
// An RGB LED is really THREE LEDs in one case -- red, green, blue -- that
// share one leg (the longest). Each colour has its own pin and its own
// resistor:
//
//     D11 ---[ 2x 1 kOhm side by side = 500 Ohm ]---  RED leg
//     D12 ---[ 330 Ohm ]---  GREEN leg
//     A4  ---[ 330 Ohm ]---  BLUE leg
//     GND -----------------  LONG leg (common cathode)
//
// Why a bigger resistor on red? Red eats only ~2 V itself, green and blue
// ~3 V. With 330 Ohm too much would be left over for red: ~9 mA, and a pin
// may only give 8 mA (see the table under "Lights" above).
// A single 1 kOhm (~3 mA) left red far too weak next to green and blue --
// the rainbow was mostly green. Two 1 kOhm side by side = 500 Ohm give
// ~6 mA: twice as bright and still safe.
//
// Why exactly these pins? We want to DIM, not just switch on and off. Only
// a PWM pin can do that: it switches on and off so fast (500 times a
// second) that the eye only sees the average -- on half the time = half as
// bright. And from three colours the eye mixes all the others: red + green
// = yellow, green + blue = turquoise, all three = white. Just like a TV.
// D11, D12 and A4 were the only free pins left that can do PWM.
const int RGB_PIN_RED   = 11;
const int RGB_PIN_GREEN = 12;
const int RGB_PIN_BLUE  = A4;

// There are two kinds, and you cannot tell them apart by looking:
//   common CATHODE -> long leg to GND,  pin HIGH = colour on (the usual one)
//   common ANODE   -> long leg to 5V,   pin LOW  = colour on
// Find out with the multimeter (diode test): black probe on the long leg,
// red on another one -- if it glows, it is a cathode. If the LED shines
// bright white while it should be off, this value is the wrong way round.
const bool RGB_COMMON_ANODE = false;

// The three colours are not equally bright (different resistors, different
// chips). If "yellow" looks rather greenish, turn green down here until
// yellow is yellow. The kids judge that best by eye. 0..255.
const int RGB_GAIN_RED   = 255;
const int RGB_GAIN_GREEN = 255;
const int RGB_GAIN_BLUE  = 255;

// How fast the stick acts at FULL deflection. Half-way it goes half as
// fast -- so you can turn roughly first and then fine-tune.
const float RAINBOW_SECONDS    = 4.0f;   // once all the way round the colours
const float BRIGHTNESS_SECONDS = 2.0f;   // from off to fully bright

// The state. The rainbow is a CIRCLE of 360 degrees:
//   0 = red, 60 = yellow, 120 = green, 180 = turquoise, 240 = blue,
//   300 = purple, and 360 is red again. That is why you can keep turning
//   in one direction forever.
float rgbHue        = 0;     // 0..360 degrees
float rgbBrightness = 0;     // 0..100 %, 0 = off. It starts off.
int   rgbStickX     = 0;     // right stick, -100..100 (see applyGamepad)
int   rgbStickY     = 0;

// Is any light on? The built-in "L" LED follows this.
bool anyLightOn() {
  for (int i = 0; i < LIGHT_COUNT; i++) if (lights[i].on) return true;
  return rgbBrightness > 0;
}

// Turn hue + brightness into three pin values and write them out.
// The colour circle is cut into six slices of 60 degrees. In each slice one
// colour is fully on, one is off, and the third is fading in or out. For
// example between 0 and 60 degrees: red full, blue off, green growing --
// red turns into yellow by way of orange.
void writeRgb() {
  float h = rgbHue / 60.0f;           // 0..6
  int   slice = (int)h % 6;
  float f = h - (int)h;               // how far into the slice, 0..1
  float r = 0, g = 0, b = 0;
  switch (slice) {
    case 0: r = 1;     g = f;     b = 0;     break;   // red       -> yellow
    case 1: r = 1 - f; g = 1;     b = 0;     break;   // yellow    -> green
    case 2: r = 0;     g = 1;     b = f;     break;   // green     -> turquoise
    case 3: r = 0;     g = 1 - f; b = 1;     break;   // turquoise -> blue
    case 4: r = f;     g = 0;     b = 1;     break;   // blue      -> purple
    case 5: r = 1;     g = 0;     b = 1 - f; break;   // purple    -> red
  }

  // Brightness SQUARED: the eye is far more sensitive in dim light than in
  // bright light. Dimmed linearly, almost everything happens at the bottom
  // and almost nothing at the top; squared, every millimetre of stick feels
  // the same.
  float bright = (rgbBrightness / 100.0f) * (rgbBrightness / 100.0f);

  int value[3] = {
    (int)(r * bright * RGB_GAIN_RED   + 0.5f),
    (int)(g * bright * RGB_GAIN_GREEN + 0.5f),
    (int)(b * bright * RGB_GAIN_BLUE  + 0.5f),
  };
  const int pin[3] = { RGB_PIN_RED, RGB_PIN_GREEN, RGB_PIN_BLUE };

  // Only touch a pin on a real change, same as with the lights.
  static int before[3] = { -1, -1, -1 };
  for (int i = 0; i < 3; i++) {
    if (value[i] == before[i]) continue;
    before[i] = value[i];
    analogWrite(pin[i], RGB_COMMON_ANODE ? 255 - value[i] : value[i]);
  }
  digitalWrite(LED_BUILTIN, anyLightOn() ? HIGH : LOW);
}

// Called on every pass: let the stick "run into" colour and brightness.
// We work with the time since last call, so the speed does not depend on
// how often loop() comes round (with WiFi requests a pass sometimes takes
// longer).
void tendRgb() {
  static unsigned long lastMs = 0;
  unsigned long now = millis();
  unsigned long dt = now - lastMs;
  lastMs = now;
  if (dt > 100) dt = 100;   // after a long pause, do not jump all at once
  if (rgbStickX == 0 && rgbStickY == 0) return;

  float sec = dt / 1000.0f;
  rgbHue        += rgbStickX / 100.0f * 360.0f / RAINBOW_SECONDS    * sec;
  rgbBrightness += rgbStickY / 100.0f * 100.0f / BRIGHTNESS_SECONDS * sec;

  // The colour circle has no end: past 360 it carries on at 0 and back.
  while (rgbHue >= 360) rgbHue -= 360;
  while (rgbHue < 0)    rgbHue += 360;
  rgbBrightness = constrain(rgbBrightness, 0.0f, 100.0f);

  writeRgb();
}

// Switch one light. Only touch the pin on a real change: the command
// arrives many times per second and there is no point rewriting it.
void setLight(int i, bool on) {
  if (lights[i].on == on) return;
  lights[i].on = on;
  digitalWrite(lights[i].pin, on ? HIGH : LOW);
  digitalWrite(LED_BUILTIN, anyLightOn() ? HIGH : LOW);
  Serial.print("Light ");
  Serial.print(lights[i].name);
  Serial.println(on ? " ON" : " off");
}

// --- Horn and sounds ---
// The sounds themselves live in an M5Stack ATOM Echo, a small cube with its
// own ESP32 and speaker (firmware/atom-sound/). The car only tells it over D1
// what is going on, as one line "L,R,horn,gamepad", e.g. "150,110,1,1".
// From L and R (the motor speeds) the ATOM makes the reversing beeper, from
// "gamepad" the starter sound when the controller connects. New fields always
// go at the END, so an ATOM with older firmware keeps working.
// D1 puts out 5 V and the ATOM only tolerates 3.3 V: a divider of 1 kOhm
// and 2 kOhm sits in between, see docs/wiring.md.
//
// The horn has its own dead man's switch: it only stays on while "honk!"
// keeps coming in. Otherwise the car would honk forever if the link died
// exactly while honking.
const unsigned long HORN_TIMEOUT_MS = 800;   // same as the motors
bool          hornOn          = false;
unsigned long hornLastMs      = 0;
bool          gamepadConnected = false;       // reported by the ESP32, for the starter sound

void setHorn(bool on) {
  if (on) hornLastMs = millis();   // every "on" extends it
  if (hornOn == on) return;
  hornOn = on;
  Serial.println(on ? "Horn ON" : "Horn off");
}

// --- Fix a reversed side in software (no re-plugging of wires) ---
// If one side spins the wrong way, flip the matching flag to true: the
// sketch then internally turns "forward" into "backward" for that side.
const bool INVERT_LEFT  = true;   // our left side is wired the other way round
const bool INVERT_RIGHT = true;   // and so is the right one

// --- Gamepad through an ESP32 ---
// Pin D0 carries the data line from an ESP32 (see firmware/esp32-gamepad/).
// The ESP32 listens to a Bluetooth game controller and sends one line
// about 50 times per second with EVERYTHING that is going on on the
// controller (see "What comes from the controller" for the layout).
//
// ONE wire plus ground, nothing else. The car never answers the ESP32: D1
// goes to the ATOM Echo instead (through a divider, see "Horn and sounds").
//
// GROUND MATTERS. Give the ESP32 its own ground wire straight to a GND pin
// of the Arduino, NOT via the breadboard rail that carries the motor
// current back to the battery. With a shared rail the line lost characters
// every time the motors pulled hard (see "Checksum" below).
//
// This is a SECOND source of control next to the browser, not a
// replacement: whoever sent the last command decides where the car goes.
// The big win is that no laptop is involved any more - controller and car,
// nothing else.
const long GAMEPAD_BAUD = 38400;
// Why 38400 and not faster? We measured 9600 and 115200, both without a
// single error. 38400 sits comfortably in between: the line is only 16 %
// busy, and every single bit lasts three times longer than at 115200 -
// margin against the electrical noise the motors make.
// The longest line is "1,-512,-512,-512,-512,1023,1023,65535,15,255*FF"
// = 47 characters. Plenty of room in case the ESP32 ever adds fields.
char gamepadBuffer[64];
uint8_t gamepadLen = 0;

// --- Diagnostics: is anything arriving from the ESP32 at all? ---
// Readable at  http://<car>/status
// Without these counters you are only guessing: is the ESP32 not sending,
// or is the Arduino not listening? That exact question cost us half a day.
// "chars" counts every single byte on D0 - if it stays at 0, nothing is
// arriving PHYSICALLY (cable, pin or ground). If it counts up, the link is
// fine and the fault is somewhere else.
unsigned long gamepadChars  = 0;
unsigned long gamepadLines  = 0;
unsigned long gamepadLastMs = 0;
char gamepadLastLine[64]    = "";
// Lines that arrived mangled and were thrown away (see parseLine).
// If this climbs while steering, the motors are disturbing the D0 line.
unsigned long gamepadBad    = 0;
char gamepadLastBad[64]     = "";

// --- Dead man's switch ---
unsigned long lastCommandMs = 0;
// No fresh command -> stop. This has to be longer than the send interval
// of the control page plus the time for one connection setup (up to
// ~300 ms), otherwise the ride stutters. At the same time it is how long
// the car keeps rolling in the worst case if the link dies - so don't
// make it huge either.
const unsigned long TIMEOUT_MS = 800;

// --- Turning on the spot needs more than a normal kick ---
// When the car spins, all four wheels are pushed sideways across the
// floor instead of rolling. Static friction takes longer than the usual
// 120 ms to break -- if the kick ends before that, the wheels just stall.
const long KICK_TURN_MS = 300;

// PULSING WHILE TURNING: one kick gets it moving, but afterwards the
// continuous force is not enough against the sideways friction and the
// wheels stall again. Running at KICK_SPEED permanently is not an option:
// that would be ~7.5 V on motors built for 3-6 V.
// So push rhythmically instead. This uses the fact that static friction
// is higher than kinetic friction: a jolt breaks it more easily than
// steady pressure, and the motors and driver cool down between pushes.
const long PULSE_ON  = 150;   // length of the strong push
const long PULSE_OFF = 250;   // normal speed again for this long

bool pulseActive = false;   // is a push running right now?
bool spinning    = false;   // turning on the spot? (only then we pulse)
int  targetLeft = 0, targetRight = 0;   // last command, for re-applying it

bool moving = false;

// --- The phone page (big arrow buttons, hold to drive) ---
const char PAGE[] = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>LEGO 4WD car</title>
<style>
  body{font-family:sans-serif;text-align:center;background:#111;color:#eee;margin:0;padding:16px;user-select:none}
  h1{font-size:1.3rem}
  .pad{display:grid;grid-template-columns:repeat(3,90px);grid-template-rows:repeat(3,90px);gap:12px;justify-content:center;margin-top:24px}
  button{font-size:2.2rem;border:none;border-radius:16px;background:#28a05a;color:#fff;touch-action:none}
  button:active{background:#3cc472}
  #stop{background:#a33}
  .fwd{grid-column:2;grid-row:1}.left{grid-column:1;grid-row:2}
  .stop{grid-column:2;grid-row:2}.right{grid-column:3;grid-row:2}.back{grid-column:2;grid-row:3}
  p{color:#888;font-size:.8rem;margin-top:24px}
</style>
</head>
<body>
<h1>&#128663; LEGO 4WD car</h1>
<div class="pad">
  <button class="fwd"   data-cmd="forward">&#9650;</button>
  <button class="left"  data-cmd="left">&#9664;</button>
  <button id="stop" class="stop" data-cmd="stop">&#9632;</button>
  <button class="right" data-cmd="right">&#9654;</button>
  <button class="back"  data-cmd="back">&#9660;</button>
</div>
<p>Hold a button to drive. Let go to stop.</p>
<script>
  // The board only manages ~3 requests per second, and the expensive part
  // is not the reply but every new connection. Send faster than that and
  // the commands pile up: after letting go the car keeps running through
  // the queued ones, and if the dead man's switch fires in between, the
  // next queued command sees a stopped motor and kicks again -- that is
  // the surprise burst of speed after releasing the button.
  // So: only ever ONE command in flight, which makes the page throttle
  // itself to whatever the board can take. Only 'stop' always goes out.
  // A command needs a DEADLINE. If one fetch() never gets an answer (a lost
  // WiFi packet), inFlight would stay at 1 forever and no further command
  // would ever go out - the buttons would be dead while the page still looks
  // fine. A fetch() on its own only gives up after minutes. 700 ms is above
  // the slowest measured reply (~550 ms) and below the 800 ms dead man's
  // switch.
  let timer = null;
  let inFlight = 0;
  function send(cmd, now){
    if (inFlight > 0 && !now) return;
    inFlight++;
    const ctrl = new AbortController();
    const t    = setTimeout(()=>ctrl.abort(), 700);
    fetch('/'+cmd, {signal: ctrl.signal})
      .catch(()=>{})
      .finally(()=>{ clearTimeout(t); inFlight--; });
  }
  function start(cmd){ send(cmd, true); clearInterval(timer); timer = setInterval(()=>send(cmd), 120); }
  function halt(){ clearInterval(timer); send('stop', true); }
  document.querySelectorAll('button').forEach(b=>{
    const cmd = b.dataset.cmd;
    if(cmd === 'stop'){ b.addEventListener('pointerdown', e=>{e.preventDefault(); halt();}); return; }
    b.addEventListener('pointerdown', e=>{e.preventDefault(); start(cmd);});
    b.addEventListener('pointerup', halt);
    b.addEventListener('pointerleave', halt);
    b.addEventListener('pointercancel', halt);
  });
</script>
</body>
</html>
)HTML";

void setup() {
  Serial.begin(115200);
  Serial1.begin(GAMEPAD_BAUD);   // D0/D1 - the line from the ESP32

  pinMode(PWMA, OUTPUT); pinMode(AIN1, OUTPUT); pinMode(AIN2, OUTPUT);
  pinMode(PWMB, OUTPUT); pinMode(BIN1, OUTPUT); pinMode(BIN2, OUTPUT);
  stopMotors();

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  for (int i = 0; i < LIGHT_COUNT; i++) {      // all lights off
    pinMode(lights[i].pin, OUTPUT);
    digitalWrite(lights[i].pin, LOW);
  }
  writeRgb();                                   // RGB LED off (brightness 0)

  matrix.begin();
  showIcon(ICON_SEARCH);

  // Is the WiFi module there at all?
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("WiFi module not found!");
    showIcon(ICON_ERROR);
    return;   // do NOT hang here - loop() keeps trying
  }

  Serial.print("MAC (use this for the fixed IP in your router): ");
  printMac();

  connectWifi();
}

// One connection attempt with a time limit. true = success.
bool connectWifi() {
  lastWifiAttempt = millis();
  showIcon(ICON_SEARCH);
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);

  WiFi.begin(ssid, pass);
  unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < WIFI_TIMEOUT_MS) {
    // Keep listening to the controller during the wait, and keep the dead
    // man's switch alive. This attempt can take 12 seconds - with a plain
    // delay() in here the car would ignore the controller for that long
    // and, worse, would happily keep driving on the last command if the
    // radio link had just died.
    gamepadAndDeadMan();
    delay(2);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("No connection. Next attempt in 10 s.");
    showIcon(ICON_ERROR);
    return false;
  }

  Serial.print("Connected! Open in a browser:  http://");
  Serial.println(WiFi.localIP());
  showIcon(ICON_OK);

  // Start web server + update receiver only ONCE
  if (!netStarted) {
    // onStart: the moment an upload begins, cut the motors first - the
    // car must not drive off while it is being flashed.
    ArduinoOTA.onStart(stopMotors);
    ArduinoOTA.begin(WiFi.localIP(), "lego4wd", SECRET_OTA_PASS, InternalStorage);
    server.begin();
    netStarted = true;
    Serial.println("Over-the-air update ready (port 65280).");
  }
  wasConnected = true;
  return true;
}

void loop() {
  // The gamepad comes FIRST - it needs no WiFi, and further down loop()
  // bails out with return when there is no network. If this call lived
  // down there, the car would be dead on the controller too whenever the
  // WiFi hiccuped. The dead man's switch rides along for the same reason.
  gamepadAndDeadMan();

  // Pulse timing while spinning. This has to live here rather than in
  // drive(): commands only arrive every ~350 ms, but the rhythm should be
  // steady. The motors are only re-written when the phase changes, not on
  // every pass through loop().
  if (spinning && moving) {
    unsigned long phase = millis() % (PULSE_ON + PULSE_OFF);
    bool pushNow = (phase < PULSE_ON);
    if (pushNow != pulseActive) {
      pulseActive = pushNow;
      leftSide(targetLeft);
      rightSide(targetRight);
    }
  }

  // WiFi watchdog: link gone? Stop and reconnect.
  if (WiFi.status() != WL_CONNECTED) {
    if (wasConnected) {
      // This used to cut the motors. Not any more: the car also drives
      // from the controller, and that path does not need WiFi at all.
      // If the controller goes quiet as well, the dead man's switch
      // stops it after TIMEOUT_MS.
      Serial.println("WiFi lost -- the controller keeps driving.");
      wasConnected = false;
      if (!moving) showIcon(ICON_ERROR);
    }
    if (millis() - lastWifiAttempt > WIFI_RETRY_MS) {
      connectWifi();
    }
    return;   // without WiFi there is nothing to do
  }

  // Print the address every 5 s so it is always visible in the serial monitor
  static unsigned long lastIpPrint = 0;
  if (millis() - lastIpPrint > 5000) {
    lastIpPrint = millis();
    Serial.print("Ready at  http://");
    Serial.println(WiFi.localIP());
  }

  ArduinoOTA.poll();   // is new firmware waiting?

  // Handle several pending requests per pass so nothing piles up. The
  // upper bound keeps loop() from getting stuck.
  for (int i = 0; i < 4; i++) {
    WiFiClient client = server.available();
    if (!client) break;
    handleRequest(client);
  }
}

// --- Read one browser request and answer it ---
void handleRequest(WiFiClient& client) {
  // IMPORTANT for responsiveness: readStringUntil otherwise waits up to
  // A FULL SECOND if the line has not arrived completely yet. That
  // paralyses the whole loop and drive commands pile up.
  client.setTimeout(60);

  // The first line is all we need, e.g. "GET /forward HTTP/1.1"
  String line = client.readStringUntil('\n');

  // We deliberately do NOT drain the remaining headers: every single
  // read() is a radio command to the WiFi co-processor, and at ~150
  // bytes of headers that costs more time than the actual request. We
  // are about to close the connection anyway.

  if (!line.startsWith("GET ")) {   // incomplete -> drop it
    client.stop();
    return;
  }

  if      (line.indexOf("GET /forward") >= 0) { driveCommand( SPEED_MAX,  SPEED_MAX); sendOk(client); }
  else if (line.indexOf("GET /back")    >= 0) { driveCommand(-SPEED_MAX, -SPEED_MAX); sendOk(client); }
  // On "left" the left side is the inner one, on "right" the right side.
  else if (line.indexOf("GET /left")    >= 0) { driveCommand(TURN_INNER_SPEED, SPEED_MAX); sendOk(client); }
  else if (line.indexOf("GET /right")   >= 0) { driveCommand(SPEED_MAX, TURN_INNER_SPEED); sendOk(client); }
  // Analog throttle from the gamepad:  /drive?l=-100..100&r=-100..100
  // The page sends percent, not raw PWM - that way the motor protection
  // (SPEED_MAX) stays here in the sketch and cannot be overridden from
  // the outside.
  else if (line.indexOf("GET /drive")   >= 0) {
    // The lights ride ALONG WITH the drive command instead of getting a
    // request of their own: a second request means a second TCP
    // connection, and connection setup is the slowest thing here.
    // Only touch what was actually sent - the arrow buttons of the phone
    // page must not switch a light off by accident.
    lightsFromRequest(line);
    driveCommandPercent(readParam(line, "?l="),
                        readParam(line, "&r="));
    sendOk(client);
  }
  else if (line.indexOf("GET /stop")    >= 0) { halt(); sendOk(client); }
  // Switch lights by hand, just open it in a browser:
  //   http://<car>/lights?red=1            only the red one
  //   http://<car>/lights?red=0&blue=1     red off, blue on
  //   http://<car>/lights?bright=100&hue=240   RGB LED fully on, blue
  else if (line.indexOf("GET /lights")  >= 0) { lightsFromRequest(line); sendOk(client); }
  // Horn by hand:  http://<car>/horn       -> a short toot (dead man's switch 800 ms)
  //                http://<car>/horn?on=0  -> silent at once
  else if (line.indexOf("GET /horn")    >= 0) { setHorn(line.indexOf("on=0") < 0); sendOk(client); }
  else if (line.indexOf("GET /status")  >= 0) { sendStatus(client); }
  // SELF TEST of the receiving side. Sends one line OUT on Serial1 (pin
  // D1). Put a jumper wire from D1 to D0 and the same line has to come
  // straight back in, with the counters in /status jumping up. That tests
  // D0, Serial1 and the software WITHOUT the ESP32, so afterwards you
  // know for certain which side the fault is on instead of interpreting
  // multimeter readings.
  // IMPORTANT: unplug the ESP32 wire from D0 first, otherwise two things
  // transmit onto the same line.
  else if (line.indexOf("GET /selftest") >= 0) { sendSelfTest(); sendOk(client); }
  else                                        { sendPage(client); }  // "/" and anything else

  client.stop();
}

// Take light states out of a browser request, e.g. "?red=1&blue=0".
// Only touch what was really sent: the arrow buttons of the phone page
// should not switch a light off by accident.
void lightsFromRequest(const String& line) {
  int value;
  for (int i = 0; i < LIGHT_COUNT; i++) {
    if (requestNumber(line, lights[i].name, value)) setLight(i, value != 0);
  }

  // RGB LED by hand:  /lights?bright=100&hue=240   (bright 0..100, hue 0..359)
  bool rgbChanged = false;
  if (requestNumber(line, "bright", value)) { rgbBrightness = constrain(value, 0, 100); rgbChanged = true; }
  if (requestNumber(line, "hue", value))    { rgbHue = ((value % 360) + 360) % 360;    rgbChanged = true; }
  if (rgbChanged) writeRgb();
}

// Is there "name=number" in the request? Then put the number into "value"
// and return true. There has to be a ? or & in front of the name, otherwise
// it is a chance match in the middle of some other word.
bool requestNumber(const String& line, const char* name, int& value) {
  String search = String(name) + "=";       // e.g. "red="
  int pos = line.indexOf(search);
  while (pos > 0) {
    char before = line.charAt(pos - 1);
    if (before == '?' || before == '&') {
      value = line.substring(pos + search.length()).toInt();
      return true;
    }
    pos = line.indexOf(search, pos + 1);
  }
  return false;
}

// Diagnostics for the browser: what actually arrived from the ESP32 on D0?
//   chars   = single bytes since startup (0 = nothing arrives physically)
//   lines   = complete lines with a line end
//   last    = the last line read, as plain text
//   age_ms  = how long ago that was (below 100 with a live controller)
//   bad     = lines thrown away because they arrived mangled
//   last_bad = the last of those, as plain text
// Below that, what the controller reports right now, unpacked - handy to
// find out which number a button has: hold the button, reload the page.
// Plenty of print() calls are fine here: you open this page by hand while
// hunting a fault, so speed does not matter. While driving it would be
// far too slow (see sendOk).
void sendStatus(WiFiClient& client) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/plain"));
  client.println(F("Connection: close"));
  client.println();
  client.print(F("chars="));  client.println(gamepadChars);
  client.print(F("lines="));  client.println(gamepadLines);
  client.print(F("last="));   client.println(gamepadLastLine);
  client.print(F("age_ms=")); client.println(gamepadLines ? (millis() - gamepadLastMs) : 0);
  client.print(F("bad="));    client.println(gamepadBad);
  client.print(F("last_bad=")); client.println(gamepadLastBad);
  client.print(F("connected=")); client.println(gp.connected ? 1 : 0);
  client.print(F("left_x="));    client.print(gp.lx);
  client.print(F(" left_y="));   client.println(gp.ly);
  client.print(F("right_x="));   client.print(gp.rx);
  client.print(F(" right_y="));  client.println(gp.ry);
  client.print(F("buttons="));   client.print(gp.buttons);
  client.print(F(" dpad="));     client.print(gp.dpad);
  client.print(F(" misc="));     client.println(gp.misc);
  client.print(F("rgb_hue="));    client.println((int)rgbHue);
  client.print(F("rgb_bright=")); client.println((int)rgbBrightness);
}

// Pick a number out of the request, e.g. "?l=" from "GET /drive?l=-40&r=80"
int readParam(const String& line, const char* name) {
  int i = line.indexOf(name);
  if (i < 0) return 0;
  return line.substring(i + strlen(name)).toInt();   // toInt() handles the minus sign
}

// Turn the two stick percentages (-100..100) into PWM values, motor
// protection included. BOTH SIDES TOGETHER -- doing them separately is what
// broke steering:
//
// Below SPEED_MIN (70) a motor does not turn, it only hums. When each side
// went through this on its own, EVERY non-zero value was lifted to at least
// 70. So between -70 and +70 there was nothing at all: the inner side could
// only stand still, run forward at 70, or run BACKWARD at 70. Steering a
// little therefore flipped it straight from "a bit slower" to "full counter
// drive" and the car spun on the spot instead of driving a curve.
// Measured: 40 degrees of stick -> inner +85, 50 degrees -> inner -85.
//
// Now the FASTER side sets the scale. Only it gets the minimum -- it is the
// one that has to break the car away from standstill. The other side keeps
// exactly the ratio from the mix and therefore passes smoothly through zero:
// 40 degrees -> inner +30, 45 -> 0, 50 -> -30.
void driveCommandPercent(int percentLeft, int percentRight) {
  percentLeft  = constrain(percentLeft,  -100, 100);
  percentRight = constrain(percentRight, -100, 100);

  int fastest = max(abs(percentLeft), abs(percentRight));
  if (fastest == 0) { driveCommand(0, 0); return; }

  long speed = SPEED_MIN + (long)fastest * (SPEED_MAX - SPEED_MIN) / 100;

  driveCommand((int)((long)percentLeft  * speed / fastest),
               (int)((long)percentRight * speed / fastest));
}

void driveCommand(int left, int right) {
  drive(left, right);
  lastCommandMs = millis();
  moving = true;
  showDirection(left, right);
}

// --- Stop, and put the display back where it belongs ---
void halt() {
  stopMotors();
  moving = false;
  showIdle();
}

// While the car stands still the matrix goes back to showing the WiFi
// state. Deliberately using the wasConnected marker instead of
// WiFi.status(): every status query is a radio command to the WiFi
// co-processor and costs time, and this runs very often.
void showIdle() {
  lastStep = -1;   // otherwise the check mark would stick on the next start
  showIcon(wasConnected ? ICON_OK : ICON_ERROR);
}

// --- Turn the two side speeds into a heading on the matrix ---
//   sum        = how hard it drives forward or backward
//   difference = how hard it pulls to one side
// Together they make an arrow that can also point diagonally, so a gentle
// arc looks different from a spin on the spot.
void showDirection(int left, int right) {
  float forward = (left + right) / 2.0f;
  float turn    = (right - left) / 2.0f;

  // MIND HOW THE BOARD IS MOUNTED: in our car the Arduino lies sideways,
  // so the LEFT edge of the matrix points FORWARD. Hence the rotation:
  //   forward    ->  to the left in the picture  (so -x)
  //   left turn  ->  downwards in the picture    (so +y)
  // Look down on the car once and trace it with a finger and it is
  // obvious. Mount the board differently and these two lines are all you
  // need to change.
  float mx = -forward;
  float my =  turn;

  if (fabs(mx) < 1.0f && fabs(my) < 1.0f) { showIdle(); return; }

  // Snap the angle to one of the steps. Without snapping we would redraw
  // on every tiny stick tremble and the picture would flicker.
  int step = (int)lroundf(atan2(my, mx) * ARROW_STEPS / (2.0f * PI));
  step = ((step % ARROW_STEPS) + ARROW_STEPS) % ARROW_STEPS;

  if (step == lastStep) return;
  lastStep = step;

  drawArrow(step);
  matrix.renderBitmap(ICON_ARROW, 8, 12);
  currentIcon = ICON_ARROW;
}

// --- Collect lines coming from the ESP32 ---
// Only one character arrives at a time. We gather them until a line end
// shows up, then evaluate the whole line.
void readGamepad() {
  while (Serial1.available()) {
    char c = Serial1.read();
    gamepadChars++;

    if (c == '\n' || c == '\r') {
      if (gamepadLen > 0) {
        gamepadBuffer[gamepadLen] = 0;
        gamepadLines++;
        gamepadLastMs = millis();
        strncpy(gamepadLastLine, gamepadBuffer, sizeof(gamepadLastLine) - 1);
        gamepadLastLine[sizeof(gamepadLastLine) - 1] = 0;
        handleGamepadLine(gamepadBuffer);
        gamepadLen = 0;
      }
    } else if (gamepadLen < sizeof(gamepadBuffer) - 1) {
      gamepadBuffer[gamepadLen++] = c;
    } else {
      gamepadLen = 0;   // nonsense or too long -> drop it and start over
    }
  }
}

// --- What comes from the controller (everything, unfiltered) ---
// The ESP32 does no maths any more, it only reports. Its line:
//    connected,lx,ly,rx,ry,throttle,brake,buttons,dpad,misc*CS
// e.g. "1,0,-400,0,0,0,0,2,0,0*06" = connected, left stick pushed forward,
// button B held. What the buttons and sticks DO on the car is ALL decided
// here in the Arduino. A new idea therefore only means updating this sketch
// over WiFi -- the ESP32 stays as it is. (Over-the-air updates are the
// fussier side on the ESP32, see docs/troubleshooting.md.)
//
// Which button has which number? Open http://<car>/status, hold the
// button, reload -- "buttons=" then shows its number.
// (The Gamepad struct itself sits up top, next to the BUTTON_... values.)
const int GAMEPAD_FIELDS = 10;   // at least this many numbers before the *

Gamepad  gpRaw = {};          // the last valid line (for checking twice)
uint16_t buttonsBefore = 0;   // for the edge: what was pressed last time?
bool     gamepadDrives = false;   // did the stick move the car last time?

// --- Sticks ---
const int STICK_MAX = 512;   // Bluepad32 gives us -512 .. 512 per axis

// DEAD ZONE: at rest the sticks do NOT sit exactly at 0 - we measured up
// to 41 off. Without a dead zone the car would drive off on its own.
// Anything below this counts as "stick released".
const int DEADZONE = 80;

// --- STRAIGHT-AHEAD HELP ---
// The problem: the car only drove straight when the thumb pushed the
// stick EXACTLY forward. One degree off and one side already ran slower.
// A four-year-old cannot hit that. Neither can an adult, really.
//
// The fix is a wedge around the vertical: if the stick sits less than
// STRAIGHT_DEGREES away from "fully forward", that counts as straight and
// the steering is simply set to 0. Backwards works the same, because we
// only ever compare magnitudes (abs()).
//
// Why an ANGLE and not just "throw away small steering values"? Because a
// fixed number would be the same width at every speed - creeping along
// slowly, you could not steer at all any more. The wedge grows with the
// throttle: lots of throttle means a wide wedge in numbers, little
// throttle a narrow one. Under the thumb it always feels the same.
//
// One number to tune. More slack = bigger (20, 25), sharper steering =
// smaller (10). Above ~30 degrees steering gets sluggish.
// Keep it in step with STRAIGHT_DEGREES in controller/gamepad/index.html,
// so both ways of driving feel the same.
const int   STRAIGHT_DEGREES = 15;
// tan() turns the angle into a ratio of sides: at 15 degrees the sideways
// deflection may be up to 27 % of the forward deflection and still count
// as straight ahead.
const float STRAIGHT_TAN = tanf(STRAIGHT_DEGREES * PI / 180.0f);

// Turn a stick value (-512..512) into percent (-100..100), dead zone included.
int stickToPercent(int value) {
  if (abs(value) < DEADZONE) return 0;

  // Subtract the dead zone FIRST, then scale up. Otherwise there would be
  // a jump at the edge of the dead zone: the slightest movement would set
  // off at 15 % instead of pulling away gently.
  long amount  = (long)abs(value) - DEADZONE;
  long percent = amount * 100 / (STICK_MAX - DEADZONE);
  if (percent > 100) percent = 100;
  return value > 0 ? (int)percent : -(int)percent;
}

// Is the stick nearly straight ahead? Then drop the steering entirely.
// Computed on the RAW stick values, before the conversion to percent -
// that is the only place where the geometry holds and 15 degrees really
// are 15 degrees.
int helpGoStraight(int sideways, int forward) {
  int slack = (int)(abs(forward) * STRAIGHT_TAN);
  if (abs(sideways) <= slack) return 0;

  // Do not just cut it off! That would put a jump right at the edge of
  // the wedge: a hair too far and the steering leaps from 0 to 27 %.
  // Instead subtract the slack and stretch what is left back to full
  // width - the steering then grows smoothly out of zero, and full
  // deflection still steers fully. Same trick as the dead zone.
  long rest      = (long)abs(sideways) - slack;
  long stretched = rest * STICK_MAX / (STICK_MAX - slack);
  return sideways > 0 ? (int)stretched : -(int)stretched;
}

// SKID STEER: the car has no steering wheel. It turns by running one side
// faster than the other, like a digger. So we turn "throttle" and
// "steering" (both on the LEFT stick) into two side speeds in percent.
void stickToDrive(int stickX, int stickY, int& l, int& r) {
  // The minus on the throttle: the y axis counts like a screen, up is
  // MINUS. Pushing the stick forward gives -400, but we want "forward".
  int rawForward  = -stickY;
  int rawSideways = helpGoStraight(stickX, rawForward);

  int throttle = stickToPercent(rawForward);
  int steering = stickToPercent(rawSideways);
  l = throttle + steering;
  r = throttle - steering;

  // At full throttle AND full steering this would come to 200. Rather
  // than simply clipping at 100 we scale both sides down by the same
  // ratio - otherwise the car would suddenly steer less at full
  // throttle than at half throttle.
  int biggest = max(abs(l), abs(r));
  if (biggest > 100) {
    l = l * 100 / biggest;
    r = r * 100 / biggest;
  }
}

// --- CHECKSUM ---
// While steering, the motors pull so much current that D0 now and then
// loses a character or picks up garbage - we measured 0 bad lines standing
// still and 33 in about 15 s of hard steering. "0,0,1,0" turns into
// "0,01,0": one comma gone, and the numbers land in the wrong field. That
// is exactly how LEDs used to light up that nobody had switched on, and
// how the horn croaked.
// So the ESP32 adds a checksum, the same way GPS receivers do: every
// character before the * is combined with XOR ("either-or"), written as
// two hex digits. We work out the same thing here. If it does not match,
// something broke on the way -- throw it away, the next line is only 20 ms
// behind. The real cure is a clean ground wire (see "Gamepad through an
// ESP32"); the checksum is the safety net.
uint8_t checksum(const char* from, const char* to) {
  uint8_t sum = 0;
  for (const char* p = from; p < to; p++) sum ^= (uint8_t)*p;
  return sum;
}

// Check one line and unpack it into "g". false = mangled.
// The "struct" in front of Gamepad is on purpose: the Arduino tools
// automatically copy every function header to the very top of the file,
// where "Gamepad" is not known yet. With "struct" in front it reads there
// as "this is coming later".
bool parseLine(const char* line, struct Gamepad& g) {
  // 1. Checksum: exactly two hex digits after the *, and they must match.
  const char* star = strchr(line, '*');
  if (!star || !isxdigit((unsigned char)star[1])
            || !isxdigit((unsigned char)star[2]) || star[3] != 0) return false;
  if (strtol(star + 1, nullptr, 16) != checksum(line, star)) return false;

  // 2. Collect the numbers in front of it. MORE fields than expected are
  //    fine -- that way a newer ESP32 may add something at the end later.
  long f[GAMEPAD_FIELDS];
  int  n = 0;
  const char* p = line;
  while (true) {
    char* end;
    long value = strtol(p, &end, 10);
    if (end == p) return false;             // there was no number here
    if (n < GAMEPAD_FIELDS) f[n] = value;
    n++;
    p = end;
    if (p == star) break;
    if (*p != ',') return false;            // some stray character
    p++;
  }
  if (n < GAMEPAD_FIELDS) return false;

  // 3. Belt and braces: do the numbers fit what a controller can deliver
  //    at all?
  for (int i = 1; i <= 4; i++) if (f[i] < -STICK_MAX || f[i] > STICK_MAX) return false;
  if (f[0] < 0 || f[0] > 1)                               return false;
  if (f[5] < 0 || f[5] > 1023 || f[6] < 0 || f[6] > 1023) return false;
  if (f[7] < 0 || f[7] > 0xFFFF)                          return false;
  if (f[8] < 0 || f[8] > 15 || f[9] < 0 || f[9] > 255)    return false;

  g.connected = f[0];
  g.lx = f[1];  g.ly = f[2];  g.rx = f[3];  g.ry = f[4];
  g.throttle = f[5]; g.brake = f[6];
  g.buttons = f[7]; g.dpad = f[8]; g.misc = f[9];
  return true;
}

void handleGamepadLine(const char* line) {
  Gamepad fresh;
  if (!parseLine(line, fresh)) {
    gamepadBad++;
    strncpy(gamepadLastBad, line, sizeof(gamepadLastBad) - 1);
    gamepadLastBad[sizeof(gamepadLastBad) - 1] = 0;
    return;
  }

  // Sticks count at once. A wrong stick value would only be wrong for 20 ms.
  gp.lx = fresh.lx;  gp.ly = fresh.ly;  gp.rx = fresh.rx;  gp.ry = fresh.ry;
  gp.throttle = fresh.throttle; gp.brake = fresh.brake;

  // Only believe the buttons once they arrive the SAME TWICE IN A ROW.
  // The checksum catches almost everything -- but only almost: roughly one
  // in 256 mangled lines happens to have the right sum anyway. For the
  // sticks that does not matter, for a light switch it does (the light
  // would stay on). Two lines in a row mangled in exactly the same way is
  // next to impossible. Costs 20 ms, which nobody notices.
  if (fresh.buttons == gpRaw.buttons && fresh.dpad == gpRaw.dpad &&
      fresh.misc == gpRaw.misc && fresh.connected == gpRaw.connected) {
    gp.buttons = fresh.buttons;  gp.dpad = fresh.dpad;
    gp.misc    = fresh.misc;     gp.connected = fresh.connected;
  }
  gpRaw = fresh;

  applyGamepad();
}

// This is where it says what the buttons do on the car. New ideas go HERE.
void applyGamepad() {
  // Whether a controller is there at all -- we pass that on to the ATOM,
  // which plays the starter sound when one connects.
  gamepadConnected = gp.connected;

  // EDGES: "just pressed" and "just released". Without them a light switch
  // would toggle 50 times a second for as long as a finger rests on the
  // button.
  uint16_t pressed  = gp.buttons & ~buttonsBefore;
  uint16_t released = buttonsBefore & ~gp.buttons;
  buttonsBefore = gp.buttons;

  // Lights: like a light switch -- press once = on, press again = off.
  for (int i = 0; i < LIGHT_COUNT; i++) {
    if (pressed & lights[i].button) setLight(i, !lights[i].on);
  }

  // Horn: honks while the button is held. Every "on" extends its dead
  // man's switch. It is only switched off on RELEASE -- that way you can
  // still test it from the browser (/horn) without the controller choking
  // it straight away.
  if (gp.buttons & BUTTON_HORN)  setHorn(true);
  if (released & BUTTON_HORN)    setHorn(false);

  // RGB LED: the right stick (minus on y: up is MINUS in Bluepad32).
  rgbStickX = stickToPercent( gp.rx);
  rgbStickY = stickToPercent(-gp.ry);

  // Driving: the left stick. Exactly the same path as the browser takes -
  // percent in, motor protection (SPEED_MAX) here in the sketch.
  // When the stick is back in the middle we send "stop" ONCE and then
  // nothing. The old ESP32 sent "0,0" 50 times a second -- so the
  // controller kept choking the browser controls, even with nobody
  // touching it.
  int l, r;
  stickToDrive(gp.lx, gp.ly, l, r);
  if (l != 0 || r != 0) {
    driveCommandPercent(l, r);
    gamepadDrives = true;
  } else if (gamepadDrives) {
    driveCommandPercent(0, 0);
    gamepadDrives = false;
  }
}

// For /selftest: send a valid "nothing pressed" line out on D1, with its
// checksum -- exactly the way the ESP32 would send it.
void sendSelfTest() {
  const char* content = "0,0,0,0,0,0,0,0,0,0";
  char line[40];
  snprintf(line, sizeof(line), "%s*%02X", content,
           checksum(content, content + strlen(content)));
  Serial1.println(line);
  Serial1.flush();
}

// These two belong together: listen to the gamepad AND make sure the car
// stops when nothing arrives any more. In its own function so it also
// keeps running during a long WiFi attempt.
void gamepadAndDeadMan() {
  readGamepad();
  if (moving && (millis() - lastCommandMs > TIMEOUT_MS)) halt();
  if (hornOn && (millis() - hornLastMs > HORN_TIMEOUT_MS)) setHorn(false);
  // Nothing from the ESP32 any more means no controller either -- and the
  // RGB LED should not keep turning forever on the last stick value.
  if (millis() - gamepadLastMs > TIMEOUT_MS) {
    gamepadConnected = false;
    rgbStickX = 0;
    rgbStickY = 0;
  }
  tendRgb();
  sendSound();
}

// --- Tell the ATOM Echo what is going on ---
// A new line goes out as soon as something changes - horn and controller at
// once, the motor speeds at most every 50 ms (otherwise D1 would be busy all
// the time with 50 controller lines per second). If nothing changes, a line
// still goes out every 200 ms as a heartbeat: if that stops, the ATOM falls
// silent by itself after 500 ms.
const unsigned long SOUND_MIN_GAP_MS   = 50;
const unsigned long SOUND_HEARTBEAT_MS = 200;

void sendSound() {
  static int           sentLeft = 0, sentRight = 0;
  static bool          sentHorn = false, sentGamepad = false;
  static unsigned long sentMs = 0;

  unsigned long since = millis() - sentMs;
  bool switchesNew = (hornOn != sentHorn || gamepadConnected != sentGamepad);
  bool speedNew    = (targetLeft != sentLeft || targetRight != sentRight);
  if (!switchesNew && !(speedNew && since >= SOUND_MIN_GAP_MS)
      && since < SOUND_HEARTBEAT_MS) return;

  // Everything in ONE print() - same reason as in sendOk().
  char line[24];
  snprintf(line, sizeof(line), "%d,%d,%d,%d\n", targetLeft, targetRight,
           hornOn ? 1 : 0, gamepadConnected ? 1 : 0);
  Serial1.print(line);

  sentLeft = targetLeft;  sentRight = targetRight;
  sentHorn = hornOn;      sentGamepad = gamepadConnected;
  sentMs = millis();
}

void sendOk(WiFiClient& client) {
  // Everything in ONE print(): each call is a separate command to the
  // WiFi co-processor. Five calls = five round trips = noticeably slower.
  client.print(F("HTTP/1.1 200 OK\r\n"
                 "Content-Type: text/plain\r\n"
                 "Content-Length: 2\r\n"
                 "Connection: close\r\n"
                 "\r\n"
                 "ok"));
}

void sendPage(WiFiClient& client) {
  // Headers in one go, then the page itself.
  client.print(F("HTTP/1.1 200 OK\r\n"
                 "Content-Type: text/html; charset=utf-8\r\n"
                 "Connection: close\r\n"
                 "\r\n"));
  client.print(PAGE);
}

void printMac() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  for (int i = 0; i < 6; i++) {
    if (mac[i] < 16) Serial.print("0");
    Serial.print(mac[i], HEX);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
}

// --- Motor helpers ---
// KICK-START: breaking away from standstill takes more force than keeping
// things turning (static friction in the gearbox). At 59 % PWM a slightly
// stiff motor therefore sometimes just sits there and hums. So: give it a
// short extra push when starting off and when reversing direction, then
// drop back to the normal speed.
// Short really means short - KICK_SPEED is above the continuous limit of
// the 3-6 V motors and would be too much as a permanent value.
const int  KICK_SPEED = 200;   // 0..255 (~7.5 V on a 9.6 V pack)
const long KICK_MS    = 120;   // how long the push lasts

// Set by drive() per manoeuvre and used for the next kick.
long kickDuration = KICK_MS;

// One state pair per side so we can detect the moment we start off
int  lastSpeedLeft = 0,  lastSpeedRight = 0;
unsigned long kickUntilLeft = 0, kickUntilRight = 0;

// Drive one side. The two state variables come in by reference so that
// left and right can kick-start independently of each other.
void driveSide(int speed, bool invert,
               int pwmPin, int in1, int in2,
               int& lastSpeed, unsigned long& kickUntil) {
  speed = constrain(speed, -SPEED_MAX, SPEED_MAX);
  if (invert) speed = -speed;

  // Starting from standstill OR changing direction -> start the kick
  bool startingOff = (speed != 0) &&
                     (lastSpeed == 0 || (speed > 0) != (lastSpeed > 0));
  if (startingOff) kickUntil = millis() + kickDuration;
  lastSpeed = speed;

  int magnitude = abs(speed);
  if (magnitude > 0 && (millis() < kickUntil || pulseActive)) {
    magnitude = max(magnitude, KICK_SPEED);
  }

  if (speed >= 0) { digitalWrite(in1, HIGH); digitalWrite(in2, LOW); }
  else            { digitalWrite(in1, LOW);  digitalWrite(in2, HIGH); }
  analogWrite(pwmPin, magnitude);
}

void leftSide(int speed) {
  driveSide(speed, INVERT_LEFT, PWMA, AIN1, AIN2,
            lastSpeedLeft, kickUntilLeft);
}

void rightSide(int speed) {
  driveSide(speed, INVERT_RIGHT, PWMB, BIN1, BIN2,
            lastSpeedRight, kickUntilRight);
}

void drive(int left, int right) {
  // Sides running against each other means it spins on the spot -> kick
  // for longer and keep pulsing afterwards (see PULSE_ON above).
  bool spin = (left > 0 && right < 0) || (left < 0 && right > 0);
  kickDuration = spin ? KICK_TURN_MS : KICK_MS;
  spinning     = spin;
  targetLeft   = left;
  targetRight  = right;

  leftSide(left);
  rightSide(right);
}

void stopMotors() {
  lastSpeedLeft  = 0;   // so the next start gets a kick again
  lastSpeedRight = 0;
  spinning    = false;  // no more pulsing once it stands still
  pulseActive = false;
  targetLeft = 0; targetRight = 0;
  analogWrite(PWMA, 0);
  analogWrite(PWMB, 0);
  digitalWrite(AIN1, LOW);
  digitalWrite(AIN2, LOW);
  digitalWrite(BIN1, LOW);
  digitalWrite(BIN2, LOW);
}
