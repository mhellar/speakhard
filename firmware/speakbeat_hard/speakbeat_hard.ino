/*
  SPEAK//HARD - the industrial fork of SPEAK//BEAT (keypad_synth_revA)
  ===========================================================================================================
  Same instrument, harder body: industrial drum kit (driven kick, metal snare, 808-style metal hats, ANVIL/PIPE
  FM clangs, SLAM, 808 sub) plus CHUG, a palm-muted drop-tuned power-chord stab on drum pad 11 that follows the
  chord progression. Kick/808/slam duck the voices (sidechain pump), a soft glue compressor sits on the master,
  MEGA (megaphone voice) replaces CHOIR, the vocoder carrier is driven, progressions are DROP/DREAD/EPIC/MACHINE,
  the BRUTE voice replaces OLD LADY, and the default is 92 BPM. The original speakbeat sketch is untouched.

  Grown out of handheld_robodj (the deck, scratching, voice FX, vocoder and sequencer come from there).
  SAM (Software Automatic Mouth, C core from earlephilhower/ESP8266SAM) and the Talkie LPC vocabularies
  (Peter Knight / Armin Joachimsmeyer) are bundled in this folder; both GPL v3, so this sketch is too.

  KEYPAD (keypad hanging below the board, S1 top-left)
     [MODE ] [ pad 0 ] [ pad 1 ] [ pad 2 ]
     [FX   ] [ pad 3 ] [ pad 4 ] [ pad 5 ]
     [SEQ  ] [ pad 6 ] [ pad 7 ] [ pad 8 ]
     [SHIFT] [ pad 9 ] [ pad 10] [ pad 11]
  Left column: tap for the quick action, HOLD it and the screen shows what the 12 pads do.
     MODE  tap = next mode (SAM > TALK > BYTE > DRUM)      hold: modes, volume, talk bank, SAM voice
     FX    tap = next voice FX                              hold: pick FX, momentary STUTTER/REVERSE/TAPE/UNDER
     SEQ   tap = REC on/off                                 hold: play/stop, groove, auto, BPM, mutate, clears
     SHIFT tap = PLAY/STOP                                  hold: per-mode extras (scratch, LPC bends, bytebeat
                                                                  params, drum mutes)
  Pads by mode:
     SAM   12 positive words. tap = say it, hold = scratch it
     TALK  12 Speak & Spell / arcade LPC words (3 banks). tap = say it, hold = scrub-scratch the LPC frames
     BYTE  12 bytebeat formulas. tap = start/stop (latched), hold = plays only while held
     DRUM  12 drums (KICK SNARE CLAP HAT / OPEN 808 ANVIL PIPE / SLAM ZAP CRASH CHUG). tap = hit, hold = roll
  With REC on, everything you play is quantized into the 16 steps.

  Board: "ESP32S3 Dev Module", USB CDC On Boot: Enabled, PSRAM: enabled
  (arduino-cli FQBN esp32:esp32:esp32s3:CDCOnBoot=cdc,PSRAM=enabled). Libraries: U8g2. ESP32 core 3.x.
*/

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <ESP_I2S.h>
#include "esp_dsp.h"
#include "SamData.h"
#include "sam.h"
#include "reciter.h"
#include "Vocab_US_Large.h"
#include "Vocab_AstroBlaster.h"
#include "Vocab_Soundbites.h"
#include "Vocab_Toms_Diner.h"

// ---------------- pins (keypad_synth_revA netlist) ----------------
#define PIN_SDA   9
#define PIN_SCL   8
#define AMP_LRC   1
#define AMP_BCLK  44
#define AMP_DIN   43
#define PIN_RGB   48
const int ROW_PINS[4] = {2, 3, 4, 5};
const int COL_PINS[4] = {6, 7, 13, 12};

#define STATUS_LOG 0      // 1 = print a status line every 2 s (only needed for debugging)
#define FS     22050
#define INV_FS (1.0f / FS)
#define BLOCK  128

const float VOLS[6] = {0.05f, 0.09f, 0.14f, 0.2f, 0.28f, 0.38f};
volatile int volIdx = 2;

// ---------------- types first (Arduino auto-prototypes) ----------------
struct Word { const char* text; uint32_t off, len; };
struct Voice { const char* name; uint8_t speed, pitch, throat, mouth; };
struct Step { int8_t word; uint8_t src, flags; int8_t byteF; uint16_t drum; bool skip; };
struct LpcFrame { uint8_t energy, period; int16_t k1, k2; int8_t k[8]; };
struct TalkWord { const char* name; const uint8_t* data; uint32_t first, n; };
struct PercDef { const char* name; uint8_t wave; float f0, f1, ptime, dec, gain, noise, drive; };
struct ModKey { bool down, used; uint32_t tDown; };

enum { M_SAM, M_TALK, M_BYTE, M_DRUM, M_COUNT };
const char* MODE_NAMES[] = {"SAM", "TALK", "BYTE", "DRUM"};
enum { K_MODE, K_FX, K_SEQ, K_SHIFT };
const char* MOD_NAMES[] = {"MODE", "FX", "SEQ", "SHIFT"};
enum { SRC_SAM, SRC_TALK };
enum { SF_SCRATCH = 1, SF_REV = 2 };
enum { FX_CLEAN, FX_CRUSH, FX_ECHO, FX_GRAIN, FX_SWARM, FX_VOX, FX_MEGA, FX_COUNT };
const char* FX_NAMES[] = {"CLEAN", "CRUSH", "ECHO", "GRAIN", "SWARM", "VOX", "MEGA"};
enum { SC_BABY, SC_CHIRP, SC_TRANS, SC_TEAR, SC_FLARE, SC_COUNT };
const char* SC_NAMES[] = {"BABY", "CHIRP", "TRANSFM", "TEAR", "FLARE"};
#define BYTE_STOP 99

const Voice VOICES[] = {
  {"ROBOT", 92, 60, 190, 190}, {"SAM", 72, 64, 128, 128}, {"ELF", 72, 64, 110, 160},
  {"E.T.", 100, 64, 150, 200}, {"BRUTE", 84, 104, 100, 150},
};
// poetic, positive, short enough to hit like a drum (the last one is the long line to chop up)
Word bank[12] = {
  {"AWAKEN"}, {"STARLIGHT"}, {"YEAH"}, {"BREATHE"}, {"GRAVITY"}, {"INFINITE"},
  {"ECHOES"}, {"WE RISE"}, {"HEARTBEAT"}, {"FREQUENCY"}, {"THUNDER"}, {"WE ARE MADE OF STARS"},
};

#define NBANKS 4
TalkWord talk[NBANKS][12] = {
  {{"READY", sp4_READY}, {"GO", sp2_GO}, {"IGNITE", sp5_IGNITE}, {"LAUNCH", sp5_LAUNCH},
   {"BOOST", sp5_BOOST}, {"POWER", sp2_POWER}, {"ACTION", sp4_ACTION}, {"CONNECT", sp2_CONNECT},
   {"GREAT", sp4_GREAT}, {"UNLIMITED", sp3_UNLIMITED}, {"FREEDOM", sp5_FREEDOM}, {"ONE SMALL STEP", spONE_SMALL_STEP}},
  {{"LIGHT", sp4_LIGHT}, {"RAIN", sp3_RAIN}, {"WIND", sp3_WIND}, {"SNOW", sp3_SNOW},
   {"MIST", sp3_MIST}, {"CALM", sp5_CALM}, {"GLIDE", sp4_GLIDE}, {"ALOFT", sp3_ALOFT},
   {"HIGH", sp3_HIGH}, {"CLEAR", sp3_CLEAR}, {"TRUE", sp4_TRUE}, {"GREEN", sp3_GREEN}},
  {{"ONE", sp3_ONE}, {"TWO", sp3_TWO}, {"THREE", sp3_THREE}, {"FOUR", sp3_FOUR},
   {"FIVE", sp3_FIVE}, {"SIX", sp3_SIX}, {"SEVEN", sp3_SEVEN}, {"EIGHT", sp3_EIGHT},
   {"PLUS", sp3_PLUS}, {"EQUALS", sp3_EQUALS}, {"MILLION", sp3_MILLION}, {"TOM'S DINER", spDINER}},
  {{"ALPHA", sp4_ALPHA}, {"BRAVO", sp4_BRAVO}, {"CHARLIE", sp4_CHARLIE}, {"DELTA", sp4_DELTA},
   {"ECHO", sp4_ECHO}, {"FOXTROT", sp4_FOXTROT}, {"TANGO", sp4_TANGO}, {"ZULU", sp4_ZULU},
   {"ROGER", sp4_ROGER}, {"AFFIRMATIVE", sp4_AFFIRMATIVE}, {"OPERATIONAL", operational}, {"HMMM BEER", spHMMM_BEER}},
};
const char* BANK_NAMES[NBANKS] = {"LIFTOFF", "SKY", "MATH", "PHONETIC"};

// industrial kit. drive > 0 = the hit goes through softclip after its envelope (hits harder, not fizzier).
// W_METAL: f0 scales the six 808 cymbal squares. W_FM: f0 carrier, f1 modulator ratio, noise = FM index.
// W_CHUG: power chord on chugHz (follows the chord progression), f0 unused.
// Levels balanced offline (drumsim.py port of this engine, 100 ms RMS): kick/snare ~-9 dB audible, hats ~-21.
enum { W_SINE, W_KICK, W_SNR, W_CLAP, W_METAL, W_FM, W_SLAM, W_CHUG };
const PercDef PERC[12] = {
  // name     wave     f0     f1     ptime   dec    gain   noise  drive
  {"KICK",  W_KICK,  190,   52,    0.045f, 0.40f, 0.95f, 0.6f,  2.2f},
  {"SNARE", W_SNR,   210,   165,   0.025f, 0.20f, 0.62f, 0.9f,  2.0f},
  {"CLAP",  W_CLAP,  0,     0,     0.01f,  0.16f, 0.72f, 0,     1.6f},
  {"HAT",   W_METAL, 1.6f,  0,     0.01f,  0.035f,0.67f, 0,     0},
  {"OPEN",  W_METAL, 1.6f,  0,     0.01f,  0.28f, 0.60f, 0,     0},
  {"808",   W_KICK,  110,   50,    0.09f,  1.00f, 0.85f, 0.2f,  1.7f},
  {"ANVIL", W_FM,    380,   2.76f, 0.06f,  0.55f, 0.40f, 1.8f,  0},
  {"PIPE",  W_FM,    520,   1.41f, 0.12f,  0.32f, 0.40f, 0.7f,  0},
  {"SLAM",  W_SLAM,  90,    48,    0.05f,  0.26f, 0.72f, 0,     3.0f},
  {"ZAP",   W_SINE,  2600,  70,    0.025f, 0.18f, 0.55f, 0,     0},
  {"CRASH", W_METAL, 1.0f,  0,     0.01f,  1.10f, 0.58f, 0,     0},
  {"CHUG",  W_CHUG,  0,     0,     0.05f,  0.20f, 0.55f, 0,     0},
};
enum { D_KICK, D_SNARE, D_CLAP, D_HAT, D_OPEN, D_808, D_ANVIL, D_PIPE, D_SLAM, D_ZAP, D_CRASH, D_CHUG };
const uint16_t DUCK_MASK = (1 << D_KICK) | (1 << D_808) | (1 << D_SLAM);

