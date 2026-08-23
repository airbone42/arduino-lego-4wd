# Next steps

The car works. These are the upgrades we have planned, in the order we intend to do
them, with the pin budget already worked out. Nothing here is built yet — treat it as
a design sketch, not tested code.

## Pins still free

Currently used: `D2`, `D4`, `D7`, `D8`, `D9`, `D10`. `D13` is the on-board LED, and
the 12×8 LED matrix is internal (no pins).

Free and useful: `D3`, `D5`, `D6`, `D11`, `D12`, `A0`–`A5` (analog pins work as plain
digital pins). PWM-capable on the UNO R4 WiFi: `D3`, `D5`, `D6`, `D9`, `D10`, `D11` —
so four PWM pins are still available.

---

## 1. Second TB6612 — one channel per motor

**Why.** Right now four motors hang on one driver, two in parallel per channel. A
channel supplies about **1.2 A continuous**, and two motors in parallel never split
it fairly — the one with slightly less resistance takes more. Under load (carpet, a
slope, a heavier body) the weaker one drops out. Giving each motor its own channel
doubles the available current and lets each wheel be driven independently.

**Cost.** Soldering one more breakout, six more pins, and a slightly longer
`drive()`.

### Pin plan

| Function | Pin | Note |
|----------|-----|------|
| PWMC — rear left speed | `D5` | PWM |
| CIN1 — rear left dir | `D12` | |
| CIN2 — rear left dir | `A0` | used as digital |
| PWMD — rear right speed | `D6` | PWM |
| DIN1 — rear right dir | `A1` | |
| DIN2 — rear right dir | `A2` | |
| STBY (driver 2) | 5 V | same as driver 1 |

Driver 1 then handles the **front** axle, driver 2 the **rear** axle. `VM` and `GND`
of the second driver go to the same rails as the first — do not run motor current
through the Arduino.

### Code change

`driveSide()` is already written so that each side carries its own state by
reference, so it extends cleanly. Add the second pair of pins and a second pair of
state variables, then:

```cpp
// front axle (driver 1)
const int PWMA = 9,  AIN1 = 7,  AIN2 = 8;    // front left
const int PWMB = 10, BIN1 = 4,  BIN2 = 2;    // front right
// rear axle (driver 2)
const int PWMC = 5,  CIN1 = 12, CIN2 = A0;   // rear left
const int PWMD = 6,  DIN1 = A1, DIN2 = A2;   // rear right

int lastFL = 0, lastFR = 0, lastRL = 0, lastRR = 0;
unsigned long kickFL = 0, kickFR = 0, kickRL = 0, kickRR = 0;

void drive(int left, int right) {
  driveSide(left,  INVERT_LEFT,  PWMA, AIN1, AIN2, lastFL, kickFL);
  driveSide(left,  INVERT_LEFT,  PWMC, CIN1, CIN2, lastRL, kickRL);
  driveSide(right, INVERT_RIGHT, PWMB, BIN1, BIN2, lastFR, kickFR);
  driveSide(right, INVERT_RIGHT, PWMD, DIN1, DIN2, lastRR, kickRR);
}
```

`stopMotors()` needs the two extra channels too. Everything above `drive()` — the web
handlers, the throttle mixing, the dead man's switch — stays exactly as it is.

Once each wheel has its own channel, per-wheel tricks become possible: slowing only
the inner *rear* wheel in a turn reduces tyre scrub noticeably.

---

## 2. Lights and a horn on the controller buttons

**The important design decision first.** Do **not** add `/horn` and `/lights`
endpoints. Every HTTP request costs a new TCP connection, and connection setup is
the measured bottleneck (60–270 ms — see [build-notes.md](build-notes.md)). Sending
a separate request for the horn would compete with the drive commands and bring back
the stuttering we just fixed.

Instead: carry the auxiliary state **inside the drive command** as a bitmask.
`/drive?l=50&r=50&a=3` — same request, one extra parameter, zero extra connections.

### Pin plan

| Function | Pin | Wiring |
|----------|-----|--------|
| Headlights (2× white LED) | `D3` | LED + 220 Ω resistor to GND each. PWM pin, so they can dim. |
| Tail / brake lights (2× red LED) | `A3` | same, 220 Ω |
| Beacon / hazards (orange LED) | `A4` | same, 220 Ω |
| Horn (buzzer) | `D11` | see note below |

An LED at 220 Ω draws about 15 mA, comfortably inside the ~20 mA a pin can source.
Two LEDs in **parallel**, each with its own resistor, is ~30 mA on one pin — that is
already at the limit. Either give each LED its own pin, or drive the pair through a
transistor (a 2N3904 with a 1 kΩ base resistor is plenty).

