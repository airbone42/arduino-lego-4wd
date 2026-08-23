# 3D printed parts

Only one part is really needed: a bracket that holds a yellow TT gear motor and
clips onto LEGO studs. Four of them, one per wheel.

## Motor mount, LEGO compatible

This is the one we use, and it works perfectly:

**`3d-motor-box.stl`** from Adafruit's *LEGO Compatible CRICKIT Rover* guide —
<https://learn.adafruit.com/lego-compatible-crickit-rover/3d-printing>

It is a box that a yellow TT motor slots into, with LEGO studs on the outside so it
clicks straight onto a plate. Print **four**. The rest of that guide builds a
different robot on different electronics — ignore it, you only want the STL.

> 🙏 Ours were printed by my cousin **Marcel**. Thank you — the brackets fit
> perfectly first try, and they are the one part of this build we could not have
> improvised half as well.

If you would rather buy than print, Adafruit also sells an injection-moulded TT motor
mount with LEGO-compatible studs as [product 3815](https://www.adafruit.com/product/3815).
Searching Printables or Thingiverse for *"TT motor LEGO mount"* turns up several
remixes too.

### Printing notes

| Setting | Value | Why |
|---------|-------|-----|
| Material | PLA or PETG | PLA is fine — nothing here gets warm |
| Layer height | 0.2 mm | |
| Infill | 30–40 % | The studs and the motor clamp take real force when a wheel snags |
| Supports | none | The part is designed to print flat |
| Orientation | flat on the bed, stud side up | Strongest in the direction that matters |

Print one first and test-fit it on a LEGO plate **and** on a motor before you print
the other three. Stud tolerances vary between printers by a few tenths of a
millimetre, which is exactly the range that decides between "clicks in nicely" and
"cracks the bracket".

## Optional prints

- **Battery holder tray** — a LEGO-compatible cradle so the AA pack does not slide
  around. We just wedged ours between bricks, which works but is not elegant.
- **Breadboard sled** — same idea for the breadboard.
- **Bumper** — four-year-olds drive into walls. Repeatedly.

## If you have no printer

The brackets are the only printed part, and they are the only thing you cannot
easily improvise. Alternatives that work:

- **Cable ties.** Two slots in a LEGO plate (or a piece of stiff cardboard), motor
  on top, cable tie around it, pull tight. Surprisingly solid.
- **Hot glue** directly onto a LEGO plate. Holds a light car, comes off with
  patience and isopropyl alcohol.
- Buy the Adafruit part — it is a couple of euros each.

Whatever you use: all four motors must be **aligned the same way**, axles on one
line front and back. If they are not, the car crabs sideways instead of driving
straight, and no amount of code will fix it.
