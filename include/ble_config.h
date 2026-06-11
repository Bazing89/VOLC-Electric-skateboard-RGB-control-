#pragma once

// GATT UUIDs — match docs/BLE_SCAN_FILTERING.md so the Expo app scan filter works.
#define VOLC_BLE_SERVICE_UUID "2f7e9c5a-0b3a-4d1e-8c2a-1a2b3c4d5e6f"
#define VOLC_BLE_DATA_CHAR_UUID "2f7e9c5b-0b3a-4d1e-8c2a-1a2b3c4d5e6f"
#define VOLC_BLE_CONTROL_CHAR_UUID "2f7e9c5c-0b3a-4d1e-8c2a-1a2b3c4d5e6f"

#define VOLC_BLE_DEVICE_NAME_PREFIX "VOLC-"

// Binary commands written to the control characteristic (WRITE / WRITE_NR).
#define VOLC_CMD_SET_MODE 0x01       // [cmd, mode]
#define VOLC_CMD_SET_COLOR 0x02      // [cmd, R, G, B]
#define VOLC_CMD_SET_PIXEL 0x03      // [cmd, index, R, G, B]
#define VOLC_CMD_SET_BRIGHTNESS 0x04 // [cmd, brightness 0-255]
#define VOLC_CMD_SET_LED_COUNT 0x05  // [cmd, count 1..MAX_LEDS]

#define VOLC_MODE_OFF 0
#define VOLC_MODE_SOLID 1
#define VOLC_MODE_RAINBOW 2
