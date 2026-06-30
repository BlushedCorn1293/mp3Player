#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_SDA 22
#define OLED_SCL 20

// Battery pin (ESP32 Feather V2 A13)
#define VBAT_PIN 35

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// -------- BATTERY --------

float readBatteryVoltage() {
    uint32_t total = 0;

    for (int i = 0; i < 16; i++) {
        total += analogRead(VBAT_PIN);
        delay(2);
    }

    float raw = total / 16.0;

    float voltage = (raw / 4095.0) * 3.3;
    voltage *= 2.0; // Feather divider

    return voltage;
}

int batteryPercent(float voltage) {
    const float minV = 3.30;
    const float maxV = 4.20;

    float percent = (voltage - minV) / (maxV - minV) * 100.0;

    if (percent > 100) percent = 100;
    if (percent < 0) percent = 0;

    return (int)(percent + 0.5);
}

// -------- SETUP --------

void setup() {
    Serial.begin(115200);

    pinMode(VBAT_PIN, INPUT);
    analogReadResolution(12);

    Wire.begin(OLED_SDA, OLED_SCL);

    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("OLED FAIL");
        while (1);
    }

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
}

// -------- LOOP --------

void loop() {
    float voltage = readBatteryVoltage();
    int percent = batteryPercent(voltage);

    Serial.print("Battery: ");
    Serial.print(voltage, 2);
    Serial.print("V  ");
    Serial.print(percent);
    Serial.println("%");

    display.clearDisplay();

    // Title
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Battery Monitor");

    display.drawLine(0, 12, 127, 12, SSD1306_WHITE);

    // Big percentage
    display.setTextSize(2);
    display.setCursor(20, 25);
    display.print(percent);
    display.println("%");

    // Voltage small text
    display.setTextSize(1);
    display.setCursor(0, 55);
    display.print(voltage, 2);
    display.println("V");

    // Battery bar
    display.drawRect(0, 45, 128, 8, SSD1306_WHITE);
    display.fillRect(2, 47, (percent * 124) / 100, 4, SSD1306_WHITE);

    display.display();

    delay(1000);
}