**The buzzer:** an *active* buzzer just needs `digitalWrite(HIGH)`. A *passive* one
needs `tone(PIN_HORN, 440)` / `noTone(PIN_HORN)` — which is nicer, because then the
horn can play different notes and the kids will absolutely want that. Louder buzzers
draw 30 mA or more, so put a transistor in front of it rather than hanging it
directly off a pin.

### Firmware sketch

```cpp
const int PIN_HEAD   = 3;
const int PIN_TAIL   = A3;
const int PIN_BEACON = A4;
const int PIN_HORN   = 11;

// bits in the "a" parameter
const int AUX_HORN   = 1;
const int AUX_HEAD   = 2;
const int AUX_BEACON = 4;

int auxBits = 0;

void applyAux(int bits, int left, int right) {
  auxBits = bits;
  digitalWrite(PIN_HEAD, (bits & AUX_HEAD) ? HIGH : LOW);

  // The horn is a passive buzzer here
  if (bits & AUX_HORN) tone(PIN_HORN, 440); else noTone(PIN_HORN);

  // Beacon blinks on its own while the bit is set
  bool beaconOn = (bits & AUX_BEACON) && ((millis() / 400) % 2);
  digitalWrite(PIN_BEACON, beaconOn ? HIGH : LOW);

  // Brake light needs no button: on whenever we are stopped or reversing
  bool braking = (left <= 0 && right <= 0);
  digitalWrite(PIN_TAIL, braking ? HIGH : LOW);
}
```

Hook it into the existing handler:

```cpp
else if (line.indexOf("GET /drive") >= 0) {
  int l = percentToSpeed(readParam(line, "?l="));
  int r = percentToSpeed(readParam(line, "&r="));
  driveCommand(l, r);
  applyAux(readParam(line, "&a="), l, r);
  sendOk(client);
}
```

Two details that are easy to forget:

- The **beacon has to keep blinking between commands**, so call
  `applyAux(auxBits, ...)` once per `loop()` as well, not only when a request arrives.
- The **dead man's switch must silence the horn.** When the timeout fires and calls
  `stopMotors()`, also call `noTone(PIN_HORN)`. Otherwise a dropped connection leaves
  a car screaming under the sofa.

### Controller mapping

Standard Gamepad API button indices — the same on Xbox, PlayStation and most
generic pads:

| Button | Index | Action |
|--------|-------|--------|
| A / ✕ | 0 | horn (hold) |
| X / ▢ | 2 | headlights (toggle) |
| Y / △ | 3 | beacon (toggle) |
| D-pad | 12–15 | already used for driving |

In `controller/gamepad/index.html`, next to `readPad()`:

```js
let auxState = 0;          // the bitmask we send
let wasPressed = {};       // for edge detection on the toggles

function readAux(pad) {
  const down = i => pad.buttons[i] && pad.buttons[i].pressed;

  // Hold-to-sound: horn follows the button directly
  auxState = down(0) ? (auxState | 1) : (auxState & ~1);

  // Toggles: only react on the rising edge, otherwise they flip 60×/second
  [[2, 2], [3, 4]].forEach(([button, bit]) => {
    if (down(button) && !wasPressed[button]) auxState ^= bit;
    wasPressed[button] = down(button);
  });

  return auxState;
}
```

and append it to the request in `setDrive()`:

```js
send('/drive?l=' + l + '&r=' + r + '&a=' + auxState);
```

One catch: `setDrive()` currently only sends when the throttle values *changed*.
Pressing the horn while standing still would then send nothing. Include `auxState`
in the change check:

```js
const changed = (l !== lastL || r !== lastR || auxState !== lastAux);
```

The phone page can get the same treatment with three extra buttons under the D-pad.

---

## 3. A face on the LED matrix

The 12×8 matrix currently shows the WiFi state. Once the car is connected that
information is boring. Ideas that cost nothing but a few arrays:

- eyes that look in the direction it is steering;
- a mouth that grins when driving forward, flattens when stopped;
- a battery bar (read the pack voltage on a spare analog pin through a divider).

This is the single best "let the kids design something" part of the project — an
8×12 grid of ones and zeros is a drawing on squared paper.

---

## 4. Smaller ideas

- **HTTP keep-alive.** Would cut the 150–400 ms reaction time to near nothing by
  reusing the connection instead of paying for setup every time.
- **Battery monitoring.** Voltage divider into `A5`, warn on the LED matrix when the
  pack sags. Better than the car slowly getting sadder and nobody knowing why.
- **Ultrasonic sensor** (HC-SR04) on the front, and a "do not drive into the wall"
  override in the firmware. Two pins.
- **A proper body.** So far it is a rolling breadboard. There is a lot of LEGO left.
