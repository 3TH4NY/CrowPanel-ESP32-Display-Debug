# CrowPanel Touch Debug & Calibration Tool

A diagnostic and calibration tool for the CrowPanel ESP32 7-inch HMI Display (800x480, GT911 capacitive touch). Boots with an on-screen debug console showing each hardware initialization step, then runs a 9-target sequential touch accuracy test.

## Hardware

- **Board:** CrowPanel ESP32 7-inch HMI Display (ESP32-S3, LX6 dual-core)
- **Display:** 800x480 RGB TFT LCD
- **Touch:** GT911 capacitive touch controller (I2C address 0x14)
- **I/O Expander:** PCA9557 (required for GT911 power sequencing on 7-inch board)

## Required Libraries

| Library | Version | Purpose | Install |
|---------|---------|---------|---------|
| LovyanGFX | latest | Display driver + built-in GT911 touch | Arduino Library Manager: search "LovyanGFX" |
| PCA9557 | latest | I/O expander for touch power sequencing | Arduino Library Manager: search "PCA9557" |
| Wire | built-in | I2C communication | Included with ESP32 Arduino core |
| SPI | built-in | Used internally by LovyanGFX | Included with ESP32 Arduino core |

### Installing Libraries in Arduino IDE

1. Open **Sketch > Include Library > Manage Libraries**
2. Search for **LovyanGFX** and click Install
3. Search for **PCA9557** and click Install

### Board Setup

1. Open **Tools > Board > ESP32 Arduino** and select **ESP32S3 Dev Module**
2. Set the following board options:
   - **Flash Size:** 16MB
   - **Partition Scheme:** Default
   - **Upload Speed:** 921600
   - **USB CDC On Boot:** Enabled (for Serial monitor over USB)

## How to Run

1. Connect the CrowPanel display to your computer via USB-C
2. Open `TouchCalibration.ino` in Arduino IDE
3. Select the correct board and port
4. Click **Upload**
5. Open **Serial Monitor** at **115200 baud**

## What Happens

### Phase 1: Boot Debug Console

The display shows a terminal-style log as each component initializes. Each step prints OK (green) or FAIL (red). All output also appears on the Serial monitor.

```
CrowPanel Touch Debug Tool v1.0
--------------------------------
GPIO init (38,17,18,42)... OK
I2C bus (SDA=19,SCL=20)... OK
PCA9557 (I/O expander)... OK
I2C bus scan...
 Found 0x14 (GT911)
RGB display init... OK (800x480)
Backlight (GPIO 2)... OK
Touch (GT911)... OK
Touch read test... OK (no touch)

All systems OK!
Tap screen to begin calibration...
```

If any step fails, the tool stops and shows a hint about what to check. If the display initialized, tap to retry; otherwise it auto-retries after 5 seconds.

### Phase 2: Touch Calibration

After all init steps pass, tap the screen to start. Nine crosshair targets appear one at a time at screen corners, midpoints, and center. Tap each crosshair within 10 seconds.

- **Green circle** = PASS (touch within 15px of target)
- **Red circle** = FAIL (touch offset > 15px)
- **TIMEOUT** = no touch within 10 seconds

### Phase 3: Summary Screen

After all 9 targets, a results screen shows:

- Per-target deviation and pass/fail
- Max X/Y deviation across all targets
- Average deviation (Euclidean distance, in pixels)
- Overall PASS (all 9 within tolerance) or FAIL

Tap the screen to restart the entire process.

## Troubleshooting

| Issue | Likely Cause | Fix |
|-------|-------------|-----|
| "No I2C devices found" | I2C bus not connected | Check SDA (GPIO 19) and SCL (GPIO 20) wiring |
| "PCA9557 FAIL" | I/O expander not responding | Verify PCA9557 is soldered; check I2C pull-ups |
| "GT911 not found at 0x14" | Touch controller not powered | PCA9557 init must succeed first; check touch FPC ribbon cable |
| "Touch (GT911)... FAIL" | Driver crash during getTouch | Reset the board; check for I2C bus conflicts |
| Display stays black | RGB panel not initializing | Check board selection is ESP32S3 Dev Module; try lowering freq_write |
| Touch coordinates always 0,0 | GT911 initialized but not reading | GT911 may need a hardware reset - try power cycling the board |

## Key Pin Assignments (CrowPanel 7-inch)

| Function | GPIO |
|----------|------|
| I2C SDA (touch + PCA9557) | 19 |
| I2C SCL (touch + PCA9557) | 20 |
| Backlight PWM | 2 |
| RGB clock | 0 |
| H-sync | 39 |
| V-sync | 40 |
| DE (data enable) | 41 |
| RGB data [B0-B4, G0-G5, R0-R4] | 15,7,6,5,4,9,46,3,8,16,1,14,21,47,48,45 |

## Serial Monitor Output

All calibration data is also printed to Serial at 115200 baud. You can copy the results from the Serial monitor for record-keeping.
