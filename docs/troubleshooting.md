# Troubleshooting

## The car does not appear on the network

| Check | |
|-------|--|
| LED matrix shows a **cross** | It cannot join your WiFi. Wrong SSID/password in `arduino_secrets.h`, or the network is 5 GHz-only — the UNO R4 is **2.4 GHz only**. |
| LED matrix shows **dots** forever | It is still trying. Out of range, or the router is blocking new clients. |
| LED matrix shows a **check mark** but the page does not load | Your phone is on a different network (guest WiFi, mobile data). Also check whether the router has *client isolation* / *AP isolation* switched on — that blocks phone-to-Arduino traffic. |
| Nothing on the matrix at all | No power, or the sketch never started. Plug in USB and watch the serial monitor at 115200 baud. |

The sketch prints its IP address over serial every 5 seconds. Start there.

## Nothing moves

1. Is `STBY` actually tied to 5 V? Without it the driver stays asleep and gives no
   sign of life.
2. Is there a **common ground**? Arduino GND, driver GND and battery − all on the
   same rail.
3. Is `VM` connected to the battery, not to the Arduino's 5 V pin? The 5 V pin cannot
   supply motor current.
4. Battery actually charged? Measure it. A tired NiMH pack sags under load.

## One side turns the wrong way

Flip `INVERT_LEFT` or `INVERT_RIGHT` in the sketch. That is exactly what those flags
are for — no need to unplug anything.

## One wheel does not turn / stops under load

In this order:

1. **Spin it by hand.** Binding bracket, rubbing tyre, hair wrapped around the axle.
   Mechanical causes are the most common and the easiest to miss.
2. **Is it stuttering rather than dead?** Then commands are piling up — see
   [build-notes.md](build-notes.md). A motor that gets restarted constantly never
   overcomes static friction.
3. **Current.** Two motors in parallel on one channel never share fairly. Under load
   the weaker one drops out. The fix is a second TB6612, one channel per motor —
   see the pin budget note in [build-notes.md](build-notes.md).

## The motor only hums

Too little PWM. Below roughly 70/255 a TT motor cannot break away. That is what
`SPEED_MIN` is for: `driveCommandPercent()` maps the stick travel onto the usable
`SPEED_MIN..SPEED_MAX` band, so even 1 % throttle comes out as a value the motor can
actually act on.

Do **not** solve this by clamping small values up to `SPEED_MIN` instead — we tried
that first, and it made 1 % and 46 % throttle produce exactly the same speed. The
lower half of the stick did nothing, and the squaring in the gamepad page that exists
to give you fine control around centre was thrown away.

And do **not** apply the minimum to each side separately, which was our second
mistake. That leaves nothing between `-SPEED_MIN` and `+SPEED_MIN`: the inner wheel
can only stand still, run forward at 70 or run backward at 70. Steer a little while
driving and it jumps straight from "slightly slower" to "full counter drive", so the
car spins on the spot instead of curving. `driveCommandPercent()` therefore gives the
minimum only to the **faster** side and scales the other one to keep the ratio from
the mix — the inner wheel then passes smoothly through zero.

## It will not turn on the spot

Spinning is the hardest thing this drivetrain does: all four wheels are pushed
**sideways** across the floor instead of rolling, and sideways friction is far higher
than rolling friction. Work through it in this order:

1. **Is the driver hot?** Let it spin for a few seconds, then touch the TB6612. Hand
   warm is fine. Properly hot means the channel is current limiting, and no software
   change will help — fit a second TB6612 so each motor gets its own channel.
2. **Do the wheels actually turn?** Draw a marker line across rim *and* tyre, and
   across axle *and* hub. If the line ends up offset, the tyre is slipping on the rim
   or the wheel on the shaft, and the motor never had a chance. A cold driver together
   with stalled wheels points straight at this — a genuinely blocked motor draws its
   full stall current and would heat the chip up.
3. **Soften the turn.** `TURN_INNER_SPEED` at `-SPEED_MAX` spins around the centre of
   the car, which is the worst case for scrub. At `-70` the inner side pushes back
   more weakly than the outer side pulls, the pivot moves outwards, and the wheels
   partly roll. Much easier on the motors.
4. **Shorten the wheelbase.** The further the wheels sit from the centre, the longer
   the lever they fight. This is why tracked machines are short and stubby, and on a
   LEGO chassis it costs nothing to try.

Raising `SPEED_MAX` is not the answer. The motors are specified for 3–6 V and 150
already puts ~5.6 V across them.

## The car keeps rolling after I let go

The dead man's timeout is 800 ms, so up to ~0.8 s of coasting is by design. Much
longer than that means commands are queueing — see [build-notes.md](build-notes.md).

A related symptom worth knowing: if it *speeds up* briefly after you release the
button, that is the queue plus the kick-start. The dead man's switch fires while
stale commands are still waiting, so the next one arrives at a motor the firmware
believes is stopped — and gets a full kick. Both control pages keep only one command
in flight to stop this happening.

