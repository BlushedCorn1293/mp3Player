#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RotaryEncoder.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"

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

#define MAX_QUEUE 32
String queue[MAX_QUEUE];
int queueHead = 0;
int queueCount = 0;
int selectedQueue = 0;
String currentSongName = "";

I2SStream i2sOut;
VolumeStream volume(i2sOut);
EncodedAudioStream decoder(&volume, new MP3DecoderHelix());
File audioFile;
StreamCopy copier(decoder, audioFile);

float currentVolume = 0.2;

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
unsigned long lastDrawTime = 0;

char songNames[MAX_SONGS][MAX_SONG_NAME_LEN];
char bluetoothDeviceNames[MAX_DEVICES][MAX_DEVICE_NAME_LEN] = {
    "galaxy buds 2",
    "JBL Speaker"
};
char songPaths[MAX_SONGS][128];

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult (BLEAdvertisedDevice advertisedDevice) {

        String addr = advertisedDevice.getAddress().toString().c_str();
        String name = advertisedDevice.haveName()
            ? advertisedDevice.getName().c_str()
            : "Unknown";
        
        for (int i = 0; i < scanCount; i++) {
            if (scanResults[i].address == addr) {
                // Address already stored, but give name if there isnt one currently
                if (name != "Unknown" && scanResults[i].name == "Unknown") {
                    scanResults[i].name = name;
                }
                return;
            }
        }

        // if new device:
        if (scanCount >= MAX_SCAN_DEVICES ) return;
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

unsigned long lastScrollTime = 0;
unsigned long songSelectTime = 0;
int scrollStartDelay = 500;
int scrollOffset = 0;
const int scrollSpeed = 15; // lower is faster
const int scrollGap = 30; // gap between end and start of scrolling text

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

enum Screen {
    SCREEN_MAIN,
    SCREEN_NOW_PLAYING,
    SCREEN_PLAYLISTS,
    SCREEN_SONGS,
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
  "Shuffle",
  "Songs",
  "Playlists",
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
int selectedBluetooth = 0;
int connectedBluetoothDevice = -1;
int currentSongIndex = -1;
bool isPlaying = false;

const int firstMenuY = 20;
const int rowHeight = 10;
const int visibleRows = 4;

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("=== ESP32 Feather V2 MP3 Player ===");

    randomSeed(esp_random());

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

    sdOK = SD.begin(SD_CS, SPI, 10 * 1000000); // 20MHz

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

    // Audio Setup
    // Configure output pins
    auto cfg = i2sOut.defaultConfig();
    cfg.pin_bck  = I2S_BCLK;   // bit clock  -> pin 7
    cfg.pin_ws   = I2S_WS;     // word select-> pin 15
    cfg.pin_data = I2S_DOUT;   // data out   -> pin 8
    cfg.sample_rate = 44100;   // will be corrected per-file by the decoder
    cfg.buffer_count = 16;
    cfg.buffer_size = 1024;
    i2sOut.begin(cfg);

    decoder.begin();

    auto vcfg = volume.defaultConfig();
    vcfg.copyFrom(cfg);         // set to match I2S format
    volume.begin(vcfg);
    volume.setVolume(currentVolume);

    // Open test file and hand to pump
    audioFile = SD.open("/Music/Fine Art-01-001-KNEECAP-3CAG.mp3");
    if (audioFile) {
        Serial.println("Audio file opened, starting playback");
    } else {
        Serial.println("FAILED to open audio file");
    }

    delay(3000);
}


void loop() {
    if (isPlaying) {
        if (!copier.copy()) {
            playNextInQueue();
        }
    }

    handleInput();

    unsigned long now = millis();
    if (now - lastDrawTime > 50) {
        drawCurrentScreen();
        lastDrawTime = now;
    }
}

void drawCurrentScreen() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 4);

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
    
    // Volume bar
    int volBarW = 128;
    display.drawRect(0, 13, volBarW, 3, SSD1306_WHITE);
    int fillW = (int)(volBarW * currentVolume);
    display.fillRect(0, 13, fillW, 3, SSD1306_WHITE);

    display.display();
}

