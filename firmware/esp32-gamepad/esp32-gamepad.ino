/*
  Gamepad bridge - ESP32 + Bluepad32
  ============================================================
  This is the piece that cuts the laptop out of the loop. The ESP32 rides
  on the car, talks Bluetooth to a game controller, works out how fast the
  left and the right side should run, and sends that to the Arduino as a
  short text line over a single wire.

  The line looks like this:   L,R,red,blue,green,horn,gamepad\n
                       e.g.   "80,-20,1,0,1,0,1"
  The first two numbers are PERCENT from -100 to 100 (minus = backwards),
  then one 1 (on) or 0 (off) per light, then the horn (1 while button Y is
  held) and last whether a controller is connected (the ATOM Echo plays a
  starter sound when that goes from 0 to 1).

  Why percent and not raw motor values? So the motor protection stays
  where it belongs: in the Arduino. The ESP32 only says "give it full
  throttle" - how much full throttle is, the Arduino decides (SPEED_MAX).
  Exactly like the browser control does through /drive?l=..&r=..

  WIRING (two wires, that is all):
    ESP32 GPIO13  ->  Arduino D0   (= RX of Serial1)
    ESP32 GND     ->  Arduino GND  - a wire of its OWN, straight to a GND
                                     pin of the Arduino
  The way back (Arduino D1 -> ESP32) is deliberately NOT wired: the
  Arduino would put 5 V on a pin that only tolerates 3.3 V. This direction
  is harmless, and measured: 0 errors in 250 lines.
  Mind the ground wire: if it shares the breadboard rail with the motor
  current, the line loses characters whenever the motors pull hard (we saw
  33 mangled lines in 15 s of steering). A direct wire fixed it.

  POWER: give the ESP32 a supply of its own, a 5 V step-down converter off
  the battery into VIN. Do NOT feed it from the Arduino's 5 V pin - see
  docs/troubleshooting.md, this is how we killed our first board.

  BOARD: DOIT ESP32 DEVKIT V1 (30 pins), chip ESP32-D0WD-V3 - a classic
  ESP32 with Bluetooth Classic (BR/EDR), which is what nearly all game
  controllers speak. Bluepad32 is a BOARD PACKAGE, not a library:

    arduino-cli config add board_manager.additional_urls \
      https://raw.githubusercontent.com/ricardoquesada/esp32-arduino-lib-builder/master/bluepad32_files/package_esp32_bluepad32_index.json
    arduino-cli core update-index
    arduino-cli core install esp32-bluepad32:esp32

  BUILD AND UPLOAD: use tools/ota-esp32.ps1. It sets the partition scheme
  (min_spiffs) that this sketch needs - WiFi and Bluetooth together are
  too big for the default layout.

  PAIRING (only needed once): unplug the controller, hold HOME for 5 s to
  switch it fully off, then hold Y + HOME until the LED blinks fast. Ours
  reports itself as "Switch Pro". The pairing is remembered afterwards.
*/

#include <Bluepad32.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include "arduino_secrets.h"

ControllerPtr controller[BP32_MAX_GAMEPADS];

// --- Over-the-air updates ---
// This is not just convenience: once the ESP32 takes its power from VIN,
// no USB cable may be plugged in at all - the board cannot take two
// supplies at once. Without OTA you would have to pull the power lead
// before every single update.
//
// The ESP32 then transmits two things at the same time: Bluetooth to the
// controller and WiFi for the updates. Both share ONE antenna. That is
// supported, but it can cost controller latency. If it ever stutters, set
// WIFI_ONLY_AT_START to true - the WiFi is then on for the first few
// seconds after power-up and cannot interfere afterwards.
const bool          WIFI_ONLY_AT_START = false;
const unsigned long WIFI_WINDOW_MS     = 90000;   // 90 s, only used when true

// A small marker for which build is actually running. It is printed at
// startup, so you can see at a glance whether an update arrived.
// THIS IS NOT DECORATION: an OTA upload can report success while the
// board keeps booting the old half of the flash (see
// docs/troubleshooting.md). The version line is the only honest proof.
const char* VERSION = "2-horn-and-starter";

