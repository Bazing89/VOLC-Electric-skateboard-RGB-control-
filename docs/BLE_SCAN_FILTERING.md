# BLE: Scan Filtering (Show Only NexusOwie Devices)

This document explains how a companion app should scan for BLE peripherals so that **only NexusOwie devices** appear in the scan list.

BLE advertising is broadcast — any scanner app can still see the device. This guide covers **filtering inside your app**, which is the standard and recommended approach. No firmware changes are required; NexusOwie already advertises the identifiers you need.

For connection, GATT layout, and data streams after pairing, see [`BLE_TRANSPORT.md`](BLE_TRANSPORT.md).

---

## Identifiers to use

| Identifier | Value | Use |
|------------|-------|-----|
| **Service UUID** (primary filter) | `2f7e9c5a-0b3a-4d1e-8c2a-1a2b3c4d5e6f` | Scan filter — OS only reports matching peripherals |
| **Data characteristic UUID** | `2f7e9c5b-0b3a-4d1e-8c2a-1a2b3c4d5e6f` | After connect — subscribe to notifications |
| **Device name** (secondary check) | `NexusOwie-XXXX` | Optional UI validation; `XXXX` = lower 16 bits of ESP32 MAC (hex, uppercase) |

Constants are defined in `include/ble_config.h`. Advertising is configured in `src/ble_transport.cpp`:

```cpp
adv->addServiceUUID(OWIE_BLE_SERVICE_UUID);
adv->setName(name);  // e.g. "NexusOwie-A1B2"
```

---

## Recommended strategy

1. **Scan with the service UUID filter** — best performance; the OS drops non-matching devices before your callback runs.
2. **Optionally verify the name prefix** `NexusOwie-` before adding a row to the UI — defense in depth if a scan record is incomplete on some platforms.
3. **Connect**, discover services, enable notifications on the data characteristic.

Do **not** scan for all BLE devices and filter in JavaScript alone — that is slower, noisier, and drains battery.

---

## iOS (CoreBluetooth)

### Scan

```swift
import CoreBluetooth

let nexusOwieServiceUUID = CBUUID(
  string: "2F7E9C5A-0B3A-4D1E-8C2A-1A2B3C4D5E6F"
)

func startScan(centralManager: CBCentralManager) {
  centralManager.scanForPeripherals(
    withServices: [nexusOwieServiceUUID],
    options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
  )
}
```

### Optional name check

```swift
func isNexusOwie(_ peripheral: CBPeripheral, advertisementData: [String: Any]) -> Bool {
  let name = peripheral.name
    ?? (advertisementData[CBAdvertisementDataLocalNameKey] as? String)
  return name?.hasPrefix("NexusOwie-") ?? false
}
```

In `centralManager(_:didDiscover:advertisementData:rssi:)`, only add the peripheral to your list if `isNexusOwie` returns true (or skip the check if UUID filtering alone is sufficient).

### Permissions

Add to `Info.plist`:

- `NSBluetoothAlwaysUsageDescription` — required for scanning and connection.

---

## Android

### Scan

```kotlin
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanSettings
import android.os.ParcelUuid

val NEXUS_OWIE_SERVICE = ParcelUuid.fromString(
  "2f7e9c5a-0b3a-4d1e-8c2a-1a2b3c4d5e6f"
)

val filter = ScanFilter.Builder()
  .setServiceUuid(NEXUS_OWIE_SERVICE)
  .build()

val settings = ScanSettings.Builder()
  .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
  .build()

bluetoothLeScanner.startScan(listOf(filter), settings, scanCallback)
```

### Optional name check

```kotlin
fun isNexusOwie(name: String?): Boolean =
  name?.startsWith("NexusOwie-") == true
```

In `onScanResult`, read `result.device.name` or `result.scanRecord?.deviceName` and apply the optional prefix check before updating the UI.

### Permissions (Android 12+)

- `BLUETOOTH_SCAN` (with `neverForLocation` flag if you do not need location)
- `BLUETOOTH_CONNECT`

Older API levels may require location permission for BLE scan — follow current Android BLE guidance for your `targetSdkVersion`.

---

## React Native (`react-native-ble-plx`)

Requires a **development build** — Expo Go does not expose full BLE scanning.

