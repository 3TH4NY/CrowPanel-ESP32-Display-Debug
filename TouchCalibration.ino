/**************************CrowPanel ESP32 Touch Debug & Calibration Tool***************
  Version     : 1.2
  Suitable for: CrowPanel ESP32 7-inch HMI Display (800x480, GT911 touch)
  Product link: https://www.elecrow.com/esp32-display-series-hmi-touch-screen.html
  Description : Boots with an on-screen debug console showing each init step,
                then runs a 9-target sequential touch calibration test.
                If any init step fails, shows failure details and won't proceed.
**************************************************************************************/

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <Wire.h>
// PCA9557 configured via raw I2C writes — no external library dependency
// Registers: 0x00=Input, 0x01=Output, 0x02=Polarity, 0x03=Config (0=out, 1=in)
#define PCA9557_ADDR 0x18

// --- Screen dimensions ---
#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 480

// --- Calibration config ---
#define TARGET_COUNT 9
#define TOLERANCE_PX 15
#define TARGET_RADIUS 30
#define TARGET_TIMEOUT 10000 // ms per target
#define INSET 50 // px from screen edge
#define TOUCH_POLL_MS 50

// --- Debug console config ---
#define CONSOLE_FONT_SIZE 1
#define LINE_HEIGHT 14

// --- Colors ---
#define COLOR_BG TFT_BLACK
#define COLOR_TEXT_OK TFT_GREEN
#define COLOR_TEXT_FAIL TFT_RED
#define COLOR_TEXT_WARN 0xFFE0 // yellow
#define COLOR_TEXT_SKIP 0x8410 // gray
#define COLOR_TEXT_NORM TFT_WHITE
#define COLOR_CROSSHAIR TFT_WHITE
#define COLOR_PASS TFT_GREEN
#define COLOR_FAIL TFT_RED

// ============================================================================
// LGFX display/touch config for CrowPanel 7-inch (ESP32-S3, GT911)
// Pin assignments from CrowPanel_70 block in the Elecrow repo gfx_conf.h
// ============================================================================
class LGFX : public lgfx::LGFX_Device {
public:
  lgfx::Bus_RGB _bus_instance;
  lgfx::Panel_RGB _panel_instance;
  lgfx::Light_PWM _light_instance;
  lgfx::Touch_GT911 _touch_instance;