const char* WIFI_HOSTNAME = "lego4wd-gamepad";
bool wifiReady = false;                 // OTA already started?
unsigned long lastWifiCheck = 0;

const int LED = 2;   // the blue on-board LED - see tendLed()

// --- The blue LED tells you where you stand ---
// Steady     = controller connected, ready to drive.
// Slow pulse = no controller, still searching.
// 3 flashes  = just restarted.
// Nothing    = the board is not running (no power, or it is stuck).
// The pulse is the real win: before we had it, "searching" looked exactly
// like "dead". There is no cable on a driving car, so this LED is the
// only report you get. Deliberately well visible - a 150 ms blip on that
// tiny blue SMD LED is easy to miss in daylight.
const unsigned long BLINK_ON_MS  = 400;
const unsigned long BLINK_OFF_MS = 600;
unsigned long lastBlink = 0;
bool          blinkOn   = false;

// --- The data line to the Arduino ---
// PICKING THIS PIN IS NOT ARBITRARY. Three traps, all of which cost us
// time, and none of which you can see from the outside:
//
//  * GPIO16/17 are wired internally to the PSRAM on ESP32-WROVER modules
//    and are dead to the outside world. On WROOM modules they are free.
//    Same "DEVKIT V1" shape, same silkscreen. Our replacement board sent
//    nothing on GPIO17 although the old one had been fine. If a signal
//    pin "does nothing", this is the first suspect.
//  * GPIO12 is a strapping pin: at power-up it sets the flash voltage,
//    and if it is HIGH the board does not boot. An idle TX line sits
//    exactly at HIGH.
//  * GPIO34/35, VP and VN can only be INPUTS. As a transmit pin they
//    simply do nothing, and you cannot tell by looking.
//
// GPIO13 has none of those problems, and it sits on the SAME pin row as
// VIN and GND - which matters on a breadboard, because the board is so
// wide that it only leaves one free row of holes. Equally fine and on the
// same row: D25, D26, D27, D32, D33.
const int  PIN_TX = 13;      // GPIO13, marked "D13" on the board
const long BAUD   = 38400;
// Why 38400 and not faster? We measured 9600 and 115200, both without a
// single error. 38400 sits comfortably in between: the line is only 16 %
// busy, and every single bit lasts three times longer than at 115200 -
// margin against the electrical noise the motors make.

// --- Sticks ---
// Bluepad32 gives us -512 .. 512 per axis.
const int STICK_MAX = 512;

// DEAD ZONE: at rest the sticks do NOT sit exactly at 0 - we measured up
// to 41 off. Without a dead zone the car would drive off on its own.
// Anything below this counts as "stick released".
const int DEAD_ZONE = 80;

// --- STRAIGHT-AHEAD HELP ---
// The problem: the car only drove straight when the thumb pushed the
// stick EXACTLY forward. One degree off and one side already ran slower.
// A four-year-old cannot hit that. Neither can an adult, really.
//
// The fix is a wedge around the vertical: if the stick sits less than
// STRAIGHT_DEGREES away from "fully forward", that counts as straight and
// the steering is simply set to 0. Backwards works the same, because we
// only ever compare magnitudes.
//
// Why an ANGLE and not just "throw away small steering values"? Because a
// fixed number would be the same width at every speed - creeping along
// slowly, you could not steer at all any more. The wedge grows with the
// throttle: lots of throttle means a wide wedge in numbers, little
// throttle a narrow one. Under the thumb it always feels the same.
//
// One number to tune. More slack = bigger (20, 25), sharper steering =
// smaller (10). Above ~30 degrees steering gets sluggish.
const int   STRAIGHT_DEGREES = 15;
// tan() turns the angle into a ratio of sides: at 15 degrees the sideways
// deflection may be up to 27 % of the forward deflection and still count
// as straight ahead.
const float STRAIGHT_TAN = tanf(STRAIGHT_DEGREES * 3.14159265f / 180.0f);

