// Keypad Synth Rev A - bench bring-up test (ESP32-S3 SuperMini)
// Board: "ESP32S3 Dev Module", Tools > USB CDC On Boot: "Enabled" (REQUIRED - TX/RX are used for the amp),
// Serial Monitor 115200. Library: U8g2.
//
// On boot: I2C scan (OLED should answer at 0x3C), then a 0.3 s beep through the amp.
// Keypad: each of the 16 keys plays a note (C major pentatonic, S1 = lowest) while held.
// Pot: volume. OLED shows the 4x4 key grid, the pot level and the last key.
// Serial prints every key press/release and the pot value every 500 ms.
// If a key lights up the wrong square, the R/C wiring is swapped - check the edge pads for solder bridges first.

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <ESP_I2S.h>
#include <math.h>
#include <esp_system.h>

// Pins - straight from the KiCad netlist (keypad_synth_revA)
const int PIN_SDA = 9, PIN_SCL = 8;
const int ROW_PINS[4] = {2, 3, 4, 5};      // R1..R4 (driven LOW one at a time)
const int COL_PINS[4] = {6, 7, 13, 12};    // C1..C4 (INPUT_PULLUP)
const int PIN_POT = 10, PIN_POT_HI = 11;   // wiper on ADC1_CH9; GPIO11 powers the pot's high leg
const int PIN_AMP_LRC = 1, PIN_AMP_BCLK = 44, PIN_AMP_DIN = 43;   // BCLK = RX, DIN = TX
const int PIN_RGB = 48;
const bool POT_INSTALLED = false;     // pot removed for now (10-02): fixed volume, ignore the floating ADC pin

// GME128128-01-IIC needs the PIMORONI variant (the generic one wraps the picture by 32 px)
U8G2_SH1107_PIMORONI_128X128_F_HW_I2C oled(U8G2_R1,   // R1 = rotated 90 deg clockwise
                                           U8X8_PIN_NONE, PIN_SCL, PIN_SDA);
bool oledOk = false;

I2SClass amp;
const int RATE = 32000, BLOCK = 256;
volatile int noteKey = -1;       // key currently sounding (-1 = none)
volatile float volume = 0.1f;
volatile uint32_t audioBlocks = 0;   // climbs ~125/s while the audio task is alive
const char *resetWhy = "?";
const int SCALE[5] = {0, 2, 4, 7, 9};

float keyFreq(int k) {           // 16 keys over ~3 octaves of C pentatonic from C4
  int semis = SCALE[k % 5] + 12 * (k / 5);
  return 261.63f * powf(2.0f, semis / 12.0f);
}

void audioTask(void *) {
  static int16_t buf[BLOCK * 2];
  float phase = 0, env = 0;
  for (;;) {
    int k = noteKey;
    float step = (k >= 0 ? keyFreq(k) : 0) / RATE;
    for (int i = 0; i < BLOCK; i++) {
      env += ((k >= 0 ? 1.0f : 0.0f) - env) * 0.002f;          // soft attack/release, no clicks
      phase += step; if (phase >= 1) phase -= 1;
      float tri = 4 * fabsf(phase - 0.5f) - 1;                   // triangle wave
      int16_t s = (int16_t)(tri * env * volume * 32767);
      buf[i * 2] = s; buf[i * 2 + 1] = s;
    }
    amp.write((uint8_t *)buf, sizeof(buf));
    audioBlocks++;
  }
}

void i2cScan() {
  Wire.begin(PIN_SDA, PIN_SCL);
  Serial.print("I2C scan (SDA 9, SCL 8):");
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.printf(" 0x%02X", a); found++; if (a == 0x3C || a == 0x3D) oledOk = true; }
  }
  Serial.println(found ? "" : " nothing found - check OLED VCC/GND/SDA/SCL");
}

// Scan the passive matrix: one row LOW at a time, the other rows floating (INPUT) so two held keys can't short rows.
uint16_t scanKeys() {
  uint16_t bits = 0;
  for (int r = 0; r < 4; r++) {
    pinMode(ROW_PINS[r], OUTPUT);
    digitalWrite(ROW_PINS[r], LOW);
    delayMicroseconds(30);
    for (int c = 0; c < 4; c++)
      if (!digitalRead(COL_PINS[c])) bits |= 1 << (r * 4 + c);
    pinMode(ROW_PINS[r], INPUT);
  }
  return bits;
}