```typescript
import { BleManager } from "react-native-ble-plx";

const NEXUS_OWIE_SERVICE = "2f7e9c5a-0b3a-4d1e-8c2a-1a2b3c4d5e6f";
const NEXUS_OWIE_NAME_PREFIX = "NexusOwie-";

const manager = new BleManager();

function isNexusOwie(name: string | null): boolean {
  return name?.startsWith(NEXUS_OWIE_NAME_PREFIX) ?? false;
}

function startNexusOwieScan(onDevice: (device: Device) => void): void {
  manager.startDeviceScan(
    [NEXUS_OWIE_SERVICE],
    { allowDuplicates: false },
    (error, device) => {
      if (error || !device) return;
      if (!isNexusOwie(device.name ?? device.localName)) return;
      onDevice(device);
    }
  );
}

function stopScan(): void {
  manager.stopDeviceScan();
}
```

After connect:

```typescript
const DATA_CHAR = "2f7e9c5b-0b3a-4d1e-8c2a-1a2b3c4d5e6f";

await device.discoverAllServicesAndCharacteristics();
device.monitorCharacteristicForService(
  NEXUS_OWIE_SERVICE,
  DATA_CHAR,
  (error, characteristic) => {
    if (characteristic?.value) {
      const bytes = Buffer.from(characteristic.value, "base64");
      // feed into chunk reassembler — see BLE_TRANSPORT.md
    }
  }
);
```

---

## Pseudocode (any platform)

```text
SERVICE_UUID = "2f7e9c5a-0b3a-4d1e-8c2a-1a2b3c4d5e6f"
NAME_PREFIX  = "NexusOwie-"

startScan(serviceFilter: [SERVICE_UUID])

onDeviceDiscovered(device):
  if device.advertisesService(SERVICE_UUID) == false:
    return
  if device.name does not start with NAME_PREFIX:
    return   // optional
  addToScanList(device)

onUserSelects(device):
  connect(device)
  discoverServices()
  enableNotifications(SERVICE_UUID, DATA_CHAR_UUID)
```

---

## What not to do

| Approach | Problem |
|----------|---------|
| Scan with no service filter, filter all devices in app code | Every nearby BLE device triggers callbacks; poor UX and battery use |
| Filter by MAC address allowlist | Impractical for discovery; MAC varies per board |
| Expect firmware to hide from other apps | Standard BLE cannot restrict visibility to one app only |
| Match on `"Nexus Owie"` (with space) | Advertised **local name** is `NexusOwie-XXXX`, not `"Nexus Owie"` — that string is only the initial GATT READ value on the data characteristic |

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| No devices in scan list | Scan filter UUID typo; Bluetooth off; permissions missing | Verify UUID matches `ble_config.h` exactly (case-insensitive on most stacks) |
| Device visible in nRF Connect but not in app | App not using service UUID scan filter, or wrong UUID | Use `2f7e9c5a-0b3a-4d1e-8c2a-1a2b3c4d5e6f` in scan filter |
| Device appears without name | Name in scan response delayed | Wait for `localName` update or show MAC/id until name resolves |
| Multiple `NexusOwie-XXXX` entries | Several boards in range | Expected — user picks by suffix; use `allowDuplicates: false` to dedupe by id |
| iOS scan returns nothing | `CBCentralManager` not powered on | Wait for `.poweredOn` before calling `scanForPeripherals` |

---

## Verification

1. Flash NexusOwie firmware and power the board.
2. Confirm with **nRF Connect** (or similar): peripheral advertises service `2f7e9c5a-…` and name `NexusOwie-XXXX`.
3. Run your app scan with the service UUID filter — only NexusOwie (and any device sharing that UUID) should appear.
4. Connect and confirm notifications on characteristic `2f7e9c5b-…`.

---

## Quick checklist

- [ ] Scan filter uses service UUID `2f7e9c5a-0b3a-4d1e-8c2a-1a2b3c4d5e6f`
- [ ] Optional UI filter: name starts with `NexusOwie-`
- [ ] Bluetooth permissions granted (platform-specific)
- [ ] After connect: notifications on `2f7e9c5b-0b3a-4d1e-8c2a-1a2b3c4d5e6f`
- [ ] No firmware change required for scan filtering

---

## Related docs and source

| Resource | Purpose |
|----------|---------|
| [`BLE_TRANSPORT.md`](BLE_TRANSPORT.md) | Full GATT layout, chunk reassembly, data streams |
| [`BLE_OVERRIDDEN_SOC.md`](BLE_OVERRIDDEN_SOC.md) | Reading fuel-gauge SOC from JSON stream |
| `include/ble_config.h` | UUID constants |
| `src/ble_transport.cpp` | Advertising and GATT server setup |
