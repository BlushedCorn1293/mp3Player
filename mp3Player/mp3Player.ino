#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RotaryEncoder.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <driver/i2s_std.h>

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_SDA 22
#define OLED_SCL 20

// Encoder
#define PIN_ENCODER_A 25
#define PIN_ENCODER_B 14

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

// Songs
#define MAX_SONGS 64
#define MAX_SONG_NAME_LEN 64

// Bluetooth Devices
#define MAX_DEVICES 10
#define MAX_DEVICE_NAME_LEN 20

// Bluetoot Scanning
#define MAX_SCAN_DEVICES 20


i2s_chan_handle_t tx_handle;

struct BleDevice {
    String name;
    String address;
    int rssi;
};

BleDevice scanResults[MAX_SCAN_DEVICES];
int scanCount = 0;
bool scanStarted = false;
bool scanning = false;
unsigned long scanStartTime = 0;
BLEScan* pBLEScan;

char songNames[MAX_SONGS][MAX_SONG_NAME_LEN];
char bluetoothDeviceNames[MAX_DEVICES][MAX_DEVICE_NAME_LEN] = {
    "wh-1000XM4",
    "galaxy buds 2",
    "JBL Speaker"
};

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult (BLEAdvertisedDevice advertisedDevice) {
        if (scanCount >= MAX_SCAN_DEVICES ) return;

        String name = advertisedDevice.haveName()
            ? advertisedDevice.getName().c_str()
            : "Unknown";
        
            scanResults[scanCount].name = name;
            scanResults[scanCount].address = advertisedDevice.getAddress().toString().c_str();
            scanResults[scanCount].rssi = advertisedDevice.getRSSI();

            scanCount++;
    }
};

int devicesCount = 3;
int selectedSaved = 0;

int songCount = 0;
int selectedSong = 0;

bool oledOK = false;
bool sdOK = false;

RotaryEncoder encoder(PIN_ENCODER_A, PIN_ENCODER_B, RotaryEncoder::LatchMode::TWO03);
void checkPosition(){
    encoder.tick();
}
int lastRotary = 0;
unsigned long lastInputTime = 0;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

enum Screen {
    SCREEN_MAIN,
    SCREEN_NOW_PLAYING,
    SCREEN_PLAYLISTS,
    SCREEN_SONGS,
    SCREEN_ARTISTS,
    SCREEN_BLUETOOTH,
    SCREEN_SETTINGS,
    SCREEN_BLUETOOTH_SAVED,
    SCREEN_BLUETOOTH_SCAN
};

Screen currentScreen = SCREEN_MAIN;

enum ScanState {
    SCAN_IDLE,
    SCAN_RUNNING,
    SCAN_DONE
};

ScanState scanState = SCAN_IDLE;
int selectedScanResult = 0;

void scanSongsOnSD();
bool isAudioFile(const char* filename);
void drawSongsMenu();
void moveSong(int direction);
Screen openSelectedMenu();
Screen openSelectedBluetooth();

const char* menuItems[] = {
  "Now Playing",
  "Playlists",
  "Songs",
  "Artists",
  "Bluetooth",
  "Settings"
};

const char* bluetoothItems[] = {
    "Connected:",
    "Scan Devices",
    "Saved Devices",
    "Disconnect",
    "Auto Connect"
};

const int menuCount = sizeof(menuItems) / sizeof(menuItems[0]);
const int blueMenuCount = sizeof(bluetoothItems) / sizeof(bluetoothItems[0]);
int selectedMenu = 0;
int selectedPlaylist = 0;
int selectedArtist = 0;
int selectedBluetooth = 0;
int connectedBluetoothDevice = -1;

