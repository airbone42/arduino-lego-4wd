/*
  Gamepad bridge - ESP32 + Bluepad32
  ============================================================
  This is the piece that cuts the laptop out of the loop. The ESP32 rides
  on the car, listens to a game controller over Bluetooth, and sends
  EVERYTHING it knows about it to the Arduino as a short text line over a
  single wire - without deciding anything itself.

  It used to do the thinking too (skid-steer mixing, light switches,
  horn ...). That meant every new idea needed TWO boards updated, and the
  ESP32 is the moody one of the two when updating over WiFi (see
  docs/troubleshooting.md). Now it is just an "extension cord" for the
  controller: new buttons, new lights, new sounds - all of that happens in
  the Arduino sketch (firmware/lego4wd). This sketch should never need
  touching again.

  The line looks like this:
      connected,lx,ly,rx,ry,throttle,brake,buttons,dpad,misc*CS\n
  e.g.  "1,0,-400,120,0,0,0,2,0,0*05"
    connected       1 = controller there, 0 = none (then all the rest is 0)
    lx,ly           left stick,  -512..512 (y: MINUS = forward, like on a
                    screen, where up counts downwards)
    rx,ry           right stick, -512..512
    throttle,brake  ZR/ZL analog 0..1023 - always 0 in Switch mode, where
                    those two are plain buttons (they show up in "buttons")
    buttons         one bit per button:  A=1 B=2 X=4 Y=8 LB=16 RB=32
                    LT=64 RT=128, left stick pressed=256, right=512.
                    So "B + Y" held together = 2 + 8 = 10.
    dpad            up=1 down=2 right=4 left=8
    misc            HOME=1 minus=2 plus=4 capture=8
    *CS             checksum, two hex digits (see checksum())

  Should a new field ever be needed, it goes at the END (before the *).

  WIRING (two wires, that is all):
    ESP32 GPIO13  ->  Arduino D0   (= RX of Serial1)
    ESP32 GND     ->  Arduino GND  - a wire of its OWN, straight to a GND
                                     pin of the Arduino
  The way back (Arduino D1 -> ESP32) is deliberately NOT wired: the
  Arduino would put 5 V on a pin that only tolerates 3.3 V. This direction
  is harmless, and measured: 0 errors in 250 lines.
  Mind the ground wire: if it shares the breadboard rail with the motor
  current, the line loses characters whenever the motors pull hard (we saw
  33 mangled lines in 15 s of steering). A direct wire fixed it, and the
  checksum makes sure the odd mangled line that still slips through is
  thrown away instead of obeyed.

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
// From the outside the quickest check is the Arduino's http://<car>/status:
// if the "last=" line ends in a "*" plus checksum, this build is running.
const char* VERSION = "20-gamepad-raw";

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
//    exactly at HIGH. (Its neighbour GPIO14 twitches during boot, so the
//    Arduino would get garbage on D0 every time the ESP32 restarts.)
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
// single error. 38400 sits comfortably in between, and every single bit
// lasts three times longer than at 115200 - margin against the electrical
// noise the motors make. The longest line is about 50 characters; at 50
// lines a second that keeps the wire at most two thirds busy.

// One line every 20 ms = 50 per second. The Arduino stops by itself when
// nothing arrives for 800 ms (dead man's switch), so there is plenty of
// slack if a line is lost.
const unsigned long SEND_INTERVAL_MS = 20;
unsigned long lastSend = 0;

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
      // Send a "nothing pressed" line straight away. The Arduino would
      // halt by itself after 800 ms anyway, but immediately is better.
      sendEmpty();
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
    sendEmpty();

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

// --- CHECKSUM ---
// When the car steers, the motors disturb the wire and characters get
// lost (we measured 33 mangled lines in 15 s). So that the Arduino can
// tell a broken line apart from a good one for SURE, we add a checksum -
// the same trick GPS receivers use in their NMEA sentences.
// The recipe: combine every character of the line, one after the other,
// with XOR ("one or the other, but not both"). The result is a number
// from 0 to 255, which we write as two hex digits after a "*". The Arduino
// does the same sum on what it received - if a character went missing or
// got garbled, a different number comes out and the line is thrown away.
uint8_t checksum(const char* text) {
  uint8_t sum = 0;
  while (*text) sum ^= (uint8_t)*text++;
  return sum;
}

// Send a finished line, checksum included. In ONE print(), so it does not
// go down the wire in pieces.
void sendLine(const char* content) {
  char line[80];
  snprintf(line, sizeof(line), "%s*%02X\n", content, checksum(content));
  Serial2.print(line);
}

// "No controller, nothing pressed" - this stops the car.
void sendEmpty() {
  sendLine("0,0,0,0,0,0,0,0,0,0");
}

// The whole state of the controller as one line. NOTHING is converted
// here - what the car makes of it is decided by the Arduino alone.
void sendGamepad(ControllerPtr ctl) {
  char content[72];
  snprintf(content, sizeof(content), "1,%ld,%ld,%ld,%ld,%ld,%ld,%u,%u,%u",
           (long)ctl->axisX(),  (long)ctl->axisY(),
           (long)ctl->axisRX(), (long)ctl->axisRY(),
           (long)ctl->throttle(), (long)ctl->brake(),
           (unsigned)ctl->buttons(), (unsigned)ctl->dpad(),
           (unsigned)ctl->miscButtons());
  sendLine(content);
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

  // Find the first connected controller - we only use one.
  ControllerPtr active = nullptr;
  for (auto ctl : controller) {
    if (ctl && ctl->isConnected() && ctl->isGamepad()) { active = ctl; break; }
  }
  tendLed(active != nullptr);

  // Send at a steady rate whether anything changed or not: the constant
  // ticking is what keeps the dead man's switch in the Arduino awake.
  if (millis() - lastSend >= SEND_INTERVAL_MS) {
    lastSend = millis();
    if (active) sendGamepad(active);
    else        sendEmpty();
  }
}
