# ESP32-2432S028R Temperature Controller (Auto-Calibration)

## 1. First Boot Calibration
The very first time you boot this code (or if you hold your finger on the screen while booting):
1.  A white screen will ask you to **"Touch the Green Square"**.
2.  Touch the **Top-Left** green square.
3.  Touch the **Bottom-Right** green square.
4.  The screen will say "Saved!".

This calibration is **permanently saved** to the chip's memory. You don't need to calibrate again unless you re-flash the "Erase Flash" or hold the screen on boot.

## 2. Wiring
*   **Relay Pin**: GPIO 27
*   **Sensor Pin**: GPIO 22

## 3. Install
1.  Open `tempcontroller.ino` in Arduino IDE.
2.  Install libraries: `TFT_eSPI`, `OneWire`, `DallasTemperature`.
3.  Set Upload Speed to **115200**.
4.  Upload.
