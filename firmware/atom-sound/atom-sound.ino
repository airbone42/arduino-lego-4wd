/*
  Car sounds - M5Stack ATOM Echo
  ============================================================
  The ATOM Echo is a 24 mm cube with its own ESP32 (ESP32-PICO-D4), a small
  speaker and a microphone. The car tells it WHAT is going on - HOW that
  sounds is decided here.

  What is a tone? Air wobbling back and forth very fast. If the speaker cone
  moves 400 times per second, we hear a tone of 400 hertz. Every wave here
  is calculated number by number, 16000 numbers per second. No recordings,
  no storage needed.

  The sounds:
    horn            - while button Y on the controller (or the top of the
                      ATOM itself) is held
    starter motor   - "rrr-rrr-rrr-VROOM" when a controller connects
    reversing beeper - beep ... beep ... while the car drives backwards

  The Arduino sends one line:  L,R,horn,gamepad\n   e.g. "-150,-150,0,1"
    L, R    = motor speed left/right (minus = backwards)
    horn    = 1 while honking
    gamepad = 1 while a controller is connected
  Missing fields (older car firmware) are simply ignored.

  The RGB LED in the ATOM tells you where you are:
    green = ready, nothing arriving from the Arduino
    cyan  = lines are arriving from the Arduino (the wire is fine)
    yellow = a sound is playing right now

  WIRING - the 4-hole header on the bottom, from the end AWAY from the
  Grove socket:
    G21  free
    G25  <-- Arduino D1 through 1 kOhm, and from here 2 kOhm to GND
             (5 V -> 3.3 V: D1 is a 5 V pin, the ATOM only takes 3.3 V)
    5V   <-- 5 V from a step-down converter. NEVER together with USB!
    GND  <-- its own wire straight to a GND pin of the Arduino
  The 5-hole header on the other side is taken by the speaker and mic.
  New firmware only goes on over USB: pull the 5 V wire FIRST.

  BUILD: any ESP32 board package with the "M5Stack-ATOM" board. We use the
  Bluepad32 package that the gamepad bridge needs anyway:
    arduino-cli compile --fqbn esp32-bluepad32:esp32:m5stack-atom firmware/atom-sound
    arduino-cli upload -p COMx --fqbn esp32-bluepad32:esp32:m5stack-atom firmware/atom-sound
*/

#include <driver/i2s.h>

// Which build is running - printed at startup.
const char* VERSION = "1-public";

// --- Pins inside the ATOM Echo ---
// Speaker amplifier (NS4168), wired inside the cube. These pins are OFF
// LIMITS for anything else, and so is G23 (microphone).
const int PIN_I2S_BCK  = 19;
const int PIN_I2S_WS   = 33;
const int PIN_I2S_DOUT = 22;

const int PIN_BUTTON = 39;   // the top of the cube, pressed = LOW
const int PIN_LED    = 27;   // RGB LED inside

// From the Arduino. We listen on TWO pins at once (G25 and G21), each with
// its own receiver - so it does not matter which of the two holes the wire
// goes into. The print on the cube is tiny.
const int  PIN_FROM_CAR_A = 25;
const int  PIN_FROM_CAR_B = 21;
const long BAUD           = 38400;   // same as ESP32 -> Arduino

// If nothing arrives from the Arduino for this long, everything counts as
// "off". Otherwise a car with a broken wire would honk forever.
const unsigned long TIMEOUT_MS = 500;

// ======================= THE SOUND =======================
const int SAMPLE_RATE = 16000;   // numbers per second

// --- Horn ---
// Two low tones close together: "toooot". Higher tones (440/550 Hz) were
// barely louder on this tiny speaker, but sounded like a bicycle horn.
const float HORN_TONE_1 = 320;   // hertz
const float HORN_TONE_2 = 380;

// --- Reversing beeper ---
// High, because the small speaker is best at high tones - and real
// reversing beepers are high for the same reason: you hear them anywhere.
const float BEEP_TONE    = 1000;   // hertz
const float BEEP_ON_S    = 0.4;    // beeps this long ...
const float BEEP_OFF_S   = 0.4;    // ... then pauses this long
const float BEEP_VOLUME  = 0.7;    // a little quieter than the horn
// When does it count as backwards? L and R arrive as motor speed (up to 150).
// While turning one side may run backwards for a moment without the car
// reversing - so the average of both sides decides.
const int   REVERSE_FROM = 20;

// --- Starter motor ---
// First the starter "cranks": a low buzz that swells 6 times per second -
// every time a piston pushes against the air in its cylinder. Then the
// engine catches: it revs up briefly and drops back to idle.
const float CRANK_S      = 1.2;   // "rrr-rrr-rrr"
const float CRANK_RATE   = 6;     // pushes per second
const float CATCH_S      = 1.0;   // "VROOMmmm"