// One line every 20 ms = 50 per second. The Arduino stops by itself when
// nothing arrives for 800 ms (dead man's switch), so there is plenty of
// slack if a line is lost.
const unsigned long SEND_INTERVAL_MS = 20;
unsigned long lastSend = 0;

// Last values sent - so the serial monitor only prints on a real change
// instead of 50 lines per second.
int lastL = 0, lastR = 0;

// --- Lights on buttons ---
// Each light behaves like a light switch: press once = on, press again =
// off. The ORDER has to match the lights[] table in the Arduino sketch -
// we only send numbers down the wire, no names.
const int LIGHT_COUNT = 3;

// Which button switches which light?
//
// MIND THE BUTTON NAMES: our controller reports itself as a "Switch Pro",
// and on Nintendo pads A/B and X/Y sit the other way round than on Xbox.
// So Bluepad32 may well call your buttons something else. If the wrong
// button switches, THIS FUNCTION is the only place to touch - just try
// a() / b() / x() / y() until it fits.
bool buttonForLight(ControllerPtr ctl, int i) {
  switch (i) {
    case 0: return ctl->b();   // red LED   on D3
    case 1: return ctl->x();   // blue LED  on D5
    case 2: return ctl->a();   // green LED on D6
  }
  return false;
}

const char* LIGHT_NAME[LIGHT_COUNT] = { "red", "blue", "green" };  // for messages
bool lightOn[LIGHT_COUNT]      = { false, false, false };  // what we send over
bool buttonBefore[LIGHT_COUNT] = { false, false, false };  // was it pressed last time?

// --- Horn ---
// Unlike the lights this is NOT a toggle but a push button: it honks for as
// long as Y is held, so no edge detection is needed. The field sits BEHIND
// the lights - new fields always go at the end. The sound itself comes from
// the ATOM Echo on the Arduino; the Arduino passes the horn on.
bool horn = false;

// Is a controller connected right now? Goes out as the last field - the
// ATOM Echo plays the starter sound when it changes from 0 to 1.
bool controllerConnected = false;

void onConnect(ControllerPtr ctl) {
  for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
    if (controller[i] == nullptr) {
      controller[i] = ctl;
      ControllerProperties p = ctl->getProperties();
      Serial.printf("Controller connected: %s  VID=0x%04x PID=0x%04x\n",
                    ctl->getModelName().c_str(), p.vendor_id, p.product_id);
      return;
    }
  }
}

void onDisconnect(ControllerPtr ctl) {
  for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
    if (controller[i] == ctl) {
      controller[i] = nullptr;
      Serial.println("Controller gone -- stop the car.");
      // Send a stop straight away. The Arduino would halt by itself after
      // 800 ms anyway, but immediately is better. The lights stay as they
      // were - light is not dangerous.
      horn = false;
      controllerConnected = false;
      sendDriveCommand(0, 0);
      return;
    }
  }
}

void setup() {
  Serial.begin(115200);          // USB - only for watching
  Serial2.begin(BAUD, SERIAL_8N1, -1, PIN_TX);   // -1 = we never receive

  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);

  delay(500);
  Serial.println();
  Serial.printf("Bluepad32 version: %s\n", BP32.firmwareVersion());

  BP32.setup(&onConnect, &onDisconnect);
  BP32.enableVirtualDevice(false);

  // DO NOT call enableNewBluetoothConnections(true) here! We tried: after
  // a few minutes the board was gone - no ping, no OTA, LED off.
  // Bluetooth and WiFi share one antenna, and a permanent pairing scan on
  // the side is too much. Bluepad32 accepts new pairings anyway, so the
  // call buys nothing.

  // We deliberately do NOT forget the pairing: the controller should find
  // its way back on its own at power-up. If you ever need a clean slate,
  // add this line for one run:
  //   BP32.forgetBluetoothKeys();

  startWifi();

  // Three quick blinks = "I restarted and the software is running". Sounds
  // like a gimmick, but after an over-the-air update it is the only sign
  // of life you get - there is no cable attached any more.
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED, HIGH); delay(80);
    digitalWrite(LED, LOW);  delay(120);
  }

  Serial.printf("Ready (build %s). Switch the controller on (Y + HOME).\n", VERSION);
}

