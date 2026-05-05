"""
Math Clock - MicroPython for Raspberry Pi Pico 2
================================================
Hardware:
  - Raspberry Pi Pico 2
  - Waveshare PCF8563 RTC Board (I2C: SDA=GP4, SCL=GP5)
  - 12x WS2812B NeoPixel RGB LEDs (Data=GP28)
  - 18650 Li-Ion battery (rechargeable)

Clock face positions (LED index 0-11):
  Index 0  → 12 o'clock  (math: 6×2)
  Index 1  → 1 o'clock   (math: 11√121)
  Index 2  → 2 o'clock   (math: 8.64-7.64)  ... etc.
  ...continuing clockwise

LED Colour Logic:
  HOUR only   → RED   (255, 0,   0  )
  MINUTE only → GREEN (0,   255, 0  )
  HOUR+MINUTE → BLUE  (0,   0,   255)  [overlap/coincidence]
  OFF         → (0, 0, 0)

No seconds LED (battery saving).
RTC is read every 30 seconds to keep the display fresh.
"""

import machine
import neopixel
import time
import struct

# ─── Pin Configuration ───────────────────────────────────────────────────────
LED_PIN      = 28          # GP28 → NeoPixel data line
NUM_LEDS     = 12          # One LED per clock-face number
I2C_SDA      = 4           # GP4  → PCF8563 SDA
I2C_SCL      = 5           # GP5  → PCF8563 SCL
I2C_FREQ     = 400_000     # 400 kHz
PCF8563_ADDR = 0x51        # Fixed I2C address of PCF8563

# ─── Brightness (0–255) ──────────────────────────────────────────────────────
BRIGHTNESS = 80            # Reduce for longer battery life (max 255)

# ─── Colours ─────────────────────────────────────────────────────────────────
def dim(r, g, b):
    """Scale an RGB colour by the global BRIGHTNESS factor."""
    factor = BRIGHTNESS / 255
    return (int(r * factor), int(g * factor), int(b * factor))

COLOR_OFF    = (0, 0, 0)
COLOR_HOUR   = dim(255, 0,   0  )   # Red   → hour
COLOR_MINUTE = dim(0,   255, 0  )   # Green → minute
COLOR_BOTH   = dim(0,   0,   255)   # Blue  → hour AND minute on same LED

# ─── NeoPixel Setup ──────────────────────────────────────────────────────────
np = neopixel.NeoPixel(machine.Pin(LED_PIN), NUM_LEDS)

# ─── I2C / PCF8563 Setup ─────────────────────────────────────────────────────
i2c = machine.I2C(0, sda=machine.Pin(I2C_SDA), scl=machine.Pin(I2C_SCL), freq=I2C_FREQ)


# ─── PCF8563 Driver ──────────────────────────────────────────────────────────

def _bcd_to_int(bcd):
    """Convert BCD byte to integer."""
    return (bcd >> 4) * 10 + (bcd & 0x0F)

