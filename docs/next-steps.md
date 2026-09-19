# Next steps

The car drives, lights up and takes a game controller. What it cannot do yet is make
a sound — which, if you ask a four-year-old, is the only thing still missing.

So the next stage is an **M5Stack ATOM Echo**: a 24 mm cube with a speaker, a
microphone and an ESP32 in it. Engine noise, a horn, and eventually a car you can
talk to. On top of that my daughter has asked for a **times-tables game**, which
turns out to fit the same hardware remarkably well.

None of the code below is built yet — treat it as a design sketch, not tested code.

## Pins still free

Currently used: `D2`, `D4`, `D7`, `D8`, `D9`, `D10` (motors), `D3`, `D5`, `D6`
(lights), `D0` (the line from the ESP32 gamepad bridge). `D13` is the on-board LED,
and the 12×8 LED matrix is internal (no pins).

Free: `D1`, `D11`, `D12`, `A0`–`A5` (analog pins work as plain digital pins). `D11`
is the only PWM pin still available.

`D1` is the transmit half of `Serial1`. It has been deliberately unused so far — see
the warning about 5 V in step 1.

---

## 1. Sound — an M5Stack ATOM Echo

**Why this part.** It is the smallest thing that has both a speaker and a microphone
already wired to an ESP32, so there is nothing to solder and nothing to level-shift
on the audio side. 24 × 24 × 17 mm and 9 g — it disappears into the LEGO.

| | |
|---|---|
| SoC | ESP32-PICO-D4, dual core, 240 MHz, WiFi + Bluetooth |
| Flash | 4 MB, **no PSRAM** |
| Microphone | SPM1423, PDM |
| Speaker | 0.8 W through an NS4168 I²S amplifier |
| Also on board | SK6812 RGB LED (`G27`), one button (`G39`) |
| Free pins | `G26` and `G32` on the Grove connector, plus 5 V and GND |
| Reserved | `G19`, `G22`, `G23`, `G33` — the internal audio I²S bus |

> ⚠️ **0.8 W is a small speaker.** It is plenty for a horn indoors and fine for engine
> noise at walking pace, but it will not shout over a carpeted room full of children.
> If that turns out to matter, the Grove port can drive a bigger amplifier later.

### Wiring

The car has to tell the Echo how fast it is going, 50 times a second, so the engine
note can follow the throttle. That is one wire:

| Arduino | | ATOM Echo |
|---|---|---|
| `D1` (TX of `Serial1`) | through a divider, see below | `G32` (Grove) |
| `GND` | | `GND` (Grove) |
| — | | 5 V from the same step-down converter as the ESP32 bridge |

> ⚠️ **`D1` puts out 5 V and ESP32 pins tolerate 3.3 V.** This is exactly why `D1`
> has been left unconnected up to now. Two resistors fix it — a divider of
> **1 kΩ and 2 kΩ** brings 5 V down to 3.3 V:
>
> ```
> D1 ---[ 1k ]---+--- G32
>                |
>              [ 2k ]
>                |
>               GND
> ```
>
> The other direction (`D0`, ESP32 → Arduino) needs nothing, because 3.3 V is already
> a valid HIGH for the Arduino. That asymmetry catches people out constantly.

Note that `D0` is **not** available: the gamepad bridge already transmits on it, and
two transmitters on one line do not work. The Echo therefore only listens.

### What goes down the wire

Reuse the format that already exists, just the other way round. The Arduino sends one
line whenever something changes, plus a slow heartbeat:

```
L,R,horn      e.g.  "80,-20,0"
```

The Echo turns that into sound on its own:

- **Engine note** from `(|L| + |R|) / 2` — a short looped sample played back at a rate
  that scales with the speed. A single loop whose pitch bends is far more convincing
  than a handful of separate samples, and it costs almost no flash.
- **Standing still** = idle loop, quieter.
- **Reversing** (both sides negative) = add a beeping reversing alarm. Free, and
  children find it hilarious.
- **Horn** on the bit, triggered from a controller button the same way the lights are.

Storage: 4 MB of flash with no PSRAM. At 16 kHz, 16-bit mono you get roughly 30 s of
audio per megabyte, so two or three megabytes of samples is a comfortable budget —
far more than a horn and an engine loop need.

### Code changes on the car

Small. In `lego4wd.ino`, next to `showDirection()`:

```cpp
// Tell the sound board what the motors are doing. Same idea as the matrix
// arrow: only send on a real change, plus a heartbeat so a lost line is
// repaired within a fraction of a second.
void sendSound(int left, int right) {
  static int lastL = 0, lastR = 0;
  static unsigned long lastBeat = 0;
  if (left == lastL && right == lastR && millis() - lastBeat < 200) return;
  lastL = left; lastR = right; lastBeat = millis();
  Serial1.print(left); Serial1.print(','); Serial1.print(right);
  Serial1.print(','); Serial1.println(hornOn ? 1 : 0);
}
```

> ⚠️ **The dead man's switch has to silence the engine too.** `halt()` must send a
> `0,0,0`. Otherwise a dropped connection leaves a car idling under the sofa forever.

> ⚠️ **Three radios now share one room.** The car does WiFi, the bridge does WiFi *and*
> Bluetooth on one antenna, and the Echo adds a third. If the controller starts
> lagging once the Echo is in, that is where to look first — the bridge sketch already
> has a `WIFI_ONLY_AT_START` switch for exactly this.

