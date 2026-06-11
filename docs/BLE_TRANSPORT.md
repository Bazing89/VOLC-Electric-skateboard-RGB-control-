# BLE data transport (NexusOwie firmware)

This document describes how battery data leaves the ESP32 firmware over Bluetooth Low Energy, how individual values (cell voltages, current, SOC, temperatures, etc.) are encoded, and how a companion app should receive and reconstruct that data.

The firmware exposes **two parallel streams** on a single GATT characteristic:

| Stream | Type byte | Content | Rate |
|--------|-----------|---------|------|
| Raw BMS | `0` | Complete BMS UART frames (binary) | As frames arrive on the wire (~1 Hz for voltages; varies by packet type) |
| Status JSON | `1` | Parsed summary as UTF-8 JSON | 1 Hz while a central is connected |

Both streams use the same **chunked notification** framing so they fit within the default BLE ATT MTU (20 bytes per notification).

---

## GATT layout

| Item | Value |
|------|-------|
| Device name | `NexusOwie-XXXX` where `XXXX` is the lower 16 bits of the ESP32 MAC (hex, uppercase) |
| Service UUID | `2f7e9c5a-0b3a-4d1e-8c2a-1a2b3c4d5e6f` |
| Data characteristic UUID | `2f7e9c5b-0b3a-4d1e-8c2a-1a2b3c4d5e6f` |
| Characteristic properties | READ, NOTIFY |
| Initial READ value | `"Nexus Owie"` (ASCII; not used for live data) |

**Discovery:** the device advertises continuously (~20–100 ms between advertisement packets) with the service UUID in the advertisement. Subscribe to notifications on the data characteristic after connecting.

Constants are defined in `include/ble_config.h`. Implementation is in `src/ble_transport.cpp`.

---

## Chunk frame layout (every notification)

Each GATT notification is at most **20 bytes**. Logical messages longer than 15 bytes are split into multiple notifications.

```
Offset  Size  Field
------  ----  -----
0       1     stream_type   0 = raw BMS, 1 = status JSON
1       2     msg_id        uint16, little-endian; new id per logical message
3       1     frag_idx      0 .. frag_count-1
4       1     frag_count    total fragments for this msg_id (1..255)
5       0-15  payload       slice of the logical message
```

- **Header:** 5 bytes fixed.
- **Payload per chunk:** up to 15 bytes (`kMaxPayloadPerChunk` in `ble_transport.cpp`).
- **Total notification size:** `5 + payload_len`, never more than 20.

`msg_id` increments for every logical message (both stream types share the same counter). It wraps from `65535` back to `1` (zero is skipped).

### Reassembly algorithm (app side)

For each notification:

1. Parse the 5-byte header.
2. Key reassembly buffers by `(stream_type, msg_id)`.
3. Store `payload` at logical offset `frag_idx * 15` (or append in `frag_idx` order).
4. When `frag_idx + 1 == frag_count`, the message is complete.
5. Concatenate all payload slices in order → one logical message.
6. Dispatch by `stream_type`:
   - `0` → parse as a BMS frame (see below).
   - `1` → decode UTF-8 JSON (see below).

**Out-of-order fragments:** the firmware always sends fragments in order (`frag_idx` 0, 1, 2, …), but BLE may deliver notifications out of order. Buffer by `frag_idx` and only finalize when all indices `0 .. frag_count-1` are present.

**Interleaved messages:** multiple logical messages can be in flight. Always use `(stream_type, msg_id)` as the reassembly key, not arrival order alone.

**MTU:** the firmware targets the default 20-byte ATT payload. If you negotiate a larger MTU, the device still sends 20-byte notifications today; do not assume larger chunks.

---

## Stream 1: Status JSON (type byte = 1)

### When it is sent

While a BLE central is connected, the firmware schedules a push every **1000 ms** (`scheduleStatusPush` in `ble_transport.cpp`). Each push builds fresh JSON from the current `BmsRelay` state and sends it through `sendChunked`.

