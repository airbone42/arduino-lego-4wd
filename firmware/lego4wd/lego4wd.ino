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
// If it turns too lazily, move this value into the negative (e.g. -60).
const int TURN_INNER_SPEED = 0;

// --- Fix a reversed side in software (no re-plugging of wires) ---
// If one side spins the wrong way, flip the matching flag to true: the
// sketch then internally turns "forward" into "backward" for that side.
const bool INVERT_LEFT  = true;   // our left side is wired the other way round
const bool INVERT_RIGHT = true;   // and so is the right one

// --- Dead man's switch ---
unsigned long lastCommandMs = 0;
// No fresh command -> stop. This has to be longer than the send interval
// of the control page plus the time for one connection setup (up to
// ~300 ms), otherwise the ride stutters. At the same time it is how long
// the car keeps rolling in the worst case if the link dies - so don't
// make it huge either.
const unsigned long TIMEOUT_MS = 800;
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
  let timer = null;
  function send(cmd){ fetch('/'+cmd).catch(()=>{}); }
  function start(cmd){ send(cmd); clearInterval(timer); timer = setInterval(()=>send(cmd), 150); }
  function halt(){ clearInterval(timer); send('stop'); }
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

  pinMode(PWMA, OUTPUT); pinMode(AIN1, OUTPUT); pinMode(AIN2, OUTPUT);
  pinMode(PWMB, OUTPUT); pinMode(BIN1, OUTPUT); pinMode(BIN2, OUTPUT);
  stopMotors();

  matrix.begin();
  matrix.renderBitmap(ICON_SEARCH, 8, 12);

  // Is the WiFi module there at all?
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("WiFi module not found!");
    matrix.renderBitmap(ICON_ERROR, 8, 12);
    return;   // do NOT hang here - loop() keeps trying
  }

  Serial.print("MAC (use this for the fixed IP in your router): ");
  printMac();

  connectWifi();
}

// One connection attempt with a time limit. true = success.
bool connectWifi() {
  lastWifiAttempt = millis();
  matrix.renderBitmap(ICON_SEARCH, 8, 12);
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);

  WiFi.begin(ssid, pass);
  unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < WIFI_TIMEOUT_MS) {
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("No connection. Next attempt in 10 s.");
    matrix.renderBitmap(ICON_ERROR, 8, 12);
    return false;
  }

  Serial.print("Connected! Open in a browser:  http://");
  Serial.println(WiFi.localIP());
  matrix.renderBitmap(ICON_OK, 8, 12);

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
  // Dead man's switch: no command for too long -> stop
  if (moving && (millis() - lastCommandMs > TIMEOUT_MS)) {
    stopMotors();
    moving = false;
  }

  // WiFi watchdog: link gone? Stop and reconnect.
  if (WiFi.status() != WL_CONNECTED) {
    if (wasConnected) {
      Serial.println("WiFi lost -- motors off.");
      stopMotors();
      moving = false;
      wasConnected = false;
      matrix.renderBitmap(ICON_ERROR, 8, 12);
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
    driveCommand(percentToSpeed(readParam(line, "?l=")),
                 percentToSpeed(readParam(line, "&r=")));
    sendOk(client);
  }
  else if (line.indexOf("GET /stop")    >= 0) { stopMotors(); moving = false; sendOk(client); }
  else                                        { sendPage(client); }  // "/" and anything else

  client.stop();
}

// Pick a number out of the request, e.g. "?l=" from "GET /drive?l=-40&r=80"
int readParam(const String& line, const char* name) {
  int i = line.indexOf(name);
  if (i < 0) return 0;
  return line.substring(i + strlen(name)).toInt();   // toInt() handles the minus sign
}

// Turn percent (-100..100) into a PWM value, motor protection included.
int percentToSpeed(int percent) {
  percent = constrain(percent, -100, 100);
  if (percent == 0) return 0;
  long speed = (long)abs(percent) * SPEED_MAX / 100;
  if (speed < SPEED_MIN) speed = SPEED_MIN;   // otherwise it only hums
  return percent > 0 ? (int)speed : -(int)speed;
}

void driveCommand(int left, int right) {
  drive(left, right);
  lastCommandMs = millis();
  moving = true;
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
  if (startingOff) kickUntil = millis() + KICK_MS;
  lastSpeed = speed;

  int magnitude = abs(speed);
  if (magnitude > 0 && millis() < kickUntil) {
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
  leftSide(left);
  rightSide(right);
}

void stopMotors() {
  lastSpeedLeft  = 0;   // so the next start gets a kick again
  lastSpeedRight = 0;
  analogWrite(PWMA, 0);
  analogWrite(PWMB, 0);
  digitalWrite(AIN1, LOW);
  digitalWrite(AIN2, LOW);
  digitalWrite(BIN1, LOW);
  digitalWrite(BIN2, LOW);
}