void drawOled(uint16_t keys, int pot, int last) {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0, 9, "Keypad Synth revA");
  for (int k = 0; k < 16; k++) {
    int x = 4 + (k % 4) * 22, y = 16 + (k / 4) * 22;
    if (keys & (1 << k)) oled.drawBox(x, y, 18, 18); else oled.drawFrame(x, y, 18, 18);
  }
  oled.drawFrame(100, 16, 20, 84);                                // pot bar
  int h = pot * 82 / 4095; oled.drawBox(101, 99 - h, 18, h);
  char line[24];
  snprintf(line, sizeof line, "pot %4d", pot); oled.drawStr(0, 114, line);
  if (last >= 0) { snprintf(line, sizeof line, "S%d", last + 1); oled.drawStr(64, 114, line); }
  static uint8_t spin = 0; const char *sp = "|/-\\";
  snprintf(line, sizeof line, "up %lus %c %s", millis() / 1000, sp[spin++ & 3], resetWhy); oled.drawStr(0, 126, line);
  oled.sendBuffer();
}

const char *resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_USB: return "USB";
    case ESP_RST_EXT: return "EXT";
    default: return "OTHER";
  }
}

void setup() {
  Serial.setTxTimeoutMs(0);   // never block when the USB host isn't reading
  Serial.begin(115200);
  delay(1500);
  resetWhy = resetReason();
  Serial.printf("\n=== Keypad Synth Rev A bring-up === last reset: %s\n", resetWhy);
  for (int c = 0; c < 4; c++) pinMode(COL_PINS[c], INPUT_PULLUP);
  for (int r = 0; r < 4; r++) pinMode(ROW_PINS[r], INPUT);
  pinMode(PIN_POT_HI, OUTPUT); digitalWrite(PIN_POT_HI, HIGH);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_POT, ADC_11db);

  i2cScan();
  if (oledOk) { oled.setBusClock(400000); oled.begin(); oled.setContrast(200); }
  else Serial.println("!! no OLED at 0x3C/0x3D - continuing without it");

  amp.setPins(PIN_AMP_BCLK, PIN_AMP_LRC, PIN_AMP_DIN);
  bool ok = amp.begin(I2S_MODE_STD, RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  Serial.printf("AMP I2S begin=%s  BCLK %d LRC %d DIN %d\n", ok ? "OK" : "FAILED", PIN_AMP_BCLK, PIN_AMP_LRC, PIN_AMP_DIN);
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, NULL, 5, NULL, 0);
  noteKey = 9; delay(300); noteKey = -1;                            // boot beep
  Serial.println("Press keys (S1..S16), turn the pot.\n");
}

void loop() {
  static uint16_t prevKeys = 0;
  static int last = -1;
  static uint32_t lastPrint = 0, lastDraw = 0;
  uint16_t keys = scanKeys();
  uint16_t changed = keys ^ prevKeys;
  for (int k = 0; k < 16; k++)
    if (changed & (1 << k)) {
      bool down = keys & (1 << k);
      Serial.printf("S%-2d (R%d C%d) %s\n", k + 1, k / 4 + 1, k % 4 + 1, down ? "down" : "up");
      if (down) last = k;
    }
  prevKeys = keys;
  noteKey = keys ? (keys & (1 << last) ? last : __builtin_ctz(keys)) : -1;

  int pot = analogRead(PIN_POT);
  if (!POT_INSTALLED) pot = 1600;    // ~0.2 volume
  volume = 0.02f + 0.5f * pot / 4095.0f;
  rgbLedWrite(PIN_RGB, keys ? 0 : pot / 64, keys ? 40 : 0, 0);
  static uint32_t loopStart = 0, loopMax = 0;
  uint32_t nowMs = millis();
  if (loopStart) loopMax = max(loopMax, nowMs - loopStart);
  loopStart = nowMs;
  if (millis() - lastPrint > 1000) {
    lastPrint = millis();
    Serial.printf("t=%lus pot %d keys %04X audioBlocks %lu loopMax %lums heap %u\n", millis() / 1000, pot, keys,
                  (unsigned long)audioBlocks, (unsigned long)loopMax, (unsigned)ESP.getFreeHeap());
    loopMax = 0;
  }
  if (oledOk && millis() - lastDraw > 40) { lastDraw = millis(); drawOled(keys, pot, last); }
  delay(5);
}