const int firstMenuY = 20;
const int rowHeight = 10;
const int visibleRows = 4;

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("=== ESP32 Feather V2 MP3 Player ===");

    pinMode(BTN_CENTER, INPUT_PULLUP);
    pinMode(BTN_DOWN,   INPUT_PULLUP);
    pinMode(BTN_RIGHT,  INPUT_PULLUP);
    pinMode(BTN_UP,     INPUT_PULLUP);
    pinMode(BTN_LEFT,   INPUT_PULLUP);

    pinMode(PIN_ENCODER_A, INPUT_PULLUP);
    pinMode(PIN_ENCODER_B, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_A), checkPosition, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_B), checkPosition, CHANGE);

    lastRotary = encoder.getPosition();

    // OLED test
    Serial.println("Testing OLED...");
    Wire.begin(OLED_SDA, OLED_SCL);
    oledOK = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

    if (oledOK) {
        Serial.println("OLED OK");
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.println("ESP32 MP3 Player");
        display.println("Initializing...");
        display.display();
        delay(50);
    } else {
    Serial.println("OLED FAILED at 0x3C");
    Serial.println("Check SDA=22, SCL=20, VCC=3V, GND=GND");
    }

    // SD test
    Serial.println("Testing SD card...");
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

    sdOK = SD.begin(SD_CS, SPI, 10 * 1000000); // 10MHz

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
        display.print("SD: ");
        display.println(sdOK ? "OK" : "FAIL");
        display.display();

        if (sdOK) {
            uint64_t cardSize = SD.cardSize() / (1024 * 1024);
            Serial.print("Card size: ");
            Serial.print(cardSize);
            Serial.println(" MB");
            scanSongsOnSD();
        }
    } else {
        Serial.println("SD FAILED");
        display.println("SD FAILED");
    }
    display.display();

    Serial.println("PCM5100 I2S Test Starting...");

    // Create I2S channel
    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));

    // Clock (44.1kHz audio)
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100);

    // Slot config (stereo 16-bit)
    i2s_std_slot_config_t slot_cfg = {
        .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
        .slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT,
        .slot_mode = I2S_SLOT_MODE_STEREO,
        .slot_mask = I2S_STD_SLOT_BOTH,
        .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
        .ws_pol = false,
        .bit_shift = true
    };

    // GPIO mapping
    i2s_std_gpio_config_t gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = (gpio_num_t)I2S_BCLK,
        .ws   = (gpio_num_t)I2S_WS,
        .dout = (gpio_num_t)I2S_DOUT,
        .din  = I2S_GPIO_UNUSED,
        .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv = false
        }
    };

    // Combine config
    i2s_std_config_t std_cfg = {
        .clk_cfg = clk_cfg,
        .slot_cfg = slot_cfg,
        .gpio_cfg = gpio_cfg
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));

    Serial.println("I2S started");
    display.println("I2S started");
    display.display();
    delay(3000);
}


void loop() {
    handleInput();
    drawCurrentScreen();
    delay(30);
}

void drawCurrentScreen() {
    switch (currentScreen) {
        case SCREEN_MAIN:
            drawMenu();
            break;
        case SCREEN_NOW_PLAYING:
            drawNowPlaying();
            break;
        case SCREEN_PLAYLISTS:
            drawPlaylists();
            break;
        case SCREEN_SONGS:
            drawSongs();
            break;
        case SCREEN_ARTISTS:
            drawArtists();
            break;
        case SCREEN_BLUETOOTH:
            drawBluetooth();
            break;
        case SCREEN_SETTINGS:
            drawSettings();
            break;
        case SCREEN_BLUETOOTH_SAVED:
            drawBluetoothSaved();
            break;
        case SCREEN_BLUETOOTH_SCAN:
            drawBluetoothScan();
            break;
    }
}

void drawNowPlaying() {
    prepareDisplay();
    display.println("Now Playing");
    display.drawLine(0, 15, 127, 15, SSD1306_WHITE);
    display.display();
}

void drawPlaylists() {
    prepareDisplay();
    display.println("Playlists");
    display.drawLine(0, 15, 127, 15, SSD1306_WHITE);
    display.display();
}

void drawSongs() {
    prepareDisplay();
    display.println("Songs");
    display.drawLine(0, 15, 127, 15, SSD1306_WHITE);

    int startIndex = selectedSong - visibleRows / 2;

    if (startIndex < 0) {
        startIndex = 0;
    }

    if (startIndex > songCount - visibleRows) {
        startIndex =  songCount - visibleRows;
    }

    if (startIndex < 0) {
        startIndex = 0;
    }

    for (int row = 0; row < visibleRows; row++) {
        int itemIndex = startIndex + row;
        
        if (itemIndex >= songCount) {
            break;
        }

        int y = firstMenuY + row * rowHeight;

        if (itemIndex == selectedSong) {
            display.drawRect(0, y-1, 118, 10, SSD1306_WHITE);
            display.setCursor(3, y);
        } else {
            display.setCursor(0, y);
        }

        display.println(songNames[itemIndex]);
    }

    if (songCount > visibleRows) {
        int barX = 124;
        int barY = firstMenuY;
        int barH = visibleRows * rowHeight;

        display.drawRect(barX, barY, 3, barH, SSD1306_WHITE);

        int thumbH = max(4, barH * visibleRows / songCount);
        int thumbY = barY + ((barH - thumbH) * selectedSong) / (songCount - 1);
        
        display.fillRect(barX, thumbY, 3, thumbH, SSD1306_WHITE);
    }

    display.display();
}