## The gamepad page does not see my controller

1. **Press a button on the controller once.** Browsers hide gamepads until the user
   has interacted with them. This is a privacy rule, not a bug.
2. The page must run in a **secure context**. `http://<some-ip>/` is not one, which is
   why this page is opened locally (`file://` or `http://localhost`) instead of being
   served from the Arduino. The page shows you whether the context is secure.
3. Check the address field on the page actually points at your car.

## Over-the-air upload fails

| Symptom | Cause |
|---------|-------|
| `401` | Wrong OTA password — must match `SECRET_OTA_PASS`. |
| Hangs forever | Missing `-H "Expect:"`. The tiny server on the Arduino does not speak `100-continue`. |
| Connection refused | The car is not on the network, or the running sketch has no OTA receiver in it. |
| Upload succeeds, car never comes back | You just flashed something that cannot join the WiFi. Get the USB cable. |

The very first upload always has to go over USB — the OTA receiver has to be running
before it can receive anything.

## The Arduino resets when the motors start

Voltage dips from the motor inrush current. Options: a fresher battery pack, a
100–470 µF electrolytic capacitor across `VM` and `GND` near the driver, and the
0.1 µF ceramics across the motor terminals mentioned in
[soldering.md](soldering.md).

A subtler version of the same picture: **no icon on the matrix, the matrix flickering,
and the voltage at `VIN` wandering between 5 and 10 V.** That looks like a power
problem and usually is — but check the **ground wire** first. With no return path the
current sneaks back through the signal lines to the motor driver: enough to make the
matrix flicker, not enough to boot. The fastest way to tell the two apart is to hang
the **Arduino alone on USB from a laptop**. If it boots there, the board is fine and
it is the supply. Thirty seconds, no guessing.

---

# The ESP32 gamepad bridge

Everything below cost us real time. None of it is visible from the outside.

## No LED, no gamepad — or: what the blue LED means

| Blue on-board LED | Meaning |
|---|---|
| steady | controller connected, ready to drive |
| slow pulse (400 ms on / 600 ms off) | no controller, still searching |
| three quick flashes | just restarted |
| nothing at all | the board is not running (no power, or stuck) |

Before we made "searching" pulse, it looked exactly like "dead" — and there is no
cable on a driving car to check with. That distinction alone saved an hour.

## Is anything reaching the Arduino at all?

Two endpoints on the car answer this without any guessing:

- **`http://<car>/status`** — what arrived on `D0`. `chars` counts every single byte.
  If it stays at **0**, nothing is arriving *physically* (cable, pin or ground). If it
  counts up, the link is fine and the fault is elsewhere. `last` shows the last line
  received, which also tells you **which build** of the ESP32 firmware is running:
  seven fields (`L,R,red,blue,green,horn,gamepad`) is current, anything shorter is an
  old build. `bad` counts lines that arrived mangled and were thrown away — see below.
- **`http://<car>/selftest`** — sends one line out on `D1`. Put a jumper from `D1` to
  `D0` (unplug the ESP32 wire first!) and the same line has to come back in, with the
  counters jumping. That tests `D0`, `Serial1` and the software *without* the ESP32,
  so afterwards you know for certain which side the fault is on.

> ⚠️ **Resistance measurements on microcontroller pins are worthless.** Ours read
> 700 Ω, then 1.3 MΩ, then 500 kΩ on the same pin — protection diodes, residual charge
> and active structures inside the chip distort every ohm reading. We nearly rebuilt
> the whole link to fix a fault that did not exist. The same goes for voltages on a
> data line: the meter shows an average that moves with the traffic, so 2.1 V and
> 1.5 V are not two different components. Only a **functional test** is conclusive.

## LEDs flash and the horn croaks while steering

Standing still everything is fine. Steer hard — especially turning on the spot, where
the car pulses the motors — and LEDs flash that nobody switched on, and the horn
comes out as a croak.

**Cause:** the line from the ESP32 to the Arduino (`D0`) loses characters whenever
the motors pull a lot of current. We counted: **0** mangled lines in three minutes
standing still, **33** in about 15 seconds of hard steering. What arrived looked like
this:

```
-100100,0,0,0,0,1        a comma swallowed
-100,100,00,0,0,1        a comma swallowed, "00"
0,0,0,1,1                the START of the line lost - looks valid, means "blue on, horn on"
```

One mangled line switches a light or the horn on for 20 ms, until the next good line
switches it off again. Many of those in a row sound like croaking.

**Why:** the motor current flows back to the battery through the ground wires. If the
ESP32's ground shares the breadboard rail with that current, "0 V" at the ESP32 is
briefly not the same as "0 V" at the Arduino — and a 3.3 V signal going into a 5 V
board has little margin to lose.

