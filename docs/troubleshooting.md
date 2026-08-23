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
`SPEED_MIN` is for — the sketch lifts small throttle values up to it.

## The car keeps rolling after I let go

The dead man's timeout is 800 ms, so up to ~0.8 s of coasting is by design. Much
longer than that means commands are queueing — see [build-notes.md](build-notes.md).

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