If nothing is connected, notifications are skipped (advertising continues).

### JSON schema

Built by `buildOwieStatusJson()` in `src/status_json.cpp`. All string values; numbers are formatted into strings.

| Field | Example | Source |
|-------|---------|--------|
| `UPTIME` | `"12m34s"` or `"1h5m0s"` | Device uptime |
| `TOTAL_VOLTAGE` | `"63.42v"` | Sum of 15 cell voltages (V, 2 decimal places) |
| `CURRENT_AMPS` | `"1.2 Amps"` | Signed current (A, 1 decimal place) |
| `BMS_SOC` | `"87%"` | SOC reported by BMS (type 0x3 packet) |
| `OVERRIDDEN_SOC` | `"85%"` | SOC sent to the main board (fuel gauge) |
| `USED_CHARGE_MAH` | `"1234 mAh"` | Discharged charge estimate |
| `REGENERATED_CHARGE_MAH` | `"56 mAh"` | Regenerated charge estimate |
| `CELL_VOLTAGE_TABLE` | HTML `<tr><td>…</td>…` | 3×5 table of cell voltages in volts (float) |
| `TEMPERATURE_TABLE` | HTML `<tr><td>…</td>…` | One row of 5 temperature cells (°C, integer) |

If `BmsRelay` is unavailable, only `UPTIME` is present.

**Note:** `CELL_VOLTAGE_TABLE` and `TEMPERATURE_TABLE` use minimal HTML fragments (legacy format from the old WiFi dashboard). For a native app, prefer parsing **raw BMS type 0x2 / 0x4 packets** (exact millivolts / °C) or add your own JSON fields upstream; the table strings are display-oriented, not a strict API.

### App handling

After reassembly, treat the payload as **UTF-8 JSON** and parse with a standard JSON library. No BOM; no trailing null required (use reassembled byte length).

This stream is equivalent to the old HTTP `GET /autoupdate` response used by the WiFi test AP (`src/wifi_test_ap.cpp`).

### SOC: `BMS_SOC` vs `OVERRIDDEN_SOC` (read this if override is missing in the app)

The firmware exposes **two different SOC values on two different BLE streams**. They are **not interchangeable**.

| What you want | BLE stream | Field / location | Meaning |
|---------------|------------|------------------|---------|
| BMS-reported SOC | **Stream 0** (raw BMS) | Type `0x3` frame, body byte at offset 4 | Original percentage from the BMS UART packet |
| BMS-reported SOC (same value) | **Stream 1** (JSON) | `"BMS_SOC": "87%"` | Cached copy of the last type `0x3` value |
| **Overridden / fuel-gauge SOC** | **Stream 1** (JSON) **only** | `"OVERRIDDEN_SOC": "85%"` | SOC the firmware sends to the OneWheel main board |
| Overridden SOC | **Stream 0** | **Not available** | Raw stream is sent *before* override is applied |

**Common app bug:** parsing type `0x3` from stream `0` for the UI SOC works for BMS SOC, but **overridden SOC is never present in stream `0`**. You must subscribe to stream `1`, reassemble JSON, and read the exact key `OVERRIDDEN_SOC`.

**Example reassembled JSON payload** (stream type byte = `1`):

```json
{
  "UPTIME": "12m34s",
  "TOTAL_VOLTAGE": "63.42v",
  "CURRENT_AMPS": "1.2 Amps",
  "BMS_SOC": "87%",
  "OVERRIDDEN_SOC": "85%",
  "USED_CHARGE_MAH": "1234 mAh",
  "REGENERATED_CHARGE_MAH": "56 mAh",
  "CELL_VOLTAGE_TABLE": "<tr><td>4.22</td>…",
  "TEMPERATURE_TABLE": "<tr><td>25</td>…"
}
```

All JSON values are **strings**, including SOC. Strip the trailing `%` and parse as integer:

