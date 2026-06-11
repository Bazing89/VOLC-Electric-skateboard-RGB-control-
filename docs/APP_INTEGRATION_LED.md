# App-side integration guide (VOLC RGB BLE)

This document is the companion-app reference for integrating the **VOLC electric skateboard RGB controller** into an **Expo / React Native** app. It covers setup, scan/connect flow, every data field the app can observe or send, and recommended local state.

Related docs:

| Doc | Scope |
|-----|--------|
| [`BLE_SCAN_FILTERING.md`](BLE_SCAN_FILTERING.md) | OS-level scan filters (iOS / Android / RN) |
| [`BLE_LED_CONTROL.md`](BLE_LED_CONTROL.md) | Short firmware + command quick reference |
| [`BLE_TRANSPORT.md`](BLE_TRANSPORT.md) | NexusOwie BMS telemetry (different device type, same service UUID) |

Reference implementation: `expo-integration/` (`volcBleConstants.ts`, `volcBleClient.ts`, `useVolcBleScan.ts`).

---

## Overview

The VOLC device is a **BLE peripheral** on an Adafruit QT Py ESP32-C3. It drives a **9-LED** NeoPixel strip and exposes a GATT service your app already uses for scanning.

| Direction | Mechanism | Current firmware behavior |
|-----------|-----------|---------------------------|
| App → device | **Write** to control characteristic | LED mode, color, per-pixel color, brightness |
| Device → app | **Read** on data characteristic | Static identity string only |
| Device → app | **Notify** on data characteristic | Registered in GATT; **no live notifications sent today** |

The app is the source of truth for LED UI state. After a write succeeds, update local state to match — the device does not push state back.

---

## Prerequisites

| Requirement | Notes |
|-------------|--------|
| Expo **development build** | Expo Go cannot perform full BLE scanning |
| `react-native-ble-plx` | BLE central role |
| `buffer` | Base64 payload encoding for writes |
| Bluetooth permissions | iOS: `NSBluetoothAlwaysUsageDescription`; Android 12+: `BLUETOOTH_SCAN`, `BLUETOOTH_CONNECT` |

### Install

```bash
npx expo install react-native-ble-plx
npm install buffer
```