void drawNowPlaying() {
    
    display.println("Now Playing");
    // Current song
    display.setCursor(0, firstMenuY);
    if (isPlaying) {
        display.println(currentSongName);
    } else {
        display.println("Nothing playing");
    }

    // Combined scrollable list: index 0 = "Up next:" heading, 1..queueCount = queue items
    int listTop = firstMenuY + rowHeight + 2;
    const int visibleQueue = 3;
    int totalItems = queueCount + 1;   // +1 for the heading

    if (queueCount == 0) {
        display.setCursor(0, listTop);
        display.println("Up next:");
        display.setCursor(0, listTop + rowHeight);
        display.println("(queue empty)");
        return;
    }

    // selectedQueue is 0-based over the QUEUE (0..queueCount-1).
    // Map it to list position: heading is list-pos 0, queue item q is list-pos q+1.
    int selectedListPos = selectedQueue + 1;

    int startIndex = selectedListPos - visibleQueue / 2;
    if (startIndex < 0) startIndex = 0;
    if (startIndex > totalItems - visibleQueue) startIndex = totalItems - visibleQueue;
    if (startIndex < 0) startIndex = 0;

    for (int row = 0; row < visibleQueue; row++) {
        int listPos = startIndex + row;
        if (listPos >= totalItems) break;

        int y = listTop + row * rowHeight;

        // list-pos 0 is the heading
        if (listPos == 0) {
            display.setCursor(0, y);
            display.println("Up next:");
            continue;
        }

        int qi = listPos - 1;   // queue index
        int idx = (queueHead - queueCount + qi + MAX_QUEUE) % MAX_QUEUE;
        String path = queue[idx];
        int slash = path.lastIndexOf('/');
        String name = (slash >= 0) ? path.substring(slash + 1) : path;
        name = String(qi + 1) + ") " + name;

        int xStart = (qi == selectedQueue) ? 3 : 0;
        int maxWidth = 116 - xStart;

        int16_t x1, y1;
        uint16_t w, h;
        display.getTextBounds(name, 0, 0, &x1, &y1, &w, &h);

        if (qi == selectedQueue) {
            display.drawRect(0, y - 1, 118, 10, SSD1306_WHITE);
        }

        display.setTextWrap(false);

        if (qi == selectedQueue && w > maxWidth) {
            if (millis() - songSelectTime > scrollStartDelay) {
                if (millis() - lastScrollTime > scrollSpeed) {
                    scrollOffset++;
                    lastScrollTime = millis();
                }
            }
            int span = w + scrollGap;
            if (scrollOffset >= span) scrollOffset = 0;

            for (int pass = 0; pass < 2; pass++) {
                int cx = xStart - scrollOffset + pass * span;
                for (int i = 0; i < (int)name.length(); i++) {
                    char c = name[i];
                    int16_t cbx, cby;
                    uint16_t cw, ch;
                    display.getTextBounds(String(c), 0, 0, &cbx, &cby, &cw, &ch);
                    if (cx + int(cw) > xStart && cx < xStart + maxWidth) {
                        display.setCursor(cx, y);
                        display.print(c);
                    }
                    cx += cw;
                }
            }
        } else {
            while (w > maxWidth && name.length() > 1) {
                name = name.substring(0, name.length() - 1);
                display.getTextBounds(name, 0, 0, &x1, &y1, &w, &h);
            }
            display.setCursor(xStart, y);
            display.print(name);
        }
    }

    // Scrollbar over the combined list
    if (totalItems > visibleQueue) {
        int barX = 124;
        int barY = listTop;
        int barH = visibleQueue * rowHeight;
        display.drawRect(barX, barY, 3, barH, SSD1306_WHITE);

        int thumbH = max(4, barH * visibleQueue / totalItems);
        int thumbY = barY + ((barH - thumbH) * selectedListPos) / (totalItems - 1);
        display.fillRect(barX, thumbY, 3, thumbH, SSD1306_WHITE);
    }
}


void drawPlaylists() {
    display.println("Playlists");
}

