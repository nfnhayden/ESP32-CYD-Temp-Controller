/*
 * ESP32-2432S028R Temperature Controller (Modern UI)
 *
 * BOARD: ESP32-2432S028R (Resistive Touch)
 * CONTROLLER: XPT2046
 *
 * FEATURE: Modern Dark UI with Red Indicator & Unit Toggle
 */

#include <DallasTemperature.h>
#include <OneWire.h>
#include <Preferences.h>
#include <TFT_eSPI.h>

// -------------------------------------------------------------------------
// HARDWARE PIN DEFINITIONS
// -------------------------------------------------------------------------
#define RELAY_PIN 27
#define ONE_WIRE_BUS 22

// --- CORRECT TOUCH PINS FOR ESP32-2432S028R ---
#define T_CLK 25
#define T_CS 33
#define T_DIN 32 // MOSI
#define T_DO 39  // MISO
#define T_IRQ 36

// -------------------------------------------------------------------------
// UI CONSTANTS (Modern Dark Theme)
// -------------------------------------------------------------------------
// Colors (5-6-5 format)
#define C_BG 0x0000       // Black
#define C_CARD 0x2965     // Lighter Grey #2D2D2D
#define C_TEXT_PRI 0xFFFF // White
#define C_TEXT_SEC 0xAD55 // Light Grey #AAAAAA
#define C_ACCENT 0xF800   // Red #FF0000 (Heating Indicator)
#define C_ON 0x2E72       // Teal #2EC4B6
#define C_OFF 0xF965      // Reddish

// -------------------------------------------------------------------------
// OBJECTS
// -------------------------------------------------------------------------
TFT_eSPI tft = TFT_eSPI();
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
Preferences prefs;

// System State
bool systemEnabled = false;
float currentTemp = 0.0;
float targetTemp = 25.0;
float hysteresis = 0.5;
bool useFahrenheit = false;

unsigned long lastTempRead = 0;
const unsigned long tempReadInterval = 1000;

// Calibration Data
int cal_minX = 3800;
int cal_maxX = 300;
int cal_minY = 200;
int cal_maxY = 3750;
bool isCalibrated = false;

// -------------------------------------------------------------------------
// LOW LEVEL TOUCH FUNCTIONS (Bit-Bang)
// -------------------------------------------------------------------------
void touch_init() {
  pinMode(T_CLK, OUTPUT);
  pinMode(T_CS, OUTPUT);
  pinMode(T_DIN, OUTPUT);
  pinMode(T_DO, INPUT);
  pinMode(T_IRQ, INPUT);

  digitalWrite(T_CS, HIGH);
  digitalWrite(T_CLK, HIGH);
  digitalWrite(T_DIN, HIGH);
}

uint16_t touch_read_raw(uint8_t cmd) {
  uint16_t result = 0;
  digitalWrite(T_CS, LOW);

  // Send Command
  for (int i = 0; i < 8; i++) {
    digitalWrite(T_DIN, (cmd & 0x80) ? HIGH : LOW);
    digitalWrite(T_CLK, LOW);
    digitalWrite(T_CLK, HIGH);
    cmd <<= 1;
  }

  // Read Data
  digitalWrite(T_CLK, LOW);
  digitalWrite(T_CLK, HIGH);

  for (int i = 0; i < 12; i++) {
    digitalWrite(T_CLK, LOW);
    result <<= 1;
    if (digitalRead(T_DO))
      result++;
    digitalWrite(T_CLK, HIGH);
  }

  digitalWrite(T_CS, HIGH);
  return result;
}

// Returns raw X/Y if touched, otherwise returns false
bool get_raw_touch(uint16_t &rawX, uint16_t &rawY) {
  if (digitalRead(T_IRQ) == HIGH)
    return false;

  // SWAPPED AXES for 2432S028R
  rawX = touch_read_raw(0x90);
  rawY = touch_read_raw(0xD0);

  // Simple noise filter
  if (rawX < 100 || rawX > 4000 || rawY < 100 || rawY > 4000)
    return false;

  return true;
}

bool get_touch_point(int &x, int &y) {
  uint16_t rx, ry;
  if (!get_raw_touch(rx, ry))
    return false;

  // Map using stored calibration data
  x = map(rx, cal_minX, cal_maxX, 0, 320);
  y = map(ry, cal_minY, cal_maxY, 0, 240);

  // Clamp to screen
  if (x < 0)
    x = 0;
  if (x > 320)
    x = 320;
  if (y < 0)
    y = 0;
  if (y > 240)
    y = 240;

  return true;
}

