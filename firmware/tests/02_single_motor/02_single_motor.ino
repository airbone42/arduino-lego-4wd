/*
  Test 2 - "The first motor turns"
  ------------------------------------------------------------
  One single motor spins forward, stops, spins backward, stops -
  over and over. This proves that the motor driver is wired up
  correctly, before you connect three more motors to it.

  IMPORTANT - the motor is NOT connected to the Arduino directly!
  A TB6612FNG motor driver sits in between:
    - the Arduino is the brain: it only sends weak control signals
    - the driver is the muscle: it takes the strong current from the
      battery and switches it through to the motor
  That way a small microcontroller can move a "strong" motor without
  destroying itself.

  Three control lines go to the driver:
    PWMA = how fast?       (0 = stopped ... 255 = full power)
    AIN1 and AIN2 = which way?  (forward / backward / brake)

  Prop the wheel up in the air before you power this on.
*/

// --- Which Arduino pin carries which driver signal ---
const int PWMA = 9;  // speed     (must be a "~" pin, i.e. PWM capable)
const int AIN1 = 7;  // direction 1
const int AIN2 = 8;  // direction 2

void setup() {
  Serial.begin(115200);        // so we can follow along in the serial monitor

  // All three control lines are OUTPUTS (Arduino sends -> driver)
  pinMode(PWMA, OUTPUT);
  pinMode(AIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);

  stopMotor();                 // make sure nothing moves at startup
  Serial.println("Motor test starting...");
}

void loop() {
  Serial.println("forward");
  forward(200);                // 200 out of 255 - brisk, but not full throttle
  delay(2000);                 // turn for 2 seconds

  Serial.println("stop");
  stopMotor();
  delay(1000);

  Serial.println("backward");
  backward(200);
  delay(2000);

  Serial.println("stop");
  stopMotor();
  delay(1000);
}

// --- Small helpers, so loop() stays readable ---

void forward(int speed) {
  digitalWrite(AIN1, HIGH);    // one direction: AIN1 on, AIN2 off
  digitalWrite(AIN2, LOW);
  analogWrite(PWMA, speed);    // set the speed
}

void backward(int speed) {
  digitalWrite(AIN1, LOW);     // the other direction: the other way round
  digitalWrite(AIN2, HIGH);
  analogWrite(PWMA, speed);
}

void stopMotor() {
  analogWrite(PWMA, 0);        // speed 0
  digitalWrite(AIN1, LOW);
  digitalWrite(AIN2, LOW);
}
