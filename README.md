# BleKissTncNimble

Header-only Arduino library for ESP32 + NimBLE-Arduino implementing the BLE-KISS API (KISS TNC over BLE).

This library targets **data KISS/TNC over BLE**, not voice/audio transports. BLE is used here to reduce RAM pressure versus Classic Bluetooth SPP/Bluedroid on ESP32.

## Spec and UUIDs

BLE-KISS API spec:
- https://github.com/hessu/aprs-specs/blob/master/BLE-KISS-API.md

UUIDs implemented by default:
- Service: `00000001-ba2a-46c9-ae49-01b0961f68bb`
- TX characteristic (app -> TNC, write): `00000002-ba2a-46c9-ae49-01b0961f68bb`
- RX characteristic (TNC -> app, notify/read): `00000003-ba2a-46c9-ae49-01b0961f68bb`

## Features

- Header-only `BleKissTnc` template class
- NimBLE-Arduino server with BLE-KISS service + characteristics
- KISS byte-stream parser (serial semantics over BLE)
- Correct KISS escaping/unescaping (`FEND/FESC/TFEND/TFESC`)
- Handles BLE chunk boundaries correctly:
  - partial KISS frame across writes
  - multiple KISS frames in one write
- Fixed-size incoming stream ring buffer
- Fixed-size decoded frame buffer
- Fixed-size outgoing frame queue (ring)
- MTU-aware notify chunking (`ATT payload = MTU - 3`)
- Callback for decoded KISS payloads (includes command/port byte at `data[0]`)
- Queue helpers and light stats counters
- Single-instance behavior is explicit (`begin()` fails if another instance is active)

## Constraints and Design Notes

- The library itself uses fixed-size buffers and does not allocate dynamic containers.
- NimBLE-Arduino internals may still allocate memory internally.
- Single active `BleKissTnc` instance per process.
- `sendDataFrame()` and `sendKissPayload()` enqueue encoded frames; sending happens via `loop()` / `drainOutgoing()`.
- Decoded RX frame callbacks (`setFrameCallback`) are dispatched from `loop()` context (not NimBLE callback/task context).
- If `loop()` is starved, RX callback dispatch can be delayed and incoming bytes may be dropped when the fixed input ring buffer fills.
- If `Config.requireNotifySubscription` is true (default), outbound data is only sent after client enables notifications.
- On disconnect, incoming parser state and outgoing queue are cleared.

## Sizing Tradeoffs

Template parameters:

```cpp
BleKissTnc<INCOMING_STREAM_SIZE, DECODED_FRAME_SIZE, OUTGOING_FRAME_SIZE, OUTGOING_QUEUE_DEPTH>
```

- Larger `INCOMING_STREAM_SIZE` tolerates bursty BLE writes before parser drain.
- `DECODED_FRAME_SIZE` is max decoded KISS payload length (command/port + payload).
- `OUTGOING_FRAME_SIZE` must fit worst-case escaped encoded frame length.
  - Rough bound for payload length `N`: encoded size is at most `N*2 + 3`.
- `OUTGOING_QUEUE_DEPTH` increases burst tolerance but costs static RAM.

## Basic Usage

```cpp
#include "BleKissTnc.h"

using MyBleKiss = BleKissTnc<512, 384, 384, 3>;
MyBleKiss bleKiss;

static void onKissFrame(const uint8_t* data, size_t len, void* ctx) {
  (void)ctx;
  if (len == 0) return;

  uint8_t cmdPort = data[0];
  uint8_t command = cmdPort & 0x0F;
  uint8_t port = (cmdPort >> 4) & 0x0F;

  if (command == 0x00 && len > 1) {
    // data[1..] is AX.25 payload for KISS data frame
  }
}

void setup() {
  bleKiss.setFrameCallback(onKissFrame);
  bleKiss.begin();
}

void loop() {
  bleKiss.loop();
}
```

See:
- [`examples/BasicBleKissTnc/BasicBleKissTnc.ino`](examples/BasicBleKissTnc/BasicBleKissTnc.ino)
- [`examples/BasicBleKissTnc/src/main.cpp`](examples/BasicBleKissTnc/src/main.cpp) (PlatformIO + resource tracking variant)

## Examples

1. `examples/BasicBleKissTnc`
- Simple BLE-KISS usage example.
- Includes resource tracking and empty-baseline build env for RAM/flash diffing.

2. `examples/ModemTnc`
- Real AFSK modem firmware (RF/audio path) using `esp32-afsk`.
- Supports both BLE and serial KISS transports:
  - `esp32dev_ble` (BLE-KISS)
  - `esp32dev_serial` (USB serial KISS)
- `esp32-afsk` is pulled via PlatformIO `lib_deps` (no local clone required).

## PlatformIO