Configure the BLE config plugin in `app.json` / `app.config.js` per [react-native-ble-plx Expo docs](https://github.com/dotintent/react-native-ble-plx#expo).

### Copy modules

Copy `expo-integration/` into your project, e.g. `src/ble/volc/`.

---

## GATT map

| Item | UUID | Properties | App usage |
|------|------|------------|-----------|
| Service | `2f7e9c5a-0b3a-4d1e-8c2a-1a2b3c4d5e6f` | — | Scan filter; service scope for reads/writes |
| Data characteristic | `2f7e9c5b-0b3a-4d1e-8c2a-1a2b3c4d5e6f` | READ, NOTIFY | One-time READ for device label; NOTIFY unused on VOLC today |
| Control characteristic | `2f7e9c5c-0b3a-4d1e-8c2a-1a2b3c4d5e6f` | WRITE, WRITE_NR | All LED commands |

Constants: `include/ble_config.h`, `expo-integration/volcBleConstants.ts`.

---

## Integration flow

```text
1. startScan([SERVICE_UUID])
2. onDeviceDiscovered → collect scan fields (see table below)
3. filter name.startsWith("VOLC-")
4. user selects device
5. connect() → discoverAllServicesAndCharacteristics()
6. optional: read data characteristic (identity string)
7. write control characteristic for LED changes
8. track LED state locally in app state
```

---

## Data collected at scan time

When `BleManager.startDeviceScan` reports a device, these fields are available from `react-native-ble-plx` `Device`:

| Field | Type | Source | Description | Example |
|-------|------|--------|-------------|---------|
| `id` | `string` | BLE stack | Opaque peripheral identifier; stable for reconnect on same platform session | `"AA:BB:CC:DD:EE:FF"` (format varies by OS) |
| `name` | `string \| null` | Advertisement / GAP | GAP name when resolved | `"VOLC-A1B2"` |
| `localName` | `string \| null` | Scan response | Local name from adv packet; use when `name` is null | `"VOLC-A1B2"` |
| `rssi` | `number \| null` | Scan callback | Received signal strength (dBm); lower (more negative) = farther | `-62` |
| `mtu` | `number` | Post-connect | Default 23 (ATT); payload often 20 bytes — not relevant for VOLC writes (commands ≤ 5 bytes) | `23` |
| `serviceUUIDs` | `string[] \| null` | Advertisement | Should include `2f7e9c5a-…` if OS exposes it | `["2f7e9c5a-0b3a-4d1e-8c2a-1a2b3c4d5e6f"]` |
| `manufacturerData` | `string \| null` | Advertisement | Raw manufacturer bytes (base64); **not used by VOLC firmware** | `null` |
| `serviceData` | `object \| null` | Advertisement | Service-specific adv data; **not used by VOLC firmware** | `null` |
| `txPowerLevel` | `number \| null` | Advertisement | TX power if present in adv | `null` |
| `isConnectable` | `boolean \| null` | Advertisement | Should be `true` for VOLC | `true` |

### Derived fields (compute in app)

| Field | Derivation | Description | Example |
|-------|------------|-------------|---------|
| `displayName` | `name ?? localName ?? id` | UI label | `"VOLC-A1B2"` |
| `macSuffix` | Parse name after `VOLC-` | Lower 16 bits of ESP32 MAC (hex, uppercase) | `"A1B2"` |
| `isVolcDevice` | `displayName.startsWith("VOLC-")` | Device-type gate | `true` |

### Scan filter (required)

```typescript
manager.startDeviceScan(
  ["2f7e9c5a-0b3a-4d1e-8c2a-1a2b3c4d5e6f"],
  { allowDuplicates: false },
  onDevice,
);
```

Optional UI filter: only list devices where `isVolcDevice(name ?? localName)` is true. See [`BLE_SCAN_FILTERING.md`](BLE_SCAN_FILTERING.md).

---

## Data collected at connect time

After `device.connect()` and `discoverAllServicesAndCharacteristics()`:

| Field | Type | How to obtain | Description |
|-------|------|---------------|-------------|
| `connected` | `boolean` | `device.isConnected()` | Link up |
| `services` | `Service[]` | Discovery | Should contain VOLC service UUID |
| `characteristics` | `Characteristic[]` | Discovery | Data + control chars under service |

### Data characteristic READ (inbound)

One-shot read of `2f7e9c5b-…`:

| Field | Type | Encoding | Value | Notes |
|-------|------|----------|-------|-------|
| `deviceLabel` | `string` | UTF-8 / ASCII | `"VOLC RGB"` | Static firmware string; confirms GATT identity |

```typescript
const char = await device.readCharacteristicForService(
  VOLC_BLE_SERVICE,
  VOLC_BLE_DATA_CHAR,
);
const deviceLabel = Buffer.from(char.value ?? "", "base64").toString("utf8");
// "VOLC RGB"
```

### NOTIFY on data characteristic

The characteristic advertises NOTIFY, but **current VOLC firmware does not send notifications**. Do not implement chunk reassembly ([`BLE_TRANSPORT.md`](BLE_TRANSPORT.md)) for VOLC devices — that protocol applies to **NexusOwie** BMS boards only.

If you subscribe anyway, you will receive nothing until a future firmware version adds status push.

---

## Control data sent to device (app → firmware)

All commands are written to **`2f7e9c5c-0b3a-4d1e-8c2a-1a2b3c4d5e6f`** as a raw byte array (base64-encoded for `react-native-ble-plx`).

Use `writeCharacteristicWithResponseForService` (recommended) or `writeCharacteristicWithoutResponseForService`.

### Command summary

| Command byte | Name | Payload bytes | Total length |
|--------------|------|---------------|--------------|
| `0x01` | Set mode | `[0x01, mode]` | 2 |
| `0x02` | Set color (all LEDs) | `[0x02, R, G, B]` | 4 |
| `0x03` | Set pixel | `[0x03, index, R, G, B]` | 5 |
| `0x04` | Set brightness | `[0x04, brightness]` | 2 |
| `0x05` | Set LED count | `[0x05, count]` | 2 |

**Connect order:** send `0x05` (LED count) **first** after connect. The firmware boots with `ledCount = 0` and ignores color/mode commands until the app sets the strip length.

### `0x05` — Set LED count

| Byte | Field | Type | Range | Description |
|------|-------|------|-------|-------------|
| 0 | `command` | `uint8` | `0x05` | Opcode |
| 1 | `count` | `uint8` | `1`–`150` | Active pixels on the strip |

Configured by the user in the app (e.g. settings screen). Firmware allocates up to `MAX_LEDS` (150) in RAM; `count` is the runtime strip length.

`count = 0` or `count > 150` is ignored. Pixels beyond `count` are turned off when count changes.

### `0x01` — Set mode

| Byte | Field | Type | Range | Description |
|------|-------|------|-------|-------------|
| 0 | `command` | `uint8` | `0x01` | Opcode |
| 1 | `mode` | `uint8` | `0`–`2` | Animation mode |

| `mode` value | Constant | Firmware behavior |
|--------------|----------|-------------------|
| `0` | `VolcLedMode.Off` | Clears strip; no animation |
| `1` | `VolcLedMode.Solid` | Holds last solid color (set via `0x02` first, or black if never set) |
| `2` | `VolcLedMode.Rainbow` | Continuous rainbow animation (~20 ms/frame) |

Invalid `mode` values (`> 2`) are ignored by firmware.

### `0x02` — Set color (all LEDs)

| Byte | Field | Type | Range | Description |
|------|-------|------|-------|-------------|
| 0 | `command` | `uint8` | `0x02` | Opcode |
| 1 | `red` | `uint8` | `0`–`255` | Red channel |
| 2 | `green` | `uint8` | `0`–`255` | Green channel |
| 3 | `blue` | `uint8` | `0`–`255` | Blue channel |

Side effect: firmware sets mode to **Solid** and fills all active LEDs (`0` .. `ledCount - 1`).

Wire order is **RGB**; NeoPixel hardware uses GRB internally (handled by firmware).

### `0x03` — Set pixel (single LED)

| Byte | Field | Type | Range | Description |
|------|-------|------|-------|-------------|
| 0 | `command` | `uint8` | `0x03` | Opcode |
| 1 | `index` | `uint8` | `0`–`ledCount - 1` | LED index on strip |
| 2 | `red` | `uint8` | `0`–`255` | Red channel |
| 3 | `green` | `uint8` | `0`–`255` | Green channel |
| 4 | `blue` | `uint8` | `0`–`255` | Blue channel |

Side effect: firmware sets mode to **Solid** but only updates the indexed pixel; other pixels keep their previous colors.

`index >= ledCount` or `ledCount = 0` is ignored.

### `0x04` — Set brightness

| Byte | Field | Type | Range | Description |
|------|-------|------|-------|-------------|
| 0 | `command` | `uint8` | `0x04` | Opcode |
| 1 | `brightness` | `uint8` | `0`–`255` | Global NeoPixel brightness |

Default at boot: **100**. Applies to all pixels and modes. Does not change mode or RGB values.

---

## Recommended app-side state model

Because the device does not report live LED state, persist this in your app after each successful write:

```typescript
type Rgb = { r: number; g: number; b: number }; // each 0-255

type VolcLedState = {
  /** From scan / connect */
  deviceId: string;
  displayName: string;
  macSuffix: string | null;
  deviceLabel: string | null;   // from READ, e.g. "VOLC RGB"
  rssi: number | null;
  connected: boolean;

  /** From control commands (local source of truth) */
  ledCount: number;               // user-configured; sent via 0x05 on connect
  mode: 0 | 1 | 2;              // Off | Solid | Rainbow
  brightness: number;             // 0-255, default 100
  solidColor: Rgb;                // last 0x02 color
  pixels: Rgb[];                  // length = ledCount; update on 0x02 (all same) or 0x03 (one index)
};
```

### State update rules

| User action | BLE write | Update local state |
|-------------|-----------|-------------------|
| User sets strip length (settings) | `setLedCount(n)` | `ledCount = n`, resize `pixels` |
| Connect / reconnect | `setLedCount(ledCount)` | Send persisted user count to firmware |
| Turn off | `setMode(0)` | `mode = 0` |
| Pick color | `setColor(r,g,b)` | `mode = 1`, `solidColor = {r,g,b}`, `pixels = array of ledCount same` |
| Rainbow | `setMode(2)` | `mode = 2` |
| Per-LED edit | `setPixel(i,r,g,b)` | `mode = 1`, `pixels[i] = {r,g,b}` |
| Brightness slider | `setBrightness(n)` | `brightness = n` |

### Boot defaults (firmware, before any app command)

| Field | Default |
|-------|---------|
| `ledCount` | `0` (no pixels active until app sends `0x05`) |
| `mode` | Rainbow (`2`) |
| `brightness` | `100` |
| `solidColor` | `{ r: 255, g: 0, b: 0 }` (internal; not shown until solid mode) |
| `pixels` | N/A until `ledCount` is set |

On connect, read the user's saved LED count from app storage, call `setLedCount()`, then apply mode/color.

---

## TypeScript API reference

### Constants (`volcBleConstants.ts`)

```typescript
VOLC_BLE_SERVICE      // "2f7e9c5a-0b3a-4d1e-8c2a-1a2b3c4d5e6f"
VOLC_BLE_DATA_CHAR    // "2f7e9c5b-0b3a-4d1e-8c2a-1a2b3c4d5e6f"
VOLC_BLE_CONTROL_CHAR // "2f7e9c5c-0b3a-4d1e-8c2a-1a2b3c4d5e6f"
VOLC_NAME_PREFIX      // "VOLC-"

VolcLedMode.Off       // 0
VolcLedMode.Solid     // 1
VolcLedMode.Rainbow   // 2

VolcLedCommand.SetMode       // 0x01
VolcLedCommand.SetColor      // 0x02
VolcLedCommand.SetPixel      // 0x03
VolcLedCommand.SetBrightness // 0x04
VolcLedCommand.SetLedCount   // 0x05
VOLC_MAX_LEDS                // 150 (firmware ceiling)
```

### `VolcBleClient` methods

| Method | Writes | Parameters |
|--------|--------|------------|
| `connect()` | — | Discovers GATT |
| `disconnect()` | — | Drops connection |
| `setLedCount(count)` | `[0x05, count]` | User strip length, 1–150; **call on connect** |
| `setMode(mode)` | `[0x01, mode]` | `mode`: 0 \| 1 \| 2 |
| `setColor(r, g, b)` | `[0x02, r, g, b]` | RGB 0–255 |
| `setPixel(index, r, g, b)` | `[0x03, index, r, g, b]` | `index` 0–`ledCount - 1` |
| `setBrightness(brightness)` | `[0x04, brightness]` | Clamped 0–255 |
| `turnOff()` | `[0x01, 0]` | Alias for off mode |
| `startRainbow()` | `[0x01, 2]` | Alias for rainbow mode |

### Scan hook (`useVolcBleScan`)

| Return field | Type | Description |
|--------------|------|-------------|
| `devices` | `Device[]` | Deduped VOLC peripherals seen this scan |
| `scanning` | `boolean` | Scan active |
| `error` | `string \| null` | Last scan error message |
| `startScan()` | `() => void` | Clears list and starts filtered scan |
| `stopScan()` | `() => void` | Stops scan |
| `manager` | `BleManager` | Shared manager instance |

---

## End-to-end example

```typescript
import { Buffer } from "buffer";
import type { Device } from "react-native-ble-plx";
import { VolcBleClient } from "./ble/volc/volcBleClient";
import { VOLC_BLE_DATA_CHAR, VOLC_BLE_SERVICE, VolcLedMode } from "./ble/volc/volcBleConstants";
import type { VolcLedState } from "./ble/volc/types";

async function connectVolc(device: Device, userLedCount: number): Promise<VolcLedState> {
  const client = new VolcBleClient(device);
  const connected = await client.connect();
  await client.setLedCount(userLedCount);

  const name = connected.name ?? connected.localName ?? connected.id;
  const macSuffix = name.startsWith("VOLC-") ? name.slice(5) : null;

  const read = await connected.readCharacteristicForService(
    VOLC_BLE_SERVICE,
    VOLC_BLE_DATA_CHAR,
  );
  const deviceLabel = Buffer.from(read.value ?? "", "base64").toString("utf8");

  return {
    deviceId: connected.id,
    displayName: name,
    macSuffix,
    deviceLabel,
    rssi: connected.rssi,
    connected: true,
    ledCount: userLedCount,
    mode: VolcLedMode.Rainbow,
    brightness: 100,
    solidColor: { r: 255, g: 0, b: 0 },
    pixels: Array.from({ length: userLedCount }, () => ({ r: 0, g: 0, b: 0 })),
  };
}

async function applySolidColor(client: VolcBleClient, state: VolcLedState, rgb: { r: number; g: number; b: number }) {
  await client.setColor(rgb.r, rgb.g, rgb.b);
  state.mode = VolcLedMode.Solid;
  state.solidColor = rgb;
  state.pixels = Array.from({ length: state.ledCount }, () => ({ ...rgb }));
}
```

---

## Sharing a scan list with NexusOwie devices

Both device families advertise service `2f7e9c5a-…`. After the user picks a row, branch on name prefix:

| Name prefix | Device | Data collected live | App action |
|-------------|--------|---------------------|------------|
| `VOLC-` | RGB controller | Scan fields + static READ `"VOLC RGB"` | Write control char; track LED state locally |
| `NexusOwie-` | BMS relay | Chunked NOTIFY JSON + raw BMS binary | See [`BLE_TRANSPORT.md`](BLE_TRANSPORT.md) |

NexusOwie JSON fields (stream type `1`, 1 Hz) for reference — **not emitted by VOLC**:

| JSON key | Example | Meaning |
|----------|---------|---------|
| `UPTIME` | `"12m34s"` | Device uptime |
| `TOTAL_VOLTAGE` | `"63.42v"` | Pack voltage |
| `CURRENT_AMPS` | `"1.2 Amps"` | Pack current |
| `BMS_SOC` | `"87%"` | BMS-reported SOC |
| `OVERRIDDEN_SOC` | `"85%"` | Fuel-gauge SOC |
| `USED_CHARGE_MAH` | `"1234 mAh"` | Discharged charge |
| `REGENERATED_CHARGE_MAH` | `"56 mAh"` | Regenerated charge |
| `CELL_VOLTAGE_TABLE` | HTML `<tr>…` | Cell voltages (display-oriented) |
| `TEMPERATURE_TABLE` | HTML `<tr>…` | Temperatures (display-oriented) |

---

## Error handling

| Condition | App behavior |
|-----------|--------------|
| Bluetooth off | `BleManager` state → prompt user to enable |
| Scan permission denied | Show settings link |
| `name` null during scan | Use `localName` or `id`; retry scan |
| Write fails (not connected) | Reconnect, rediscover, retry write |
| Write to wrong UUID | Ensure `2f7e9c5c-…` (control), not `2f7e9c5b-…` (data) |
| Invalid command length | Firmware ignores; validate lengths client-side before write |

---

## Verification checklist

- [ ] Development build (not Expo Go)
- [ ] Scan filter: service `2f7e9c5a-0b3a-4d1e-8c2a-1a2b3c4d5e6f`
- [ ] UI filter: name prefix `VOLC-`
- [ ] Connect + `discoverAllServicesAndCharacteristics()`
- [ ] Optional READ data char → `"VOLC RGB"`
- [ ] `setLedCount()` sent immediately after connect
- [ ] Writes go to control char `2f7e9c5c-…`
- [ ] App state updated after each successful write
- [ ] Do not expect NOTIFY / BMS JSON from VOLC hardware

---

## Hardware limits (for UI validation)

| Limit | Value |
|-------|-------|
| LED count (app → firmware) | 1–150, user-defined |
| Firmware RAM ceiling | `MAX_LEDS` = 150 in `include/led_config.h` |
| Pixel index | 0–`ledCount - 1` |
| RGB channels | 0–255 each |
| Brightness | 0–255 |
| Modes | 3 (off, solid, rainbow) |

Clamp inputs in the app before writing to avoid silent firmware ignores.
