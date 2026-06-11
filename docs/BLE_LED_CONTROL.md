# BLE LED control (VOLC + Expo)

> **App developers:** see [`APP_INTEGRATION.md`](APP_INTEGRATION.md) for the full integration guide, including every scan/connect field and control data point.

This document describes how to control the skateboard RGB strip from an **Expo** app using the same BLE service UUID as [`BLE_SCAN_FILTERING.md`](BLE_SCAN_FILTERING.md).

[`BLE_TRANSPORT.md`](BLE_TRANSPORT.md) documents the NexusOwie **BMS data stream** (notify-only). VOLC adds a **write** characteristic for LED commands. Your app can use one scan filter for both device families, then branch on device name after connect.

---

## GATT layout

| Item | UUID / value |
|------|----------------|
| Service | `2f7e9c5a-0b3a-4d1e-8c2a-1a2b3c4d5e6f` |
| Data characteristic | `2f7e9c5b-0b3a-4d1e-8c2a-1a2b3c4d5e6f` (READ, NOTIFY) |
| **Control characteristic** | `2f7e9c5c-0b3a-4d1e-8c2a-1a2b3c4d5e6f` (WRITE, WRITE_NR) |
| Device name | `VOLC-XXXX` (`XXXX` = lower 16 bits of MAC, hex uppercase) |

Firmware: `include/ble_config.h`, `src/ble_transport.cpp`.

---

## Scan and connect (Expo)

Requires a **development build** — Expo Go does not expose full BLE. Use `react-native-ble-plx` (or `expo prebuild` + config plugin).

### 1. Install

```bash
npx expo install react-native-ble-plx
npm install buffer
```

Add the BLE plugin to `app.json` / `app.config.js` per [react-native-ble-plx Expo setup](https://github.com/dotintent/react-native-ble-plx#expo).

### 2. Permissions

**iOS** — `Info.plist`:

- `NSBluetoothAlwaysUsageDescription`

**Android 12+** — `AndroidManifest.xml` (often via plugin):

- `BLUETOOTH_SCAN`
- `BLUETOOTH_CONNECT`

### 3. Copy integration modules

Copy `expo-integration/` into your app (e.g. `src/ble/`):

- `volcBleConstants.ts`
- `volcBleClient.ts`
- `useVolcBleScan.ts`

### 4. Scan (service UUID filter)

```typescript
import { useVolcBleScan } from "./ble/useVolcBleScan";
import { VolcBleClient } from "./ble/volcBleClient";

function LedScreen() {
  const { devices, scanning, startScan, stopScan } = useVolcBleScan();

  async function connectAndSetRed(device: Device) {
    const client = new VolcBleClient(device);
    await client.connect();
    await client.setColor(255, 0, 0);
  }

  // render device list, call startScan() on mount, etc.
}
```

Scan uses service UUID `2f7e9c5a-…` and optional name prefix `VOLC-` — same pattern as [`BLE_SCAN_FILTERING.md`](BLE_SCAN_FILTERING.md).

---

## Control protocol (write to control characteristic)

All commands are a byte array written with response to `2f7e9c5c-…`.

| Command | Bytes | Effect |
|---------|-------|--------|
| Set mode | `[0x01, mode]` | `0` off, `1` solid, `2` rainbow |
| Set color (all LEDs) | `[0x02, R, G, B]` | Solid color on entire strip |
| Set pixel | `[0x03, index, R, G, B]` | One LED (`index` 0–`ledCount - 1`) |
| Set brightness | `[0x04, brightness]` | NeoPixel brightness 0–255 |
| Set LED count | `[0x05, count]` | Strip length 1–150; **send on connect** |

### TypeScript examples

```typescript
import { VolcBleClient } from "./ble/volcBleClient";

const client = new VolcBleClient(device);
await client.connect();

await client.setColor(0, 128, 255);   // cyan solid
await client.setBrightness(80);
await client.startRainbow();
await client.setPixel(0, 255, 255, 255); // first LED white
await client.turnOff();
```

### Raw write (without helper class)

```typescript
import { Buffer } from "buffer";
import {
  VOLC_BLE_SERVICE,
  VOLC_BLE_CONTROL_CHAR,
} from "./ble/volcBleConstants";

const payload = Buffer.from([0x02, 255, 0, 0]); // red
await device.writeCharacteristicWithResponseForService(
  VOLC_BLE_SERVICE,
  VOLC_BLE_CONTROL_CHAR,
  payload.toString("base64"),
);
```

---

## NexusOwie vs VOLC in one app

| Device | Name prefix | After connect |
|--------|-------------|---------------|
| NexusOwie (BMS) | `NexusOwie-` | Subscribe to data char `2f7e9c5b-…`; reassemble chunks per [`BLE_TRANSPORT.md`](BLE_TRANSPORT.md) |
| VOLC (RGB) | `VOLC-` | Write LED commands to control char `2f7e9c5c-…` |

Both advertise service `2f7e9c5a-…`, so one scan filter finds both. Branch on `device.name` / `device.localName` after the user picks a device.

```typescript
const name = device.name ?? device.localName ?? "";

if (name.startsWith("VOLC-")) {
  const client = new VolcBleClient(device);
  await client.connect();
  // LED UI
} else if (name.startsWith("NexusOwie-")) {
  // existing BMS notify + chunk reassembly
}
```

---

## Flash firmware

```bash
pio run -t upload
```

Verify with **nRF Connect**:

1. Peripheral advertises service `2f7e9c5a-…` and name `VOLC-XXXX`.
2. Control characteristic `2f7e9c5c-…` accepts writes.
3. Writing `02 FF 00 00` turns the strip solid red.

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| No devices in scan | Confirm service UUID filter; grant Bluetooth permissions; use dev build not Expo Go |
| Connect works, writes ignored | Write to `2f7e9c5c-…`, not the data characteristic |
| Wrong device type in list | Filter UI by `VOLC-` name prefix |
| LEDs stay rainbow | Send `setMode(0)` or `setColor(r,g,b)` to override default animation |
