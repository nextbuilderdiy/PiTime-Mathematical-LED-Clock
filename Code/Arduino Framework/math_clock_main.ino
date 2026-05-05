/*
 ============================================================
  Math Clock — ESP32-H2-Zero Edition
 ============================================================
  Hardware:
    - Waveshare ESP32-H2-Zero
    - DS3231 RTC Module (I2C: SDA=GPIO4, SCL=GPIO5)
    - 12x WS2812B NeoPixel LEDs  (Data = GPIO2)
    - 1x WS2812B Logo LED        (Data = GPIO10)
    - 18650 Li-Ion battery + TP4056 charging module

  LED Colour Logic:
    HOUR only    → RED   (255, 0,   0  )
    MINUTE only  → GREEN (0,   255, 0  )
    HOUR+MINUTE  → BLUE  (0,   0,   255)
    OFF          → (0, 0, 0)

  Logo LED:
    Changes colour every 5 seconds, cycling through
    a set of pleasant colours automatically.

  No seconds LED — battery saving.
  RTC re-synced every 30 seconds.

  Upload via Arduino IDE:
    Board   : ESP32H2 Dev Module
    Port    : your COM port
 ============================================================
*/

#include <Wire.h>
#include <RTClib.h>           // Adafruit RTClib — install via Library Manager
#include <Adafruit_NeoPixel.h> // Adafruit NeoPixel — install via Library Manager

// ─── Pin Configuration ─────────────────────────────────────────────────────
#define LED_RING_PIN     2    // GPIO2  → 12x WS2812B clock ring
#define LED_LOGO_PIN    10    // GPIO10 → 1x WS2812B logo LED
#define I2C_SDA          4    // GPIO4  → DS3231 SDA
#define I2C_SCL          5    // GPIO5  → DS3231 SCL
#define NUM_RING_LEDS   12    // Clock ring LED count
#define NUM_LOGO_LEDS    1    // Logo LED count

// ─── Brightness (0–255) ────────────────────────────────────────────────────
#define BRIGHTNESS      80    // Lower = dimmer = longer battery life

// ─── Colours ───────────────────────────────────────────────────────────────
#define COLOR_OFF        strip.Color(0, 0, 0)
#define COLOR_HOUR       strip.Color(scale(255), scale(0),   scale(0)  )
#define COLOR_MINUTE     strip.Color(scale(0),   scale(255), scale(0)  )
#define COLOR_BOTH       strip.Color(scale(0),   scale(0),   scale(255))

// Logo LED colour palette — cycles every 5 seconds
const uint8_t LOGO_COLORS[][3] = {
  {255,  80,   0},   // Warm orange
  {  0, 200, 255},   // Cyan
  {180,   0, 255},   // Purple
  {255, 220,   0},   // Golden yellow
  {  0, 255, 120},   // Mint green
  {255,  20, 100},   // Hot pink
  {255, 255, 255},   // White
};
#define NUM_LOGO_COLORS  7
#define LOGO_INTERVAL  5000  // ms between logo colour changes