**The real fix — a ground wire of its own:** run the ESP32's `GND` (and the ATOM's)
**straight to a GND pin of the Arduino** instead of via the rail. The UNO has three
GND pins; there is room. This is what cured it for us, immediately. If you still see
errors: twist each data wire together with its ground wire, and put an electrolytic
capacitor across `VM`/`GND` at the motor driver (see "The Arduino resets when the
motors start" above).

**The safety net in software:** the Arduino checks every line field by field — speeds
between −100 and 100, lights, horn and controller exactly `0` or `1`, and **all seven
fields present** — and throws away anything else. Switches only count once they
arrive the same **twice in a row**. `http://<car>/status` shows how many lines were
thrown away (`bad`) and the last one (`last_bad`); watch it while you steer.

## The upload says it worked, but the old firmware keeps running

This is the nastiest one, because everything *looks* fine: checksum confirmed, script
reports success — and the board runs the old code. We spent half a day hunting a
feature that was not in the running build at all.

The cause is `otadata`, the pointer the bootloader uses to pick which half of the
flash to boot. If it gets stuck on the old half, the upload writes dutifully into the
other one and the bootloader ignores it. **A flash over the USB cable erases `otadata`
and repairs it** (`-Usb` / `USB=...`).

This is exactly why the sketch prints a `VERSION` string at startup, and why `/status`
is worth reading: the version line is the only honest proof that an update arrived.
"Done" from the upload script is not. **If an update changes nothing, check the
version first.**

## Bluetooth is dead after every over-the-air update

After an OTA update the ESP32 only does a **software** restart, not a real power
cycle. The Bluetooth block stays in whatever state it was in and often does not come
back up. `BP32.update()` then blocks at the top of `loop()`: LED off, no more OTA —
but **the ping still answers**, because the WiFi runs as its own task. It looks like a
dying board.

The fix is in the sketch already: **`btStop()` inside `ArduinoOTA.onStart`**. Since
then an update goes through without a hard reset, and as a bonus the WiFi has the
antenna to itself during the upload.

## The upload script reports an error although it worked

`espota` restarts the ESP32 the instant the write finishes, then waits for an
acknowledgement that never comes — and its own error path is broken
(`NameError: global name 'e' is not defined`, a Python 2 leftover). So it reports a
failure although everything went fine. The scripts here therefore check the device
itself (ping: first gone, then back) instead of believing `espota`.

## The board reboots in a loop as soon as Bluetooth and WiFi both run

You called `WiFi.setSleep(false)`. Don't. Both radios share one antenna, and the WiFi
**must** leave gaps — those pauses are the only time Bluetooth gets the antenna. The
driver bails out with a very clear message and the board restarts endlessly:

```
Should enable WiFi modem sleep when both WiFi and Bluetooth are enabled!!!!!!
```

The default is "sleep on", so simply leave it alone. For the same reason, do not call
`enableNewBluetoothConnections(true)` permanently — a constant pairing scan on the
side was enough to make our board disappear after a few minutes.

## A signal pin simply does nothing

Three traps, none of them visible:

- **GPIO16/17** are wired to internal PSRAM on ESP32-**WROVER** modules and are dead
  to the outside world. On WROOM modules they are free. Same "DEVKIT V1" shape, same
  silkscreen — our replacement board sent nothing on GPIO17 although the old one had
  been fine for weeks. If a pin "does nothing", suspect this first.
- **GPIO12** is a strapping pin: at power-up it sets the flash voltage, and if it is
  HIGH the board does not boot at all. An idle transmit line sits exactly at HIGH.
  **GPIO14** wobbles during boot, so the Arduino would get garbage on `D0`.
- **GPIO34/35, VP and VN can only be inputs.** As a transmit pin they do nothing, and
  you cannot tell by looking.

`GPIO13` avoids all three. Equally fine: `D25`, `D26`, `D27`, `D32`, `D33`.

## The board died after I connected VIN

Our first one did. The `VIN` wire landed on the **plus rail of the breadboard**, which
carries the raw 9.6 V from the battery. The AMS1117 regulator tolerates that on paper,
but it then has to burn 6.3 V instead of 1.7 V as heat, and it shuts down thermally.
It ran a little longer and was then gone for good (USB chip dead).

> ⚠️ **The plus rail of the breadboard is not the 5 V pin of the Arduino.** Measure
> against `GND` before connecting: ~5 V is right, 9.6 V is wrong.

And again: once `VIN` is connected, **never plug in USB as well** — on the DEVKIT V1
those two are tied together.

## Do not leave a serial monitor running while testing

Opening the port pulls DTR/RTS and **resets the ESP32**. In the middle of an OTA
upload that looks exactly like a broken board, and a monitor that reconnects
automatically will hold it in a reset loop.

## No COM port at all under Windows

The DEVKIT V1 has a **CP2102** USB chip, and Windows 11 does not ship a driver for it —
the board gets no COM port. In Device Manager it shows as "CP2102 USB to UART Bridge
Controller" with **code 28**. Install the
[Silicon Labs CP210x driver](https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers),
and **extract the ZIP completely**: dragging single files out of the archive window
leaves the per-architecture subfolders behind, the install then fails with a vague
error and the driver never lands in the driver store.