const char* BYTE_NAMES[12] = {"SIERPINSKI", "CRYSTAL", "FORTY-TWO", "VIZNUT", "STAIRS", "CHIP ARP",
                              "SKURK", "XPANSIVE", "VISY", "NOISEWALL", "TANGLE", "TEJEEZ"};
const int8_t BYTE_A[12] = {8, 5, 10, 7, 8, 9, 6, 8, 9, 10, 9, 8};
const int8_t BYTE_B[12] = {8, 8, 8, 10, 8, 8, 16, 9, 13, 15, 7, 11};

// Serial log that never blocks: with USB plugged in but no serial monitor open, the USB-CDC TX buffer fills
// and a plain Serial.printf hangs (screen froze ~3 s after boot, audio task stalled). Lines that don't fit are dropped.
void logf(const char* fmt, ...) {
  char b[160];
  va_list ap; va_start(ap, fmt);
  int n = vsnprintf(b, sizeof b, fmt, ap);
  va_end(ap);
  if (n <= 0) return;
  n = min(n, (int)sizeof b - 1);
  if (Serial.availableForWrite() >= n) Serial.write((const uint8_t*)b, n);
}

U8G2_SH1107_PIMORONI_128X128_F_HW_I2C oled(U8G2_R1, U8X8_PIN_NONE, PIN_SCL, PIN_SDA);   // R1 = 90 deg clockwise
I2SClass amp;

void drawFrame();

// =====================================================================
// SAM -> sample pool (from Robo DJ)
// =====================================================================
SamData* samdata;
int8_t* pool = nullptr;
uint32_t poolSize = 0, poolPos = 0;
volatile bool bankBusy = true;
volatile int voiceIdx = 0;
int renderProgress = 0;

static void samByte(void*, unsigned char b) { if (poolPos < poolSize) pool[poolPos++] = (int8_t)((int)b - 128); }

bool sayToPool(const char* text, const Voice& v) {
  char input[256];
  int n = 0;
  for (; text[n] && n < 200; n++) input[n] = toupper((int)text[n]);
  input[n] = 0;
  strcat(input, "[");
  samdata = new SamData;
  if (!samdata) return false;
  EnableSingmode(0);
  SetSpeed(v.speed); SetPitch(v.pitch); SetThroat(v.throat); SetMouth(v.mouth);
  bool ok = TextToPhonemes(input);
  if (ok) { SetInput(input); SAMMain(samByte, nullptr); }
  delete samdata;
  return ok;
}

void renderBank(int vi) {
  bankBusy = true;
  delay(30);
  poolPos = 0;
  for (int i = 0; i < 12; i++) {
    renderProgress = i;
    drawFrame();
    uint32_t start = poolPos;
    bank[i].off = start; bank[i].len = 0;
    if (poolSize - poolPos < FS / 2) continue;
    if (!sayToPool(bank[i].text, VOICES[vi])) { poolPos = start; continue; }
    uint32_t a = start, b = poolPos;
    while (a < b && abs(pool[a]) < 4) a++;
    while (b > a && abs(pool[b - 1]) < 4) b--;
    bank[i].off = a; bank[i].len = b > a + 8 ? b - a : 0;
  }
  renderProgress = 12;
  logf("SAM voice %s: %u bytes\n", VOICES[vi].name, poolPos);
  bankBusy = false;
}

static inline float wordAt(int w, float p) {
  const Word& wd = bank[w];
  if (p < 0 || p >= (float)wd.len - 1) return 0;          // float compare: len 0 must not wrap to 4 billion
  int i = (int)p; float f = p - i;
  float a = pool[wd.off + i], b = pool[wd.off + i + 1];
  return (a + (b - a) * f) * (1.0f / 128.0f);
}

// =====================================================================
// Talkie LPC: words decoded to frames at boot, re-synthesized live (so they can be bent)
// Tables and lattice filter from Talkie (Peter Knight) via ESP8266Audio's AudioGeneratorTalkie.
// =====================================================================
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnarrowing"
static const uint8_t tmsEnergy[0x10] = {0x00,0x02,0x03,0x04,0x05,0x07,0x0a,0x0f,0x14,0x20,0x29,0x39,0x51,0x72,0xa1,0xff};
static const uint8_t tmsPeriod[0x40] = {0x00,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2A,0x2B,0x2D,0x2F,0x31,0x33,0x35,0x36,0x39,0x3B,0x3D,0x3F,0x42,0x45,0x47,0x49,0x4D,0x4F,0x51,0x55,0x57,0x5C,0x5F,0x63,0x66,0x6A,0x6E,0x73,0x77,0x7B,0x80,0x85,0x8A,0x8F,0x95,0x9A,0xA0};
static const int16_t tmsK1[0x20] = {0x82C0,0x8380,0x83C0,0x8440,0x84C0,0x8540,0x8600,0x8780,0x8880,0x8980,0x8AC0,0x8C00,0x8D40,0x8F00,0x90C0,0x92C0,0x9900,0xA140,0xAB80,0xB840,0xC740,0xD8C0,0xEBC0,0x0000,0x1440,0x2740,0x38C0,0x47C0,0x5480,0x5EC0,0x6700,0x6D40};
static const int16_t tmsK2[0x20] = {0xAE00,0xB480,0xBB80,0xC340,0xCB80,0xD440,0xDDC0,0xE780,0xF180,0xFBC0,0x0600,0x1040,0x1A40,0x2400,0x2D40,0x3600,0x3E40,0x45C0,0x4CC0,0x5300,0x5880,0x5DC0,0x6240,0x6640,0x69C0,0x6CC0,0x6F80,0x71C0,0x73C0,0x7580,0x7700,0x7E80};
static const int8_t tmsK3[0x10] = {0x92,0x9F,0xAD,0xBA,0xC8,0xD5,0xE3,0xF0,0xFE,0x0B,0x19,0x26,0x34,0x41,0x4F,0x5C};
static const int8_t tmsK4[0x10] = {0xAE,0xBC,0xCA,0xD8,0xE6,0xF4,0x01,0x0F,0x1D,0x2B,0x39,0x47,0x55,0x63,0x71,0x7E};
static const int8_t tmsK5[0x10] = {0xAE,0xBA,0xC5,0xD1,0xDD,0xE8,0xF4,0xFF,0x0B,0x17,0x22,0x2E,0x39,0x45,0x51,0x5C};
static const int8_t tmsK6[0x10] = {0xC0,0xCB,0xD6,0xE1,0xEC,0xF7,0x03,0x0E,0x19,0x24,0x2F,0x3A,0x45,0x50,0x5B,0x66};
static const int8_t tmsK7[0x10] = {0xB3,0xBF,0xCB,0xD7,0xE3,0xEF,0xFB,0x07,0x13,0x1F,0x2B,0x37,0x43,0x4F,0x5A,0x66};
static const int8_t tmsK8[0x08] = {0xC0,0xD8,0xF0,0x07,0x1F,0x37,0x4F,0x66};
static const int8_t tmsK9[0x08] = {0xC0,0xD4,0xE8,0xFC,0x10,0x25,0x39,0x4D};
static const int8_t tmsK10[0x08] = {0xCD,0xDF,0xF1,0x04,0x16,0x20,0x3B,0x4D};
static const int8_t chirp[] = {0x00,0x2a,0xd4,0x32,0xb2,0x12,0x25,0x14,0x02,0xe1,0xc5,0x02,0x5f,0x5a,0x05,0x0f,0x26,0xfc,0xa5,0xa5,0xd6,0xdd,0xdc,0xfc,0x25,0x2b,0x22,0x21,0x0f,0xff,0xf8,0xee,0xed,0xef,0xf7,0xf6,0xfa,0x00,0x03,0x02,0x01};
#pragma GCC diagnostic pop

LpcFrame* lpcFrames = nullptr;
uint32_t lpcCap = 0, lpcUsed = 0;
volatile int talkBank = 0;

struct BitReader {
  const uint8_t* p; uint8_t bit = 0;
  static uint8_t rev(uint8_t a) {
    a = (a >> 4) | (a << 4); a = ((a & 0xcc) >> 2) | ((a & 0x33) << 2); a = ((a & 0xaa) >> 1) | ((a & 0x55) << 1); return a;
  }
  uint8_t get(uint8_t bits) {
    uint16_t data = rev(p[0]) << 8;
    if (bit + bits > 8) data |= rev(p[1]);
    data <<= bit;
    uint8_t v = data >> (16 - bits);
    bit += bits;
    if (bit >= 8) { bit -= 8; p++; }
    return v;
  }
};

// decode one LPC word; out == nullptr just counts frames
uint32_t decodeLpc(const uint8_t* data, LpcFrame* out, uint32_t maxN) {
  BitReader br{data};
  LpcFrame cur = {};
  uint32_t n = 0;
  while (n < maxN) {
    uint8_t e = br.get(4);
    if (e == 0xF) break;
    if (e == 0) cur.energy = 0;
    else {
      cur.energy = tmsEnergy[e];
      uint8_t rep = br.get(1);
      cur.period = tmsPeriod[br.get(6)];
      if (!rep) {
        cur.k1 = tmsK1[br.get(5)]; cur.k2 = tmsK2[br.get(5)];
        cur.k[0] = tmsK3[br.get(4)]; cur.k[1] = tmsK4[br.get(4)];
        if (cur.period) {
          cur.k[2] = tmsK5[br.get(4)]; cur.k[3] = tmsK6[br.get(4)]; cur.k[4] = tmsK7[br.get(4)];
          cur.k[5] = tmsK8[br.get(3)]; cur.k[6] = tmsK9[br.get(3)]; cur.k[7] = tmsK10[br.get(3)];
        }
      }
    }
    if (out) out[n] = cur;
    n++;
  }
  return n;
}

void decodeTalk() {
  const uint32_t MAXW = 8000;                       // ~200 s per word, plenty (Tom's Diner is the long one)
  uint32_t total = 0;
  for (auto& b : talk) for (auto& w : b) total += decodeLpc(w.data, nullptr, MAXW);
  lpcCap = total;
  lpcFrames = psramFound() ? (LpcFrame*)ps_malloc(total * sizeof(LpcFrame)) : nullptr;
  if (!lpcFrames) lpcFrames = (LpcFrame*)malloc(total * sizeof(LpcFrame));
  if (!lpcFrames) { logf("!! no memory for LPC frames\n"); return; }
  for (auto& b : talk) for (auto& w : b) {
    uint32_t n = decodeLpc(w.data, lpcFrames + lpcUsed, MAXW);
    uint32_t a = 0;
    while (a < n && lpcFrames[lpcUsed + a].energy == 0) a++;      // trim silent frames at both ends
    while (n > a && lpcFrames[lpcUsed + n - 1].energy == 0) n--;
    w.first = lpcUsed + a; w.n = n - a;
    lpcUsed += n;
  }
  logf("LPC: %u frames decoded (%u KB)\n", lpcUsed, lpcUsed * sizeof(LpcFrame) / 1024);
}