// -------------------------------------------------------------------------
// CALIBRATION LOGIC
// -------------------------------------------------------------------------
void runCalibration() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Touch Calibration", 160, 20, 2);
  tft.drawString("Follow the Green Square", 160, 40, 2);

  // 1. Top-Left Target
  tft.fillRect(10, 10, 10, 10, TFT_GREEN);

  uint16_t x1 = 0, y1 = 0;
  // Wait for touch
  while (!get_raw_touch(x1, y1)) {
    delay(10);
  }
  // Wait for release
  delay(500);

  tft.fillRect(10, 10, 10, 10, TFT_BLACK); // Erase

  // 2. Bottom-Right Target
  tft.fillRect(300, 220, 10, 10, TFT_GREEN);

  uint16_t x2 = 0, y2 = 0;
  // Wait for touch
  while (!get_raw_touch(x2, y2)) {
    delay(10);
  }

  // Save Data
  cal_minX = x1;
  cal_maxX = x2;
  cal_minY = y1;
  cal_maxY = y2;

  prefs.begin("touch_v2", false);
  prefs.putInt("minX", cal_minX);
  prefs.putInt("maxX", cal_maxX);
  prefs.putInt("minY", cal_minY);
  prefs.putInt("maxY", cal_maxY);
  prefs.putBool("done", true);
  prefs.end();

  tft.fillScreen(TFT_BLACK);
  tft.drawString("Saved!", 160, 120, 4);
  delay(1000);
}

// -------------------------------------------------------------------------
// UI HELPERS (Modern Dark)
// -------------------------------------------------------------------------

// Helper to convert C to F if needed
float getDisplayTemp(float tempC) {
  if (useFahrenheit) {
    return (tempC * 9.0 / 5.0) + 32.0;
  }
  return tempC;
}

void drawCard(int x, int y, int w, int h, const char *title) {
  tft.fillRoundRect(x, y, w, h, 8, C_CARD);

  if (title) {
    tft.setTextColor(C_TEXT_SEC, C_CARD);
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(1);
    tft.drawString(title, x + 10, y + 8, 2);
  }
}

void drawToggle(int x, int y, int w, int h, bool state) {
  uint16_t color = state ? C_ON : C_OFF;
  tft.fillRoundRect(x, y, w, h, h / 2, C_BG); // Track background

  // Knob
  int knobR = (h / 2) - 4;
  int knobX =
      state ? (x + w - knobR - 4) : (x + knobR + 4); // ON = Right, OFF = Left
  int knobY = y + (h / 2);

  tft.fillRoundRect(x, y, w, h, h / 2, color); // Active track
  tft.fillCircle(knobX, knobY, knobR, C_TEXT_PRI);

  // Label
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT_PRI, color);
  int textX =
      state ? (x + 30) : (x + w - 30); // ON text on left, OFF text on right
  tft.drawString(state ? "ON" : "OFF", textX, y + h / 2 + 1, 2);
}

void drawHeader() {
  tft.fillRect(0, 0, 320, 30, C_BG);
  tft.setTextColor(C_TEXT_SEC, C_BG);
  tft.setTextDatum(ML_DATUM);
  tft.drawString("CHAMBER TEMP CONTROL", 10, 15, 2);
}

void drawMainTemp(float temp) {
  // Card Background Area
  int x = 10, y = 40, w = 300, h = 100;

  // Clear old text area to avoid flicker
  tft.fillRect(x + 10, y + 30, w - 20, 60, C_CARD);

  char buf[10];
  if (temp <= -100) {
    sprintf(buf, "ERR");
    tft.setTextColor(C_OFF, C_CARD);
  } else {
    sprintf(buf, "%.1f", getDisplayTemp(temp));
    tft.setTextColor(C_TEXT_PRI, C_CARD);
  }

  tft.setTextDatum(MC_DATUM);
  // Use Font 7 (7-seg) or 6 if available, else 4
  tft.drawString(buf, x + w / 2 - 20, y + h / 2 + 5, 7);

  // Unit
  tft.setTextDatum(BL_DATUM);
  tft.drawString(useFahrenheit ? "F" : "C", x + w / 2 + 60, y + h / 2 + 30, 4);

  // Label
  tft.setTextColor(C_TEXT_SEC, C_CARD);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("CURRENT TEMP", x + w / 2, y + 15, 2);
}

void drawTargetControl(float target) {
  int x = 10, y = 150, w = 180, h = 80;

  tft.fillRect(x + 50, y + 20, w - 100, 40, C_CARD); // Clear value

  tft.setTextColor(C_TEXT_PRI, C_CARD);
  tft.setTextDatum(MC_DATUM);

  char buf[10];
  sprintf(buf, "%.1f", getDisplayTemp(target));
  tft.drawString(buf, x + w / 2, y + h / 2 + 5,
                 4); // Font 4, slightly shifted down

  // Buttons (approximate locations for touch)
  // Left (-)
  tft.fillCircle(x + 30, y + h / 2, 20, C_BG);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.drawString("-", x + 30, y + h / 2, 4);

  // Right (+)
  tft.fillCircle(x + w - 30, y + h / 2, 20, C_BG);
  tft.drawString("+", x + w - 30, y + h / 2, 4);

  // Label
  tft.setTextColor(C_TEXT_SEC, C_CARD);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("TARGET", x + w / 2, y + 10, 2);
}