  LGFX(void) {
    { auto cfg = _panel_instance.config();
      cfg.memory_width = SCREEN_WIDTH;
      cfg.memory_height = SCREEN_HEIGHT;
      cfg.panel_width = SCREEN_WIDTH;
      cfg.panel_height = SCREEN_HEIGHT;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      _panel_instance.config(cfg);
    }

    { auto cfg = _bus_instance.config();
      cfg.panel = &_panel_instance;
      cfg.pin_d0 = GPIO_NUM_15; cfg.pin_d1 = GPIO_NUM_7;
      cfg.pin_d2 = GPIO_NUM_6; cfg.pin_d3 = GPIO_NUM_5;
      cfg.pin_d4 = GPIO_NUM_4; cfg.pin_d5 = GPIO_NUM_9;
      cfg.pin_d6 = GPIO_NUM_46; cfg.pin_d7 = GPIO_NUM_3;
      cfg.pin_d8 = GPIO_NUM_8; cfg.pin_d9 = GPIO_NUM_16;
      cfg.pin_d10 = GPIO_NUM_1; cfg.pin_d11 = GPIO_NUM_14;
      cfg.pin_d12 = GPIO_NUM_21; cfg.pin_d13 = GPIO_NUM_47;
      cfg.pin_d14 = GPIO_NUM_48; cfg.pin_d15 = GPIO_NUM_45;
      cfg.pin_henable = GPIO_NUM_41;
      cfg.pin_vsync = GPIO_NUM_40;
      cfg.pin_hsync = GPIO_NUM_39;
      cfg.pin_pclk = GPIO_NUM_0;
      cfg.freq_write = 12000000;
      cfg.hsync_polarity = 0;
      cfg.hsync_front_porch = 40;
      cfg.hsync_pulse_width = 48;
      cfg.hsync_back_porch = 40;
      cfg.vsync_polarity = 0;
      cfg.vsync_front_porch = 1;
      cfg.vsync_pulse_width = 31;
      cfg.vsync_back_porch = 13;
      cfg.pclk_active_neg = 1;
      cfg.de_idle_high = 0;
      cfg.pclk_idle_high = 0;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    { auto cfg = _light_instance.config();
      cfg.pin_bl = GPIO_NUM_2;
      _light_instance.config(cfg);
      _panel_instance.light(&_light_instance);
    }

    { auto cfg = _touch_instance.config();
      cfg.x_min = 0;
      cfg.x_max = SCREEN_WIDTH - 1;
      cfg.y_min = 0;
      cfg.y_max = SCREEN_HEIGHT - 1;
      cfg.pin_int = -1;
      cfg.pin_rst = -1;
      cfg.bus_shared = false; // touch is on I2C_NUM_1, display on RGB — separate buses
      cfg.offset_rotation = 0;
      cfg.i2c_port = I2C_NUM_1;
      cfg.pin_sda = GPIO_NUM_19;
      cfg.pin_scl = GPIO_NUM_20;
      cfg.freq = 400000;
      cfg.i2c_addr = 0x14;
      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }

    setPanel(&_panel_instance);
  }
};

LGFX tft;

// ============================================================================
// Debug console — buffered so early steps show even before display init
// ============================================================================
#define MAX_CONSOLE_LINES 30

struct ConsoleLine {
  char text[60];
  uint16_t color;
};

ConsoleLine consoleBuf[MAX_CONSOLE_LINES];
int consoleLineCount = 0;
bool lastLineComplete = true; // true = last line was closed by debugPrintln
bool tftInitialized = false;
bool firstBoot = true; // true on first boot, false after first init attempt

void debugPrint(const char* msg, uint16_t color = COLOR_TEXT_NORM) {
  Serial.print(msg);
  if (consoleLineCount < MAX_CONSOLE_LINES) {
    if (!lastLineComplete && consoleLineCount > 0) {
      // Append to the incomplete last line
      int last = consoleLineCount - 1;
      size_t existing = strlen(consoleBuf[last].text);
      size_t remaining = sizeof(ConsoleLine::text) - 1 - existing;
      if (remaining > 0) {
        strncat(consoleBuf[last].text, msg, remaining);
      }
    } else {
      // Start a new line
      strncpy(consoleBuf[consoleLineCount].text, msg, sizeof(ConsoleLine::text) - 1);
      consoleBuf[consoleLineCount].text[sizeof(ConsoleLine::text) - 1] = '\0';
      consoleBuf[consoleLineCount].color = color;
      consoleLineCount++;
    }
    lastLineComplete = false;
  }
}

void debugPrintln(const char* msg, uint16_t color = COLOR_TEXT_NORM) {
  Serial.println(msg);
  if (consoleLineCount < MAX_CONSOLE_LINES) {
    if (!lastLineComplete && consoleLineCount > 0) {
      // Append to the incomplete last line (e.g., "Backlight... " + "OK")
      int last = consoleLineCount - 1;
      size_t existing = strlen(consoleBuf[last].text);
      size_t remaining = sizeof(ConsoleLine::text) - 1 - existing;
      if (remaining > 0) {
        strncat(consoleBuf[last].text, msg, remaining);
      }
      // Update color to the result color (OK=green, FAIL=red)
      consoleBuf[last].color = color;
    } else {
      // Start a new line
      strncpy(consoleBuf[consoleLineCount].text, msg, sizeof(ConsoleLine::text) - 1);
      consoleBuf[consoleLineCount].text[sizeof(ConsoleLine::text) - 1] = '\0';
      consoleBuf[consoleLineCount].color = color;
      consoleLineCount++;
    }
    lastLineComplete = true;
  }
}

void debugStep(const char* label, bool ok, const char* failHint = nullptr) {
  char buf[80];
  snprintf(buf, sizeof(buf), "%s%s", label, ok ? "OK" : "FAIL");
  debugPrintln(buf, ok ? COLOR_TEXT_OK : COLOR_TEXT_FAIL);
  if (!ok && failHint) {
    debugPrintln(failHint, COLOR_TEXT_FAIL);
  }
}

// Render the buffered console to the display (call after tft.begin())
void renderConsole() {
  tft.fillScreen(COLOR_BG);
  tft.setTextSize(CONSOLE_FONT_SIZE);

  int startLine = 0;
  int maxLines = SCREEN_HEIGHT / LINE_HEIGHT;
  if (consoleLineCount > maxLines) {
    startLine = consoleLineCount - maxLines;
  }

  for (int i = startLine; i < consoleLineCount; i++) {
    int y = (i - startLine) * LINE_HEIGHT;
    tft.setTextColor(consoleBuf[i].color, COLOR_BG);
    tft.setCursor(4, y);
    tft.print(consoleBuf[i].text);
  }
}

// ============================================================================
// I2C scan
// ============================================================================
int i2cScan(TwoWire& wire) {
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    wire.beginTransmission(addr);
    if (wire.endTransmission() == 0) {
      found++;
      char buf[40];
      snprintf(buf, sizeof(buf), "  Found 0x%02X%s", addr,
               addr == 0x14 ? " (GT911)" : "");
      debugPrintln(buf, COLOR_TEXT_NORM);
    }
  }
  return found;
}