// live LPC voice with circuit bends
volatile bool lpWhisper = false, lpMono = false, lpFreeze = false, lpStretch = false, lpGlitch = false, lpRev = false;
volatile float lpPitch = 1, lpFormant = 1;
volatile float monoFreq = 130;

struct Lpc {
  int bank = 0, word = -1; float fpos = 0; bool on = false, scratch = false, rev = false; float base = 0;
  int loaded = -1;
  uint8_t period = 0; int32_t energy = 0, k1 = 0, k2 = 0; int32_t k[8] = {0};
  uint8_t pc = 0; int32_t x[10] = {0}; uint16_t rnd = 1;
  float ph = 0, y0 = 0, y1 = 0;
} lpc;
volatile int lpcVisFrame = -1;

void talkPlay(int w, bool rev) {
  const TalkWord& t = talk[talkBank][w];
  if (!t.n) return;
  lpc.bank = talkBank; lpc.word = w; lpc.rev = rev ^ lpRev; lpc.scratch = false;
  lpc.fpos = lpc.rev ? t.n - 0.01f : 0; lpc.loaded = -1; lpc.on = true;
}
void talkScratch(int w) {
  const TalkWord& t = talk[talkBank][w];
  if (!t.n) return;
  lpc.bank = talkBank; lpc.word = w; lpc.scratch = true; lpc.base = 0; lpc.fpos = 0; lpc.loaded = -1; lpc.on = true;
}

void lpcLoad(int fi) {
  const TalkWord& t = talk[lpc.bank][lpc.word];
  const LpcFrame& f = lpcFrames[t.first + fi];
  lpc.loaded = fi;
  lpc.energy = f.energy;
  lpc.k1 = f.k1; lpc.k2 = f.k2;
  for (int i = 0; i < 8; i++) lpc.k[i] = f.k[i];
  if (lpGlitch) {                                   // circuit bend: corrupt some coefficients
    if (random(3) == 0) lpc.k1 = tmsK1[random(32)];
    if (random(3) == 0) lpc.k2 = tmsK2[random(32)];
    if (random(2) == 0) lpc.k[random(8)] = -lpc.k[random(8)];
  }
  int per = f.period;
  if (lpWhisper) per = 0;
  else if (per) {
    if (lpMono) per = (int)(8000.0f / monoFreq);
    else per = (int)(per / lpPitch);
    per = constrain(per, 10, 255);
  }
  lpc.period = per;
}

// one 8 kHz sample
static inline float lpcTick() {
  const TalkWord& t = talk[lpc.bank][lpc.word];
  if (!lpc.on || !t.n) return 0;
  int fi = (int)lpc.fpos;
  if (fi < 0 || fi >= (int)t.n) { if (!lpc.scratch) { lpc.on = false; return 0; } fi = constrain(fi, 0, (int)t.n - 1); }
  if (fi != lpc.loaded) lpcLoad(fi);
  if (!lpc.scratch && !lpFreeze) lpc.fpos += (lpc.rev ? -1.0f : 1.0f) * (lpStretch ? 0.4f : 1.0f) / 200.0f;

  int32_t u10;
  if (lpc.period) {
    if (lpc.pc < lpc.period) lpc.pc++; else lpc.pc = 0;
    u10 = lpc.pc < sizeof(chirp) ? (chirp[lpc.pc] * lpc.energy) >> 8 : 0;
  } else {
    lpc.rnd = (lpc.rnd >> 1) ^ ((lpc.rnd & 1) ? 0xB800 : 0);
    u10 = (lpc.rnd & 1) ? lpc.energy : -lpc.energy;
  }
  int32_t* x = lpc.x; int32_t* k = lpc.k;
  int32_t u9 = u10 - ((k[7] * x[9]) >> 7);
  int32_t u8 = u9 - ((k[6] * x[8]) >> 7);
  int32_t u7 = u8 - ((k[5] * x[7]) >> 7);
  int32_t u6 = u7 - ((k[4] * x[6]) >> 7);
  int32_t u5 = u6 - ((k[3] * x[5]) >> 7);
  int32_t u4 = u5 - ((k[2] * x[4]) >> 7);
  int32_t u3 = u4 - ((k[1] * x[3]) >> 7);
  int32_t u2 = u3 - ((k[0] * x[2]) >> 7);
  int32_t u1 = u2 - ((lpc.k2 * x[1]) >> 15);
  int32_t u0 = u1 - ((lpc.k1 * x[0]) >> 15);
  u0 = constrain(u0, -512, 511);
  x[9] = x[8] + ((k[6] * u8) >> 7);
  x[8] = x[7] + ((k[5] * u7) >> 7);
  x[7] = x[6] + ((k[4] * u6) >> 7);
  x[6] = x[5] + ((k[3] * u5) >> 7);
  x[5] = x[4] + ((k[2] * u4) >> 7);
  x[4] = x[3] + ((k[1] * u3) >> 7);
  x[3] = x[2] + ((k[0] * u2) >> 7);
  x[2] = x[1] + ((lpc.k2 * u1) >> 15);
  x[1] = x[0] + ((lpc.k1 * u0) >> 15);
  x[0] = u0;
  for (int i = 0; i < 10; i++) x[i] = constrain(x[i], -32768, 32767);
  return u0 * (1.0f / 512.0f);
}

// =====================================================================
// Bytebeat
// =====================================================================
struct ByteBeat {
  bool on = false; int f = 0; uint32_t t = 0; float acc = 0; float rate = 1; int a = 8, b = 8;
  bool sync = true, crush = false, rev = false; uint8_t cur = 128;
} bb;
volatile uint8_t byteVis[128];

static inline uint32_t sh(uint32_t v, int s) { return v >> (s & 31); }
static inline uint8_t byteFormula(int f, uint32_t t, int a, int b) {
  switch (f) {
    case 0: return t & sh(t, a);
    case 1: return t * (sh(t, a) | sh(t, b));
    case 2: return t * (42 & sh(t, a));
    case 3: return (t * 5 & sh(t, a)) | (t * 3 & sh(t, b));
    case 4: return t * ((t >> 12 | sh(t, a)) & 63 & t >> 4);
    case 5: return (t * (0xCA98 >> (sh(t, a) & 14) & 15)) | sh(t, b);
    case 6: return (sh(t, a) | t | sh(t, (t >> b) & 31)) * 10 + ((t >> 11) & 7);
    case 7: return ((t * (sh(t, a) | t >> 9) & 46 & sh(t, a)) ^ (t & t >> 13 | t >> 6));
    case 8: return t * ((sh(t, a) | sh(t, b)) & 25 & t >> 6);
    case 9: return t * (t ^ (t + (sh(t, b) | 1)) ^ sh(t - 1280 ^ t, a));
    case 10: return (t | (sh(t, a) | t >> 7)) * t & (t >> 11 | sh(t, a));
    default: return t * (t >> 11 & sh(t, a)) & 123 & t >> 3;
  }
}
void byteStart(int f) {
  if (bb.f != f || !bb.on) { bb.a = BYTE_A[f]; bb.b = BYTE_B[f]; }
  bb.f = f; bb.t = 0; bb.on = true;
}
void byteStop() { bb.on = false; }

// =====================================================================
// DSP helpers + drums
// =====================================================================
float sineTab[1025];
static inline float wrap1(float p) { return p - floorf(p); }
static inline float fsin(float ph) {
  float x = ph * 1024.0f; int i = (int)x; float f = x - i; i &= 1023;
  return sineTab[i] + (sineTab[i + 1] - sineTab[i]) * f;
}
static uint32_t nstate = 22222;
static inline float nz() { nstate ^= nstate << 13; nstate ^= nstate >> 17; nstate ^= nstate << 5; return (int32_t)nstate * (1.0f / 2147483648.0f); }
static inline float dcoef(float sec) { return expf(-1.0f / (sec * FS)); }
static inline float softclip(float x) { if (x > 3) return 1; if (x < -3) return -1; return x * (27 + x * x) / (27 + 9 * x * x); }

volatile float chugHz = 73.4f;                       // set by vocChord(): drop-D-ish root of the current chord

struct Perc {
  float env = 0, d = 0.99f, penv = 0, pd = 0.99f, ph = 0, ph2 = 0, prev = 0, lp = 0, bp = 0;
  float mph[6] = {0};
  uint32_t age = 0;
  void trig(const PercDef& p, float a) { env = a; d = dcoef(p.dec); penv = 1; pd = dcoef(p.ptime); ph = 0; age = 0; }
  inline float s(const PercDef& p, float rate) {
    if (env < 0.0001f) return 0;
    float f = (p.f1 + (p.f0 - p.f1) * penv) * rate, o = 0;
    penv *= pd;
    switch (p.wave) {
      case W_SINE: ph = wrap1(ph + f * INV_FS); o = fsin(ph) + nz() * p.noise * penv; break;
      case W_KICK: {                                   // sine sweep + a noise click on the front
        ph = wrap1(ph + f * INV_FS);
        o = fsin(ph) + (age < 110 ? nz() * p.noise * (1 - age / 110.0f) : 0);
        break;
      }
      case W_SNR: {                                    // body that thins out + bright noise that stays
        ph = wrap1(ph + f * INV_FS);
        float n = nz(), h = n - prev; prev = n;
        o = fsin(ph) * (0.2f + 0.8f * penv) + h * p.noise;
        break;
      }
      case W_CLAP: {                                   // three quick bursts, then the tail
        float n = nz(), q = 0.42f; lp += q * bp; bp += q * (n - lp - 0.6f * bp);
        float g = age < 660 ? 1.0f - (age % 220) / 260.0f : 1.0f;
        o = bp * 1.6f * g;
        break;
      }
      case W_METAL: {                                  // 808 cymbal: six detuned squares, high-passed
        static const float MF[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};
        float sum = 0, sc = p.f0 * rate * INV_FS;
        for (int i = 0; i < 6; i++) { mph[i] = wrap1(mph[i] + MF[i] * sc); sum += mph[i] < 0.5f ? 1 : -1; }
        lp += 0.55f * (sum - lp);
        float h = sum - lp;
        bp += 0.55f * (h - bp);
        o = h - bp;
        break;
      }
      case W_FM: {                                     // inharmonic clang, index falls with penv
        float fc = p.f0 * rate;
        ph = wrap1(ph + fc * INV_FS); ph2 = wrap1(ph2 + fc * p.f1 * INV_FS);
        o = fsin(wrap1(ph + p.noise * (0.25f + penv) * fsin(ph2)));
        break;
      }
      case W_SLAM: {                                   // dark noise + thump: a big steel door
        ph = wrap1(ph + f * INV_FS);
        lp += 0.12f * (nz() - lp);
        o = lp * 2.6f * (0.35f + 0.65f * penv) + fsin(ph) * penv;
        break;
      }
      case W_CHUG: {                                   // fuzz power chord (root, fifth, octave), palm-muted lowpass
        static const float CR[6] = {1.0f, 1.006f, 1.4983f, 1.5073f, 2.0f, 1.994f};
        float sum = 0, r = chugHz * rate * INV_FS;
        for (int i = 0; i < 6; i++) { mph[i] = wrap1(mph[i] + r * CR[i]); sum += mph[i] * 2 - 1; }
        float fz = softclip(sum * 1.1f);
        float c = 0.05f + 0.32f * penv;
        lp += c * (fz - lp); bp += c * (lp - bp);
        o = bp * 1.3f;
        break;
      }
    }
    age++;
    o *= env; env *= d;
    if (p.drive > 0) o = softclip(o * p.drive) * (1.0f / 1.15f);
    return o * p.gain;
  }
};
Perc perc[12];
volatile uint16_t drumMute = 0;