void drawArtists() {
    prepareDisplay();
    display.println("Artists");
    display.drawLine(0, 15, 127, 15, SSD1306_WHITE);
    display.display();
}


void drawBluetooth() {
    prepareDisplay();
    display.println("Bluetooth");
    display.drawLine(0, 15, 127, 15, SSD1306_WHITE);
    int startIndex = selectedBluetooth - visibleRows / 2;

    if (startIndex < 0) {
        startIndex = 0;
    }

    if (startIndex > blueMenuCount - visibleRows) {
        startIndex =  blueMenuCount - visibleRows;
    }

    if (startIndex < 0) {
        startIndex = 0;
    }

    for (int row = 0; row < visibleRows; row++) {
        int itemIndex = startIndex + row;

        if (itemIndex >= blueMenuCount) {
            break;
        } 
        int y = firstMenuY + row * rowHeight;

        if (itemIndex == selectedBluetooth) {
            display.drawRect(0, y-1, 118, 10, SSD1306_WHITE);
            display.setCursor(3, y);
        } else {
            display.setCursor(0, y);
        }

        
        if (itemIndex == 0) {
            display.print(bluetoothItems[itemIndex]);
            if (connectedBluetoothDevice != -1){
                display.println(bluetoothDeviceNames[connectedBluetoothDevice]);
            } else {
                display.println("none");
            }
        } else {
            display.println(bluetoothItems[itemIndex]);
        }
    }
    if (blueMenuCount > visibleRows) {
        int barX = 124;
        int barY = firstMenuY;
        int barH = visibleRows * rowHeight;

        display.drawRect(barX, barY, 3, barH, SSD1306_WHITE);

        int thumbH = max(4, barH * visibleRows / blueMenuCount);
        int thumbY = barY + ((barH - thumbH) * selectedBluetooth) / (blueMenuCount - 1);
        
        display.fillRect(barX, thumbY, 3, thumbH, SSD1306_WHITE);
    }

    display.display();
}

void drawBluetoothSaved() {
    prepareDisplay();
    display.println("Saved Devices");
    display.drawLine(0, 15, 127, 15, SSD1306_WHITE);

    int startIndex = selectedSaved - visibleRows / 2;

    if (startIndex < 0) startIndex = 0;
    if (startIndex > devicesCount - visibleRows)
        startIndex = devicesCount - visibleRows;
    if (startIndex < 0) startIndex = 0;

    for (int row = 0; row < visibleRows; row++) {
        int itemIndex = startIndex + row;

        if (itemIndex >= devicesCount) {
            break;
        } 
        int y = firstMenuY + row * rowHeight;

        if (itemIndex == selectedSaved) {
            display.drawRect(0, y-1, 118, 10, SSD1306_WHITE);
            display.setCursor(3, y);
        } else {
            display.setCursor(0, y);
        }

        
        if (itemIndex == connectedBluetoothDevice) {
            display.print(bluetoothDeviceNames[itemIndex]);
            display.println(" (Connected)");
        } else {
            display.println(bluetoothDeviceNames[itemIndex]);
        }
    }
    if (devicesCount > visibleRows) {
        int barX = 124;
        int barY = firstMenuY;
        int barH = visibleRows * rowHeight;

        display.drawRect(barX, barY, 3, barH, SSD1306_WHITE);

        int thumbH = max(4, barH * visibleRows / devicesCount);
        int thumbY = barY + ((barH - thumbH) * selectedSaved) / (devicesCount - 1);
        
        display.fillRect(barX, thumbY, 3, thumbH, SSD1306_WHITE);
    }

    display.display();
}