// ============================================================================
// Wait for a touch (with optional timeout). Returns true if touched.
// ============================================================================
bool waitForTouch(uint16_t& tx, uint16_t& ty, unsigned long timeoutMs = 0) {
  unsigned long start = millis();
  while (true) {
    if (tft.getTouch(&tx, &ty)) {
      // Debounce: wait for release to avoid double-fire
      delay(50);
      while (tft.getTouch(&tx, &ty)) { delay(20); }
      return true;
    }
    if (timeoutMs > 0 && (millis() - start) >= timeoutMs) {
      return false;
    }
    delay(TOUCH_POLL_MS);
  }
}

// ============================================================================
// Phase 1: Boot & Initialization
// Returns true if all systems OK
// ============================================================================
bool runInitSequence() {
  // On retry/restart, re-calling tft.begin() reinitializes the display
  // and touch — no explicit deinit needed (LGFX_Device has no end()).
  // The previous run already released Wire (I2C_NUM_0) above.

  consoleLineCount = 0;
  lastLineComplete = true;
  tftInitialized = false;
  bool allOK = true;

  debugPrintln("CrowPanel Touch Debug Tool v1.2", COLOR_TEXT_NORM);
  debugPrintln("--------------------------------", COLOR_TEXT_NORM);

  // Step 1: GPIO init (isolate peripherals)
  {
    pinMode(38, OUTPUT); digitalWrite(38, LOW);
    pinMode(17, OUTPUT); digitalWrite(17, LOW);
    pinMode(18, OUTPUT); digitalWrite(18, LOW);
    pinMode(42, OUTPUT); digitalWrite(42, LOW);
    debugStep("GPIO init (38,17,18,42)... ", true);
  }

  // Step 2: I2C bus init
  // Wire (I2C_NUM_0) is used for PCA9557 + scan before LovyanGFX claims
  // I2C_NUM_1 on the same pins. Wire is ended at the end of this function
  // before tft.begin(), so no manual Wire.end() is needed here — even on
  // retry the previous run already released Wire (I2C_NUM_0).
  {
    Wire.begin(19, 20);
    debugStep("I2C bus (SDA=19,SCL=20)... ", true);
  }

  // Step 3: PCA9557 init (required for GT911 power on 7-inch board)
  // Configured via raw I2C register writes — no PCA9557 library dependency
  {
    bool pcaOK = true;
    uint8_t pcaAddr = PCA9557_ADDR;

    // Verify PCA9557 responds on I2C (try default 0x18, then alternate 0x19)
    Wire.beginTransmission(pcaAddr);
    if (Wire.endTransmission() != 0) {
      Wire.beginTransmission(0x19);
      if (Wire.endTransmission() != 0) {
        pcaOK = false;
      } else {
        pcaAddr = 0x19;
      }
    }

    if (pcaOK) {
      // Reset: config all-output (reg 0x03 = 0x00), output low (reg 0x01 = 0x00)
      Wire.beginTransmission(pcaAddr);
      Wire.write(0x03); Wire.write(0x00); // Config: all outputs
      Wire.endTransmission();
      Wire.beginTransmission(pcaAddr);
      Wire.write(0x01); Wire.write(0x00); // Output: all low
      Wire.endTransmission();

      delay(20);

      // IO0 HIGH = enable GT911 touch power
      Wire.beginTransmission(pcaAddr);
      Wire.write(0x01); Wire.write(0x01); // Output: IO0=HIGH, IO1=LOW
      Wire.endTransmission();

      delay(100);

      // IO1 as input (GT911 interrupt pin): set bit 1 in config register
      Wire.beginTransmission(pcaAddr);
      Wire.write(0x03); Wire.write(0x02); // Config: IO0=output, IO1=input
      Wire.endTransmission();
    }

    debugStep("PCA9557 (I/O expander)... ", pcaOK,
              " Check PCA9557 wiring: SDA=19, SCL=20");
    if (!pcaOK) allOK = false;
  }

  // Step 4: I2C bus scan
  {
    debugPrintln("I2C bus scan...", COLOR_TEXT_NORM);
    int found = i2cScan(Wire);

    bool gt911Found = false;
    Wire.beginTransmission(0x14);
    if (Wire.endTransmission() == 0) {
      gt911Found = true;
    }

    if (found == 0) {
      debugPrintln("  No I2C devices found!", COLOR_TEXT_FAIL);
      debugPrintln("  Check I2C wiring: SDA=19, SCL=20", COLOR_TEXT_FAIL);
      allOK = false;
    } else if (!gt911Found) {
      debugPrintln("  GT911 not found at 0x14!", COLOR_TEXT_FAIL);
      debugPrintln("  Check touch FPC connector & PCA9557", COLOR_TEXT_FAIL);
      allOK = false;
    }
  }

  // Release Wire (I2C_NUM_0) before LovyanGFX initializes I2C_NUM_1
  // on the same physical pins — prevents dual-controller bus contention.
  Wire.end();

  // Step 5: Display init
  {
    debugPrint("RGB display init... ", COLOR_TEXT_NORM);
    bool displayOK = tft.begin();
    if (displayOK) {
      tft.fillScreen(COLOR_BG);
      tftInitialized = true;
      char buf[30];
      snprintf(buf, sizeof(buf), "OK (%dx%d)", SCREEN_WIDTH, SCREEN_HEIGHT);
      debugPrintln(buf, COLOR_TEXT_OK);
      renderConsole();
    } else {
      debugPrintln("FAIL", COLOR_TEXT_FAIL);
      debugPrintln(" Display driver returned error", COLOR_TEXT_FAIL);
      allOK = false;
    }
  }

  // Step 6: Backlight init (display is on, so user can see from here on)
  {
    ledcAttach(2, 300, 8);  // pin 2, 300Hz, 8-bit resolution (Arduino 3.x API)
    ledcWrite(2, 200); // ~78% brightness
    debugStep("Backlight (GPIO 2)... ", true);
    if (tftInitialized) renderConsole(); // refresh with new line
  }

  // Step 7: Touch controller verify
  {
    if (allOK && tftInitialized) {
      uint16_t dummyX, dummyY;
      bool touched = tft.getTouch(&dummyX, &dummyY);
      debugStep("Touch (GT911)... ", true);

      // Step 8: Touch read test
      debugPrint("Touch read test... ", COLOR_TEXT_NORM);
      if (touched) {
        debugPrintln("TOUCH DETECTED", COLOR_TEXT_WARN);
      } else {
        debugPrintln("OK (no touch)", COLOR_TEXT_OK);
      }
    } else {
      debugPrintln("Touch (GT911)... SKIPPED", COLOR_TEXT_SKIP);
      debugPrintln("Touch read test... SKIPPED", COLOR_TEXT_SKIP);
    }
    if (tftInitialized) renderConsole();
  }

  firstBoot = false;
  return allOK;
}

