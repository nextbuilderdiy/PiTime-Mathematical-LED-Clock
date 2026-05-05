/*
 ============================================================
  SetTime — Run this ONCE to set your DS3231 RTC time
 ============================================================
  1. Edit the date and time values below to your current time
  2. Upload to ESP32-H2-Zero
  3. Open Serial Monitor at 115200 baud
  4. Confirm "Time set successfully!" appears
  5. Upload math_clock_esp32h2.ino — you're done!

  IMPORTANT: After setting the time, upload the main
  math_clock sketch immediately. If you leave this sketch
  running it will reset the time every reboot.
 ============================================================
*/

#include <Wire.h>
#include <RTClib.h>

#define I2C_SDA 4
#define I2C_SCL 5

RTC_DS3231 rtc;

// ▼▼▼ EDIT THESE VALUES TO YOUR CURRENT TIME ▼▼▼
#define SET_YEAR    2026
#define SET_MONTH      5   // 1=Jan, 2=Feb ... 12=Dec
#define SET_DAY        1
#define SET_HOUR      16   // 24-hour format: 0-23
#define SET_MINUTE    42
#define SET_SECOND     0
// ▲▲▲ EDIT THESE VALUES TO YOUR CURRENT TIME ▲▲▲

void setup() {
  Serial.begin(115200);
  delay(500);
  Wire.begin(I2C_SDA, I2C_SCL);

  if (!rtc.begin()) {
    Serial.println("ERROR: DS3231 not found!");
    Serial.println("Check: SDA→GPIO4, SCL→GPIO5, VCC→3.3V, GND→GND");
    while (1);
  }

  rtc.adjust(DateTime(SET_YEAR, SET_MONTH, SET_DAY,
                       SET_HOUR, SET_MINUTE, SET_SECOND));

  Serial.println("Time set successfully!");
  Serial.print("Set to: ");
  DateTime now = rtc.now();
  Serial.print(now.year());  Serial.print("/");
  Serial.print(now.month()); Serial.print("/");
  Serial.print(now.day());   Serial.print("  ");
  Serial.print(now.hour());  Serial.print(":");
  if (now.minute() < 10) Serial.print("0");
  Serial.print(now.minute()); Serial.print(":");
  if (now.second() < 10) Serial.print("0");
  Serial.println(now.second());
  Serial.println("\nNow upload the main math_clock_esp32h2 sketch!");
}

void loop() {
  // Nothing — time is already set
}
