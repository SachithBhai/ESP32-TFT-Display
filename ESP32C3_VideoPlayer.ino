#include <SPI.h>
#include <SD.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>   // CHANGED from Adafruit_ILI9341 -> most 128x160 "1.8 TFT" modules use ST7735
#include <JPEGDEC.h>

// ===================================================================
// ESP32-C3 SUPER MINI PIN MAP
// The C3 Super Mini has no fixed/default hardware SPI pins like a
// classic ESP32, and only ~13 usable GPIOs. GPIO2, GPIO8, GPIO9 are
// boot-strapping pins on the C3 -- avoided below on purpose.
//
// TFT and SD card share ONE SPI bus (SCK/MOSI/MISO); each device gets
// its own CS line.
// ===================================================================

// --- Shared SPI bus ---
#define TFT_SCK   4
#define TFT_MISO  5
#define TFT_MOSI  6

// --- TFT Display Pins ---
#define TFT_CS    7
#define TFT_DC    0
#define TFT_RST   1

// --- SD Card Pin (module behind the display, shares the bus above) ---
#define SD_CS     10

// --- Frame buffer for one JPEG frame ---
// The ESP32-C3 Super Mini has NO PSRAM and only ~400KB total SRAM,
// so this buffer must be much smaller than on a full ESP32 dev board.
// A 128x160 frame's compressed JPEG size is normally well under 20KB.
// Raise this only if you see "frame incomplete or buffer too small" warnings,
// and watch your free heap in the Serial Monitor if you do.
#define FRAME_BUF_SIZE (20 * 1024)
static uint8_t *frameBuf = nullptr;

// --- Target playback rate ---
#define TARGET_FPS 15
#define FRAME_INTERVAL_MS (1000 / TARGET_FPS)

// --- Manual GRAM offset tuning (see setup() for why) ---
// Neither INITR_BLACKTAB nor INITR_GREENTAB matched this panel's offset table,
// so we set the column/row start manually. Adjust these two numbers to remove
// the stray "static" pixels -- see the tuning guide below.
#define TFT_COLSTART 2
#define TFT_ROWSTART 1

// setColRowStart() is declared "protected" in this library version, so it can't
// be called directly on a plain Adafruit_ST7735 object. This subclass exposes it.
class ST7735_Custom : public Adafruit_ST7735 {
  public:
    ST7735_Custom(int8_t cs, int8_t dc, int8_t rst) : Adafruit_ST7735(cs, dc, rst) {}
    using Adafruit_ST7735::setColRowStart;
};

ST7735_Custom tft = ST7735_Custom(TFT_CS, TFT_DC, TFT_RST);
JPEGDEC jpeg;
File videoFile;

// Drawing callback function
int JPEGDraw(JPEGDRAW *pDraw) {
  tft.drawRGBBitmap(pDraw->x, pDraw->y, pDraw->pPixels, pDraw->iWidth, pDraw->iHeight);
  return 1;
}

// Reads one JPEG frame (SOI 0xFFD8 ... EOI 0xFFD9) from videoFile into frameBuf.
// Returns frame size in bytes, or 0 if no more frames / error.
size_t readNextFrame() {
  // 1. Find SOI marker (0xFF 0xD8)
  int b1 = -1, b2 = -1;
  bool foundSOI = false;

  while (videoFile.available() >= 2) {
    b1 = videoFile.read();
    if (b1 == 0xFF) {
      b2 = videoFile.peek();
      if (b2 == 0xD8) {
        videoFile.read(); // consume the 0xD8
        foundSOI = true;
        break;
      }
    }
  }

  if (!foundSOI) return 0; // EOF reached without finding a new frame

  frameBuf[0] = 0xFF;
  frameBuf[1] = 0xD8;
  size_t idx = 2;

  // 2. Read bytes until EOI marker (0xFF 0xD9) is found
  int prevByte = 0;
  while (videoFile.available() && idx < FRAME_BUF_SIZE) {
    int curByte = videoFile.read();
    frameBuf[idx++] = (uint8_t)curByte;
    if (prevByte == 0xFF && curByte == 0xD9) {
      return idx; // complete frame captured
    }
    prevByte = curByte;
  }

  // Ran out of buffer space or file ended mid-frame
  Serial.print("WARN: frame incomplete or buffer too small, got ");
  Serial.print(idx);
  Serial.println(" bytes before running out of buffer/file");
  return 0;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- ESP32-C3 Super Mini Video Player Initializing ---");

  frameBuf = (uint8_t *)malloc(FRAME_BUF_SIZE);
  if (!frameBuf) {
    Serial.println("ERROR: Could not allocate frame buffer! Reduce FRAME_BUF_SIZE.");
    while (1) delay(1000);
  }
  Serial.print("Free heap after buffer alloc: ");
  Serial.println(ESP.getFreeHeap());

  // Bring up the shared SPI bus with EXPLICIT pins.
  // (ESP32-C3 does not have a fixed default SPI pin mapping like a classic ESP32.)
  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);

  tft.initR(INITR_BLACKTAB);
  // Neither BLACKTAB nor GREENTAB's built-in offsets matched this panel, so set the
  // column/row start manually instead (requires library version >= 1.10.0).
  // TUNING GUIDE: change TFT_COLSTART / TFT_ROWSTART above by 1 or 2 at a time and
  // re-flash. Try (0,0), (1,1), (1,2), (2,1), (2,2), (3,2) etc. until the stray
  // pixels disappear. Note: which value clears WHICH edge (top vs right) can flip
  // depending on setRotation() below, so re-check after any rotation change too.
  tft.setColRowStart(TFT_COLSTART, TFT_ROWSTART);
  tft.setRotation(1); // Landscape orientation
  tft.fillScreen(ST77XX_BLACK);
  Serial.println("Display Initialized.");

  if (!SD.begin(SD_CS, SPI)) {
    Serial.println("ERROR: SD Card initialization failed!");
    while (1) delay(1000);
  }
  Serial.println("SD Card initialized successfully!");

  if (!SD.exists("/video.mjpeg")) {
    Serial.println("ERROR: /video.mjpeg not found on SD card!");
    while (1) delay(1000);
  }

  videoFile = SD.open("/video.mjpeg", FILE_READ);
  if (!videoFile) {
    Serial.println("ERROR: Could not open /video.mjpeg!");
    while (1) delay(1000);
  }
  Serial.println("Found and opened /video.mjpeg on SD card!");
}

void loop() {
  unsigned long frameStart = millis();
  size_t frameSize = readNextFrame();

  if (frameSize == 0) {
    // End of file (or bad frame) -- loop the video back to the start
    Serial.println("End of video, looping...");
    videoFile.seek(0);
    return;
  }

  if (jpeg.openRAM(frameBuf, frameSize, JPEGDraw)) {
    jpeg.decode(0, 0, 0);
    jpeg.close();
  } else {
    Serial.print("ERROR: jpeg.openRAM failed on this frame, size=");
    Serial.println(frameSize);
  }

  // Pace playback to target FPS
  unsigned long elapsed = millis() - frameStart;
  if (elapsed < FRAME_INTERVAL_MS) {
    delay(FRAME_INTERVAL_MS - elapsed);
  }
}
