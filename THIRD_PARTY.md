# Third-party code

Bundled in `firmware/speakbeat_hard/`:

| Files | Origin | License as stated by the source |
|---|---|---|
| `sam.c`, `render.c`, `reciter.c`, `*Tabs.h`, `SamData.h`, `sam.h`, `render.h`, `reciter.h`, `esp8266sam_debug.*` | SAM, Software Automatic Mouth (Don't Ask Software, 1982), C port by Sebastian Macke (github.com/s-macke/SAM), ESP8266 adaptation by Earle F. Philhower III (github.com/earlephilhower/ESP8266SAM) | ESP8266SAM is distributed under GPL v3. The original 1982 SAM is old commercial software whose copyright status the C port's author describes as unclear. |
| `Vocab_US_Large.*`, `Vocab_AstroBlaster.*`, `Vocab_Soundbites.*`, `Vocab_Toms_Diner.*` | Talkie by Peter Knight (2011), vocabularies converted to .c/.h by Armin Joachimsmeyer (github.com/ArminJo/Talkie) | File headers say "released under GPLv2 license" |
| LPC tables and lattice filter inside `speakbeat_hard.ino` | Talkie by Peter Knight, via ESP8266Audio's AudioGeneratorTalkie (github.com/earlephilhower/ESP8266Audio) | GPL v3 (ESP8266Audio) |

Used as libraries, not bundled:

| Library | Author | License |
|---|---|---|
| U8g2 | olikraus | 2-clause BSD |
| esp-dsp | Espressif | Apache 2.0 |
| arduino-esp32 core (ESP_I2S, Wire) | Espressif | LGPL 2.1 |

The bytebeat formulas are well-known one-liners from the bytebeat scene started by viznut in 2011.