def _int_to_bcd(value):
    """Convert integer to BCD byte."""
    return ((value // 10) << 4) | (value % 10)

def rtc_read_time():
    """
    Read current time from PCF8563.
    Returns (hours_24, minutes) as plain integers.
    Raises OSError if the RTC is not responding.
    """
    # Registers 0x02 (VL_seconds) … 0x04 (hours)
    data = i2c.readfrom_mem(PCF8563_ADDR, 0x02, 3)
    seconds = _bcd_to_int(data[0] & 0x7F)   # mask VL bit
    minutes = _bcd_to_int(data[1] & 0x7F)
    hours   = _bcd_to_int(data[2] & 0x3F)   # 24-h mode, mask reserved bits
    return hours, minutes

def rtc_set_time(hours, minutes, seconds=0):
    """
    Write time to PCF8563.
    Use this once (or via REPL) to initialise the RTC.
    Example: rtc_set_time(14, 30)  → sets 14:30:00
    """
    buf = bytes([
        _int_to_bcd(seconds) & 0x7F,   # VL bit cleared → clock is valid
        _int_to_bcd(minutes) & 0x7F,
        _int_to_bcd(hours)   & 0x3F,
    ])
    i2c.writeto_mem(PCF8563_ADDR, 0x02, buf)

def rtc_is_valid():
    """Return True if the RTC voltage-loss flag is NOT set (clock is valid)."""
    data = i2c.readfrom_mem(PCF8563_ADDR, 0x02, 1)
    return not bool(data[0] & 0x80)


# ─── Clock Logic ─────────────────────────────────────────────────────────────

def hour_to_led(hour_24):
    """
    Map 24-hour hour value to LED index 0-11.
    12-hour clock: 0/12 → LED 0, 1/13 → LED 1, … 11/23 → LED 11
    """
    return hour_24 % 12      # 0 represents the 12 o'clock position

def minute_to_led(minute):
    """
    Map minutes (0-59) to LED index 0-11.
    Each LED covers a 5-minute block:
      0-4 → LED 0 (12), 5-9 → LED 1 (1), … 55-59 → LED 11 (11)
    """
    return minute // 5

def update_leds(hour_24, minute):
    """
    Set all 12 LEDs based on current hour and minute.
    """
    h_led = hour_to_led(hour_24)
    m_led = minute_to_led(minute)

    for i in range(NUM_LEDS):
        if i == h_led and i == m_led:
            np[i] = COLOR_BOTH        # Same LED → overlap colour (blue)
        elif i == h_led:
            np[i] = COLOR_HOUR        # Hour only → red
        elif i == m_led:
            np[i] = COLOR_MINUTE      # Minute only → green
        else:
            np[i] = COLOR_OFF         # Off

    np.write()

def clear_leds():
    """Turn all LEDs off."""
    for i in range(NUM_LEDS):
        np[i] = COLOR_OFF
    np.write()


# ─── Startup Self-Test ───────────────────────────────────────────────────────

def startup_test():
    """
    Brief startup animation: sweep through all LEDs, then clear.
    Useful for verifying wiring before the clock starts.
    """
    colors = [COLOR_HOUR, COLOR_MINUTE, COLOR_BOTH]
    for color in colors:
        for i in range(NUM_LEDS):
            np[i] = color
            np.write()
            time.sleep_ms(60)
    time.sleep_ms(200)
    clear_leds()
    time.sleep_ms(200)


# ─── Error Indicator ─────────────────────────────────────────────────────────

def show_rtc_error():
    """
    Flash all LEDs red+blue alternately if the RTC cannot be reached.
    Exits when the RTC becomes available again.
    """
    toggle = True
    while True:
        color = COLOR_HOUR if toggle else COLOR_BOTH
        for i in range(NUM_LEDS):
            np[i] = color
        np.write()
        toggle = not toggle
        time.sleep_ms(300)

        # Try to re-establish contact
        try:
            rtc_read_time()
            break          # RTC is back → exit error loop
        except OSError:
            pass

    clear_leds()


# ─── Main Loop ───────────────────────────────────────────────────────────────

def main():
    print("Math Clock starting up…")

    # 1. Startup visual test
    startup_test()

    # 2. Verify RTC is reachable
    try:
        devices = i2c.scan()
        if PCF8563_ADDR not in devices:
            print(f"ERROR: PCF8563 not found on I2C bus. Devices found: {devices}")
            show_rtc_error()
    except OSError as e:
        print(f"I2C scan error: {e}")
        show_rtc_error()

    # 3. Check RTC clock validity (voltage-loss flag)
    try:
        if not rtc_is_valid():
            print("WARNING: RTC lost power — time may be wrong.")
            print("Call rtc_set_time(HH, MM) via REPL to set the correct time.")
    except OSError:
        pass

    # 4. Display initial time
    last_h, last_m = -1, -1    # Force first update
    last_rtc_read  = time.ticks_ms()

    # We only read the RTC every READ_INTERVAL_MS milliseconds.
    # Between reads we use ticks to advance the local minute counter.
    READ_INTERVAL_MS = 30_000   # 30 s — balances accuracy vs I2C traffic

    try:
        hours, minutes = rtc_read_time()
    except OSError:
        print("Could not read RTC at startup.")
        show_rtc_error()
        hours, minutes = 0, 0

    last_rtc_read = time.ticks_ms()
    display_hours   = hours
    display_minutes = minutes
    minute_tick_start = time.ticks_ms()

    print(f"Current time: {hours:02d}:{minutes:02d}")
    update_leds(display_hours, display_minutes)
    last_h, last_m = display_hours, display_minutes

    # ── Main loop ──
    while True:
        now = time.ticks_ms()

        # Re-sync from RTC periodically
        if time.ticks_diff(now, last_rtc_read) >= READ_INTERVAL_MS:
            try:
                hours, minutes = rtc_read_time()
                display_hours   = hours
                display_minutes = minutes
                last_rtc_read   = now
                minute_tick_start = now
                print(f"RTC sync: {hours:02d}:{minutes:02d}")
            except OSError:
                print("RTC read failed — keeping last known time.")
                last_rtc_read = now   # Reset timer; try again after next interval

        # Only refresh LEDs when h or m has changed
        if display_hours != last_h or display_minutes != last_m:
            update_leds(display_hours, display_minutes)
            print(f"Display → {display_hours:02d}:{display_minutes:02d}")
            last_h = display_hours
            last_m = display_minutes

        # Sleep briefly to yield CPU; no busy-spinning
        time.sleep_ms(500)


# ─── Entry Point ─────────────────────────────────────────────────────────────

if __name__ == "__main__":
    main()
