#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// --- ARGB LED Strip ---
#define LED_PIN 7      // Pin where your ARGB strip is connected
#define NUM_LEDS 9     // Number of LEDs in your strip
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// --- Onboard LED ---
#define ONBOARD_LED 8  // GPIO for ESP32-C3 onboard LED (try 2 if 8 doesn't work)

// Helper function to generate rainbow colors
uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if(WheelPos < 85) {
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  } else if(WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  } else {
    WheelPos -= 170;
    return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
  }
}

void setup() {
  // Setup ARGB strip
  strip.begin();
  strip.show();            // Turn OFF all pixels
  strip.setBrightness(100);

  // Setup onboard LED
  pinMode(ONBOARD_LED, OUTPUT);
}

void loop() {
  // Rainbow animation
  static uint16_t j = 0;
  for(int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, Wheel((i + j) & 255));
  }
  strip.show();
  j++;
  if(j >= 256) j = 0;

  // Blink onboard LED independently
  static unsigned long lastBlink = 0;
  static bool ledState = false;
  if (millis() - lastBlink > 500) { // toggle every 500ms
    ledState = !ledState;
    digitalWrite(ONBOARD_LED, ledState ? HIGH : LOW);
    lastBlink = millis();
  }

  delay(20); // speed of rainbow
}
