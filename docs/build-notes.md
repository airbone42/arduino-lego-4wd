# Build notes — what we learned the hard way

This project was built with two kids (4 and 8) over a few weekends, in small steps,
testing after each one. These are the things that cost us an evening each. If you
build this, they will probably cost you one too — unless you read this first.

## Build it in this order

Do not wire everything up and hope. Each step is testable on its own:

1. **Blink** ([firmware/tests/01_blink](../firmware/tests/01_blink)) — proves the
   toolchain works: right board, right port, upload goes through.
2. **One motor** ([02_single_motor](../firmware/tests/02_single_motor)) — one motor,
   one channel, wheel in the air. Proves the driver is wired right.
3. **All four** ([03_drive_test](../firmware/tests/03_drive_test)) — still propped up.
   Now you find out which side spins backwards.
4. **Put it on the floor** — same sketch, on the ground, and watch it drive itself
   into a wall. This is the moment the kids get hooked.
5. **The real firmware** ([firmware/lego4wd](../firmware/lego4wd)) — WiFi, web page,
   over-the-air updates.

## The commands were piling up

**Symptom:** the car kept rolling for 2–3 seconds after we let go of the button,
drove in stutters, and one wheel eventually stopped turning altogether.

**What we measured:** the Arduino manages about **2.7 requests per second**. The page
was sending **12.5 per second**. The bottleneck is *not* answering a request (~60 ms)
— it is **accepting a new TCP connection**, which takes 60–270 ms and, once a queue
builds, up to 1.3 s. And every `fetch()` opens a new connection.

**The fix** is in the control pages, not the firmware: keep only **one command in
flight at a time**. The page waits for the previous request to finish before sending
the next, so it automatically throttles itself to whatever the Arduino can actually
take. Stop commands jump the queue. The dead man's timeout is 800 ms.

**The dead wheel was a symptom, not a separate bug.** The stuttering meant that motor
was being restarted constantly and never got past static friction. Once the commands
stopped queueing, it ran fine again.

If you ever need genuinely lower latency: implement **HTTP keep-alive** so the
connection stays open and the expensive part disappears. We did not need it.

## Turning on the spot needed rhythm, not more power

**Symptom:** with `TURN_INNER_SPEED` at `-SPEED_MAX` the car broke away, moved for a
moment and then simply stalled — the outer side kept pulling while the inner wheels
stood still. Propped up in the air it spun happily, so the motors themselves were
fine.

**Why it is hard:** spinning around the centre pushes all four wheels *sideways*
across the floor rather than rolling them, and sideways friction is far higher than
rolling friction. The 120 ms kick-start that gets the car moving in a straight line
is nowhere near enough here.

Three things fixed it, in this order:

1. **A longer kick for turns.** `drive()` notices that the two sides are running
   against each other and stretches the kick to `KICK_TURN_MS` (300 ms). That got it
   moving reliably — and then it stalled again once the kick ended.
2. **Keep pulsing.** Running at `KICK_SPEED` permanently is not allowed: ~7.5 V on
   motors built for 3–6 V. So the firmware pushes rhythmically instead, 150 ms on and
   250 ms off, for as long as the car is spinning. Static friction is higher than
   kinetic friction, so a jolt breaks it more easily than steady pressure — and the
   motors and driver cool down in between. The timing lives in `loop()`, not in
   `drive()`: commands only arrive every ~350 ms and the rhythm has to be steady.
3. **Stop spinning around the centre.** `TURN_INNER_SPEED = -70` makes the inner side
   push back more weakly than the outer side pulls. The pivot point moves out of the
   middle of the car, so the wheels partly roll instead of only scrubbing. This is
   also why turning has always felt easier with the gamepad: the stick almost never
   sits at pure rotation, there is nearly always some throttle mixed in.

**Check the driver temperature before reaching for the code.** If the TB6612 gets
properly hot, the channel is current limiting and none of the above will help —
that is the case for the second driver in [next-steps.md](next-steps.md). Ours stayed
hand warm, which is what made the pulsing worth trying.

## The tyres came off the rims

Skid steering scrubs the tyres sideways across the floor. That pulls the rubber
straight off the plastic rims. Fix: a thin, even film of **superglue** between rim
and tyre. **Not hot glue** — it builds up too thick and the wheel ends up out of
balance.

## Check the axles turn freely after mounting

Once the motors are bolted into their brackets, spin every wheel by hand. If one
binds or rubs, you will see the "one motor stops" symptom again — but this time the
cause is mechanical, not electrical, and no amount of code will help.

## Grip on smooth floors

On parquet the wheels just spin. Things that help:

- **rubber bands** stretched over the tyres;
- **weight over the driven wheels** — put the battery pack directly above them.

Things that do **not** help: raising `SPEED_MAX`. If the tyres are already slipping,
traction is the limit, not power. More PWM just heats the motors.

## Known limits of this design

- **Motor current.** Four motors on **one** TB6612, two in parallel per channel. A
  channel supplies about **1.2 A continuous**, and parallel motors never share it
  fairly. Under real load — carpet, a slope, a heavy body — one of them can drop out.
  The fix is a second driver, see [next-steps.md](next-steps.md).
- **Reaction time.** About 150–400 ms per command, dominated by connection setup.
  Fine for a toy, not fine for anything that needs to react.
- **Kick-start.** 120 ms at `KICK_SPEED = 200` instead of 150 when starting off or
  reversing. If the driver gets warm or the ride feels jerky, reduce the value or the
  duration.

## Things that mattered more than we expected

- **The LED matrix.** Once the car runs on battery there is no serial monitor and no
  laptop. A check mark / cross / dots on the built-in matrix is the only way to see
  what the WiFi is doing. Worth the twenty lines of code.
- **Over-the-air updates.** Unplugging the car, carrying it to the desk, plugging in
  USB, uploading, carrying it back — every single time — kills the momentum. With OTA
  a change is on the car in about 20 seconds while it sits on the floor.
- **Strain relief on the motor wires.** The wires break at the solder tab, not
  anywhere else. A blob of hot glue prevents an evening of "why did the left side
  stop working".