```typescript
function parseSocField(value: string | undefined): number | null {
  if (value == null) return null;
  const n = parseInt(value.replace(/%$/, ""), 10);
  if (Number.isNaN(n) || n < 0) return null; // "-1%" means not ready yet
  return n;
}

// After reassembling stream_type === 1:
const bmsSoc = parseSocField(json.BMS_SOC);
const overrideSoc = parseSocField(json.OVERRIDDEN_SOC); // use this for displayed SOC
```

**Invalid / not-ready values:** until the first type `0x3` BMS packet is processed, both fields may be `"-1%"`. The fuel gauge may also report `0%` briefly before type `0x2` voltage packets establish a range. Do not treat raw stream `0x3` as overridden SOC.

**Minimal fix checklist for companion apps:**

1. Reassemble **both** stream types (`0` and `1`) from the same characteristic — key buffers by `(stream_type, msg_id)`.
2. On each complete **stream `1`** message, `JSON.parse` the UTF-8 payload.
3. Read **`OVERRIDDEN_SOC`** (exact spelling, all caps, underscore) for the fuel-gauge SOC shown to the user.
4. Optionally read **`BMS_SOC`** for comparison, or parse stream `0` type `0x3` for the raw BMS value.
5. Do **not** expect overridden SOC inside raw type `0x3` frames on stream `0`.

---

## Stream 0: Raw BMS frames (type byte = 0)

### When it is sent

Every time the firmware receives a complete BMS frame on UART, it invokes `streamBMSPacket()` (`src/bms_main.cpp`). That happens in the **received packet callback**, which runs **before** any SOC/serial overrides are applied for forwarding to the main board. The BLE stream therefore carries **original BMS bytes**, not the modified packets sent to the OneWheel controller.

Unknown bytes that do not match the BMS preamble are also streamed (as a growing buffer, capped at 128 bytes) for debugging.

### BMS frame format

Documented in the project README; validated in `lib/bms/packet.cpp`.

```
[ FF 55 AA ] [ type ] [ body … ] [ CRC hi ] [ CRC lo ]
  preamble     1 B      variable      2 bytes (BE sum)
```

- **Preamble:** fixed `FF 55 AA`.
- **Type:** message type at offset 3 (see table below).
- **Body:** length depends on type.
- **CRC:** 16-bit big-endian. Sum of all bytes before the CRC field must equal the CRC value (firmware validates by subtracting each byte from CRC until zero).

### Frame lengths by type

From `PACKET_LENGTHS_BY_TYPE` in `lib/bms/packet.h`:

| Type | Total frame length (bytes) | `-1` = unknown / not seen |
|------|------------------------------|---------------------------|
| 0 | 7 | Status |
| 1 | — | Unused |
| 2 | 38 | Cell voltages |
| 3 | 7 | Battery percentage |
| 4 | 11 | Temperatures |
| 5 | 8 | Current |
| 6 | 10 | BMS serial |
| 7 | 13 | |
| 8 | 7 | |
| 9 | 7 | |
| 10 | — | Unused |
| 11 | 8 | Power-on (once) |
| 12–17 | 8–16 | Other |

### How values are encoded inside frames

Parsing logic lives in `lib/bms/packet_parsers.cpp`. Multi-byte integers in the body are **big-endian** unless noted.

#### Type 0x2 — Cell voltages (primary source for per-cell values)

- **Frame length:** 38 bytes.
- **Body:** 32 bytes = 16 × `int16` big-endian.
- **Cells 0–14:** millivolts per cell (`uint16` range; stored as signed int16 on wire).
- **16th value:** present on the wire; firmware reads it but does not expose it in JSON.
- **Total pack voltage** (JSON `TOTAL_VOLTAGE`): firmware sums cells 0–14 (`cellVoltageParser`).

Layout (body offsets):

```
cell[i] = BE int16 at body offset (i * 2), i = 0..14
millivolts = unsigned interpretation of int16
```

#### Type 0x5 — Current