// ============================================================================
// Calibration target definitions (must be declared before drawTarget)
// ============================================================================
struct Target {
  uint16_t x, y;
  const char* label;
};

const Target targets[TARGET_COUNT] = {
  {INSET, INSET, "Top-left"},
  {SCREEN_WIDTH / 2, INSET, "Top-center"},
  {SCREEN_WIDTH - INSET, INSET, "Top-right"},
  {INSET, SCREEN_HEIGHT / 2, "Mid-left"},
  {SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, "Center"},
  {SCREEN_WIDTH - INSET, SCREEN_HEIGHT / 2, "Mid-right"},
  {INSET, SCREEN_HEIGHT - INSET, "Bottom-left"},
  {SCREEN_WIDTH / 2, SCREEN_HEIGHT - INSET, "Bottom-center"},
  {SCREEN_WIDTH - INSET, SCREEN_HEIGHT - INSET, "Bottom-right"},
};

struct TargetResult {
  int16_t devX, devY;
  bool pass;
  bool timeout;
};

TargetResult results[TARGET_COUNT];

// ============================================================================
// Draw a crosshair target
// ============================================================================
void drawTarget(uint16_t cx, uint16_t cy, int targetNum) {
  tft.fillScreen(COLOR_BG);

  // Label
  tft.setTextSize(2);
  tft.setTextColor(COLOR_TEXT_NORM, COLOR_BG);
  tft.setCursor(10, 10);
  tft.printf("Target %d/%d", targetNum + 1, TARGET_COUNT);

  // Target description
  tft.setTextSize(1);
  tft.setCursor(10, 30);
  tft.printf("Tap the crosshair (%s)", targets[targetNum].label);

  // Crosshair lines
  tft.drawLine(cx - TARGET_RADIUS, cy, cx + TARGET_RADIUS, cy, COLOR_CROSSHAIR);
  tft.drawLine(cx, cy - TARGET_RADIUS, cx, cy + TARGET_RADIUS, COLOR_CROSSHAIR);

  // Circle (double-draw for thickness)
  tft.drawCircle(cx, cy, TARGET_RADIUS, COLOR_CROSSHAIR);
  tft.drawCircle(cx, cy, TARGET_RADIUS - 1, COLOR_CROSSHAIR);

  Serial.printf("Target %d/%d at (%d, %d) - %s - waiting for tap...\n",
                targetNum + 1, TARGET_COUNT, cx, cy, targets[targetNum].label);
}

