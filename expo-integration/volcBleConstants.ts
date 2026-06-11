/** UUIDs and names — must match include/ble_config.h and docs/BLE_SCAN_FILTERING.md */
export const VOLC_BLE_SERVICE = "2f7e9c5a-0b3a-4d1e-8c2a-1a2b3c4d5e6f";
export const VOLC_BLE_DATA_CHAR = "2f7e9c5b-0b3a-4d1e-8c2a-1a2b3c4d5e6f";
export const VOLC_BLE_CONTROL_CHAR = "2f7e9c5c-0b3a-4d1e-8c2a-1a2b3c4d5e6f";

export const VOLC_NAME_PREFIX = "VOLC-";

/** Firmware RAM ceiling — must match MAX_LEDS in include/led_config.h */
export const VOLC_MAX_LEDS = 150;

export const VolcLedMode = {
  Off: 0,
  Solid: 1,
  Rainbow: 2,
} as const;

export type VolcLedMode = (typeof VolcLedMode)[keyof typeof VolcLedMode];

export const VolcLedCommand = {
  SetMode: 0x01,
  SetColor: 0x02,
  SetPixel: 0x03,
  SetBrightness: 0x04,
  SetLedCount: 0x05,
} as const;
