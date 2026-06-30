#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Adafruit ESP32 Feather V2 I2C pins
#define OLED_SDA 22
#define OLED_SCL 20

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Starting OLED test...");

  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found at 0x3C");
    Serial.println("Check wiring: SDA=22, SCL=20, VCC=3V, GND=GND");
    while (true) {
      delay(100);
    }
  }

  Serial.println("OLED found!");

  display.clearDisplay();

  // Your yellow/blue OLED has a physical yellow strip at the top.
  // This title should appear in the yellow part.
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 4);
  display.println("OLED TEST");

  display.drawLine(0, 15, 127, 15, SSD1306_WHITE);

  // Main content should appear in the blue part.
  display.setCursor(0, 20);
  display.println("ESP32 Feather V2");
  display.println("SDA: GPIO 22");
  display.println("SCL: GPIO 20");
  display.println("Address: 0x3C");

  display.display();
}

void loop() {
  static int counter = 0;

  display.fillRect(0, 54, 128, 10, SSD1306_BLACK);
  display.setCursor(0, 54);
  display.print("Count: ");
  display.println(counter++);
  display.display();

  Serial.println("OLED still running...");
  delay(1000);
}