// ============================================================================
// Phase 2: Sequential Target Calibration
// ============================================================================
void runCalibration() {
  // Wait for user to tap to begin
  renderConsole();
  debugPrintln("", COLOR_TEXT_NORM);
  debugPrintln("All systems OK!", COLOR_TEXT_OK);
  debugPrintln("Tap screen to begin calibration...", COLOR_TEXT_NORM);
  renderConsole();

  uint16_t tx, ty;
  waitForTouch(tx, ty, 0); // no timeout - wait forever

  for (int i = 0; i < TARGET_COUNT; i++) {
    drawTarget(targets[i].x, targets[i].y, i);

    uint16_t touchX, touchY;
    bool touched = waitForTouch(touchX, touchY, TARGET_TIMEOUT);

    if (!touched) {
      // Timeout
      results[i].timeout = true;
      results[i].devX = 999;
      results[i].devY = 999;
      results[i].pass = false;

      // Show timeout on screen
      tft.setTextSize(2);
      tft.setTextColor(COLOR_TEXT_WARN, COLOR_BG);
      int16_t tyPos = targets[i].y + TARGET_RADIUS + 10;
      if (tyPos + 20 > SCREEN_HEIGHT) tyPos = targets[i].y - TARGET_RADIUS - 25;
      tft.setCursor(targets[i].x - 40, tyPos);
      tft.print("TIMEOUT");

      Serial.printf("  TIMEOUT\n");
      delay(1000);

    } else {
      // Touch received - compute deviation
      results[i].timeout = false;
      results[i].devX = (int16_t)touchX - (int16_t)targets[i].x;
      results[i].devY = (int16_t)touchY - (int16_t)targets[i].y;
      results[i].pass = (abs(results[i].devX) <= TOLERANCE_PX &&
                         abs(results[i].devY) <= TOLERANCE_PX);

      // Draw result at touch point
      uint16_t color = results[i].pass ? COLOR_PASS : COLOR_FAIL;
      tft.fillCircle(touchX, touchY, 8, color);

      // Draw deviation text near target
      tft.setTextSize(1);
      tft.setTextColor(color, COLOR_BG);
      char devBuf[40];
      snprintf(devBuf, sizeof(devBuf), "dev:%+d,%+d %s",
               results[i].devX, results[i].devY,
               results[i].pass ? "PASS" : "FAIL");
      int16_t textX = (int16_t)targets[i].x - 40;
      int16_t textY = (int16_t)targets[i].y + TARGET_RADIUS + 10;
      if (textX < 0) textX = 5;
      if (textX + 100 > SCREEN_WIDTH) textX = SCREEN_WIDTH - 105;
      if (textY + 12 > SCREEN_HEIGHT) textY = targets[i].y - TARGET_RADIUS - 20;
      tft.setCursor(textX, textY);
      tft.print(devBuf);

      Serial.printf("  Touched (%d, %d) dev: %+d, %+d -> %s\n",
                     touchX, touchY, results[i].devX, results[i].devY,
                     results[i].pass ? "PASS" : "FAIL");

      delay(500);
    }
  }
}

