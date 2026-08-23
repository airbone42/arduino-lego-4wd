# Soldering, for beginners

There are exactly two things to solder in this project: the pin headers onto the
TB6612 breakout, and wires onto the motors. Both are easy, and both are a perfect
first soldering project.

## Safety first (there are kids around)

- The iron runs at **~350 °C**. **Only an adult holds the iron.** Full stop.
- Small children watch from a safe distance. Older ones can assist — handing parts
  over, counting pins, watching — but never the hot end.
- The iron goes **back in its stand** every single time, never on the table.
- **Open a window.** Do not breathe the smoke.
- **Wash hands afterwards**, especially with leaded solder.

## What you need

- Soldering iron with a stand, and a damp sponge or brass wool to wipe the tip
- **Thin solder, 0.8–1 mm, with flux core.** 60/40 leaded is the most forgiving for
  beginners — it flows beautifully. Then ventilate and wash your hands.
- **A breadboard as a jig** — the single most useful trick, see below

## The trick: let the breadboard hold everything straight

1. Snap the pin header to length (count the holes on the breakout board).
2. Push the header into the breadboard, **long pins down**.
3. Drop the TB6612 board **onto the short pins sticking up**. Now everything sits
   square and cannot wobble, and you have both hands free.

## How to make a good joint

1. **Tin the tip:** let it heat up, melt a little solder onto it, wipe the excess on
   the sponge. The tip should look shiny silver.
2. **Heat both parts:** touch the tip to the **pin AND the pad at the same time**,
   about 1–2 seconds.
3. **Feed the solder into the joint from the side** — not onto the tip. As soon as
   it flows and covers pad and pin: **solder away first, then the iron.**
4. **Check:** good = a small shiny cone, a little "volcano" wrapping pad and pin.
   Bad = a dull ball or a blob sitting on top (cold joint) → just reheat it.
5. **Use little solder.** No **bridges** between neighbouring pins. If one happens,
   drag it away with the hot tip or use desoldering braid.
6. One pin at a time. Do not rush.

## Wires onto the motors

The TT motors have two metal tabs on top, usually with a small hole in them.

1. **Thread the wire through the hole** and bend it over — now it holds mechanically
   even before there is any solder.
2. **Tin the tab and the wire end** separately, then join them.
3. **Be quick.** The tab conducts heat straight into the motor; linger and you melt
   plastic inside.
4. **Strain relief:** a blob of **hot glue** over the joint and a bit of the wire.
   Otherwise the wire snaps right at the tab after a few days of rattling around.
   This matters more than it sounds on a car that keeps hitting furniture.

**Which wire?** Flexible **stranded** wire, not the stiff single-core breadboard
jumpers — those crack from vibration. A neat trick: sacrifice a female-to-male
Dupont lead, cut one end off and solder that to the motor. The connector on the
other end then plugs straight into the breadboard.

## Optional: noise suppression capacitor

Brushed motors spark electrically and can disturb or even reset the Arduino. A small
**0.1 µF ceramic capacitor (marked `104`)** soldered **directly across the motor's
two terminals** damps that. Not needed for a first test, worth doing once all four
motors run together.

## Checking your work with a multimeter

Set the multimeter to **continuity** (the sound/diode symbol):

- probe on a pin **and** on its pad → **beep** = good joint ✅
- probe on **two neighbouring pins** → **beep** = accidental **bridge**, fix it ❌

Kids love this part. It turns an invisible thing into a sound.