- **Frame length:** 8 bytes.
- **Body:** 2 bytes, signed `int16` big-endian.
- **Scale:** `current_mA = raw * 55` (`CURRENT_SCALER` in `bms_relay.h`).
- **Sign:** positive/negative per int16 convention (charging vs discharging depends on BMS sign).

#### Type 0x3 — State of charge

- **Frame length:** 7 bytes.
- **Body:** 1 byte, signed `%` (0–100 typical).

#### Type 0x4 — Temperatures

- **Frame length:** 11 bytes.
- **Body:** 5 × `int8` °C (one per thermistor/channel).

#### Type 0x0 — Status flags

- **Frame length:** 7 bytes.
- **Body:** 1 status byte. Bit masks used by firmware:
  - `0x20` — charging
  - `0x04` — battery empty
  - `0x03` — temperature out of range (hot/cold)
  - `0x08` — overcharged

#### Type 0x6 — BMS serial

- **Body:** 4-byte big-endian serial (lower 7 digits of sticker number).

### App handling

After reassembly, interpret the buffer as a single BMS frame:

1. Verify preamble `FF 55 AA`.
2. Read `type` at index 3.
3. Check length against the table above.
4. Validate CRC.
5. Parse body per type.

Maintain **latest value per type** in app state; update UI when a new frame of that type arrives. Rates are not fixed for all types (cell voltage ~1 Hz; others differ). The firmware may also **replay** stale packets if the BMS goes quiet (`maybeReplayPackets` in `bms_relay.cpp`).

This stream matches the binary payloads previously sent on the WebSocket `/rawdata` endpoint (see `data/monitor.html`).

---

## End-to-end data flow (firmware)

```
BMS UART (115200 8N1)
        │
        ▼
   BmsRelay::loop()
   - frame sync on FF 55 AA
   - CRC validate
   - parse into cell_millivolts_, current, SOC, temps, …
        │
        ├─► receivedPacketCallback ──► streamBMSPacket() ──► sendChunked(type=0)
        │
        └─► (internal state) ──► buildOwieStatusJson() every 1s ──► sendChunked(type=1)
                                        │
                                        ▼
                              GATT NOTIFY (20-byte chunks)
```

---

## Implementing the receiver (companion app)

### 1. Scan and connect

- Scan with service UUID filter `2f7e9c5a-0b3a-4d1e-8c2a-1a2b3c4d5e6f` so only NexusOwie devices appear in your app. Optionally verify name prefix `NexusOwie-`. See [`BLE_SCAN_FILTERING.md`](BLE_SCAN_FILTERING.md) for platform examples (iOS, Android, React Native).
- Connect, discover services, enable notifications on `2f7e9c5b-0b3a-4d1e-8c2a-1a2b3c4d5e6f`.

### 2. Chunk reassembler (language-agnostic)

```text
struct PendingMessage {
  stream_type: u8
  msg_id: u16
  frag_count: u8
  parts: map<u8, ByteArray>   // frag_idx -> payload
}

onNotification(data: ByteArray):
  if data.length < 5: return

  stream_type = data[0]
  msg_id = data[1] | (data[2] << 8)
  frag_idx = data[3]
  frag_count = data[4]
  payload = data[5..]

  key = (stream_type, msg_id)
  pending = map[key] or new PendingMessage(...)
  pending.parts[frag_idx] = payload

  if pending.parts.count == frag_count:
    message = concat(pending.parts[0], pending.parts[1], …)
    delete map[key]
    onCompleteMessage(stream_type, message)
```

### 3. Dispatch complete messages

**JSON (`stream_type == 1`):**

```text
json = UTF8.decode(message)
state.totalVoltage = parseFloat(json.TOTAL_VOLTAGE)
state.currentAmps = parseFloat(json.CURRENT_AMPS)
state.bmsSoc = parseSoc(json.BMS_SOC)           // optional; raw 0x3 is equivalent
state.overrideSoc = parseSoc(json.OVERRIDDEN_SOC)  // required for fuel-gauge SOC
// … or parse CELL_VOLTAGE_TABLE / use raw 0x2 instead

function parseSoc(s):
  if s is missing: return null
  n = int(s.replace("%", ""))
  if n < 0: return null
  return n
```