// ─── Objects ───────────────────────────────────────────────────────────────
Adafruit_NeoPixel strip(NUM_RING_LEDS, LED_RING_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel logo(NUM_LOGO_LEDS,  LED_LOGO_PIN, NEO_GRB + NEO_KHZ800);
RTC_DS3231 rtc;

// ─── State ─────────────────────────────────────────────────────────────────
int  last_h       = -1;
int  last_m       = -1;
int  logo_index   = 0;
unsigned long last_rtc_sync  = 0;
unsigned long last_logo_change = 0;
int  disp_h = 0;
int  disp_m = 0;

// ─── Helper: scale colour by BRIGHTNESS ────────────────────────────────────
uint8_t scale(uint8_t val) {
  return (uint8_t)((uint32_t)val * BRIGHTNESS / 255);
}

// ─── Map hour (0-23) to LED index (0-11) ───────────────────────────────────
int hourToLed(int hour24) {
  return hour24 % 12;   // 0 and 12 both → LED 0 (12 o'clock)
}

// ─── Map minute (0-59) to LED index (0-11) ─────────────────────────────────
int minuteToLed(int minute) {
  return minute / 5;    // 0-4→0, 5-9→1 … 55-59→11
}

// ─── Update the 12-LED clock ring ──────────────────────────────────────────
void updateRing(int hour24, int minute) {
  int h_led = hourToLed(hour24);
  int m_led = minuteToLed(minute);

  for (int i = 0; i < NUM_RING_LEDS; i++) {
    if (i == h_led && i == m_led) {
      strip.setPixelColor(i, COLOR_BOTH);     // Overlap → blue
    } else if (i == h_led) {
      strip.setPixelColor(i, COLOR_HOUR);     // Hour only → red
    } else if (i == m_led) {
      strip.setPixelColor(i, COLOR_MINUTE);   // Minute only → green
    } else {
      strip.setPixelColor(i, COLOR_OFF);      // Off
    }
  }
  strip.show();
}

// ─── Update the logo LED ───────────────────────────────────────────────────
void updateLogo(int index) {
  uint8_t r = scale(LOGO_COLORS[index][0]);
  uint8_t g = scale(LOGO_COLORS[index][1]);
  uint8_t b = scale(LOGO_COLORS[index][2]);
  logo.setPixelColor(0, logo.Color(r, g, b));
  logo.show();
}

// ─── Clear all LEDs ────────────────────────────────────────────────────────
void clearAll() {
  for (int i = 0; i < NUM_RING_LEDS; i++) strip.setPixelColor(i, COLOR_OFF);
  strip.show();
  logo.setPixelColor(0, logo.Color(0, 0, 0));
  logo.show();
}

// ─── Startup sweep animation ───────────────────────────────────────────────
void startupTest() {
  // Sweep ring: red → green → blue
  uint32_t colors[] = {
    strip.Color(scale(255), 0, 0),
    strip.Color(0, scale(255), 0),
    strip.Color(0, 0, scale(255))
  };
  for (int c = 0; c < 3; c++) {
    for (int i = 0; i < NUM_RING_LEDS; i++) {
      strip.setPixelColor(i, colors[c]);
      strip.show();
      delay(55);
    }
  }
  delay(200);
  clearAll();
  delay(200);
}

// ─── RTC error: flash ring red/blue until RTC comes back ──────────────────
void showRtcError() {
  Serial.println("ERROR: DS3231 not found! Check SDA(GPIO4) and SCL(GPIO5) wiring.");
  bool toggle = true;
  while (!rtc.begin()) {
    uint32_t color = toggle
      ? strip.Color(scale(255), 0, 0)
      : strip.Color(0, 0, scale(255));
    for (int i = 0; i < NUM_RING_LEDS; i++) strip.setPixelColor(i, color);
    strip.show();
    toggle = !toggle;
    delay(300);
  }
  clearAll();
}

// ─── Setup ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nMath Clock — ESP32-H2-Zero starting up...");

  // Start NeoPixels
  strip.begin();
  strip.show();
  logo.begin();
  logo.show();

  // Start I2C on GPIO4/GPIO5
  Wire.begin(I2C_SDA, I2C_SCL);

  // Start RTC
  if (!rtc.begin()) {
    showRtcError();  // Loops until RTC found
  }

  // Warn if RTC lost power (first use or dead coin cell)
  if (rtc.lostPower()) {
    Serial.println("WARNING: RTC lost power — time may be wrong.");
    Serial.println("Set time by uploading the SetTime sketch (see instructions).");
  }

  // Startup LED sweep
  startupTest();

  // Read initial time
  DateTime now = rtc.now();
  disp_h = now.hour();
  disp_m = now.minute();
  last_rtc_sync = millis();

  Serial.print("Current time: ");
  Serial.print(disp_h); Serial.print(":");
  if (disp_m < 10) Serial.print("0");
  Serial.println(disp_m);

  updateRing(disp_h, disp_m);
  last_h = disp_h;
  last_m = disp_m;

  // Show first logo colour
  updateLogo(logo_index);
  last_logo_change = millis();
}

// ─── Main loop ─────────────────────────────────────────────────────────────
void loop() {
  unsigned long now_ms = millis();

  // ── Re-sync from RTC every 30 seconds ──
  if (now_ms - last_rtc_sync >= 30000) {
    DateTime now = rtc.now();
    disp_h = now.hour();
    disp_m = now.minute();
    last_rtc_sync = now_ms;
    Serial.print("RTC sync: ");
    Serial.print(disp_h); Serial.print(":");
    if (disp_m < 10) Serial.print("0");
    Serial.println(disp_m);
  }

  // ── Update ring only when time changes ──
  if (disp_h != last_h || disp_m != last_m) {
    updateRing(disp_h, disp_m);
    Serial.print("Display → ");
    Serial.print(disp_h); Serial.print(":");
    if (disp_m < 10) Serial.print("0");
    Serial.println(disp_m);
    last_h = disp_h;
    last_m = disp_m;
  }

  // ── Cycle logo LED colour every 5 seconds ──
  if (now_ms - last_logo_change >= LOGO_INTERVAL) {
    logo_index = (logo_index + 1) % NUM_LOGO_COLORS;
    updateLogo(logo_index);
    last_logo_change = now_ms;
  }

  delay(500);  // Yield CPU, save power
}