void drawBluetoothScan() {
    prepareDisplay();
    display.drawLine(0, 15, 127, 15, SSD1306_WHITE);
    
    switch (scanState) {
        case SCAN_IDLE:
            display.println("Scan for Devices");
            display.setCursor(0, firstMenuY);
            display.println("Press center to scan...");
            break;
        case SCAN_RUNNING:
            display.println("Scanning...");
            display.setCursor(0, firstMenuY);
            display.println("Please wait (~5s)");
            display.display();
            // Perform blocking scan then transition to DONE
            startBluetoothScan();
            scanState = SCAN_DONE;
            return;
        case SCAN_DONE:
            display.println("Scan Results");
            if (scanCount == 0) {
                display.setCursor(0, firstMenuY);
                display.println("No devices found.");
                break;
            }
            int startIndex = selectedScanResult - visibleRows / 2;
            if (startIndex < 0) startIndex = 0;
            if (startIndex > scanCount - visibleRows) startIndex = scanCount - visibleRows;
            if (startIndex < 0) startIndex = 0;
            for (int row = 0; row < visibleRows; row++) {
                int itemIndex = startIndex + row;
                if (itemIndex >= scanCount) break;

                int y = firstMenuY + row * rowHeight;

                if (itemIndex == selectedScanResult) {
                    display.drawRect(0, y - 1, 118, 10, SSD1306_WHITE);
                    display.setCursor(3, y);
                } else {
                    display.setCursor(0, y);
                }

                display.println(scanResults[itemIndex].name);
            }

            if (scanCount > visibleRows) {
                int barX = 124, barY = firstMenuY;
                int barH = visibleRows * rowHeight;
                display.drawRect(barX, barY, 3, barH, SSD1306_WHITE);
                int thumbH = max(4, barH * visibleRows / scanCount);
                int thumbY = barY + ((barH - thumbH) * selectedScanResult) / (scanCount - 1);
                display.fillRect(barX, thumbY, 3, thumbH, SSD1306_WHITE);
            }
            break;
    }

    display.display();
}

void startBluetoothScan() {
    scanCount = 0;

    BLEDevice::init("");

    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(), true);

    pBLEScan->setActiveScan(true); // More powerful scan
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);

    Serial.println("Starting BLE scan...");

    pBLEScan->start(5, true); // scan for 5 seconds, true = blocks

    Serial.print("Scan complete. Devices found: ");
    Serial.println(scanCount);
}

void drawSettings() {
    prepareDisplay();
    display.println("Settings");
    display.drawLine(0, 15, 127, 15, SSD1306_WHITE);
    display.display();
}

void prepareDisplay() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 4);
}

void handleInput() {
    unsigned long now = millis();

    // Buttons with debounce
    if (now - lastInputTime > 180) {
        if (digitalRead(BTN_CENTER) == LOW) {
            Serial.println("Button: CENTER");
            switch (currentScreen) {
                case SCREEN_MAIN: currentScreen = openSelectedMenu(); break;
                case SCREEN_BLUETOOTH: currentScreen = openSelectedBluetooth(); break;
                case SCREEN_BLUETOOTH_SCAN:
                    if (scanState == SCAN_IDLE) scanState = SCAN_RUNNING;
                    else if (scanState == SCAN_DONE) scanState = SCAN_IDLE; // allow rescan
                    break;
            }
            lastInputTime = now;
        }
        if (digitalRead(BTN_DOWN) == LOW) {
            Serial.println("Button: DOWN");
            lastInputTime = now;
        }
        if (digitalRead(BTN_RIGHT) == LOW) {
            Serial.println("Button: RIGHT");
            lastInputTime = now;
        }
        if (digitalRead(BTN_UP) == LOW) {
            Serial.println("Button: UP");
            lastInputTime = now;
        }
        if (digitalRead(BTN_LEFT) == LOW) {
            Serial.println("Button: LEFT");
            switch (currentScreen) {
                case SCREEN_BLUETOOTH_SAVED: currentScreen = SCREEN_BLUETOOTH; break;
                case SCREEN_BLUETOOTH_SCAN: scanState = SCAN_IDLE; selectedScanResult = 0; currentScreen = SCREEN_BLUETOOTH; break;
                default: currentScreen = SCREEN_MAIN; break;
            }
            lastInputTime = now;
        }
    }

    int currentRotary = encoder.getPosition();
    RotaryEncoder::Direction direction = encoder.getDirection();
    if (currentRotary != lastRotary) {
        Serial.print("Encoder Value: ");
        Serial.print(currentRotary);
        Serial.print("  Direction: ");
        const int moveDirection = (int)direction;
        Serial.println(moveDirection);
        switch (currentScreen) {
            case SCREEN_MAIN: moveSelection(selectedMenu, menuCount, moveDirection); break;
            case SCREEN_SONGS: moveSelection(selectedSong, songCount, moveDirection); break;
            case SCREEN_BLUETOOTH: moveSelection(selectedBluetooth, blueMenuCount, moveDirection); break;
            case SCREEN_BLUETOOTH_SAVED: moveSelection(selectedSaved, devicesCount, moveDirection); break;
            case SCREEN_BLUETOOTH_SCAN: if (scanState == SCAN_DONE) moveSelection(selectedScanResult, scanCount, moveDirection); break;
        }
    }
    lastRotary = currentRotary;
}


