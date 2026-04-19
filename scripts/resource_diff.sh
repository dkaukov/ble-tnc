#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="examples/BasicBleKissTnc"
FULL_ENV="esp32dev"
BASE_ENV="esp32dev_baseline"

parse_usage() {
  local log_file="$1"
  local ram_used ram_total flash_used flash_total

  ram_used="$(grep -E 'RAM:.*\(used [0-9]+ bytes from [0-9]+ bytes\)' "$log_file" | sed -E 's/.*\(used ([0-9]+) bytes from ([0-9]+) bytes\).*/\1/' | tail -n1)"
  ram_total="$(grep -E 'RAM:.*\(used [0-9]+ bytes from [0-9]+ bytes\)' "$log_file" | sed -E 's/.*\(used ([0-9]+) bytes from ([0-9]+) bytes\).*/\2/' | tail -n1)"
  flash_used="$(grep -E 'Flash:.*\(used [0-9]+ bytes from [0-9]+ bytes\)' "$log_file" | sed -E 's/.*\(used ([0-9]+) bytes from ([0-9]+) bytes\).*/\1/' | tail -n1)"
  flash_total="$(grep -E 'Flash:.*\(used [0-9]+ bytes from [0-9]+ bytes\)' "$log_file" | sed -E 's/.*\(used ([0-9]+) bytes from ([0-9]+) bytes\).*/\2/' | tail -n1)"

  if [[ -z "$ram_used" || -z "$flash_used" ]]; then
    echo "Failed to parse RAM/Flash usage" >&2
    cat "$log_file" >&2
    exit 1
  fi

  printf '%s;%s;%s;%s\n' "$ram_used" "$ram_total" "$flash_used" "$flash_total"
}

TMP_FULL="$(mktemp)"
TMP_BASE="$(mktemp)"
trap 'rm -f "$TMP_FULL" "$TMP_BASE"' EXIT

pio run -d "$PROJECT_DIR" -e "$BASE_ENV" >"$TMP_BASE" 2>&1
pio run -d "$PROJECT_DIR" -e "$FULL_ENV" >"$TMP_FULL" 2>&1

IFS=';' read -r BASE_RAM_USED BASE_RAM_TOTAL BASE_FLASH_USED BASE_FLASH_TOTAL <<<"$(parse_usage "$TMP_BASE")"
IFS=';' read -r FULL_RAM_USED FULL_RAM_TOTAL FULL_FLASH_USED FULL_FLASH_TOTAL <<<"$(parse_usage "$TMP_FULL")"

RAM_DELTA=$((FULL_RAM_USED - BASE_RAM_USED))
FLASH_DELTA=$((FULL_FLASH_USED - BASE_FLASH_USED))

printf 'Baseline env (empty app): %s\n' "$BASE_ENV"
printf 'Full env: %s\n' "$FULL_ENV"
printf 'RAM   baseline: %s / %s\n' "$BASE_RAM_USED" "$BASE_RAM_TOTAL"
printf 'RAM   full    : %s / %s\n' "$FULL_RAM_USED" "$FULL_RAM_TOTAL"
printf 'RAM   delta   : %+d bytes\n' "$RAM_DELTA"
printf 'Flash baseline: %s / %s\n' "$BASE_FLASH_USED" "$BASE_FLASH_TOTAL"
printf 'Flash full    : %s / %s\n' "$FULL_FLASH_USED" "$FULL_FLASH_TOTAL"
printf 'Flash delta   : %+d bytes\n' "$FLASH_DELTA"