volatile float duckEnv = 0;                         // sidechain: kick / 808 / slam push the voices down
void hitDrums(uint16_t bits) {
  for (int i = 0; i < 12; i++) if (bits & (1 << i)) perc[i].trig(PERC[i], 1);
  if (bits & DUCK_MASK) duckEnv = 1;
  if (bits & (1 << D_HAT)) perc[D_OPEN].env *= 0.05f;   // closed hat chokes the open one
}

#define DLY_N 16384
float* dlyBuf = nullptr;
int dlyW = 0;

// =====================================================================
// Instrument state
// =====================================================================
Step seq[16];
volatile int mode = M_SAM, curStep = 0, curPad = 0, fx = FX_CLEAN, scStyle = SC_BABY, bpm = 92;
volatile bool rec = false, playing = false, groove = false, stutterOn = false, reverseOn = false, tapeOn = false, underOn = false;
volatile bool scratchHeld = false, talkScratchHeld = false;
volatile float tapeRate = 1, stepFrac = 0;
volatile uint16_t rollBits = 0;
char msg[28] = ""; volatile uint32_t msgAt = 0;
void flash(const char* m) { strncpy(msg, m, sizeof(msg) - 1); msgAt = millis(); }

struct Deck {
  int word = -1;
  float pos = 0, rate = 1, base = 0;
  bool on = false, scratch = false, rev = false;
  int scratchSteps = 0;
} deck;
volatile float deckVis = 0, outPeak = 0;
volatile float scopeBuf[128];

void deckPlay(int w, bool rev) {
  if (bankBusy || w < 0 || bank[w].len == 0) return;
  deck.word = w; deck.rev = rev; deck.scratch = false; deck.scratchSteps = 0;
  deck.pos = rev ? bank[w].len - 2 : 0;
  deck.on = true;
}
void deckScratch(int w, int steps) {
  if (bankBusy || w < 0 || bank[w].len == 0) return;
  deck.word = w; deck.scratch = true; deck.scratchSteps = steps; deck.base = 0; deck.pos = 0; deck.on = true;
}

// built-in groove: heavy half-time-ish rap beat (kick 1, a+ of 2, 3+; snare 2 and 4; 8th metal hats)
uint16_t grooveBits(int s) {
  uint16_t d = 0;
  if (s == 0 || s == 7 || s == 10) d |= 1 << D_KICK;
  if (s == 4 || s == 12) d |= 1 << D_SNARE;
  if ((s & 1) == 0) d |= 1 << (s == 14 ? D_OPEN : D_HAT);
  return d;
}

int quantStep() { return (curStep + (stepFrac > 0.5f ? 1 : 0)) & 15; }
void recVoice(uint8_t src, int w, uint8_t flags) {
  if (!rec || !playing) return;
  int q = quantStep();
  seq[q].word = w; seq[q].src = src; seq[q].flags = flags;
  if (q != curStep) seq[q].skip = true;
}
void recDrum(int d) {
  if (!rec || !playing) return;
  int q = quantStep();
  seq[q].drum |= 1 << d;
  if (q != curStep) seq[q].skip = true;
}
void recByte(int f) {
  if (!rec || !playing) return;
  int q = quantStep();
  seq[q].byteF = f;
  if (q != curStep) seq[q].skip = true;
}

void clearVoices() { for (auto& s : seq) { s.word = -1; s.flags = 0; } }
void clearDrums() { for (auto& s : seq) s.drum = 0; }
void clearBytes() { for (auto& s : seq) s.byteF = -1; }
void clearAll() { for (auto& s : seq) s = {-1, 0, 0, -1, 0, false}; }
void mutate() { for (auto& s : seq) if (s.word >= 0) s.word = random(12); }
void autoPattern() {
  clearAll();
  static const uint8_t K[6][16] = {                                // boom-bap, nu-metal and broken industrial kicks
    {1,0,0,0, 0,0,0,1, 0,0,1,0, 0,0,0,0}, {1,0,0,1, 0,0,1,0, 0,0,1,0, 0,0,0,0},
    {1,0,1,0, 0,0,0,1, 0,1,0,0, 0,0,1,0}, {1,1,0,0, 0,0,1,0, 1,0,0,0, 0,0,1,1},
    {1,0,0,0, 0,0,1,0, 0,1,0,0, 1,0,0,0}, {1,0,0,0, 0,0,0,0, 1,0,1,0, 0,0,0,1}};
  int k = random(6);
  bool chugs = random(2), metal = random(2);
  for (int i = 0; i < 16; i++) {
    if (K[k][i]) {
      seq[i].drum |= 1 << D_KICK;
      if (chugs && random(3)) seq[i].drum |= 1 << D_CHUG;          // the riff rides the kick
    }
    if (i == 4 || i == 12) seq[i].drum |= 1 << (random(3) ? D_SNARE : D_CLAP);
    if (i == 12 && random(2)) seq[i].drum |= 1 << D_SLAM;
    if ((i & 1) == 0 || random(5) == 0) seq[i].drum |= 1 << (metal && i == 14 ? D_OPEN : D_HAT);
    if (random(7) == 0) seq[i].drum |= 1 << (D_808 + random(6));   // 808 ANVIL PIPE SLAM ZAP CRASH
  }
  if (random(2)) seq[0].drum |= 1 << D_808;
  uint8_t src = mode == M_TALK ? SRC_TALK : SRC_SAM;
  int a = random(12), b = random(12), hits = 3 + random(3);
  for (int h = 0; h < hits; h++) {
    int st = random(16);
    seq[st].word = random(3) ? a : b; seq[st].src = src;
    seq[st].flags = random(4) == 0 ? SF_SCRATCH : 0;
  }
  if (mode == M_BYTE) { seq[0].byteF = random(12); seq[8].byteF = random(12); }
  groove = false;
  flash("AUTO");
}