// --- Join the WiFi. Does NOT wait: the controller matters more. ---
void startWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(WIFI_HOSTNAME);
  WiFi.begin(SECRET_SSID, SECRET_PASS);

  // DO NOT call WiFi.setSleep(false)! When WiFi and Bluetooth run
  // together, the WiFi MUST sleep in between - those pauses are the only
  // time Bluetooth gets the shared antenna. Without them the driver bails
  // out immediately with a very clear message and the board reboots in an
  // endless loop:
  //   "Should enable WiFi modem sleep when both WiFi and Bluetooth are
  //    enabled!!!!!!"  ->  abort()
  // The default is "sleep on", so we simply leave it alone.

  Serial.print("WiFi MAC (for the fixed IP in your router): ");
  Serial.println(WiFi.macAddress());
}

// --- Called as soon as the WiFi is up: arm the update receiver ---
void startOta() {
  ArduinoOTA.setHostname(WIFI_HOSTNAME);
  ArduinoOTA.setPassword(SECRET_OTA_PASS);

  // IMPORTANT: the car has to stand still before new firmware is written.
  // Nothing else runs here during an upload - without this stop it would
  // simply keep driving on the last command.
  ArduinoOTA.onStart([]() {
    // Switch the lights off too: the ESP32 is about to restart and will
    // come back up with everything off, so both sides stay in agreement.
    for (int i = 0; i < LIGHT_COUNT; i++) lightOn[i] = false;
    horn = false;
    sendDriveCommand(0, 0);

    // Turn Bluetooth off BEFORE the new firmware is written. Two reasons:
    //  1. After an OTA update the ESP32 only does a SOFTWARE restart, not
    //     a real power cycle. The Bluetooth block stays in whatever state
    //     it was in and often does not come back up. The board then looks
    //     dead: LED off, no ping, no OTA - until you pull the power once.
    //     This cost us half a day.
    //  2. During the upload the WiFi has the antenna to itself. The 1.1 MB
    //     go across faster and more reliably.
    btStop();
    Serial.println("Update incoming -- car stopped, Bluetooth off.");
  });
  ArduinoOTA.onEnd([]() { Serial.println("Update done, restarting."); });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Update failed (error %u)\n", error);
  });

  ArduinoOTA.begin();
  wifiReady = true;

  Serial.print("Over-the-air update ready at  ");
  Serial.print(WiFi.localIP());
  Serial.printf("  (name: %s)\n", WIFI_HOSTNAME);
}

// Check once a second whether the WiFi has turned up. Only look, never
// wait - the controller must never stutter because of this.
void tendWifi() {
  // Should the WiFi go off again after the window? (see WIFI_ONLY_AT_START)
  if (WIFI_ONLY_AT_START && millis() > WIFI_WINDOW_MS) {
    if (WiFi.getMode() != WIFI_OFF) {
      Serial.println("WiFi window closed -- Bluetooth only from here.");
      ArduinoOTA.end();
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      wifiReady = false;
    }
    return;
  }

  if (wifiReady) {
    ArduinoOTA.handle();       // is new firmware waiting?
    if (WiFi.status() != WL_CONNECTED) wifiReady = false;
    return;
  }

  if (millis() - lastWifiCheck < 1000) return;
  lastWifiCheck = millis();

  if (WiFi.status() == WL_CONNECTED) {
    startOta();
  } else {
    WiFi.reconnect();
  }
}

// Turn a stick value (-512..512) into percent (-100..100), dead zone included.
int stickToPercent(int value) {
  if (abs(value) < DEAD_ZONE) return 0;

  // Subtract the dead zone FIRST, then scale up. Otherwise there would be
  // a jump at the edge of the dead zone: the slightest movement would set
  // off at 15 % instead of pulling away gently.
  long amount  = (long)abs(value) - DEAD_ZONE;
  long percent = amount * 100 / (STICK_MAX - DEAD_ZONE);
  if (percent > 100) percent = 100;
  return value > 0 ? (int)percent : -(int)percent;
}

