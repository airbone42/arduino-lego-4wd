# Next steps

The car drives, lights up, takes a game controller — and since the sound stage it
also honks, cranks its starter when the controller connects and beeps when it backs
up. That part is built and written up in **[sound.md](sound.md)**.

What follows is planned, not built. Treat the code in here as a design sketch.
On top of that my daughter has asked for a **times-tables game**, which turns out to
fit the hardware remarkably well.

## Pins still free

In use: `D2`, `D4`, `D7`, `D8`, `D9`, `D10` (motors), `D3`, `D5`, `D6` (lights), `D0`
(from the ESP32 gamepad bridge), `D1` (to the ATOM Echo). `D13` is the on-board LED,
and the 12×8 LED matrix is internal (no pins).

Free: `D11`, `D12`, `A0`–`A5` (analog pins work as plain digital pins). `D11` is the
only PWM pin still available.

---

## 1. More sound

The ATOM Echo already gets everything it needs — the motor speeds arrive with every
line — so most of this is firmware on the cube only.

- **An engine note that follows the throttle.** The idle at the end of the starter
  sound is already there; let its pitch rise with `(|L| + |R|) / 2`. A single loop
  whose pitch bends is far more convincing than a handful of separate samples.
  Expect it to be modest on the built-in speaker (see below).
- **One-shot sounds** — a fanfare, a "wrong" buzzer, "battery low". Because the line
  repeats every 200 ms, a sound number alone would restart the sound every 200 ms.
  Send a **number plus a counter** that goes up by one per trigger; a new counter
  value means "play it". Append both at the end of the line, as always.
- **Louder.** The built-in speaker is the honest limit — a horn at full level is
  still barely louder than the motors. A MAX98357A I²S amplifier (~€4) with a 3 W
  speaker on the free Grove socket would be many times louder; only three pin
  numbers in the firmware change.
- **Updates over WiFi for the cube.** Right now every update means pulling the 5 V
  wire and plugging in USB. The gamepad bridge shows how OTA works on an ESP32 —
  including its traps (see [troubleshooting.md](troubleshooting.md)).

> ⚠️ **Three radios now share one room.** The car does WiFi, the bridge does WiFi *and*
> Bluetooth on one antenna, and the ATOM adds a third as soon as it gets OTA. If the
> controller starts lagging, that is where to look first — the bridge sketch already
> has a `WIFI_ONLY_AT_START` switch for exactly this.

---

## 2. Speech input — and what is actually realistic

This is the part where it pays to be honest about what a small chip can do, because
the marketing does not make the distinction.

**Wake word detection on the ATOM Echo: yes.** Espressif's WakeNet runs on a plain ESP32
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
switch — should stay as boring and as small as it is. The Echo already hears the car
over its wire; to put the question on the matrix it has to talk back, and HTTP to the
car's endpoints (once the cube has WiFi, see step 1) is the easy route — the same one
the speech path in step 2 would use.

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