// vocoder chord progression (one chord per bar)
struct Prog { const char* name; int8_t root[4]; uint8_t q[4]; };
enum { Q_MAJ, Q_MIN, Q_MAJ7, Q_MIN7, Q_DOM7, Q_SUS4, Q_POW };
const int8_t QI[7][4] = {{0, 4, 7, -1}, {0, 3, 7, -1}, {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 4, 7, 10}, {0, 5, 7, -1}, {0, 7, 12, -1}};
const char* QN[7] = {"", "m", "maj7", "m7", "7", "sus4", "5"};
const char* NOTE[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
const Prog PROGS[] = {
  {"DROP",    {2, 10, 5, 0}, {Q_POW, Q_POW, Q_POW, Q_POW}},       // D5 Bb5 F5 C5
  {"DREAD",   {4, 5, 4, 7},  {Q_MIN, Q_MAJ, Q_MIN, Q_MAJ}},       // E phrygian: Em F Em G
  {"EPIC",    {9, 5, 0, 7},  {Q_MIN, Q_MAJ, Q_MAJ, Q_MAJ}},
  {"MACHINE", {0, 1, 0, 6},  {Q_POW, Q_POW, Q_POW, Q_POW}},       // C5 Db5 C5 F#5: half steps + tritone
};
const int NPROGS = 4;
volatile int progIdx = 0, progPos = 0;
void vocChord(int i);

// =====================================================================
// Keypad (scanned inside the audio task every 5.8 ms: low latency)
// =====================================================================
ModKey mods[4];
bool padDown[12], padLong[12];
int8_t padMenu[12], padHoldMode[12];   // padHoldMode: mode when the hold started                 // which modifier was held when this pad went down (-1 = none)
uint32_t padT[12];
uint16_t keysStable = 0, keysRaw = 0;
volatile uint32_t bounceCount = 0;
volatile int heldModVis = -1;       // for the screen overlay
volatile uint16_t keysVis = 0;

uint16_t scanKeys() {
  uint16_t bits = 0;
  for (int r = 0; r < 4; r++) {
    pinMode(ROW_PINS[r], OUTPUT);
    digitalWrite(ROW_PINS[r], LOW);
    delayMicroseconds(20);
    for (int c = 0; c < 4; c++) if (!digitalRead(COL_PINS[c])) bits |= 1 << (r * 4 + c);
    pinMode(ROW_PINS[r], INPUT);
  }
  return bits;
}

int heldMod() {
  int best = -1; uint32_t t = 0;
  for (int m = 0; m < 4; m++) if (mods[m].down && (best < 0 || mods[m].tDown > t)) { best = m; t = mods[m].tDown; }
  return best;
}

void setMode(int m) { mode = m; flash(MODE_NAMES[m]); }

void menuAction(int m, int p, bool down) {
  char t[28];
  switch (m) {
    case K_MODE:
      if (!down) return;
      if (p < 4) setMode(p);
      else if (p == 4) { volIdx = max(0, volIdx - 1); snprintf(t, sizeof t, "VOLUME %d/6", volIdx + 1); flash(t); }
      else if (p == 5) { volIdx = min(5, volIdx + 1); snprintf(t, sizeof t, "VOLUME %d/6", volIdx + 1); flash(t); }
      else if (p == 6) { talkBank = (talkBank + 1) % NBANKS; snprintf(t, sizeof t, "BANK %s", BANK_NAMES[talkBank]); flash(t); }
      else voiceIdx = p - 7;                       // loop() sees the change and re-renders the SAM words
      break;
    case K_FX:
      if (p < 7) { if (down) { fx = p; flash(FX_NAMES[p]); } }
      else if (p == 7) stutterOn = down;
      else if (p == 8) reverseOn = down;
      else if (p == 9) tapeOn = down;
      else if (p == 10) underOn = down;
      else if (down) { scStyle = (scStyle + 1) % SC_COUNT; snprintf(t, sizeof t, "SCRATCH %s", SC_NAMES[scStyle]); flash(t); }
      break;
    case K_SEQ:
      if (!down) return;
      switch (p) {
        case 0: rec = !rec; flash(rec ? "REC" : "rec off"); break;
        case 1: playing = !playing; flash(playing ? "PLAY" : "STOP"); break;
        case 2: groove = !groove; flash(groove ? "groove on" : "groove off"); break;
        case 3: autoPattern(); break;
        case 4: bpm = max(60, bpm - 5); snprintf(t, sizeof t, "%d BPM", bpm); flash(t); break;
        case 5: bpm = min(180, bpm + 5); snprintf(t, sizeof t, "%d BPM", bpm); flash(t); break;
        case 6: mutate(); flash("MUTATE"); break;
        case 7: progIdx = (progIdx + 1) % NPROGS; vocChord(progPos); flash(PROGS[progIdx].name); break;
        case 8: clearVoices(); flash("voices cleared"); break;
        case 9: clearDrums(); flash("drums cleared"); break;
        case 10: clearBytes(); flash("bytes cleared"); break;
        default: clearAll(); groove = false; flash("all cleared"); break;
      }
      break;
    case K_SHIFT:
      if (mode == M_SAM) {
        if (down) { curPad = p; scratchHeld = true; deckScratch(p, 0); }
        else if (scratchHeld) { scratchHeld = false; deck.scratch = false; deck.on = false; }
      } else if (mode == M_TALK) {
        if (p == 2) { lpFreeze = down; return; }
        if (!down) return;
        switch (p) {
          case 0: lpWhisper = !lpWhisper; flash(lpWhisper ? "WHISPER" : "whisper off"); break;
          case 1: lpMono = !lpMono; flash(lpMono ? "ROBOT MONO" : "mono off"); break;
          case 3: lpStretch = !lpStretch; flash(lpStretch ? "STRETCH" : "stretch off"); break;
          case 4: lpPitch = max(0.25f, lpPitch * 0.84f); flash("PITCH -"); break;
          case 5: lpPitch = min(4.0f, lpPitch * 1.19f); flash("PITCH +"); break;
          case 6: lpPitch = 1; flash("PITCH 0"); break;
          case 7: lpGlitch = !lpGlitch; flash(lpGlitch ? "GLITCH" : "glitch off"); break;
          case 8: lpRev = !lpRev; flash(lpRev ? "BACKWARDS" : "forwards"); break;
          case 9: lpFormant = max(0.5f, lpFormant * 0.89f); flash("FORMANT -"); break;
          case 10: lpFormant = min(2.0f, lpFormant * 1.12f); flash("FORMANT +"); break;
          default: lpWhisper = lpMono = lpStretch = lpGlitch = lpRev = false; lpPitch = lpFormant = 1; flash("BENDS RESET"); break;
        }
      } else if (mode == M_BYTE) {
        if (!down) return;
        switch (p) {
          case 0: bb.rate = max(1.0f / 16, bb.rate * 0.5f); break;
          case 1: bb.rate = min(8.0f, bb.rate * 2); break;
          case 2: bb.a = max(1, bb.a - 1); break;
          case 3: bb.a = min(24, bb.a + 1); break;
          case 4: bb.b = max(1, bb.b - 1); break;
          case 5: bb.b = min(24, bb.b + 1); break;
          case 6: bb.sync = !bb.sync; break;
          case 7: byteStop(); break;
          case 8: bb.crush = !bb.crush; break;
          case 9: bb.rev = !bb.rev; break;
          case 10: bb.a = 1 + random(16); bb.b = 1 + random(16); break;
          default: bb.a = BYTE_A[bb.f]; bb.b = BYTE_B[bb.f]; bb.rate = 1; bb.crush = bb.rev = false; bb.sync = true; break;
        }
        snprintf(t, sizeof t, "A%d B%d x%g%s", bb.a, bb.b, bb.rate, bb.sync ? " SYNC" : ""); flash(t);
      } else {
        if (!down) return;
        drumMute ^= 1 << p;
        snprintf(t, sizeof t, "%s %s", PERC[p].name, drumMute & (1 << p) ? "MUTED" : "on"); flash(t);
      }
      break;
  }
}

void tapAction(int m) {
  if (m == K_MODE) setMode((mode + 1) % M_COUNT);
  else if (m == K_FX) { fx = (fx + 1) % FX_COUNT; flash(FX_NAMES[fx]); }
  else if (m == K_SEQ) { rec = !rec; flash(rec ? "REC" : "rec off"); }
  else { playing = !playing; flash(playing ? "PLAY" : "STOP"); }
}

void padPress(int p, uint32_t now) {
  padDown[p] = true; padLong[p] = false; padT[p] = now;
  int m = heldMod();
  padMenu[p] = m;
  if (m >= 0) { mods[m].used = true; menuAction(m, p, true); return; }
  curPad = p;
  switch (mode) {
    case M_SAM: deckPlay(p, reverseOn); recVoice(SRC_SAM, p, reverseOn ? SF_REV : 0); break;
    case M_TALK: talkPlay(p, reverseOn); recVoice(SRC_TALK, p, reverseOn ? SF_REV : 0); break;
    case M_BYTE:
      if (bb.on && bb.f == p) { byteStop(); recByte(BYTE_STOP); }
      else { byteStart(p); recByte(p); }
      break;
    default: hitDrums(1 << p); recDrum(p); break;
  }
}

void padHold(int p) {
  padLong[p] = true;
  if (padMenu[p] >= 0) return;
  padHoldMode[p] = mode;
  switch (mode) {
    case M_SAM: scratchHeld = true; deckScratch(p, 0); break;
    case M_TALK: talkScratchHeld = true; talkScratch(p); break;
    case M_BYTE: if (!(bb.on && bb.f == p)) byteStart(p); break;   // held = momentary
    default: rollBits |= 1 << p; break;
  }
}

void padRelease(int p) {
  padDown[p] = false;
  if (padMenu[p] >= 0) { menuAction(padMenu[p], p, false); return; }
  if (!padLong[p]) return;
  switch (padHoldMode[p]) {
    case M_SAM: if (scratchHeld) { scratchHeld = false; deck.scratch = false; deck.on = false; } break;
    case M_TALK: if (talkScratchHeld) { talkScratchHeld = false; lpc.scratch = false; lpc.on = false; } break;
    case M_BYTE: if (bb.f == p) byteStop(); break;
    default: rollBits &= ~(1 << p); break;
  }
}

void inputTick() {
  uint32_t now = millis();
  uint16_t raw = scanKeys();
  // asymmetric debounce: down after 2 scans (~12 ms), up only after 8 scans (~46 ms) - rides over flaky contacts
  static uint8_t upCnt[16], dnCnt[16];
  uint16_t stable = keysStable;
  for (int k = 0; k < 16; k++) {
    bool r = raw & (1 << k);
    if (r) { upCnt[k] = 0; if (dnCnt[k] < 255) dnCnt[k]++; if (dnCnt[k] >= 2) stable |= 1 << k; }
    else { dnCnt[k] = 0; if (upCnt[k] < 255) upCnt[k]++; if (upCnt[k] >= 8) stable &= ~(1 << k); }
  }
  if ((raw ^ keysRaw) & 0x1111) bounceCount++;           // left-column changes, for the serial log
  keysRaw = raw;
  uint16_t pressed = stable & ~keysStable, released = keysStable & ~stable;
  keysStable = stable;
  keysVis = stable;
  for (int r = 0; r < 4; r++) {
    int bit = 1 << (r * 4);
    if (pressed & bit) { mods[r].down = true; mods[r].used = false; mods[r].tDown = now; }
    if (released & bit) {
      mods[r].down = false;
      if (!mods[r].used && now - mods[r].tDown < 500) tapAction(r);
    }
    for (int c = 1; c < 4; c++) {
      int b = 1 << (r * 4 + c), p = r * 3 + c - 1;
      if (pressed & b) padPress(p, now);
      if (released & b) padRelease(p);
    }
  }
  for (int p = 0; p < 12; p++) if (padDown[p] && !padLong[p] && now - padT[p] > 300) padHold(p);
  int m = heldMod();
  heldModVis = (m >= 0 && now - mods[m].tDown > 180) ? m : -1;
  if (scratchHeld && rec && playing) { int q = quantStep(); seq[q].word = curPad; seq[q].src = SRC_SAM; seq[q].flags = SF_SCRATCH; }
  if (talkScratchHeld && rec && playing) { int q = quantStep(); seq[q].word = curPad; seq[q].src = SRC_TALK; seq[q].flags = SF_SCRATCH; }
}

// =====================================================================
// Audio
// =====================================================================
static inline void scratchShape(int style, float ph, float& x, float& g) {
  switch (style) {
    case SC_CHIRP: x = 0.5f - 0.5f * cosf(2 * PI * ph); g = ph < 0.45f ? 1 : 0; break;
    case SC_TRANS: { float p2 = ph * 0.5f; x = 0.5f - 0.5f * cosf(2 * PI * p2); g = ((int)(ph * 8)) & 1 ? 0 : 1; break; }
    case SC_TEAR:
      if (ph < 0.3f) x = ph / 0.3f;
      else if (ph < 0.55f) x = 1 - (ph - 0.3f) / 0.25f * 0.5f;
      else if (ph < 0.65f) x = 0.5f;
      else x = 0.5f - (ph - 0.65f) / 0.35f * 0.5f;
      g = 1; break;
    case SC_FLARE: x = 0.5f - 0.5f * cosf(2 * PI * ph); g = (fabsf(ph - 0.25f) < 0.04f || fabsf(ph - 0.75f) < 0.04f) ? 0 : 1; break;
    default: x = 0.5f - 0.5f * cosf(2 * PI * ph); g = 1; break;
  }
}

struct Grain { int word; float pos, inc, age, len; bool on; };
Grain grains[10];
int grainTimer = 0;

void spawnGrain(bool swarm) {
  if (deck.word < 0) return;
  for (auto& g : grains) if (!g.on) {
    static const float GP[6] = {1.0f, 1.0f, 0.5f, 1.5f, 2.0f, 0.75f};
    g.word = deck.word;
    g.len = swarm ? FS * (0.03f + 0.02f * random(100) / 100.0f) : FS * (0.05f + 0.04f * random(100) / 100.0f);
    float spray = (random(200) - 100) / 100.0f * FS * (swarm ? 0.012f : 0.03f);
    g.pos = max(0.0f, deck.pos + spray);
    g.inc = swarm ? 1.0f + (random(200) - 100) / 100.0f * 0.12f : GP[random(6)];
    if (deck.rev) g.inc = -g.inc;
    g.age = 0; g.on = true;
    return;
  }
}

static inline float grainSum() {
  float v = 0;
  for (int i = 0; i < 10; i++) {
    Grain& g = grains[i];
    if (!g.on) continue;
    float w = fsin(g.age / g.len * 0.5f);
    v += wordAt(g.word, g.pos) * w * w;
    g.pos += g.inc * tapeRate;
    if (++g.age >= g.len) g.on = false;
  }
  return v;
}

// ---- vocoder (from Robo Choir) ----
#define NB 16
struct VOsc { float ph = 0, ph2 = 0, f = 110, target = 110; bool on = false; };
VOsc vosc[5];
float coefM[NB][5], coefC[NB][5], wM[NB][2], wC[NB][2], bandEnv[NB];
float vBuf[BLOCK], dBuf[BLOCK], carBuf[BLOCK], noiseBuf[BLOCK], tmpM[BLOCK], tmpC[BLOCK], tmpC2[BLOCK], vocOut[BLOCK];
float vocAgc = 6;

void setupVocoder() {
  for (int b = 0; b < NB; b++) {
    float f = 150.0f * powf(5500.0f / 150.0f, b / (float)(NB - 1));
    dsps_biquad_gen_bpf_f32(coefM[b], f / FS, 5.0f);
    dsps_biquad_gen_bpf_f32(coefC[b], f / FS, 5.0f);
  }
}

void vocChord(int i) {
  const Prog& p = PROGS[progIdx];
  int root = p.root[i & 3], q = p.q[i & 3];
  for (int k = 0; k < 4; k++) {
    int iv = QI[q][k];
    if (iv < 0) { vosc[k].on = false; continue; }
    int n = 48 + root + iv;
    while (n < 52) n += 12;
    while (n > 67) n -= 12;
    vosc[k].target = 440.0f * powf(2.0f, (n - 69) / 12.0f);
    if (!vosc[k].on) vosc[k].f = vosc[k].target;
    vosc[k].on = true;
  }
  int b = 36 + root; if (b > 43) b -= 12;
  vosc[4].target = 440.0f * powf(2.0f, (b - 69) / 12.0f);
  if (!vosc[4].on) vosc[4].f = vosc[4].target;
  vosc[4].on = true;
  monoFreq = vosc[4].target * 2;                    // LPC "robot mono" sings the chord root
  float ch = vosc[4].target;                        // CHUG: keep it in drop-tuned guitar range (~65-130 Hz)
  while (ch < 65) ch *= 2;
  chugHz = ch;
}

void chordLabel(char* out, size_t n) {
  const Prog& p = PROGS[progIdx];
  snprintf(out, n, "%s%s", NOTE[p.root[progPos & 3]], QN[p.q[progPos & 3]]);
}

static inline float blep(float t, float dt) {
  if (t < dt) { t /= dt; return t + t - t * t - 1; }
  if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
  return 0;
}

void vocodeBlock(bool clear) {
  for (int n = 0; n < BLOCK; n++) {
    float c = 0;
    for (int k = 0; k < 5; k++) {
      VOsc& o = vosc[k];
      if (!o.on) continue;
      o.f += (o.target - o.f) * 0.0015f;
      float dt = o.f * INV_FS, dt2 = dt * 1.007f;
      o.ph += dt; if (o.ph >= 1) o.ph -= 1;
      o.ph2 += dt2; if (o.ph2 >= 1) o.ph2 -= 1;
      c += (2 * o.ph - 1 - blep(o.ph, dt) + 2 * o.ph2 - 1 - blep(o.ph2, dt2)) * (k == 4 ? 0.8f : 0.5f);
    }
    carBuf[n] = softclip(c * 0.7f) * 1.6f;          // driven carrier: grittier robot
    noiseBuf[n] = nz();
  }
  memset(vocOut, 0, sizeof vocOut);
  for (int b = 0; b < NB; b++) {
    dsps_biquad_f32(vBuf, tmpM, BLOCK, coefM[b], wM[b]);
    if (b >= NB - 3) for (int n = 0; n < BLOCK; n++) tmpC[n] = carBuf[n] * 0.4f + noiseBuf[n] * 1.2f;
    else memcpy(tmpC, carBuf, sizeof tmpC);
    dsps_biquad_f32(tmpC, tmpC2, BLOCK, coefC[b], wC[b]);
    float env = bandEnv[b];
    for (int n = 0; n < BLOCK; n++) {
      float a = fabsf(tmpM[n]);
      env += (a - env) * (a > env ? 0.02f : 0.0025f);
      vocOut[n] += tmpC2[n] * env;
    }
    bandEnv[b] = env;
  }
  float pk = 0.0001f;
  for (int n = 0; n < BLOCK; n++) pk = max(pk, fabsf(vocOut[n]));
  float want = min(40.0f, 0.55f / pk);
  if (want < vocAgc) vocAgc += (want - vocAgc) * 0.3f;
  else if (pk > 0.002f) vocAgc += (want - vocAgc) * 0.01f;
  for (int n = 0; n < BLOCK; n++) vBuf[n] = vocOut[n] * vocAgc + (clear ? vBuf[n] * 0.45f : 0);
}

void setupAmp() {
  amp.setPins(AMP_BCLK, AMP_LRC, AMP_DIN);
  bool ok = amp.begin(I2S_MODE_STD, FS, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  logf("I2S %s\n", ok ? "OK" : "FAILED");
}

volatile uint32_t audioBlocks = 0;
TaskHandle_t audioHandle = nullptr;

void audioTask(void*) {
  static int16_t out[BLOCK * 2];
  float stepPos = 0, crushHold = 0, underLP = 0, under = 0, deckLP = 0, gateSm = 1, byteLP = 0;
  float megaHP = 0, megaLP = 0, megaLP2 = 0, compEnv = 0, subLP = 0;
  const float duckD = dcoef(0.16f);
  int crushCnt = 0;
  float stutterBase = -1, lpcStutter = -1;
  for (;;) {
    inputTick();
    tapeRate += ((tapeOn ? 0.0f : 1.0f) - tapeRate) * (tapeOn ? 0.03f : 0.08f);
    under += ((underOn ? 1.0f : 0.0f) - under) * 0.05f;
    float stepLen = FS * 60.0f / bpm / 4.0f;
    float master = VOLS[volIdx];
    bool busy = bankBusy;
    if (busy) { deck.on = false; for (auto& g : grains) g.on = false; }
    if (!stutterOn) { stutterBase = -1; lpcStutter = -1; }
    float lpcInc = 8000.0f * lpFormant * tapeRate * INV_FS;
    float byteInc = 8000.0f * bb.rate * tapeRate * INV_FS;

    for (int n = 0; n < BLOCK; n++) {
      // ---- 16th-note clock (always runs; the sequencer only fires while playing) ----
      stepPos += tapeRate;
      if (stepPos >= stepLen) {
        stepPos -= stepLen;
        int s = (curStep + 1) & 15;
        curStep = s;
        Step& st = seq[s];
        uint16_t d = 0;
        if (playing) {
          if (s == 0) { progPos = (progPos + 1) & 3; vocChord(progPos); if (bb.sync) bb.t = 0; }
          d = st.skip ? 0 : st.drum;
          if (groove) d |= grooveBits(s);
          d &= ~drumMute;
          if (!st.skip && !busy && st.word >= 0) {
            if (st.src == SRC_SAM && !scratchHeld) {
              if (st.flags & SF_SCRATCH) deckScratch(st.word, 1); else deckPlay(st.word, st.flags & SF_REV);
            } else if (st.src == SRC_TALK && !talkScratchHeld) {
              if (st.flags & SF_SCRATCH) { talkScratch(st.word); }
              else talkPlay(st.word, st.flags & SF_REV);
            }
          }
          if (!st.skip && st.byteF >= 0) { if (st.byteF == BYTE_STOP) byteStop(); else byteStart(st.byteF); }
        }
        d |= rollBits;
        if (rollBits && rec && playing) seq[s].drum |= rollBits;
        hitDrums(d);
        if (deck.scratch && deck.scratchSteps > 0 && --deck.scratchSteps == 0) { deck.scratch = false; deck.on = false; }
        if (lpc.scratch && !talkScratchHeld && (s & 1) == 0) { lpc.scratch = false; lpc.on = false; }
        st.skip = false;
        if (stutterOn && !busy) {                          // retrigger every 16th from where we were
          if (deck.word >= 0) {
            if (stutterBase < 0) stutterBase = deck.on ? deck.pos : 0;
            deck.pos = stutterBase; deck.on = true; deck.scratch = false;
          }
          if (lpc.word >= 0) {
            if (lpcStutter < 0) lpcStutter = lpc.on ? lpc.fpos : 0;
            lpc.fpos = lpcStutter; lpc.loaded = -1; lpc.on = true;
          }
          if (bb.on) bb.t = 0;
        }
      }
      stepFrac = stepPos / stepLen;

      // ---- SAM deck ----
      float v = 0, gTarget = 1;
      float hx = 0, hg = 1;                               // scratch hand, shared by SAM and LPC
      scratchShape(scStyle, wrap1(((curStep & 1) + stepPos / stepLen) * 0.5f), hx, hg);
      if (deck.on && deck.word >= 0) {
        const Word& w = bank[deck.word];
        if (deck.scratch) {
          float depth = min((float)w.len - 2, FS * 0.22f);
          deck.pos += (deck.base + hx * depth - deck.pos) * 0.35f;
          gTarget = hg;
        } else if (fx == FX_GRAIN || fx == FX_SWARM) {
          bool sw = fx == FX_SWARM;
          deck.pos += (deck.rev ? -1 : 1) * (sw ? 0.8f : 0.33f) * tapeRate;
          if (deck.pos < 0 || deck.pos >= w.len - 1) deck.on = false;
          if (--grainTimer <= 0) { spawnGrain(sw); grainTimer = sw ? FS / 160 : FS / 55; }
        } else {
          deck.pos += (deck.rev ? -1 : 1) * deck.rate * tapeRate;
          if (deck.pos < 0 || deck.pos >= w.len - 1) deck.on = false;
          if (stutterOn && stutterBase >= 0 && deck.pos > stutterBase + stepLen * 0.5f) gTarget = 0;
        }
        if (!(fx == FX_GRAIN || fx == FX_SWARM) || deck.scratch) v = wordAt(deck.word, deck.pos) * 1.3f;
      }
      if (fx == FX_GRAIN || fx == FX_SWARM) v += grainSum() * (fx == FX_SWARM ? 0.55f : 0.8f);
      gateSm += (gTarget - gateSm) * 0.25f;
      v *= gateSm;
      deckLP += 0.6f * (v - deckLP);
      v = deckLP;

      // ---- Talkie LPC (8 kHz engine, interpolated up) ----
      if (lpc.on) {
        if (lpc.scratch) {
          const TalkWord& t = talk[lpc.bank][lpc.word];
          float depth = min((float)t.n - 1, 12.0f);
          lpc.fpos += (lpc.base + hx * depth - lpc.fpos) * 0.02f;
        }
        lpc.ph += lpcInc;
        while (lpc.ph >= 1) { lpc.ph -= 1; lpc.y0 = lpc.y1; lpc.y1 = lpcTick(); }
        float lv = lpc.y0 + (lpc.y1 - lpc.y0) * lpc.ph;
        v += lv * (lpc.scratch ? hg : 1.0f) * 1.1f;
      }

      // ---- bytebeat (8 kHz, sample and hold, a little smoothing) ----
      if (bb.on) {
        bb.acc += byteInc;
        while (bb.acc >= 1) {
          bb.acc -= 1;
          bb.t += bb.rev ? -1 : 1;
          uint8_t c = byteFormula(bb.f, bb.t, bb.a, bb.b);
          bb.cur = bb.crush ? (c & 0xE0) : c;
        }
        byteLP += 0.5f * ((bb.cur - 128) * (1.0f / 128.0f) - byteLP);
        v += byteLP * 0.4f;
      } else byteLP *= 0.995f;
      byteVis[n] = bb.on ? bb.cur : 128;

      // ---- voice FX ----
      if (fx == FX_CRUSH) { if (++crushCnt >= 4) { crushCnt = 0; crushHold = roundf(v * 6) / 6; } v = crushHold; }
      else if (fx == FX_MEGA) {                         // megaphone: cut lows ~500 Hz, overdrive, cut highs ~2 kHz
        megaHP += 0.13f * (v - megaHP);
        float d = softclip((v - megaHP) * 5.0f);
        megaLP += 0.45f * (d - megaLP); megaLP2 += 0.45f * (megaLP - megaLP2);
        v = megaLP2 * 0.8f;
      }
      vBuf[n] = v;
      float dr = 0;
      for (int i = 0; i < 12; i++) dr += perc[i].s(PERC[i], tapeRate);
      dBuf[n] = dr * 0.8f;
    }

    if (fx == FX_VOX) vocodeBlock(true);

    float blockPeak = 0;
    for (int n = 0; n < BLOCK; n++) {
      float v = vBuf[n];
      float echo = 0;
      if (dlyBuf) {
        int dl = (int)(stepLen * 3);
        if (dl >= DLY_N) dl = DLY_N - 1;
        int r = dlyW - dl; if (r < 0) r += DLY_N;
        echo = dlyBuf[r];
        dlyBuf[dlyW] = (fx == FX_ECHO ? v : 0) + echo * 0.5f;
        if (++dlyW >= DLY_N) dlyW = 0;
      }
      float duck = 1.0f - 0.5f * duckEnv;              // voices + echo pump under the kick
      duckEnv *= duckD;
      float x = (v * 0.9f + echo * 0.6f) * duck + dBuf[n];
      subLP += 0.0113f * (x - subLP);                 // ~40 Hz highpass: bytebeat DC + sub the speaker can't play
      x -= subLP;
      underLP += (1.0f - under * 0.95f) * (x - underLP);
      // glue compressor: ~3:1 above 0.5, 3 ms attack / 180 ms release, then the old softclip
      float a = fabsf(underLP);
      compEnv += (a - compEnv) * (a > compEnv ? 0.015f : 0.00025f);
      float gr = compEnv > 0.5f ? powf(0.5f / compEnv, 0.667f) : 1.0f;
      float y = softclip(underLP * gr * 1.35f);
      scopeBuf[n] = y;
      if (fabsf(y) > blockPeak) blockPeak = fabsf(y);
      int16_t s = (int16_t)(y * master * 32000);
      out[2 * n] = s; out[2 * n + 1] = s;
    }
    outPeak = blockPeak;
    deckVis = deck.on ? deck.pos : -1;
    lpcVisFrame = lpc.on ? lpc.loaded : -1;
    amp.write((uint8_t*)out, sizeof(out));
    audioBlocks++;
  }
}

// =====================================================================
// Visuals
// =====================================================================
void brackets(int x, int y, int w, int h) {
  const int l = 5;
  oled.drawHLine(x, y, l);             oled.drawVLine(x, y, l);
  oled.drawHLine(x + w - l, y, l);     oled.drawVLine(x + w - 1, y, l);
  oled.drawHLine(x, y + h - 1, l);     oled.drawVLine(x, y + h - l, l);
  oled.drawHLine(x + w - l, y + h - 1, l); oled.drawVLine(x + w - 1, y + h - l, l);
}
int tab(int x, int y, const char* t) {
  oled.setFont(u8g2_font_4x6_tf);
  int w = oled.getStrWidth(t) + 4;
  oled.drawBox(x, y, w, 7);
  oled.setDrawColor(0); oled.drawStr(x + 2, y + 6, t); oled.setDrawColor(1);
  return w;
}
void dottedH(int x, int y, int w) { for (int i = 0; i < w; i += 2) oled.drawPixel(x + i, y); }

const char* padName(int m, int p) {
  switch (m) {
    case M_SAM: return bank[p].text;
    case M_TALK: return talk[talkBank][p].name;
    case M_BYTE: return BYTE_NAMES[p];
    default: return PERC[p].name;
  }
}

// a label squeezed into n chars (no spaces)
void shortName(const char* s, char* out, int n) {
  int j = 0;
  for (int i = 0; s[i] && j < n; i++) if (s[i] != ' ' && s[i] != '\'') out[j++] = s[i];
  out[j] = 0;
}

void drawTopBar() {
  int w = tab(0, 0, "SPEAK//HARD");
  oled.setFont(u8g2_font_4x6_tf);
  char t[12]; snprintf(t, sizeof t, "%d", bpm);
  oled.drawStr(w + 3, 6, t);
  int bx = w + 5 + oled.getStrWidth(t);
  int beat = curStep / 4;
  for (int i = 0; i < 4; i++) { if (playing && i == beat) oled.drawBox(bx + i * 5, 1, 4, 4); else oled.drawFrame(bx + i * 5, 1, 4, 4); }
  const char* mn = MODE_NAMES[mode];
  int mw = oled.getStrWidth(mn) + 4;
  oled.drawFrame(128 - mw, 0, mw, 7);
  oled.drawStr(128 - mw + 2, 6, mn);
  dottedH(0, 8, 128);
}

void drawSigPane() {
  const int x = 0, y = 11, w = 54, h = 40, mid = y + 22;
  brackets(x, y, w, h);
  tab(x + 2, y + 2, "SIG");
  int py = mid;
  for (int i = 0; i < 44; i++) {
    float v = scopeBuf[i * 128 / 44];
    int yy = constrain(mid - (int)(v * 14), y + 9, y + h - 4);
    if (i) oled.drawLine(x + 3 + i, py, x + 4 + i, yy);
    py = yy;
  }
  static float lvl = 0;
  lvl = max((float)outPeak, lvl * 0.9f);
  int segs = (int)(lvl * 8 + 0.5f);
  for (int i = 0; i < 8; i++) {
    int sy = y + h - 5 - i * 4;
    if (i < segs) oled.drawBox(x + w - 5, sy, 3, 3); else oled.drawPixel(x + w - 4, sy + 1);
  }
}

void drawPadPane() {                                 // the 12 pads as they sit on the keypad
  const int x0 = 56, y0 = 11, cw = 24, ch = 10;
  uint16_t k = keysVis;
  oled.setFont(u8g2_font_4x6_tf);
  for (int p = 0; p < 12; p++) {
    int r = p / 3, c = p % 3, x = x0 + c * cw, y = y0 + r * ch;
    bool down = k & (1 << (r * 4 + c + 1));
    bool active = (mode == M_BYTE && bb.on && bb.f == p) || (mode == M_DRUM && perc[p].env > 0.05f) ||
                  (mode == M_SAM && deck.on && deck.word == p) || (mode == M_TALK && lpc.on && lpc.word == p && lpc.bank == talkBank);
    char nm[8]; shortName(padName(mode, p), nm, 5);
    if (down || active) { oled.drawBox(x, y, cw - 1, ch - 1); oled.setDrawColor(0); }
    else oled.drawFrame(x, y, cw - 1, ch - 1);
    if (mode == M_DRUM && (drumMute & (1 << p))) oled.drawLine(x + 1, y + ch - 3, x + cw - 3, y + 1);
    oled.drawStr(x + 2, y + 7, nm);
    oled.setDrawColor(1);
  }
}

float envStrip[120];
int envWord = -2;
void buildEnvStrip(int w) {
  envWord = w;
  for (int k = 0; k < 120; k++) envStrip[k] = 0;
  if (w < 0 || bank[w].len == 0) return;
  uint32_t len = bank[w].len;
  for (int k = 0; k < 120; k++) {
    uint32_t a = len * k / 120, b = len * (k + 1) / 120;
    int pk = 0;
    for (uint32_t i = a; i < b; i += 4) pk = max(pk, abs((int)pool[bank[w].off + i]));
    envStrip[k] = pk / 128.0f;
  }
}

void drawVizStrip() {                                // y 53..72: mode-specific view
  const int y = 53, h = 20, mid = y + 12;
  char lab[24];
  oled.setFont(u8g2_font_4x6_tf);
  if (mode == M_SAM) {
    int wd = deck.on ? deck.word : curPad;
    if (wd != envWord) buildEnvStrip(wd);
    for (int k = 0; k < 120; k++) { int a = (int)(envStrip[k] * 7); if (a) oled.drawVLine(4 + k, mid - a, a * 2 + 1); else oled.drawPixel(4 + k, mid); }
    float pos = deckVis;
    if (pos >= 0 && wd >= 0 && bank[wd].len) {
      int px = 4 + (int)(pos / bank[wd].len * 119);
      oled.setDrawColor(2); oled.drawVLine(px, y + 2, h - 2); oled.setDrawColor(1);
    }
    snprintf(lab, sizeof lab, "%s", deck.scratch ? "SCRATCH" : VOICES[voiceIdx].name);
  } else if (mode == M_TALK) {
    int fi = lpcVisFrame;
    if (fi >= 0) {                                   // 10 reflection coefficients + energy
      int32_t kk[10] = {lpc.k1 >> 8, lpc.k2 >> 8, lpc.k[0], lpc.k[1], lpc.k[2], lpc.k[3], lpc.k[4], lpc.k[5], lpc.k[6], lpc.k[7]};
      for (int i = 0; i < 10; i++) {
        int a = constrain((int)(kk[i] * 8 / 128), -8, 8), bx = 30 + i * 8;
        if (a > 0) oled.drawBox(bx, mid - a, 6, a); else if (a < 0) oled.drawBox(bx, mid, 6, -a); else oled.drawHLine(bx, mid, 6);
      }
      int e = constrain((int)(lpc.energy * 18 / 255), 0, 18);
      oled.drawFrame(116, y + 1, 8, 19); oled.drawBox(118, y + 19 - e, 4, e);
      const TalkWord& t = talk[lpc.bank][lpc.word];
      if (t.n) oled.drawHLine(30, y + 19, (int)(fi * 78 / t.n) + 1);
    } else for (int i = 0; i < 10; i++) oled.drawHLine(30 + i * 8, mid, 6);
    oled.drawStr(2, mid + 3, lpMono ? "MONO" : (lpWhisper ? "WHSP" : "LPC"));
    snprintf(lab, sizeof lab, "%s", BANK_NAMES[talkBank]);
  } else if (mode == M_BYTE) {
    for (int i = 0; i < 120; i++) oled.drawPixel(4 + i, y + 19 - byteVis[i] * 18 / 255);
    snprintf(lab, sizeof lab, "A%d B%d x%g%s%s", bb.a, bb.b, bb.rate, bb.crush ? " CR" : "", bb.rev ? " RV" : "");
  } else {
    for (int i = 0; i < 12; i++) {
      int e = constrain((int)(perc[i].env * 16), 0, 16);
      oled.drawFrame(4 + i * 10, y + 2, 8, 18);
      if (e) oled.drawBox(5 + i * 10, y + 19 - e, 6, e);
    }
    snprintf(lab, sizeof lab, "%s", drumMute ? "MUTES ON" : "");
  }
  if (lab[0]) tab(128 - oled.getStrWidth(lab) - 4, y, lab);
}

void drawReadout() {                                 // current item + FX line
  oled.setFont(u8g2_font_6x10_tf);
  char wq[28]; snprintf(wq, sizeof wq, "[%s]", padName(mode, curPad));
  if (mode == M_BYTE) snprintf(wq, sizeof wq, "[%s]", bb.on ? BYTE_NAMES[bb.f] : "--");
  oled.setFont(u8g2_font_5x8_tf);
  oled.drawStr(64 - oled.getStrWidth(wq) / 2, 81, wq);
  oled.setFont(u8g2_font_4x6_tf);
  char ch[12] = "--";
  if (fx == FX_VOX || (mode == M_DRUM && !(drumMute & (1 << D_CHUG)))) chordLabel(ch, sizeof ch);
  char info[40]; snprintf(info, sizeof info, "FX:%s SCR:%s CH:%s", FX_NAMES[fx], SC_NAMES[scStyle], ch);
  oled.drawStr(64 - oled.getStrWidth(info) / 2, 88, info);
}

void drawSeqPane() {                                 // 16 steps: VOX / DRM / BYT lanes
  const int y = 90, h = 29;
  brackets(0, y, 128, h);
  for (int s = 0; s < 16; s++) {
    int x = 4 + s * 7 + (s / 4);
    const Step& st = seq[s];
    int lv = y + 3, ld = y + 12, lb = y + 21;
    if (st.word >= 0) {
      if (st.flags & SF_SCRATCH) { oled.drawLine(x, lv + 6, x + 2, lv); oled.drawLine(x + 2, lv, x + 5, lv + 6); }
      else if (st.src == SRC_TALK) oled.drawFrame(x, lv, 6, 7);
      else oled.drawBox(x, lv, 6, 7);
    } else oled.drawPixel(x + 2, lv + 3);
    uint16_t d = st.drum;
    if (groove) d |= grooveBits(s);
    d &= ~drumMute;
    if (d & 1) oled.drawBox(x, ld, 6, 6);
    else if (d & 6) oled.drawFrame(x, ld, 6, 6);
    else if (d) oled.drawPixel(x + 2, ld + 3);
    if (st.byteF >= 0) { if (st.byteF == BYTE_STOP) oled.drawHLine(x, lb + 3, 6); else oled.drawTriangle(x, lb, x, lb + 6, x + 5, lb + 3); }
    if (s == curStep && playing) { oled.setDrawColor(2); oled.drawBox(x - 1, y + 2, 8, h - 4); oled.setDrawColor(1); }
  }
}

void drawChips() {
  struct { const char* t; bool on; } chips[6] = {
    {"REC", rec}, {"PLAY", playing}, {"GRV", groove}, {"STUT", stutterOn}, {"REV", reverseOn}, {"TAPE", tapeOn}};
  oled.setFont(u8g2_font_4x6_tf);
  for (int i = 0; i < 6; i++) {
    int x = i * 21 + 1, w = 20;
    if (chips[i].on && !(i == 0 && ((millis() / 300) & 1))) {
      oled.drawBox(x, 120, w, 8);
      oled.setDrawColor(0); oled.drawStr(x + (w - oled.getStrWidth(chips[i].t)) / 2, 126, chips[i].t); oled.setDrawColor(1);
    } else {
      dottedH(x, 120, w); dottedH(x, 127, w);
      oled.drawStr(x + (w - oled.getStrWidth(chips[i].t)) / 2, 126, chips[i].t);
    }
  }
}

// full-screen menu while a left-column key is held: 12 labels laid out like the pads
void drawMenu(int m) {
  static const char* MODE_L[12] = {"SAM", "TALK", "BYTE", "DRUM", "VOL-", "VOL+", "BANK", "ROBOT", "SAM", "ELF", "E.T.", "BRUTE"};
  static const char* FX_L[12] = {"CLEAN", "CRUSH", "ECHO", "GRAIN", "SWARM", "VOX", "MEGA", "STUTTR", "REVRS", "TAPE", "UNDER", "SCRTCH"};
  static const char* SEQ_L[12] = {"REC", "PLAY", "GROOVE", "AUTO", "BPM-", "BPM+", "MUTATE", "CHORDS", "CLR VOX", "CLR DRM", "CLR BYT", "CLR ALL"};
  static const char* TALK_L[12] = {"WHISPER", "MONO", "FREEZE", "STRETCH", "PITCH-", "PITCH+", "PITCH 0", "GLITCH", "BACKWRD", "FORMNT-", "FORMNT+", "RESET"};
  static const char* BYTE_L[12] = {"RATE/2", "RATE*2", "A-", "A+", "B-", "B+", "SYNC", "STOP", "CRUSH", "REVERSE", "RANDOM", "RESET"};
  char title[28];
  snprintf(title, sizeof title, "%s", MOD_NAMES[m]);
  if (m == K_SHIFT) snprintf(title, sizeof title, "SHIFT: %s", mode == M_SAM ? "SCRATCH" : mode == M_TALK ? "LPC BENDS" : mode == M_BYTE ? "BYTEBEAT" : "MUTE");
  if (m == K_MODE) snprintf(title, sizeof title, "MODE  vol %d  %s", volIdx + 1, BANK_NAMES[talkBank]);
  if (m == K_SEQ) snprintf(title, sizeof title, "SEQ  %d BPM", bpm);
  tab(0, 0, title);
  oled.setFont(u8g2_font_5x8_tf);
  const int cw = 42, ch = 28, y0 = 10;
  for (int p = 0; p < 12; p++) {
    int r = p / 3, c = p % 3, x = c * cw + 1, y = y0 + r * ch;
    const char* l = "";
    bool on = false;
    switch (m) {
      case K_MODE: l = MODE_L[p]; on = (p < 4 && p == mode) || (p >= 7 && p - 7 == voiceIdx); break;
      case K_FX: l = FX_L[p]; on = (p < 7 && p == fx) || (p == 7 && stutterOn) || (p == 8 && reverseOn) || (p == 9 && tapeOn) || (p == 10 && underOn); break;
      case K_SEQ: l = SEQ_L[p]; on = (p == 0 && rec) || (p == 1 && playing) || (p == 2 && groove); break;
      default:
        if (mode == M_TALK) { l = TALK_L[p]; on = (p == 0 && lpWhisper) || (p == 1 && lpMono) || (p == 2 && lpFreeze) || (p == 3 && lpStretch) || (p == 7 && lpGlitch) || (p == 8 && lpRev); }
        else if (mode == M_BYTE) { l = BYTE_L[p]; on = (p == 6 && bb.sync) || (p == 8 && bb.crush) || (p == 9 && bb.rev); }
        else if (mode == M_DRUM) { l = PERC[p].name; on = drumMute & (1 << p); }
        else l = bank[p].text;
        break;
    }
    char nm[10]; strncpy(nm, l, 8); nm[8] = 0;
    if (on) { oled.drawBox(x, y, cw - 2, ch - 2); oled.setDrawColor(0); } else oled.drawFrame(x, y, cw - 2, ch - 2);
    oled.drawStr(x + (cw - 2 - oled.getStrWidth(nm)) / 2, y + 16, nm);
    oled.setDrawColor(1);
  }
}

void drawAlert(const char* m) {
  oled.setFont(u8g2_font_6x10_tf);
  char t[32]; snprintf(t, sizeof t, ">> %s", m);
  int w = oled.getStrWidth(t) + 10, x = max(2, 64 - w / 2);
  oled.setDrawColor(0); oled.drawBox(x - 2, 40, w + 4, 20); oled.setDrawColor(1);
  oled.drawFrame(x - 2, 40, w + 4, 20); oled.drawFrame(x, 42, w, 16);
  oled.drawStr(x + 5, 53, t);
}

void drawFrame() {
  oled.clearBuffer();
  if (bankBusy) {
    tab(0, 0, "SPEAK//HARD");
    oled.setFont(u8g2_font_4x6_tf);
    oled.drawStr(0, 30, "> VOICE CORE");
    char v[24]; snprintf(v, sizeof v, "> LOADING %s", VOICES[voiceIdx].name);
    oled.drawStr(0, 40, v);
    char wtxt[32]; snprintf(wtxt, sizeof wtxt, "> %s", bank[min(renderProgress, 11)].text);
    oled.drawStr(0, 50, wtxt);
    brackets(10, 60, 108, 14);
    int segs = 20 * renderProgress / 12;
    for (int i = 0; i < 20; i++) { if (i < segs) oled.drawBox(13 + i * 5, 63, 4, 8); else oled.drawPixel(15 + i * 5, 67); }
    oled.sendBuffer();
    return;
  }
  int hm = heldModVis;
  if (hm >= 0) { drawMenu(hm); if (millis() - msgAt < 700) drawAlert(msg); oled.sendBuffer(); return; }
  drawTopBar();
  drawSigPane();
  drawPadPane();
  drawVizStrip();
  drawReadout();
  drawSeqPane();
  drawChips();
  if (millis() - msgAt < 900) drawAlert(msg);
  oled.sendBuffer();
}

// =====================================================================
void setup() {
  Serial.setTxTimeoutMs(0);
  Serial.begin(115200);
  delay(300);
  for (int c = 0; c < 4; c++) pinMode(COL_PINS[c], INPUT_PULLUP);
  for (int r = 0; r < 4; r++) pinMode(ROW_PINS[r], INPUT);
  for (int i = 0; i <= 1024; i++) sineTab[i] = sinf(2.0f * PI * i / 1024.0f);
  for (int p = 0; p < 12; p++) padMenu[p] = -1;
  rgbLedWrite(PIN_RGB, 0, 0, 0);

  oled.setBusClock(400000);
  oled.begin();
  oled.setContrast(140);

  if (psramFound()) { poolSize = 768 * 1024; pool = (int8_t*)ps_malloc(poolSize); dlyBuf = (float*)ps_calloc(DLY_N, sizeof(float)); }
  if (!pool) { poolSize = 120 * 1024; pool = (int8_t*)malloc(poolSize); }
  if (!dlyBuf) dlyBuf = (float*)calloc(DLY_N, sizeof(float));
  logf("speakbeat_hard: PSRAM %s, pool %u KB, free heap %u\n", psramFound() ? "yes" : "no", poolSize / 1024, ESP.getFreeHeap());
  decodeTalk();

  nstate = esp_random() | 1;
  randomSeed(esp_random());
  clearAll();

  setupVocoder();
  vocChord(0);
  setupAmp();
  xTaskCreatePinnedToCore(audioTask, "audio", 16384, nullptr, 10, &audioHandle, 0);
  drawFrame();
  renderBank(voiceIdx);
  talkPlay(2, false);                               // "IGNITE"
}

void loop() {
  static int renderedVoice = 0;
  if (voiceIdx != renderedVoice) {
    renderedVoice = voiceIdx;
    renderBank(renderedVoice);
    flash(VOICES[renderedVoice].name);
    deckPlay(curPad, false);
  }
  drawFrame();
  static uint32_t lastLog = 0;
  if (STATUS_LOG && millis() - lastLog > 2000) {
    lastLog = millis();
    logf("t=%lus mode %s fx %s step %d blocks %lu heap %u stackfree %u keys %04X\n", millis() / 1000,
         MODE_NAMES[mode], FX_NAMES[fx], curStep, (unsigned long)audioBlocks, (unsigned)ESP.getFreeHeap(),
         audioHandle ? (unsigned)uxTaskGetStackHighWaterMark(audioHandle) : 0u, keysRaw);
  }
  delay(5);
}