void drawSongs() {
    display.println("Songs");
    

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

        int xStart = (itemIndex == selectedSong) ? 3 : 0;
        int maxWidth = 116-xStart;
        String name = songNames[itemIndex];
        int16_t x1, y1;
        uint16_t w, h;
        display.getTextBounds(name, 0, 0, &x1, &y1, &w, &h);
        
        int y = firstMenuY + row * rowHeight;

        if (itemIndex == selectedSong) {
            display.drawRect(0, y-1, 118, 10, SSD1306_WHITE);
        } 

        display.setTextWrap(false);

        if (itemIndex == selectedSong && w > maxWidth) {

            if (millis() - songSelectTime > scrollStartDelay) {
                if (millis() - lastScrollTime > scrollSpeed) {
                    scrollOffset++;
                    lastScrollTime = millis();
                }
            }

            int span = w + scrollGap;
            if (scrollOffset >= span) scrollOffset = 0;

            for (int pass=0; pass < 2; pass++) {
                int cx = xStart - scrollOffset + pass * span;
                for (int i = 0; i < (int)name.length(); i++) {
                    char c = name[i];
                    int16_t cbx, cby;
                    uint16_t cw, ch;
                    display.getTextBounds(String(c), 0, 0, &cbx, &cby, &cw, &ch);
                    if (cx + int(cw) > xStart && cx < xStart + maxWidth) {
                        display.setCursor(cx, y);
                        display.print(c);
                    }
                    cx += cw;
                }
            }

        } else {
            while (w > maxWidth && name.length() > 1) {
                name = name.substring(0, name.length() - 1);
                display.getTextBounds(name, 0, 0, &x1, &y1, &w, &h);
            }
            display.setCursor(xStart, y);
            display.print(name);
        }

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
}


void drawBluetooth() {
    display.println("Bluetooth");
    
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
}

void drawBluetoothSaved() {
    display.println("Saved Devices");
    

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
}

void drawBluetoothScan() {
    
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
    display.println("Settings");
    
    display.display();
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
                case SCREEN_SONGS:
                    playSong(songPaths[selectedSong]);
                    currentSongIndex = selectedSong;
                    break;
                case SCREEN_NOW_PLAYING:
                    playFromQueue(selectedQueue);
                    break;
            }
            lastInputTime = now;
        }
        if (digitalRead(BTN_DOWN) == LOW) {
            currentVolume -= 0.05;
            if (currentVolume < 0.0) currentVolume = 0.0;
            volume.setVolume(currentVolume);
            Serial.print("Volume: "); Serial.println(currentVolume);
            lastInputTime = now;
        }
        if (digitalRead(BTN_RIGHT) == LOW) {
            switch (currentScreen) {
                case SCREEN_SONGS:
                    addToQueue(songPaths[selectedSong]);
                    if (!isPlaying) playNextInQueue();
                    break;
                case SCREEN_NOW_PLAYING:
                    playNextInQueue();
                    break;
            }
            lastInputTime = now;
        }
        if (digitalRead(BTN_UP) == LOW) {
            currentVolume += 0.05;
            if (currentVolume > 1.0) currentVolume = 1.0;
            volume.setVolume(currentVolume);
            Serial.print("Volume: "); Serial.println(currentVolume);
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
            case SCREEN_SONGS: 
                moveSelection(selectedSong, songCount, moveDirection); 
                scrollOffset = 0;
                lastScrollTime = now;
                songSelectTime = now;
                break;
            case SCREEN_BLUETOOTH: moveSelection(selectedBluetooth, blueMenuCount, moveDirection); break;
            case SCREEN_BLUETOOTH_SAVED: moveSelection(selectedSaved, devicesCount, moveDirection); break;
            case SCREEN_BLUETOOTH_SCAN: if (scanState == SCAN_DONE) moveSelection(selectedScanResult, scanCount, moveDirection); break;
            case SCREEN_NOW_PLAYING:
                if (queueCount > 0) {
                    moveSelection(selectedQueue, queueCount, moveDirection);
                    scrollOffset = 0;
                    lastScrollTime = now;
                    songSelectTime = now;
                }
                break;
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
  // Yellow top band title
  display.println("Menu");
  

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
}

Screen openSelectedMenu() {
    switch (selectedMenu) {
        case 0: selectedQueue = 0; return SCREEN_NOW_PLAYING;                    // Now Playing
        case 1: shuffleQueue(); selectedQueue = 0; return SCREEN_NOW_PLAYING;    // Shuffle
        case 2: songSelectTime = millis(); scrollOffset = 0; return SCREEN_SONGS; // Songs
        case 3: return SCREEN_PLAYLISTS;                                          // Playlists
        case 4: return SCREEN_BLUETOOTH;                                          // Bluetooth
        case 5: return SCREEN_SETTINGS;                                           // Settings
    }
    return SCREEN_MAIN;
}