This repo now includes `library.json` for PlatformIO library metadata.
The example folder is also a standalone PlatformIO project:
- `examples/BasicBleKissTnc/platformio.ini` (uses `lib_extra_dirs = ../..`)
- `examples/BasicBleKissTnc/src/main.cpp`

Compile the example with PlatformIO CI:

```bash
pio ci --board esp32dev --lib=. -O "lib_deps=h2zero/NimBLE-Arduino" examples/BasicBleKissTnc/BasicBleKissTnc.ino
```

Or build the basic example project directly:

```bash
pio run -d examples/BasicBleKissTnc
```

Build modem firmware examples:

```bash
pio run -d examples/ModemTnc -e esp32dev_ble
pio run -d examples/ModemTnc -e esp32dev_serial
```

Run host unit tests (no hardware required):

```bash
pio test -e native
```

## Unit Tests

The repository includes host-native unit tests under `test/test_kiss_core` for core KISS behavior:
- command/port byte packing
- frame encoding and escape correctness
- byte-stream reassembly for split frames
- multiple frames within one stream chunk
- malformed escape detection
- decoded frame overflow handling

These tests validate logic used by `BleKissTnc` via the shared `src/BleKissCore.h`.

## Resource Usage (ESP32)

Current example uses a low-RAM profile:
- template sizes: `BleKissTnc<512, 384, 384, 3>`
- preferred MTU: `185`
- TX enqueue guarded by `canSend()`
- fixed buffer estimate for this profile: `2060` bytes (`estimatedStaticBufferBytes()`)
- NimBLE low-RAM compile flags (see `examples/BasicBleKissTnc/platformio.ini`):
  - `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`
  - `CONFIG_BT_NIMBLE_MAX_BONDS=1`
  - `CONFIG_BT_NIMBLE_MAX_CCCDS=2`
  - `CONFIG_BT_NIMBLE_MSYS1_BLOCK_COUNT=8`
  - `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=185`
  - `CONFIG_BT_NIMBLE_ROLE_CENTRAL_DISABLED=1`
  - `CONFIG_BT_NIMBLE_ROLE_OBSERVER_DISABLED=1`

Library fixed-buffer estimate is available at compile time:
- `BleKissTnc<...>::estimatedStaticBufferBytes()`
- Formula: `INCOMING_STREAM_SIZE + DECODED_FRAME_SIZE + OUTGOING_QUEUE_DEPTH * (OUTGOING_FRAME_SIZE + sizeof(size_t))`

Baseline diff workflow (empty app baseline):

```bash
# compares:
# - esp32dev_baseline (empty app, no BleKissTnc instance)
# - esp32dev (full example)
scripts/resource_diff.sh
```

Latest measured `resource_diff.sh` output:
- RAM baseline: `22876 / 327680`
- RAM full: `36856 / 327680`
- RAM delta: `+13980 bytes`
- Flash baseline: `328608 / 1310720`
- Flash full: `593096 / 1310720`
- Flash delta: `+264488 bytes`

Runtime heap probe in example:
- The example prints `[heap]` snapshots at:
  - `boot`
  - `afterBegin`
  - `onConnect`
  - `onDisconnect`
  - periodic (every 10s)
- Fields:
  - `free`: current free heap (`ESP.getFreeHeap()`)
  - `min`: minimum-ever free heap (`ESP.getMinFreeHeap()`)
  - `largest`: largest free 8-bit block (`heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)`)
  - `deltaBoot`: `free - free_at_boot`

## API Summary

Core:
- `bool begin()`
- `void end()`
- `void loop()`
- `size_t drainOutgoing(size_t maxChunks = 1)`
- `bool startAdvertising()`

State:
- `bool isBegun() const`
- `bool isConnected() const`
- `bool isNotifySubscribed() const`
- `bool canSend() const`
- `uint16_t getMtu() const`

Callbacks:
- `setFrameCallback(FrameCallback cb, void* ctx = nullptr)`
- `setConnectCallback(EventCallback cb, void* ctx = nullptr)`
- `setDisconnectCallback(EventCallback cb, void* ctx = nullptr)`

TX:
- `bool sendDataFrame(const uint8_t* payload, size_t len, uint8_t port = 0)`
- `bool sendKissPayload(const uint8_t* kissPayload, size_t len)`

Queue:
- `size_t outgoingQueueCount() const`
- `size_t outgoingQueueCapacity() const`
- `size_t outgoingQueueFree() const`
- `bool outgoingQueueEmpty() const`
- `bool outgoingQueueFull() const`

Stats:
- `const Stats& stats() const`
- `void clearStats()`

## Notes for BLE-KISS Clients

This is intended for BLE-KISS capable clients/apps implementing the BLE-KISS spec, rather than as a Classic Bluetooth SPP transport for APRSdroid.
