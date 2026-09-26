# Wiring

![Wiring diagram](../images/wiring-diagram.svg)

## The idea in one paragraph

The Arduino is not strong enough to drive a motor from a pin — an output pin can
supply about 20 mA, a TT motor wants a few hundred. So the Arduino only sends
*signals* to the TB6612 motor driver, and the driver switches the *battery* current
through to the motors. Two motors per channel, wired in parallel: the two left
wheels always do the same thing, and so do the two right ones. That is why the car
steers like a tank — one side faster than the other.

## Connection table

### Power

| From | To | Why |
|------|----|-----|
| Battery **+** | breadboard **V+ rail** | one rail feeds everything |
| Battery **−** | breadboard **GND rail** | |
| V+ rail | Arduino **VIN** | ⚠️ **VIN, never 5V** — see the warning below |
| V+ rail | TB6612 **VM** | motor current comes straight from the battery |
| Arduino **5V** | TB6612 **VCC** | logic supply for the driver |
| Arduino **5V** | TB6612 **STBY** | pulls the driver out of standby permanently |
| Arduino **GND** | GND rail | |
| TB6612 **GND** | GND rail | |

> ⚠️ **Battery goes to VIN, never to the 5V pin.** The 5V pin sits *behind* the
> board's voltage regulator. Feeding 9.6 V into it bypasses the regulator and
> destroys the board. VIN is the input that expects a raw battery (7–12 V).

> **Common ground is not optional.** The Arduino and the driver have to share a
> ground reference, otherwise the driver cannot tell what "HIGH" on a signal pin
> even means. Every GND ends up on the same rail.

### Signals — Arduino to TB6612

| Arduino pin | TB6612 pin | Meaning |
|-------------|-----------|---------|
| **D9** (~) | PWMA | speed, left side |
| **D7** | AIN1 | direction, left side |
| **D8** | AIN2 | direction, left side |
| **D10** (~) | PWMB | speed, right side |
| **D4** | BIN1 | direction, right side |
| **D2** | BIN2 | direction, right side |

`D9` and `D10` must be PWM-capable pins (the ones marked `~` on the board) — that
is how the speed is set. The direction pins can be any digital pin.

How a channel behaves:

| IN1 | IN2 | PWM | Result |
|-----|-----|-----|--------|
| HIGH | LOW | 0–255 | turn one way, at that speed |
| LOW | HIGH | 0–255 | turn the other way |
| LOW | LOW | – | coast (motor freewheels) |
| HIGH | HIGH | – | brake (motor is shorted, stops hard) |

### Motors

| TB6612 pin | To |
|------------|----|
| **AO1** | left front motor **+**, left rear motor **+** |
| **AO2** | left front motor **−**, left rear motor **−** |
| **BO1** | right front motor **+**, right rear motor **+** |
| **BO2** | right front motor **−**, right rear motor **−** |

A DC motor has no real polarity — the `+` and `−` above are just "the two terminals,
consistently the same way round on both motors of a side". If a side turns the wrong
way you have two options:

1. swap that channel's two motor wires, or
2. leave the wiring alone and flip `INVERT_LEFT` / `INVERT_RIGHT` in the sketch.

Option 2 is what we did. Both of ours are `true`.

### Lights (optional)

Three LEDs that the controller buttons switch like light switches: press once = on,
press again = off.

| LED | Pin | Button | Resistor |
|-----|-----|--------|----------|
| red | `D3` | **B** | 1 kΩ |
| blue | `D5` | **X** | 1 kΩ |
| green | `D6` | **A** | ~500 Ω (two 1 kΩ in parallel) |

Three parts per LED, no soldering: a jumper from the pin to a free row, the resistor
from that row to another free row, then the LED with its **long leg** in that row and
its **short leg** in the ground rail.

**The long leg is plus** (anode) and faces the pin. Two mnemonics for when the legs
have already been trimmed: *long = long line = plus*, and the rim of the LED head is
**flattened on the minus side**. Putting one in backwards is harmless — it just does
not light up.

**Which side the resistor goes on does not matter.** Current flows in a loop, so
whatever goes through the LED also goes through the resistor: a narrow spot slows the
whole hose down no matter where it sits. We still always put it at the pin, so all
three chains look the same and every short leg ends up in the same ground rail.

> ⚠️ **A pin of the UNO R4 may only source 8 mA.** The old UNO allowed 20 mA, which
> is why nearly every tutorial online says "220 Ω". On an R4 that is more than twice
> the limit, and a pin killed that way stays dead.

Every LED eats part of the voltage itself, and how much depends on the colour. What
is left over sits across the resistor and sets the current:

| LED | eats | 1 kΩ | ~500 Ω | 330 Ω |
|-----|------|------|--------|-------|
| red | ~2.0 V | 3.0 mA | 6.0 mA | ❌ 9.1 mA |
| green (pale) | ~2.1 V | 2.9 mA | 5.8 mA | ❌ 8.8 mA |
| green (bright) | ~3.1 V | 1.9 mA | 3.8 mA | 5.8 mA |
| blue | ~3.2 V | 1.8 mA | 3.6 mA | 5.5 mA |

