# Next steps

The car works. These are the upgrades we have planned, in the order we intend to do
them, with the pin budget already worked out. None of the code below is built yet —
treat it as a design sketch, not tested code.

## Pins still free

Currently used: `D2`, `D4`, `D7`, `D8`, `D9`, `D10` (motors), `D3`, `D5`, `D6`
(lights), `D0` (the line from the ESP32 gamepad bridge). `D13` is the on-board LED,
and the 12×8 LED matrix is internal (no pins).

Free and useful: `D11`, `D12`, `A0`–`A5` (analog pins work as plain digital pins).
`D11` is the only PWM pin still available.

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

> ⚠️ **This collides with the lights.** A second driver needs **two more PWM pins**,
> and the UNO R4 WiFi only has six in total: `D3`, `D5`, `D6`, `D9`, `D10`, `D11`.
> Four are taken (`D9`/`D10` motors, `D3`/`D5` lights) and `D6` is the green light, so
> only `D11` is left. Move two of the lights to **analog pins first** — an on/off LED
> does not need PWM at all, so `A3`/`A4` do the job and `D5`/`D6` come free again.
> Change the pin numbers in the `lights[]` table, nothing else.

| Function | Pin | Note |
|----------|-----|------|
| PWMC — rear left speed | `D5` | PWM, freed up by moving the blue light to `A3` |
| CIN1 — rear left dir | `D12` | |
| CIN2 — rear left dir | `A0` | used as digital |
| PWMD — rear right speed | `D6` | PWM, freed up by moving the green light to `A4` |
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

## 2. A horn on the controller buttons

**Lights are built** — three LEDs on `D3`/`D5`/`D6`, switched from the controller
buttons. See [wiring.md](wiring.md) for how they are wired and why the resistor value
is what it is, and the `lights[]` table in the firmware for how to add another one
(one line there, one in the ESP32 sketch, one in the gamepad page — the order has to
match, because only numbers go down the wire).

The horn is the same idea with a buzzer instead of an LED.

**The important design decision first.** Do **not** give it an endpoint of its own.
Every HTTP request costs a new TCP connection, and connection setup is the measured
bottleneck (60–270 ms — see [build-notes.md](build-notes.md)). A separate request for
the horn would compete with the drive commands and bring back the stuttering we just
fixed. Instead it rides **inside the drive command**, exactly like the lights do:
`/drive?l=50&r=50&red=1&horn=1` — same request, one more parameter, zero extra
connections.

### Pin plan

| Function | Pin | Wiring |
|----------|-----|--------|
| Horn (buzzer) | `D11` | through a transistor, see below |

**The buzzer:** an *active* buzzer just needs `digitalWrite(HIGH)`. A *passive* one
needs `tone(PIN_HORN, 440)` / `noTone(PIN_HORN)` — which is nicer, because then the
horn can play different notes and the kids will absolutely want that.

> ⚠️ **A pin of the UNO R4 may only source 8 mA** (the old UNO allowed 20 mA, which is
> why most tutorials online say otherwise). Louder buzzers draw 30 mA or more, so put
> a transistor in front of it rather than hanging it off a pin — a 2N3904 with a 1 kΩ
> base resistor is plenty.

### Two details that are easy to forget

- The **dead man's switch must silence the horn.** When the timeout fires and calls
  `halt()`, also call `noTone(PIN_HORN)`. Otherwise a dropped connection leaves a car
  screaming under the sofa.
- A **blinking beacon has to keep blinking between commands**, so it needs to be
  driven once per `loop()` as well, not only when a request arrives. The plain on/off
  lights do not have that problem.

---

## 3. A face on the LED matrix

The 12×8 matrix already shows the WiFi state when the car stands still and a rotating
heading arrow while it drives. Ideas that cost nothing but a few arrays:

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
