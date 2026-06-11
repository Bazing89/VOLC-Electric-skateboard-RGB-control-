#include "led_controller.h"

#include "ble_config.h"
#include "led_config.h"

#include <Arduino.h>

LedController g_leds;

void LedController::begin() {
  strip_.begin();
  strip_.show();
  strip_.setBrightness(brightness_);
  pinMode(ONBOARD_LED, OUTPUT);
}

void LedController::setMode(uint8_t mode) {
  if (mode > VOLC_MODE_RAINBOW) {
    return;
  }
  mode_ = mode;
  if (mode_ == VOLC_MODE_OFF) {
    strip_.clear();
    strip_.show();
  }
}

void LedController::setBrightness(uint8_t brightness) {
  brightness_ = brightness;
  strip_.setBrightness(brightness_);
  strip_.show();
}

void LedController::setLedCount(uint8_t count) {
  if (count == 0 || count > MAX_LEDS) {
    return;
  }
  numLeds_ = count;
  for (uint16_t i = numLeds_; i < MAX_LEDS; i++) {
    strip_.setPixelColor(i, 0);
  }
  strip_.show();
}

void LedController::setColor(uint8_t r, uint8_t g, uint8_t b) {
  solidR_ = r;
  solidG_ = g;
  solidB_ = b;
  mode_ = VOLC_MODE_SOLID;
  renderSolid();
}

void LedController::setPixel(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
  if (numLeds_ == 0 || index >= numLeds_) {
    return;
  }
  mode_ = VOLC_MODE_SOLID;
  strip_.setPixelColor(index, strip_.Color(r, g, b));
  strip_.show();
}

uint32_t LedController::wheel(byte wheelPos) {
  wheelPos = 255 - wheelPos;
  if (wheelPos < 85) {
    return strip_.Color(255 - wheelPos * 3, 0, wheelPos * 3);
  }
  if (wheelPos < 170) {
    wheelPos -= 85;
    return strip_.Color(0, wheelPos * 3, 255 - wheelPos * 3);
  }
  wheelPos -= 170;
  return strip_.Color(wheelPos * 3, 255 - wheelPos * 3, 0);
}

void LedController::renderSolid() {
  if (numLeds_ == 0) {
    return;
  }
  const uint32_t color = strip_.Color(solidR_, solidG_, solidB_);
  for (uint8_t i = 0; i < numLeds_; i++) {
    strip_.setPixelColor(i, color);
  }
  strip_.show();
}

void LedController::renderRainbow() {
  if (numLeds_ == 0) {
    return;
  }
  for (uint8_t i = 0; i < numLeds_; i++) {
    strip_.setPixelColor(i, wheel((i + rainbowOffset_) & 255));
  }
  strip_.show();
  rainbowOffset_++;
}

void LedController::blinkOnboardLed() {
  static unsigned long lastBlink = 0;
  static bool ledState = false;
  if (millis() - lastBlink > 500) {
    ledState = !ledState;
    digitalWrite(ONBOARD_LED, ledState ? HIGH : LOW);
    lastBlink = millis();
  }
}

void LedController::loop() {
  switch (mode_) {
    case VOLC_MODE_OFF:
      break;
    case VOLC_MODE_SOLID:
      break;
    case VOLC_MODE_RAINBOW:
      renderRainbow();
      delay(20);
      break;
  }
  blinkOnboardLed();
}
