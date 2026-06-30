#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <driver/i2s_std.h>

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_SDA 22
#define OLED_SCL 20

// Encoder
#define ENC_A 14
#define ENC_B 25

// Buttons
#define BTN_CENTER 13
#define BTN_DOWN   26
#define BTN_RIGHT  32
#define BTN_UP     27
#define BTN_LEFT   33

// SD Card SPI
#define SD_CS   4
#define SD_SCK  5
#define SD_MISO 21
#define SD_MOSI 19

// DAC
#define I2S_WS    15
#define I2S_DOUT   8
#define I2S_BCLK   7

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

bool oledOK = false;
bool sdOK = false;

int lastEncA = HIGH;
long encoderCount = 0;

unsigned long lastPrint = 0;
unsigned long lastDisplay = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("=== ESP32 Feather V2 Pin Test ===");

  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);

  pinMode(BTN_CENTER, INPUT_PULLUP);
  pinMode(BTN_DOWN,   INPUT_PULLUP);
  pinMode(BTN_RIGHT,  INPUT_PULLUP);
  pinMode(BTN_UP,     INPUT_PULLUP);
  pinMode(BTN_LEFT,   INPUT_PULLUP);

  lastEncA = digitalRead(ENC_A);

  // OLED test
  Serial.println("Testing OLED...");
  Wire.begin(OLED_SDA, OLED_SCL);

  oledOK = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  if (oledOK) {
    Serial.println("OLED OK at 0x3C");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Pin Test");
    display.println("OLED OK");
    display.display();
  } else {
    Serial.println("OLED FAILED at 0x3C");
    Serial.println("Check SDA=22, SCL=20, VCC=3V, GND=GND");
  }

  // SD test
  Serial.println("Testing SD card...");
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  sdOK = SD.begin(SD_CS, SPI, 4000000);

  if (sdOK) {
    Serial.println("SD OK");

    uint8_t cardType = SD.cardType();
    Serial.print("Card type: ");

    if (cardType == CARD_NONE) {
      Serial.println("NONE");
      sdOK = false;
    } else if (cardType == CARD_MMC) {
      Serial.println("MMC");
    } else if (cardType == CARD_SD) {
      Serial.println("SDSC");
    } else if (cardType == CARD_SDHC) {
      Serial.println("SDHC/SDXC");
    } else {
      Serial.println("UNKNOWN");
    }

    if (sdOK) {
      uint64_t cardSize = SD.cardSize() / (1024 * 1024);
      Serial.print("Card size: ");
      Serial.print(cardSize);
      Serial.println(" MB");

      File f = SD.open("/pin_test.txt", FILE_WRITE);
      if (f) {
        f.println("ESP32 Feather V2 pin test OK");
        f.close();
        Serial.println("SD write OK: /pin_test.txt");
      } else {
        Serial.println("SD write FAILED");
      }
    }
  } else {
    Serial.println("SD FAILED");
    Serial.println("Check CS=4/A5, SCK=5, MISO=21, MOSI=19, 3V, GND");
    Serial.println("Also check card is FAT32, not exFAT.");
  }

  updateOLED();
}

void loop() {
  readEncoder();

  unsigned long now = millis();

  if (now - lastPrint > 300) {
    lastPrint = now;
    printInputs();
  }

  if (now - lastDisplay > 250) {
    lastDisplay = now;
    updateOLED();
  }
}

void readEncoder() {
  int encA = digitalRead(ENC_A);
  int encB = digitalRead(ENC_B);

  if (encA != lastEncA) {
    if (encA == LOW) {
      if (encB == HIGH) {
        encoderCount++;
        Serial.println("Encoder: +");
      } else {
        encoderCount--;
        Serial.println("Encoder: -");
      }
    }

    lastEncA = encA;
  }
}

void printInputs() {
  Serial.print("Buttons: ");

  Serial.print("CENTER=");
  Serial.print(digitalRead(BTN_CENTER) == LOW ? "PRESSED " : "up ");

  Serial.print("DOWN=");
  Serial.print(digitalRead(BTN_DOWN) == LOW ? "PRESSED " : "up ");

  Serial.print("RIGHT=");
  Serial.print(digitalRead(BTN_RIGHT) == LOW ? "PRESSED " : "up ");

  Serial.print("UP=");
  Serial.print(digitalRead(BTN_UP) == LOW ? "PRESSED " : "up ");

  Serial.print("LEFT=");
  Serial.print(digitalRead(BTN_LEFT) == LOW ? "PRESSED " : "up ");

  Serial.print(" ENC=");
  Serial.println(encoderCount);
}

void updateOLED() {
  if (!oledOK) {
    return;
  }

  display.clearDisplay();

  display.setCursor(0, 0);
  display.println("Pin Test");
  display.drawLine(0, 15, 127, 15, SSD1306_WHITE);

  display.setCursor(0, 18);
  display.print("OLED: ");
  display.println(oledOK ? "OK" : "FAIL");

  display.print("SD: ");
  display.println(sdOK ? "OK" : "FAIL");

  display.print("ENC: ");
  display.println(encoderCount);

  display.print("U:");
  display.print(digitalRead(BTN_UP) == LOW ? "P " : ". ");

  display.print("D:");
  display.print(digitalRead(BTN_DOWN) == LOW ? "P " : ". ");

  display.print("L:");
  display.print(digitalRead(BTN_LEFT) == LOW ? "P " : ". ");

  display.print("R:");
  display.println(digitalRead(BTN_RIGHT) == LOW ? "P" : ".");

  display.print("C:");
  display.println(digitalRead(BTN_CENTER) == LOW ? "PRESSED" : ".");

  display.display();
}