void moveSelection(int &selectedIndex, int itemCount, int direction) {
    selectedIndex += direction;

    if (selectedIndex < 0) {
        selectedIndex = itemCount - 1;
    } else if (selectedIndex >= itemCount) {
        selectedIndex = 0;
    }
}


void drawMenu() {
  
  prepareDisplay();
  // Yellow top band title
  display.println("Menu");
  display.drawLine(0, 15, 127, 15, SSD1306_WHITE);

  int startIndex = selectedMenu - visibleRows / 2;

  if (startIndex < 0) {
    startIndex = 0;
  }

  if (startIndex > menuCount - visibleRows) {
    startIndex =  menuCount - visibleRows;
  }

  if (startIndex < 0) {
    startIndex = 0;
  }

  for (int row = 0; row < visibleRows; row++) {
    int itemIndex = startIndex + row;
    
    if (itemIndex >= menuCount) {
        break;
    }

    int y = firstMenuY + row * rowHeight;

    if (itemIndex == selectedMenu) {
        display.drawRect(0, y-1, 118, 10, SSD1306_WHITE);
        display.setCursor(3, y);
    } else {
        display.setCursor(0, y);
    }

    display.println(menuItems[itemIndex]);
  }

  if (menuCount > visibleRows) {
    int barX = 124;
    int barY = firstMenuY;
    int barH = visibleRows * rowHeight;

    display.drawRect(barX, barY, 3, barH, SSD1306_WHITE);

    int thumbH = max(4, barH * visibleRows / menuCount);
    int thumbY = barY + ((barH - thumbH) * selectedMenu) / (menuCount - 1);
    
    display.fillRect(barX, thumbY, 3, thumbH, SSD1306_WHITE);
  }

  display.display();
}

Screen openSelectedMenu() {
    switch (selectedMenu) {
        case 0: return SCREEN_NOW_PLAYING;
        case 1: return SCREEN_PLAYLISTS;
        case 2: return SCREEN_SONGS;
        case 3: return SCREEN_ARTISTS;
        case 4: return SCREEN_BLUETOOTH;
        case 5: return SCREEN_SETTINGS;
        default: return SCREEN_MAIN;
    }
}

Screen openSelectedBluetooth() {
    switch (selectedBluetooth) {
        case 1: return SCREEN_BLUETOOTH_SCAN;
        case 2: return SCREEN_BLUETOOTH_SAVED;
        default: return SCREEN_BLUETOOTH;
    }
}

void scanSongsOnSD() {
    songCount = 0;

    File root = SD.open("/");

    if(!root) {
        Serial.println("Failed to open SD root");
        return;
    }

    if(!root.isDirectory()) {
        Serial.println("SD root is not a directory");
        return;
    }

    Serial.println("Scanning SD card for songs...");
    display.println("Scanning SD for songs");
    display.display();

    while (true) {
        File entry = root.openNextFile();

        if(!entry) {
            break; // no more files
        }

        if(!entry.isDirectory()) {
            const char* filename = entry.name();

            if(isAudioFile(filename)){
                if(songCount < MAX_SONGS) {
                    strncpy(songNames[songCount], filename, MAX_SONG_NAME_LEN -1);
                    songNames[songCount][MAX_SONG_NAME_LEN - 1] = '\0';

                    Serial.print("Song ");
                    Serial.println(songCount + 1);
                    Serial.print(": ");
                    Serial.println(songNames[songCount]);

                    songCount++;
                }
            }
        }
        entry.close();
    }

    root.close();

    Serial.print("Total songs found: ");
    Serial.println(songCount);
    display.print("Total songs found: ");
    display.println(songCount);
    display.display();
}

bool isAudioFile(const char* filename) {
    String name = String(filename);
    name.toLowerCase();
    return name.endsWith(".mp3");
}

