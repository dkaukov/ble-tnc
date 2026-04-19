# ModemTnc Example

Real AFSK modem firmware example using:
- `esp32-afsk` for AFSK modulation/demodulation
- `arduino-audio-tools` for ESP32 ADC/DAC audio path
- `DRA818` for SA818 radio control
- this library (`BleKissTnc`) for BLE-KISS transport

This example supports two host transports via PlatformIO environments:
- `esp32dev_ble`: BLE-KISS transport (BLE-KISS API UUIDs)
- `esp32dev_serial`: USB serial KISS transport (classic KISS framing on `Serial`)
- `esp32dev_dual`: BLE-KISS + USB serial KISS simultaneously

## Dependency Model

`esp32-afsk` is referenced as a PlatformIO dependency (`lib_deps`), not as a local clone.

## Build

BLE transport firmware:

```bash
pio run -d examples/ModemTnc -e esp32dev_ble
```

Serial transport firmware:

```bash
pio run -d examples/ModemTnc -e esp32dev_serial
```

Dual transport firmware:

```bash
pio run -d examples/ModemTnc -e esp32dev_dual
```

## Upload

BLE firmware:

```bash
pio run -d examples/ModemTnc -e esp32dev_ble -t upload --upload-port /dev/cu.usbserial-0001
```

Serial firmware:

```bash
pio run -d examples/ModemTnc -e esp32dev_serial -t upload --upload-port /dev/cu.usbserial-0001
```

Dual firmware:

```bash
pio run -d examples/ModemTnc -e esp32dev_dual -t upload --upload-port /dev/cu.usbserial-0001
```

Monitor:

```bash
pio device monitor -b 115200 -p /dev/cu.usbserial-0001
```

If your port differs:

```bash
pio device list
```

## Notes

- RF pin mapping and SA818 defaults follow the upstream `esp32-afsk` `kiss_tnc` example.
- BLE mode sends decoded AX.25 frames as KISS DATA (`0x00`) over BLE notifications.
- BLE mode accepts incoming KISS DATA frames from BLE writes and transmits over RF.
- Serial mode keeps the same modem behavior with KISS framing over USB serial.
- Dual mode enables both paths at once; decoded RF frames are emitted on both transports.
