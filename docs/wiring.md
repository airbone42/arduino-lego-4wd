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