// --- The compressor ---
// All sounds are ADDED UP first (each roughly -1..+1), and only at the very
// end ONE compressor squashes the sum at the top and bottom (tanh). Quiet
// parts get louder and loud ones never hit the limit - not even when horn
// and beeper play at the same time.
// GRIT: how hard it squashes.
//   0.5 = almost a pure sine, sounds like a flute or a doorbell
//   2   = round, but with some bite - "toooot"
//   6   = almost a square wave - "braaap"
const float GRIT = 2.0;
// 32767 is the largest number that fits in 16 bits. The ATOM has no volume
// knob - this IS the volume knob. It does not get much louder with the tiny
// speaker; for that you would need a bigger one.
const int   FULL = 32000;

// Fade in and out softly (10 ms). A wave jumping hard from 0 to full makes
// the speaker click.
const float RAMP = 1.0 / (SAMPLE_RATE * 0.010);

// Sound goes out in small chunks: 128 numbers = 8 ms. Left and right get the
// same number - there is only one speaker.
const int BLOCK = 128;
int16_t   block[BLOCK * 2];

// ======================= STATE =======================
// What the Arduino last told us
unsigned long lastLineMs      = 0;
int           speedLeft = 0, speedRight = 0;
bool          hornFromCar     = false;
bool          controllerThere = false;

// What is sounding right now
bool honking   = false;
bool reversing = false;

float hornPhase1 = 0, hornPhase2 = 0, hornLevel = 0;
float beepPhase  = 0, beepLevel = 0;
long  beepCounter = 0;          // counts numbers since reversing started
float starterPhase = 0;
long  starterPos   = -1;        // -1 = not playing

int lastColour = -1;

// One line collector per receiver.
struct Receiver {
  HardwareSerial* port;
  char            buffer[48];
  unsigned int    len;
};
Receiver receivers[] = { { &Serial1, "", 0 }, { &Serial2, "", 0 } };

// LED dimmed - the SK6812 is blinding otherwise.
void colour(int nr) {
  if (nr == lastColour) return;
  lastColour = nr;
  if (nr == 0) neopixelWrite(PIN_LED, 0, 20, 0);    // green
  if (nr == 1) neopixelWrite(PIN_LED, 0, 20, 20);   // cyan
  if (nr == 2) neopixelWrite(PIN_LED, 20, 20, 0);   // yellow
}

// Move a wave one step on (the phase runs from 0 to 1, then starts over).
void advance(float& phase, float hertz) {
  phase += hertz / SAMPLE_RATE;
  if (phase >= 1) phase -= 1;
}

// Move a volume softly towards its target (see RAMP).
void glide(float& level, bool on) {
  float target = on ? 1.0 : 0.0;
  if (level < target) level = min(target, level + RAMP);
  if (level > target) level = max(target, level - RAMP);
}

float compressor(float sum) {
  return tanhf(GRIT * sum) / tanhf(GRIT);   // "full" lands exactly on 1
}

// --- The three sounds. Each returns a number between about -1 and +1. ---

float hornSample() {
  glide(hornLevel, honking);
  advance(hornPhase1, HORN_TONE_1);
  advance(hornPhase2, HORN_TONE_2);
  return hornLevel * (sinf(2 * PI * hornPhase1) + sinf(2 * PI * hornPhase2)) / 2;
}

float beepSample() {
  // The clock only runs while reversing and starts at 0 every time - so the
  // first beep comes at once and not somewhere in the middle of the rhythm.
  bool beepNow = false;
  if (reversing) {
    long cycle = (long)((BEEP_ON_S + BEEP_OFF_S) * SAMPLE_RATE);
    beepNow = (beepCounter % cycle) < (long)(BEEP_ON_S * SAMPLE_RATE);
    beepCounter++;
  } else {
    beepCounter = 0;
  }
  glide(beepLevel, beepNow);
  advance(beepPhase, BEEP_TONE);
  return BEEP_VOLUME * beepLevel * sinf(2 * PI * beepPhase);
}

// A narrow pulse instead of a sine: one short bang per turn. That is full of
// overtones - so you can hear 55 Hz on a speaker that cannot play the low
// fundamental itself (the ear fills it in).
// -0.18 at the bottom so the wave averages out at 0.
float pulse(float phase) {
  return phase < 0.15 ? 1.0 : -0.18;
}

void startStarter() {
  starterPos = 0;
  Serial.println("Controller connected -> starter");
}

float starterSample() {
  if (starterPos < 0) return 0;
  float t = (float)starterPos / SAMPLE_RATE;   // seconds since start
  starterPos++;

  float hertz, volume;
  if (t < CRANK_S) {
    // Cranking: 6 pushes per second. Each push gets louder and higher -
    // in between it struggles along.
    float push = 0.5 + 0.5 * sinf(2 * PI * CRANK_RATE * t);   // 0..1
    hertz  = 55 + 20 * push;
    volume = 0.3 + 0.7 * push * push;
  } else if (t < CRANK_S + CATCH_S) {
    // Caught: revs high first (170 Hz), then drops to idle (80 Hz).
    float s = t - CRANK_S;
    hertz  = 80 + 90 * expf(-4 * s);
    volume = min(1.0f, (CRANK_S + CATCH_S - t) / 0.3f);   // fade out at the end
  } else {
    starterPos = -1;   // done
    return 0;
  }
  advance(starterPhase, hertz);
  return volume * pulse(starterPhase);
}

