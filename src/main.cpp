#include <Arduino.h>

#include "ble_transport.h"
#include "led_controller.h"

void setup() {
  g_leds.begin();
  bleTransportBegin();
}

void loop() {
  g_leds.loop();
  bleTransportLoop();
}