void updateRelayIndicator(bool on) {
  // Red dot in header
  uint16_t color = on ? C_ACCENT : C_BG;
  tft.fillCircle(305, 15, 6, color);

  if (on) {
    tft.fillCircle(305, 15, 6, C_ACCENT);
  } else {
    tft.fillCircle(305, 15, 6, C_BG);
  }
}

void drawStaticUI() {
  tft.fillScreen(C_BG);
  drawHeader();

  // Main Temp Card
  drawCard(10, 40, 300, 100, NULL);

  // Target Card
  drawCard(10, 150, 180, 80, NULL);

  // Power Toggle
  drawToggle(210, 165, 100, 50, systemEnabled);
}

void refreshData() {
  drawMainTemp(currentTemp);
  drawTargetControl(targetTemp);
}

// -------------------------------------------------------------------------
// SETUP
// -------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("Starting...");

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  touch_init();

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(1);

  // --- LOAD SETTINGS ---
  prefs.begin("settings", false);
  targetTemp = prefs.getFloat("target", 25.0);
  prefs.end();

  // --- LOAD CALIBRATION ---
  prefs.begin("touch_v2", false);
  bool done = prefs.getBool("done", false);
  if (done) {
    cal_minX = prefs.getInt("minX", 3800);
    cal_maxX = prefs.getInt("maxX", 300);
    cal_minY = prefs.getInt("minY", 200);
    cal_maxY = prefs.getInt("maxY", 3750);
  }
  prefs.end();

  // Calibration check
  uint16_t dummyX, dummyY;
  if (!done || get_raw_touch(dummyX, dummyY)) {
    runCalibration();
  }

  drawStaticUI();
  sensors.begin();

  // Initial read
  sensors.requestTemperatures();
  currentTemp = sensors.getTempCByIndex(0);
  refreshData();
}

// -------------------------------------------------------------------------
// LOOP
// -------------------------------------------------------------------------
void loop() {
  int tx, ty;

  if (get_touch_point(tx, ty)) {
    delay(150); // Debounce

    // Power Toggle Area (210, 165, 100, 50)
    if (tx > 210 && tx < 310 && ty > 165 && ty < 215) {
      systemEnabled = !systemEnabled;
      drawToggle(210, 165, 100, 50, systemEnabled);
      delay(250);
    }

    // Target (-) Button approx x=40 (20-80)
    if (tx > 20 && tx < 80 && ty > 170 && ty < 210) {
      targetTemp -= 0.5;
      drawTargetControl(targetTemp);

      prefs.begin("settings", false);
      prefs.putFloat("target", targetTemp);
      prefs.end();
      delay(150);
    }

    // Target (+) Button approx x=150 (140-200)
    if (tx > 140 && tx < 200 && ty > 170 && ty < 210) {
      targetTemp += 0.5;
      drawTargetControl(targetTemp);

      prefs.begin("settings", false);
      prefs.putFloat("target", targetTemp);
      prefs.end();
      delay(150);
    }

    // Unit Toggle Area (Main Temp Card: x=10-310, y=40-140)
    if (tx > 10 && tx < 310 && ty > 40 && ty < 140) {
      useFahrenheit = !useFahrenheit;
      refreshData();
      delay(250);
    }
  }

  // Temp Reading
  if (millis() - lastTempRead > tempReadInterval) {
    lastTempRead = millis();
    sensors.requestTemperatures();
    float t = sensors.getTempCByIndex(0);

    // Only update if changed significantly or if error recovered
    if (abs(t - currentTemp) > 0.1 || (currentTemp <= -100 && t > -100)) {
      currentTemp = t;
      drawMainTemp(currentTemp);
    }
  }

  // Logic
  if (currentTemp <= -100) {
    digitalWrite(RELAY_PIN, LOW);
    updateRelayIndicator(false);
  } else if (systemEnabled) {
    if (currentTemp < (targetTemp - hysteresis)) {
      if (digitalRead(RELAY_PIN) == LOW) {
        digitalWrite(RELAY_PIN, HIGH);
        updateRelayIndicator(true);
      }
    } else if (currentTemp > targetTemp) {
      if (digitalRead(RELAY_PIN) == HIGH) {
        digitalWrite(RELAY_PIN, LOW);
        updateRelayIndicator(false);
      }
    }
  } else {
    if (digitalRead(RELAY_PIN) == HIGH) {
      digitalWrite(RELAY_PIN, LOW);
      updateRelayIndicator(false);
    }
  }
}