Screen openSelectedBluetooth() {
    switch (selectedBluetooth) {
        case 1: return SCREEN_BLUETOOTH_SCAN;
        case 2: return SCREEN_BLUETOOTH_SAVED;
        default: return SCREEN_BLUETOOTH;
    }
}

void scanDir(File dir) {
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;  // no more entries in this directory

        if (entry.isDirectory()) {
            scanDir(entry);           // recurse into the subfolder
        } else {
            const char* filename = entry.name();
            if (isAudioFile(filename)) {
                if (songCount < MAX_SONGS) {
                    strncpy(songNames[songCount], filename, MAX_SONG_NAME_LEN - 1);
                    songNames[songCount][MAX_SONG_NAME_LEN - 1] = '\0';

                    strncpy(songPaths[songCount], entry.path(), 127);
                    songPaths[songCount][127] = '\0';

                    Serial.print("Song ");
                    Serial.print(songCount + 1);
                    Serial.print(": ");
                    Serial.println(songNames[songCount]);

                    songCount++;
                }
            }
        }
        entry.close();
    }
}

void scanSongsOnSD() {
    songCount = 0;

    File root = SD.open("/");
    if (!root) {
        Serial.println("Failed to open SD root");
        return;
    }
    if (!root.isDirectory()) {
        Serial.println("SD root is not a directory");
        return;
    }

    Serial.println("Scanning SD card for songs...");
    display.println("Scanning SD for songs");
    display.display();

    scanDir(root);   // walk the whole tree from root
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

void playSong(const char* path) {
    if (audioFile) audioFile.close(); // close any playing song

    audioFile = SD.open(path);
    if (audioFile) {
        copier.begin(decoder, audioFile);
        isPlaying = true;

        String p = String(path);
        int slash = p.lastIndexOf('/');
        currentSongName = (slash >= 0) ? p.substring(slash + 1) : p;

        Serial.print("Now playing: ");
    } else {
        isPlaying = false;
        Serial.print("FAILED to open: ");
    }
    Serial.println(path);
}

void addToQueue(const char* path) {
    if (queueCount >= MAX_QUEUE) {
        Serial.println("Queue Full");
        return;
    }
    queue[queueHead] = String(path);
    queueHead = (queueHead + 1) % MAX_QUEUE;
    queueCount++;
    Serial.print("Added to queue: ");
    Serial.println(path);
}

void playNextInQueue() {

    if (queueCount == 0) {
        isPlaying = false;
        Serial.println("Queue Finished");
        return;
    }
    int tail = (queueHead - queueCount + MAX_QUEUE) % MAX_QUEUE;

    playSong(queue[tail].c_str());
    queueCount--;
}

void playFromQueue(int qi) {
    if (qi < 0 || qi >= queueCount) return;

    int idx = (queueHead - queueCount + qi + MAX_QUEUE) % MAX_QUEUE;
    playSong(queue[idx].c_str());

    // Remove item song by shifting everything after it one step toward the tail
    for (int i = qi; i < queueCount - 1; i++) {
        int cur  = (queueHead - queueCount + i + MAX_QUEUE) % MAX_QUEUE;
        int next = (queueHead - queueCount + i + 1 + MAX_QUEUE) % MAX_QUEUE;
        queue[cur] = queue[next];
    }
    queueCount--;

    // keep selection valid
    if (selectedQueue >= queueCount) selectedQueue = queueCount > 0 ? queueCount - 1 : 0;
}

void shuffleQueue() {
    queueHead = 0;
    queueCount = 0;
    selectedQueue = 0;

    int total = songCount;
    if (total > MAX_SONGS) total = MAX_SONGS;

    int order[MAX_SONGS];
    for (int i = 0; i <  total; i++) order[i] = i;

    // Fisher-Yates randomisation
    for (int i = total - 1; i > 0; i--) {
        int j = random(i + 1);
        int tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
    }

    for (int i = 0; i < total && i < MAX_QUEUE; i++) {
        queue[queueHead] = String(songPaths[order[i]]);
        queueHead = (queueHead + 1) % MAX_QUEUE;
        queueCount++;
    }

    playNextInQueue();
}