// ============================================================================
// Phase 3: Summary Screen
// ============================================================================
void showSummary() {
  tft.fillScreen(COLOR_BG);
  tft.setTextSize(2);
  tft.setTextColor(COLOR_TEXT_NORM, COLOR_BG);
  tft.setCursor(10, 10);
  tft.print("Calibration Results");
  tft.setTextSize(1);

  Serial.println("\n===== CALIBRATION RESULTS =====");

  // Calculate stats in a single clean pass
  int passedCount = 0;
  int maxDevX = 0, maxDevY = 0;
  float totalDist = 0;
  int nonTimeoutCount = 0;

  for (int i = 0; i < TARGET_COUNT; i++) {
    if (results[i].pass) passedCount++;
    if (!results[i].timeout) {
      nonTimeoutCount++;
      int absX = abs(results[i].devX);
      int absY = abs(results[i].devY);
      if (absX > maxDevX) maxDevX = absX;
      if (absY > maxDevY) maxDevY = absY;
      totalDist += sqrtf((float)(results[i].devX * results[i].devX +
                                  results[i].devY * results[i].devY));
    }
  }

  // Per-target rows
  int y = 40;
  int maxRows = (SCREEN_HEIGHT - 100) / LINE_HEIGHT; // leave room for summary
  for (int i = 0; i < TARGET_COUNT && i < maxRows; i++) {
    uint16_t color = results[i].pass ? COLOR_PASS : COLOR_FAIL;
    tft.setTextColor(color, COLOR_BG);
    tft.setCursor(10, y);

    if (results[i].timeout) {
      tft.printf("%d. (%d,%d) TIMEOUT FAIL",
                 i + 1, targets[i].x, targets[i].y);
      Serial.printf("%d. (%d,%d) -> TIMEOUT -> FAIL\n",
                    i + 1, targets[i].x, targets[i].y);
    } else {
      tft.printf("%d. (%d,%d) dev:%+d,%+d %s",
                 i + 1, targets[i].x, targets[i].y,
                 results[i].devX, results[i].devY,
                 results[i].pass ? "PASS" : "FAIL");
      Serial.printf("%d. (%d,%d) dev: %+d, %+d -> %s\n",
                    i + 1, targets[i].x, targets[i].y,
                    results[i].devX, results[i].devY,
                    results[i].pass ? "PASS" : "FAIL");
    }

    y += LINE_HEIGHT;
  }

  // Summary stats
  y += LINE_HEIGHT;
  tft.setTextColor(COLOR_TEXT_NORM, COLOR_BG);
  tft.setCursor(10, y);
  tft.printf("Max deviation: X=%d, Y=%d", maxDevX, maxDevY);
  y += LINE_HEIGHT;
  tft.setCursor(10, y);
  if (nonTimeoutCount > 0) {
    tft.printf("Avg deviation: %.1fpx", totalDist / nonTimeoutCount);
  } else {
    tft.print("Avg deviation: N/A (all timed out)");
  }
  y += LINE_HEIGHT;
  tft.setCursor(10, y);
  tft.printf("Targets: %d/%d passed", passedCount, TARGET_COUNT);

  // Overall result
  y += LINE_HEIGHT * 2;
  bool overallPass = (passedCount == TARGET_COUNT);
  tft.setTextSize(2);
  tft.setTextColor(overallPass ? COLOR_PASS : COLOR_FAIL, COLOR_BG);
  tft.setCursor(10, y);
  tft.printf("OVERALL: %s", overallPass ? "PASS" : "FAIL");

  // Tap to restart
  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT_NORM, COLOR_BG);
  tft.setCursor(10, SCREEN_HEIGHT - 20);
  tft.print("Tap to restart");

  // Serial summary
  Serial.printf("Max deviation: X=%d, Y=%d\n", maxDevX, maxDevY);
  if (nonTimeoutCount > 0) {
    Serial.printf("Avg deviation: %.1fpx\n", totalDist / nonTimeoutCount);
  }
  Serial.printf("Targets: %d/%d passed\n", passedCount, TARGET_COUNT);
  Serial.printf("OVERALL: %s\n", overallPass ? "PASS" : "FAIL");
  Serial.println("================================");
}

