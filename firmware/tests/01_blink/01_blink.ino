/*
  Test 1 - "Is the board alive?"
  ------------------------------------------------------------
  The classic first sketch: blink the LED that is already soldered
  onto the board (marked "L", on pin 13).

  If this works, your toolchain works: the right board is selected,
  the right port, and the upload goes through. Do this BEFORE you
  wire up any motors.

    arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi firmware/tests/01_blink
    arduino-cli upload -p COM4 --fqbn arduino:renesas_uno:unor4wifi firmware/tests/01_blink
*/

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);   // the built-in LED is an output
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);   // on
  delay(500);                        // wait half a second
  digitalWrite(LED_BUILTIN, LOW);    // off
  delay(500);
}
