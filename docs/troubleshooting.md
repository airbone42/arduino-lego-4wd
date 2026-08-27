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
   the weaker one drops out. The fix is the second driver in
   [next-steps.md](next-steps.md).

## The motor only hums

Too little PWM. Below roughly 70/255 a TT motor cannot break away. That is what
`SPEED_MIN` is for: `percentToSpeed()` maps the whole stick travel onto the usable
`SPEED_MIN..SPEED_MAX` band, so even 1 % throttle comes out as a value the motor can
actually act on.

Do **not** solve this by clamping small values up to `SPEED_MIN` instead — we tried
that first, and it made 1 % and 46 % throttle produce exactly the same speed. The
lower half of the stick did nothing, and the squaring in the gamepad page that exists
to give you fine control around centre was thrown away.

## It will not turn on the spot

Spinning is the hardest thing this drivetrain does: all four wheels are pushed
**sideways** across the floor instead of rolling, and sideways friction is far higher
than rolling friction. Work through it in this order:

1. **Is the driver hot?** Let it spin for a few seconds, then touch the TB6612. Hand
   warm is fine. Properly hot means the channel is current limiting, and no software
   change will help — fit the second driver from [next-steps.md](next-steps.md).
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