// Work out 8 ms of sound and hand it to the amplifier. Handing it over waits
// until there is room - which keeps loop() at exactly the right pace.
void sendBlock() {
  for (int i = 0; i < BLOCK; i++) {
    float sum = hornSample() + beepSample() + starterSample();
    int16_t value = FULL * compressor(sum);
    block[2 * i]     = value;   // left
    block[2 * i + 1] = value;   // right
  }
  size_t written;
  i2s_write(I2S_NUM_0, block, sizeof(block), &written, portMAX_DELAY);
}

// Pick field n out of a line "a,b,c" (n = 0 is the first), -1 if missing.
// The same function as in the car's sketch.
int readField(const char* line, int n) {
  for (int i = 0; i < n; i++) {
    line = strchr(line, ',');
    if (!line) return -1;
    line++;
  }
  return atoi(line);
}

// Handle one line "L,R,horn,gamepad".
void handleLine(const char* line) {
  int horn = readField(line, 2);
  if (horn < 0) return;   // without a horn field it is not a valid line
  lastLineMs  = millis();
  speedLeft   = readField(line, 0);
  speedRight  = readField(line, 1);
  hornFromCar = (horn != 0);

  // Controller: only the CHANGE from "not there" to "there" starts the
  // starter - the line comes again every 200 ms even if nothing changes.
  int gamepad = readField(line, 3);
  if (gamepad >= 0) {
    if (gamepad && !controllerThere) startStarter();
    controllerThere = (gamepad != 0);
  }
}

// Collect lines from the Arduino, one character at a time.
void readCar(Receiver& r) {
  while (r.port->available()) {
    char c = r.port->read();
    if (c == '\n' || c == '\r') {
      if (r.len > 0) {
        r.buffer[r.len] = 0;
        handleLine(r.buffer);
        r.len = 0;
      }
    } else if (r.len < sizeof(r.buffer) - 1) {
      r.buffer[r.len++] = c;
    } else {
      r.len = 0;   // too long = nonsense, drop it
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUTTON, INPUT);   // the pull-up is already on the board

  // Receive only, no transmit pin (-1): the ATOM never answers the car.
  Serial1.begin(BAUD, SERIAL_8N1, PIN_FROM_CAR_A, -1);
  Serial2.begin(BAUD, SERIAL_8N1, PIN_FROM_CAR_B, -1);

  // Set up the sound output (I2S): 16000 numbers/s, 16 bits each, left+right.
  i2s_config_t cfg = {};
  cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate          = SAMPLE_RATE;
  cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.dma_buf_count        = 4;
  cfg.dma_buf_len          = BLOCK;
  cfg.tx_desc_auto_clear   = true;   // if the buffer runs dry: silence, no buzzing
  i2s_pin_config_t pins = {};
  pins.mck_io_num   = I2S_PIN_NO_CHANGE;
  pins.bck_io_num   = PIN_I2S_BCK;
  pins.ws_io_num    = PIN_I2S_WS;
  pins.data_out_num = PIN_I2S_DOUT;
  pins.data_in_num  = I2S_PIN_NO_CHANGE;
  if (i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL) != ESP_OK ||
      i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) {
    Serial.println("Speaker (I2S) does not start!");
    neopixelWrite(PIN_LED, 20, 0, 0);   // red
    while (true) delay(1000);
  }

  Serial.printf("\nATOM sound, build %s. Press the top = horn.\n", VERSION);

  // A short toot at startup: you hear straight away that everything lives.
  honking = true;
  for (int i = 0; i < 20; i++) sendBlock();    // ~160 ms
  honking = false;
  for (int i = 0; i < 10; i++) sendBlock();
  colour(0);
}

void loop() {
  readCar(receivers[0]);
  readCar(receivers[1]);

  bool carThere = (millis() - lastLineMs < TIMEOUT_MS);
  if (!carThere) {
    // Wire off or the Arduino is restarting: everything quiet. Controller
    // back to "not there", so the starter plays again on the next connect.
    hornFromCar = false;
    speedLeft = speedRight = 0;
    controllerThere = false;
  }

  bool button = (digitalRead(PIN_BUTTON) == LOW);
  honking = button || hornFromCar;

  bool reversingNow = (speedLeft + speedRight) / 2 < -REVERSE_FROM;
  if (reversingNow != reversing) {
    Serial.println(reversingNow ? "Reversing -> beep" : "Reversing over");
    reversing = reversingNow;
  }

  static bool honkedBefore = false;
  if (honking != honkedBefore) {
    Serial.printf("Horn %s (%s)\n", honking ? "ON" : "off",
                  button ? "button" : hornFromCar ? "car" : "-");
    honkedBefore = honking;
  }

  bool playing = honking || reversing || starterPos >= 0;
  colour(playing ? 2 : carThere ? 1 : 0);

  sendBlock();
}
