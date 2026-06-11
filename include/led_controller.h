#pragma once

#include <Adafruit_NeoPixel.h>
#include <stdint.h>

#include "led_config.h"

class LedController {
public:
  void begin();
  void loop();

  void setMode(uint8_t mode);
  void setBrightness(uint8_t brightness);
  void setLedCount(uint8_t count);
  void setColor(uint8_t r, uint8_t g, uint8_t b);
  void setPixel(uint8_t index, uint8_t r, uint8_t g, uint8_t b);

  uint8_t mode() const { return mode_; }
  uint8_t brightness() const { return brightness_; }
  uint8_t ledCount() const { return numLeds_; }

private:
  Adafruit_NeoPixel strip_{MAX_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800};
  uint8_t numLeds_ = 0;
  uint8_t mode_ = 2; // rainbow default
  uint8_t brightness_ = 100;
  uint8_t solidR_ = 255;
  uint8_t solidG_ = 0;
  uint8_t solidB_ = 0;
  uint16_t rainbowOffset_ = 0;

  uint32_t wheel(byte wheelPos);
  void renderSolid();
  void renderRainbow();
  void blinkOnboardLed();
};

extern LedController g_leds;