Note the two rows for green: there really are two kinds, the old pale (yellowish) one
and the modern bright one, and you cannot tell them apart by looking. Measuring the
voltage across the LED settles it in ten seconds — a nice little experiment, because
the part gives nothing away and the multimeter does.

Green at 1 kΩ turned out too dim for us, so it gets **~500 Ω made from two 1 kΩ
resistors side by side**, since our kit has no 470 Ω. That is worth showing a child:
two resistors next to each other resist **less** than one, the same way two open doors
let twice as many people through. 500 Ω is safe for *both* kinds of green; a single
330 Ω would not be.

Test it with nothing plugged in at all: the little **"L"** LED already on the board
joins in whenever any light is on, so you can check the button before wiring anything.
By hand, in a browser: `http://<car>/lights?red=1&blue=1&green=1`.

### ESP32 gamepad bridge (optional)

This is what cuts the laptop out of the loop — see
[firmware/esp32-gamepad/](../firmware/esp32-gamepad/). Two wires:

| ESP32 | To |
|-------|----|
| **GPIO13** | Arduino **D0** (RX of `Serial1`) |
| **GND** | Arduino **GND** — its own wire, straight to a GND pin |

The way back (Arduino `D1` → ESP32) is deliberately **not** wired: the Arduino would
put 5 V on a pin that only tolerates 3.3 V. This direction is harmless, and measured
at 0 errors in 250 lines.

> ⚠️ **Give the data line a clean ground.** Run the ESP32's `GND` straight to one of
> the Arduino's GND pins, not via the breadboard rail that carries the motor current.
> Sharing the rail cost us 33 mangled lines in 15 seconds of steering — flashing LEDs
> and a croaking horn. See
> [troubleshooting.md](troubleshooting.md#leds-flash-and-the-horn-croaks-while-steering).

> ⚠️ **Give the ESP32 its own 5 V supply** — a small step-down converter (MP1584EN or
> similar) from the battery into `VIN`. Do **not** feed it from the Arduino's 5 V pin:
> ours already sagged to 4.7 V at standstill, and the regulator on the ESP32 needs
> about 1.2 V of headroom. Set the converter to 5.0 V **with no load and measure it**
> before connecting anything. See [troubleshooting.md](troubleshooting.md) — this is
> how we killed our first board.

> ⚠️ **Once `VIN` is connected, never plug in USB.** On the DEVKIT V1 the USB 5 V rail
> and `VIN` are tied together, so two supplies would fight each other.

Pin choice is not arbitrary, and none of the traps are visible from the outside:
`GPIO16/17` are wired to internal PSRAM on WROVER modules and dead to the outside;
`GPIO12` is a strapping pin and stops the board booting if it is HIGH at power-up
(an idle transmit line sits exactly at HIGH); `GPIO34/35`, `VP` and `VN` can only be
inputs. `GPIO13` avoids all of that and sits on the same pin row as `VIN` and `GND`,
which matters on a breadboard. Equally fine: `D25`, `D26`, `D27`, `D32`, `D33`.

### Sound — ATOM Echo (optional)

Horn, starter and reversing beeper — see [sound.md](sound.md) for the whole story.
The 4-hole header on the bottom of the cube, counted from the end **away from** the
Grove socket:

| ATOM | To |
|------|----|
| **G21** | nothing |
| **G25** | Arduino **D1** through **1 kΩ**, and from `G25` **2 kΩ** to GND |
| **5V** | 5 V from the step-down converter (the same one as the ESP32) |
| **GND** | Arduino **GND** — its own wire, straight to a GND pin |

The divider brings the Arduino's 5 V down to the 3.3 V the ATOM tolerates. The
5-hole header on the other side belongs to the speaker and microphone.

> ⚠️ **5 V from the converter and USB never at the same time.** To update the ATOM,
> pull the 5 V wire first.

## Order of operations (safety)

1. Wire everything up with the **battery disconnected**. Arduino on USB from the PC.
2. **Prop the car up** so the wheels spin in the air.
3. Upload the sketch.
4. *Then* connect the battery.
5. Measure: multimeter across one motor's terminals while it runs forward. It should
   read roughly 5–6 V, not 9. If it reads more, lower `SPEED_MAX` in the sketch.

If your battery holder has no switch, pulling the **plus** wire off the rail is your
off switch. Get into the habit of doing that before you touch any wiring.

## Why the speed cap exists

The pack delivers ~9.6 V. The motors are rated 3–6 V. PWM does not lower the voltage,
it chops it — but the *average* is what the motor sees, so a PWM value of 150 out of
255 on a 9.6 V pack averages out to about 5.6 V. That is why the sketch never writes
255, and why raising `SPEED_MAX` past ~160 will slowly cook the motors.

The one exception is the deliberate 120 ms kick at startup (`KICK_SPEED = 200`).
Static friction in the gearbox means a motor that is standing still needs more push
than one already turning. Short bursts above the rating are fine; a permanent 200
is not.
