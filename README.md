# ESP32-C3 MJPEG Video Player

Plays a Motion-JPEG (`.mjpeg`) video from an SD card on a 128x160 ST7735 SPI TFT display, using an **ESP32-C3 Super Mini**. Frames are decoded on the fly with JPEGDEC and streamed straight to the display — no full-frame buffer, so it fits comfortably in the C3's limited RAM (no PSRAM).

## Hardware

- ESP32-C3 Super Mini
- 1.8" 128x160 SPI TFT display (ST7735 driver)
- MicroSD card module (behind/integrated with the TFT board, 4-pin SPI: CS, MOSI, MISO, SCK)
- MicroSD card, FAT32 formatted, Class 10+

## Wiring

TFT and SD card share a single SPI bus (SCK/MOSI/MISO); each has its own CS pin.

| Signal | ESP32-C3 GPIO | Connects to |
|---|---|---|
| SPI SCK | 4 | TFT SCK **and** SD SCK |
| SPI MOSI | 6 | TFT SDA/MOSI/DIN **and** SD MOSI |
| SPI MISO | 5 | SD MISO *(TFT usually has no MISO)* |
| TFT CS | 7 | TFT CS |
| TFT DC | 0 | TFT DC / A0 / RS |
| TFT RST | 1 | TFT RESET |
| SD CS | 10 | SD CS |
| 3V3 | — | TFT VCC, TFT LED/backlight |
| GND | — | TFT GND, SD GND |

> GPIO 2, 8, and 9 are avoided intentionally — they're boot-strapping pins on the ESP32-C3 and can interfere with flashing/boot if used for other signals.

## Libraries required

Install via Arduino Library Manager:
- `Adafruit GFX Library`
- `Adafruit ST7735 and ST7789 Library` (v1.10.0+, needed for `setColRowStart()`)
- `SD` (use the ESP32 core's bundled version, not a separate one)
- `JPEGDEC`

## Display offset tuning

Some 128x160 ST7735 panels have a memory offset that doesn't match either built-in "tab" preset (`INITR_BLACKTAB`/`INITR_GREENTAB`), causing a couple of stray/static pixels along one edge. This sketch sets the offset manually:

```cpp
#define TFT_COLSTART 2
#define TFT_ROWSTART 1
```

If you see static pixels on an edge, adjust these two values by 1–2 at a time and re-flash until it clears.

## Preparing a video

Convert any video file to the required raw MJPEG format with `ffmpeg`:

```bash
ffmpeg -i input.mp4 -vf "scale=160:128" -r 15 -q:v 5 -pix_fmt yuvj420p -f mjpeg video.mjpeg
```

- `scale=160:128` — match the display resolution (landscape)
- `-r 15` — match `TARGET_FPS` in the sketch
- `-q:v 5` — JPEG quality (2=best/largest, 31=worst/smallest); raise if frames don't fit the buffer
- `-pix_fmt yuvj420p` — forces baseline (non-progressive) JPEG, required by JPEGDEC

Copy the resulting `video.mjpeg` to the root of the SD card.

## Usage

1. Wire everything per the table above.
2. Flash the sketch (board: **ESP32C3 Dev Module**).
3. Insert the SD card with `/video.mjpeg` on it and power on.
4. Video loops automatically when it reaches the end.

## Notes

- `FRAME_BUF_SIZE` (default 20KB) must be larger than your largest single compressed frame. If you see `WARN: frame incomplete or buffer too small` in Serial output, either increase this or increase JPEG compression (`-q:v` higher).
- The C3 Super Mini has no PSRAM, so keep resolution/quality modest — this project targets 160x128 @ 15fps.