---

## 2. Speech input — and what is actually realistic

This is the part where it pays to be honest about what a small chip can do, because
the marketing does not make the distinction.

**Wake word detection on the Echo: yes.** Espressif's WakeNet runs on a plain ESP32
and spots one fixed phrase ("Hi, ESP") reliably. That alone is enough for a push-free
"listen now".

**Full command recognition on the Echo: probably not.** Espressif's MultiNet, which
recognises whole command phrases offline, is documented around the **ESP32-S3**, and
the Echo is a plain ESP32-PICO-D4 with 4 MB of flash and no PSRAM. Assume it will not
fit until you have proven otherwise on the actual part.

So recognition happens **off the car**, which is not a compromise at all here:

- The Echo streams the audio over WiFi to something bigger — a Raspberry Pi, a PC, or
  Home Assistant. **ESPHome has a ready-made voice assistant component for exactly
  this board**, which is by far the shortest path to something that works.
- That something recognises the words and then calls the endpoints the car *already*
  has: `GET /drive?l=..&r=..`, `/stop`, `/lights?red=1`. **No new firmware on the
  Arduino, and no extra wire** — the voice path is simply another client, exactly like
  the phone page and the gamepad page.

That last point is the nice part of the design we already have: anything that can make
an HTTP request can drive this car.

**Start small.** One word — "stop" — is a genuinely useful feature and a complete
project on its own. Add "forward", "left", "right", "horn" after that. A car that
understands five words well beats one that half-understands fifty.

---

## 3. The times-tables game (my daughter's idea)

She wants the car to test her on multiplication. The obvious version — the car asks
out loud, she answers out loud — needs the speech recognition from step 2 and is
therefore the *last* version to build, not the first.

There is a much better one that needs no recognition at all, and it uses everything
the car can already do:

1. The Echo asks out loud: **"Seven times eight?"**
2. The **12×8 matrix shows the question** at the same time. Two digits of a 5×7 font
   are 11 dots wide — they fit exactly, with one column to spare.
3. Number cards are spread on the floor. **She drives the car onto her answer** and
   presses **A** to confirm.
4. Right: a fanfare and the matrix grins. Wrong: a buzz, and it asks again.

This turns a worksheet into a driving game, and the driving *is* the answer rather
than a reward tacked on afterwards. Everything needed already exists except the
question logic and a handful of samples.

**Where should the logic live?** On the **Echo**, not the Arduino. It has the speaker,
it has the flash for samples, and the Arduino's job — motors, safety, the dead man's
switch — should stay as boring and as small as it is. The Echo already talks HTTP to
the car for step 2, so the same route works here.

**How does it know which card she drove onto?** Cheapest honest answer: it does not —
the child presses A when she is there and a grown-up is the referee. Step two would be
a colour sensor or a line of RFID tags under the cards, but that is a whole project of
its own and should not hold up the fun version.

A quieter variant for the car-less moments: the phone page grows a numeric keypad and
the same quiz runs there. Same logic, no floor space needed.

---

## 4. Battery monitoring

The car does not get suddenly slower, it gets *gradually* sadder, and nobody knows
why. A voltage divider into a spare analog pin fixes that.

**The Arduino cannot measure `VIN` by itself** — there is no internal tap, and 9.6 V
straight into an analog pin destroys it (5 V maximum). So: **20 kΩ** (two 10 kΩ in
series) from the battery rail to the measuring point, **10 kΩ** from there to GND, tap
into **`A1`**. Plus **100 nF from `A1` to GND**, or the reading jitters because the
motors dirty the rail.

That is a 1:3 divider, so a fresh 11.2 V pack gives 3.73 V at the pin, with headroom
to 15 V. It draws 0.3 mA.

```cpp
float volts = analogRead(A1) * 5.0 / 1023.0 * 3.0;
```

Then **calibrate it against a multimeter** and adjust the factor — resistors have 5 %
tolerance and the 5 V rail is not exactly 5.000 V either. The calibration is the good
part to do with a child: a measured value starts out as an estimate, and you make it
true by checking it.

> ⚠️ **A percentage reading would be a lie.** The NiMH discharge curve is almost flat
> and only drops at the very end. What is honest is a traffic light, measured **while
> standing still** (8 cells): above 10.0 V green · 9.0–10.0 V amber · below 8.8 V
> charge · below 8.0 V cut the motors, since the cells take damage below that. Under
> load it always sags, so only measure when the motors are stopped, or take the
> maximum of the last few seconds.

**The real reason to build this is not the charge level — it is the sag under load.**
Show the maximum and the minimum of the last few seconds and the difference between
them, and you can finally *see* whether a capacitor or a separate supply actually
helped, instead of guessing. That number is the symptom of every "one wheel stops
under load" problem, written down.

*First step is small:* just measure, print it on the web page and to the serial
monitor. The matrix icon and the cut-off come later, once the numbers are trusted.

*Without any code:* a two-wire **mini digital voltmeter** (~€2) glued to the rail and
always readable. Or an **INA219/INA226** (I²C, ~€4), which measures voltage **and**
current, works as a real fuel gauge (counting mAh), and would incidentally answer the
open question of how many amps the motors actually pull while turning.
