# Repository Guidelines

## Project Structure & Module Organization
- `src/`: core header-only library (`BleKissCore.h`, `BleKissTnc.h`).
- `test/test_kiss_core/`: host-native Unity tests for KISS framing/decoder behavior.
- `examples/BasicBleKissTnc/`: minimal BLE-KISS example plus baseline resource profile.
- `examples/ModemTnc/`: full modem firmware with selectable BLE/serial KISS transports.
- `scripts/`: helper tooling for resource snapshots/diffs (for ESP32 memory/flash tracking).
- Root metadata: `platformio.ini`, `library.json`, `library.properties`, `README.md`.

## Build, Test, and Development Commands
- `pio test -e native`: run host unit tests (default env from root `platformio.ini`).
- `pio run -d examples/BasicBleKissTnc`: build the basic ESP32 BLE-KISS example.
- `pio run -d examples/ModemTnc -e esp32dev_ble`: build modem firmware with BLE transport.
- `pio run -d examples/ModemTnc -e esp32dev_serial`: build modem firmware with USB serial transport.
- `scripts/resource_diff.sh`: compare baseline vs full Basic example RAM/Flash usage.
- `pio ci --board esp32dev --lib=. -O "lib_deps=h2zero/NimBLE-Arduino" examples/BasicBleKissTnc/BasicBleKissTnc.ino`: quick CI-style compile check.

## Coding Style & Naming Conventions
- Language: C++17 (`-std=gnu++17`).
- Indentation/style: 2-space indentation, braces on same line, short focused functions.
- Naming: `snake_case` for local/static test helpers (for example `test_stream_decoder_*`), `PascalCase` for types/classes/templates, `UPPER_SNAKE_CASE` for constants/macros.
- Prefer fixed-size/static buffers and explicit bounds checks; avoid adding heap-heavy patterns in core paths.

## Testing Guidelines
- Framework: Unity (`#include <unity.h>`), tests live under `test/`.
- Add regression tests for parser edge cases: split frames, escapes, malformed sequences, and overflow behavior.
- Test function names should be descriptive and start with `test_`.
- Run `pio test -e native` before opening a PR.

## Commit & Pull Request Guidelines
- Keep commits scoped and imperative; current history uses concise messages such as `Fix BLE callback context by deferring modem TX to loop task`.
- Suggested format: `Fix/Add/Refactor <area>: <what changed>`.
- PRs should include: summary, rationale, test evidence (command + result), and resource impact notes when ESP32 memory/flash changes.
- Link related issues/spec references (for BLE-KISS behavior) and include logs/screenshots when runtime behavior changes.
