/*
  Test 3 - "The car drives" (4 motors, skid steering)
  ------------------------------------------------------------
  All four wheels driven together: forward, backward, spin left,
  spin right, stop - on repeat. No WiFi involved, so this is the
  test that tells you whether the mechanics and the wiring are
  fine before you add the network on top.

  How the motors are wired (only ONE TB6612 needed):
    - LEFT side  = both left motors in PARALLEL on channel A
    - RIGHT side = both right motors in PARALLEL on channel B
  "Skid steering" (like a tank): to turn, one side runs forward
  while the other runs backward.

  POWER: an 8xAA pack (NiMH, ~9.6 V) feeds the breadboard rail,
  which supplies both the Arduino VIN pin and the TB6612 VM pin.
  Arduino 5V -> VCC/STBY. Common ground everywhere.
  -> Battery goes to VIN, NEVER to the 5V pin!

  IMPORTANT - the speed cap (SPEED_MAX):
  The pack delivers ~9.6 V, but the little TT motors want 3-6 V.
  So we never write a full 255. At ~9.6 V, 150 is roughly 5.6 V at
  the motor (130 would be ~5 V). If the car is too slow, raise it in
  small steps and check the motor voltage with a multimeter - never
  above 6 V, so about 160 max.

  Prop the car up (wheels in the air) for the first run.
*/

// --- LEFT side (channel A) - same as test 2 ---
const int PWMA = 9;   // speed left      (~PWM pin)
const int AIN1 = 7;   // direction left 1
const int AIN2 = 8;   // direction left 2

// --- RIGHT side (channel B) ---
const int PWMB = 10;  // speed right     (~PWM pin)
const int BIN1 = 4;   // direction right 1
const int BIN2 = 2;   // direction right 2

// STBY is tied to 5V permanently (always enabled) -> no pin needed.

// Speed cap to protect the motors (see the header comment).
const int SPEED_MAX = 150;   // 0..255, capped at ~5.6 V at the motor

// --- Fix a reversed side in software (no re-plugging of wires) ---
// If one side spins the wrong way, flip the matching flag to true.
const bool INVERT_LEFT  = true;
const bool INVERT_RIGHT = true;

void setup() {
  Serial.begin(115200);      // so we can follow along in the serial monitor

  pinMode(PWMA, OUTPUT);
  pinMode(AIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);
  pinMode(PWMB, OUTPUT);
  pinMode(BIN1, OUTPUT);
  pinMode(BIN2, OUTPUT);

  stopMotors();              // make sure nothing moves at startup
  Serial.println("Drive test starting... (prop the wheels up!)");
}

void loop() {
  Serial.println("forward");
  forward();
  delay(2000);

  Serial.println("stop");
  stopMotors();
  delay(1000);

  Serial.println("backward");
  backward();
  delay(2000);

  Serial.println("stop");
  stopMotors();
  delay(1000);

  Serial.println("spin left");
  spinLeft();
  delay(1500);

  Serial.println("stop");
  stopMotors();
  delay(1000);

  Serial.println("spin right");
  spinRight();
  delay(1500);

  Serial.println("stop");
  stopMotors();
  delay(1000);
}

// --- Building blocks: drive each side on its own ---
// speed positive = forward, negative = backward, 0 = off.
// The value is always clamped to +/- SPEED_MAX (motor protection).

void leftSide(int speed) {
  speed = constrain(speed, -SPEED_MAX, SPEED_MAX);
  if (INVERT_LEFT) speed = -speed;
  if (speed >= 0) {          // forward
    digitalWrite(AIN1, HIGH);
    digitalWrite(AIN2, LOW);
  } else {                   // backward
    digitalWrite(AIN1, LOW);
    digitalWrite(AIN2, HIGH);
  }
  analogWrite(PWMA, abs(speed));
}

void rightSide(int speed) {
  speed = constrain(speed, -SPEED_MAX, SPEED_MAX);
  if (INVERT_RIGHT) speed = -speed;
  if (speed >= 0) {          // forward
    digitalWrite(BIN1, HIGH);
    digitalWrite(BIN2, LOW);
  } else {                   // backward
    digitalWrite(BIN1, LOW);
    digitalWrite(BIN2, HIGH);
  }
  analogWrite(PWMB, abs(speed));
}

// Both sides together - this is what the web buttons build on later.
void drive(int left, int right) {
  leftSide(left);
  rightSide(right);
}

// --- Ready-made moves ---
void forward()   { drive( SPEED_MAX,  SPEED_MAX); }
void backward()  { drive(-SPEED_MAX, -SPEED_MAX); }
void spinLeft()  { drive(-SPEED_MAX,  SPEED_MAX); }  // left back, right forward
void spinRight() { drive( SPEED_MAX, -SPEED_MAX); }  // left forward, right back

void stopMotors() {
  analogWrite(PWMA, 0);
  analogWrite(PWMB, 0);
  digitalWrite(AIN1, LOW);
  digitalWrite(AIN2, LOW);
  digitalWrite(BIN1, LOW);
  digitalWrite(BIN2, LOW);
}
