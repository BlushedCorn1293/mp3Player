#define ENC_A 32
#define ENC_B 33

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);

  Serial.println("Raw encoder pin test");
  Serial.println("Rotate the wheel slowly.");
}

void loop() {
  int a = digitalRead(ENC_A);
  int b = digitalRead(ENC_B);

  Serial.print("ENC_A=");
  Serial.print(a);
  Serial.print("  ENC_B=");
  Serial.println(b);

  delay(200);
}