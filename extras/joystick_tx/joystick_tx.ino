// Pro Mini Joystick TX — reads joystick, outputs serial to Bluetooth module
// Wiring:
//   VRx → A0
//   VRy → A1
//   SW  → D2 (INPUT_PULLUP, active LOW)
//   Pro Mini TX(D1) → BT module RX
//   Pro Mini RX(D0) → BT module TX (optional, not used)
//   GND shared, VCC shared (5V for HC-05/06)

#define JOY_X A0
#define JOY_Y A1
#define JOY_SW 2

void setup() {
  Serial.begin(9600);
  pinMode(JOY_SW, INPUT_PULLUP);
}

void loop() {
  int x = analogRead(JOY_X);
  int y = analogRead(JOY_Y);
  int b = !digitalRead(JOY_SW);

  Serial.print(x);
  Serial.print(',');
  Serial.print(y);
  Serial.print(',');
  Serial.println(b);

  delay(50);
}