// Is the stick nearly straight ahead? Then drop the steering entirely.
// Computed on the RAW stick values, before the conversion to percent -
// that is the only place where the geometry holds and 15 degrees really
// are 15 degrees. (The dead zone below then trims a little more off, so
// the wedge ends up slightly wider than 15 degrees in practice. For us
// that is the right direction.)
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

// Send one line "L,R,red,blue,green,horn,gamepad" to the Arduino. Lights,
// horn and controller state live in globals, so they do not have to be
// passed in.
void sendDriveCommand(int l, int r) {
  Serial2.printf("%d,%d", l, r);
  for (int i = 0; i < LIGHT_COUNT; i++) Serial2.printf(",%d", lightOn[i] ? 1 : 0);
  Serial2.printf(",%d,%d\n", horn ? 1 : 0, controllerConnected ? 1 : 0);
}

// Work the LED. Called on every pass and uses millis() instead of
// delay() - a delay() would hold up the sending, and the dead man's
// switch over there only waits 800 ms.
void tendLed(bool connected) {
  if (connected) {
    digitalWrite(LED, HIGH);
    blinkOn = false;              // start fresh on the next disconnect
    return;
  }

  unsigned long span = blinkOn ? BLINK_ON_MS : BLINK_OFF_MS;
  if (millis() - lastBlink >= span) {
    lastBlink = millis();
    blinkOn = !blinkOn;
    digitalWrite(LED, blinkOn ? HIGH : LOW);
  }
}

void loop() {
  BP32.update();
  tendWifi();

  int l = 0, r = 0;
  bool haveController = false;

  for (auto ctl : controller) {
    if (ctl && ctl->isConnected() && ctl->isGamepad()) {
      haveController = true;

      // --- Lights: only the EDGE counts, the change from "released" to
      // "pressed". Without that check a light would toggle 50 times a
      // second for as long as a finger rests on the button.
      for (int i = 0; i < LIGHT_COUNT; i++) {
        bool button = buttonForLight(ctl, i);
        if (button && !buttonBefore[i]) {
          lightOn[i] = !lightOn[i];
          Serial.printf("Light %s %s\n", LIGHT_NAME[i], lightOn[i] ? "ON" : "off");
        }
        buttonBefore[i] = button;
      }

      // --- Horn: simply pass it on while Y is held.
      bool y = ctl->y();
      if (y != horn) Serial.printf("Horn %s\n", y ? "ON" : "off");
      horn = y;

      // SKID STEER: the car has no steering wheel. It turns by running one
      // side faster than the other, like a digger. So we turn "throttle"
      // and "steering" into two side speeds.
      //
      // The minus on the throttle: the y axis counts like a screen, up is
      // MINUS. Pushing the stick forward gives -400, but we want "forward".
      int rawForward  = -ctl->axisY();
      int rawSideways =  ctl->axisX();

      // Nearly straight? Then perfectly straight (see STRAIGHT_DEGREES).
      rawSideways = helpGoStraight(rawSideways, rawForward);

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
      break;   // we only use the first controller
    }
  }

  // No controller? Then every button counts as released, so the next press
  // after reconnecting registers as a fresh edge again.
  if (!haveController) {
    l = 0; r = 0;
    horn = false;
    for (int i = 0; i < LIGHT_COUNT; i++) buttonBefore[i] = false;
  }

  controllerConnected = haveController;
  tendLed(haveController);

  // Send at a steady rate whether anything changed or not: the constant
  // ticking is what keeps the dead man's switch in the Arduino awake.
  if (millis() - lastSend >= SEND_INTERVAL_MS) {
    lastSend = millis();
    sendDriveCommand(l, r);

    if (l != lastL || r != lastR) {
      lastL = l; lastR = r;
      Serial.printf("left=%4d %%   right=%4d %%\n", l, r);
    }
  }
}