**Raw BMS (`stream_type == 0`):**

```text
if message[0..2] != [0xFF, 0x55, 0xAA]: handleUnknown(message); return
type = message[3]
if !validateCrc(message): return

switch type:
  case 0x2:
    for i in 0..14:
      mv = BE_int16(message, 4 + i*2)
      state.cellMillivolts[i] = mv
    state.totalMillivolts = sum(state.cellMillivolts)
  case 0x5:
    raw = BE_int16(message, 4)
    state.currentMilliamps = raw * 55
  case 0x3:
    state.bmsSocPercent = message[4]
  case 0x4:
    for i in 0..4:
      state.temperaturesC[i] = int8(message[4 + i])
  case 0x0:
    state.statusFlags = message[4]
```

### 4. CRC helper (raw BMS)

```text
function validateCrc(frame):
  n = frame.length
  crc = (frame[n-2] << 8) | frame[n-1]
  for i in 0 .. n-3:
    crc -= frame[i]
  return crc == 0
```

### 5. Recommended app strategy

| Approach | Pros | Cons |
|----------|------|------|
| **JSON only** | Simple; one parse per second; matches dashboard fields | Cell table is HTML strings; less precise control |
| **Raw BMS only** | Exact millivolts and full protocol access | Must implement frame parser and type dispatch |
| **Both** | JSON for UI labels / fuel gauge fields; raw 0x2/0x5 for charts | Two code paths; keep `msg_id` spaces separate |

For live cell voltage graphs, subscribe to stream `0` and handle type `0x2`. For a simple status screen, stream `1` alone is enough.

### 6. iOS (CoreBluetooth) sketch

- `CBCentralManager` scan with `CBUUID(string: "2F7E9C5A-0B3A-4D1E-8C2A-1A2B3C4D5E6F")`.
- On connect, `peripheral.discoverServices` → `discoverCharacteristics` for `2F7E9C5B-0B3A-4D1E-8C2A-1A2B3C4D5E6F`.
- `setNotifyValue(true, for: characteristic)`.
- In `peripheral(_:didUpdateValueFor:)`, feed `characteristic.value` into the reassembler.

### 7. Android (BLE) sketch

- `BluetoothLeScanner` with `ScanFilter` on the service UUID.
- `BluetoothGattCallback.onCharacteristicChanged` receives notification bytes.
- Same reassembly logic as above.

---

## Reference: example chunk sequence

Logical JSON message 40 bytes, `msg_id = 0x0102`, `stream_type = 1`:

| Notification | Bytes (hex) | Meaning |
|--------------|-------------|---------|
| 1 | `01 02 01 00 03` + 15 payload bytes | frag 0 of 3 |
| 2 | `01 02 01 01 03` + 15 payload bytes | frag 1 of 3 |
| 3 | `01 02 01 02 03` + 10 payload bytes | frag 2 of 3 (last) |

Reassembled payload = 15 + 15 + 10 = 40 bytes → parse as JSON.

---

## Related source files

| File | Role |
|------|------|
| `include/ble_config.h` | UUIDs, stream type constants, wire format comments |
| `src/ble_transport.cpp` | GATT server, chunking, 1 Hz JSON push |
| `src/bms_main.cpp` | Hooks UART packets → `streamBMSPacket` |
| `src/status_json.cpp` | JSON field definitions and formatting |
| `lib/bms/packet_parsers.cpp` | Binary decoding of cell/current/SOC/temp |
| `lib/bms/packet.h` | Frame lengths by type |
| `src/wifi_test_ap.cpp` | HTTP `/autoupdate` JSON (same payload as BLE JSON stream) |