// ============================================================================
// Main
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("CrowPanel Touch Debug Tool v1.2");
  Serial.println("================================");

  // Run init sequence with retry loop (not recursion)
  while (true) {
    memset(results, 0, sizeof(results));
    bool allOK = runInitSequence();

    if (!allOK) {
      debugPrintln("", COLOR_TEXT_NORM);
      debugPrintln("Init FAILED - see above for details", COLOR_TEXT_FAIL);

      if (tftInitialized) {
        debugPrintln("Tap to retry... (or wait 10s)", COLOR_TEXT_NORM);
        renderConsole();
        uint16_t tx, ty;
        waitForTouch(tx, ty, 10000); // wait for touch or 10s timeout
      } else {
        debugPrintln("Auto-retry in 5s...", COLOR_TEXT_NORM);
        delay(5000); // no display/touch available, just wait and retry
      }
      delay(300);
      continue; // restart the loop
    }

    // Init OK - proceed to calibration
    runCalibration();
    showSummary();
    return; // exit setup, enter loop() for tap-to-restart
  }
}

void loop() {
  // After summary, wait for tap to restart the whole process
  uint16_t tx, ty;
  if (tft.getTouch(&tx, &ty)) {
    delay(300); // debounce
    // Re-run everything
    memset(results, 0, sizeof(results));
    runInitSequence();
    runCalibration();
    showSummary();
  }
  delay(TOUCH_POLL_MS);
}
