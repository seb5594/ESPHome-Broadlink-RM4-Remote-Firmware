#!/usr/bin/env bash
# Checks that the IR receiver fix made it into a compiled firmware. Before
# ESPHome 2026.11 that is this project's digitalRead() wrapper: it is linked in,
# and ESPHome's interrupt-safe pin read (used by remote_receiver) calls it
# instead of LibreTiny's original digitalRead(). ESPHome 2026.11 reads pins with
# an interrupt through the interrupt object itself, so there the pin read has to
# call gpio_irq_read() and the wrapper is merely left in place.
#
# Usage: check_receiver_fix.sh <esphome build directory>
set -euo pipefail

build_dir="${1:?usage: $0 <esphome build directory>}"
isr_read="esphome::ISRInternalGPIOPin::digital_read()"

# Messages also show up as annotations when running in GitHub Actions.
fail() {
  echo "::error::$1"
  exit 1
}

nm_tool=$(find "$HOME/.platformio/packages" -type f -name 'arm-none-eabi-nm' -print -quit)
[[ -n "$nm_tool" ]] || fail "arm-none-eabi-nm not found, was the firmware compiled?"
objdump_tool="${nm_tool%nm}objdump"

mapfile -t elf_files < <(find "$build_dir" -name '*.elf')
[[ ${#elf_files[@]} -gt 0 ]] || fail "No ELF file found below $build_dir"

work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT

for elf in "${elf_files[@]}"; do
  # Full listings go to files first: grep -q stops reading early, which
  # would make the producing tool fail under pipefail.
  "$nm_tool" "$elf" > "$work_dir/symbols"
  grep -q ' T __wrap_digitalRead$' "$work_dir/symbols" || continue
  echo "Wrapper linked into $elf"

  "$objdump_tool" -d -C "$elf" > "$work_dir/disassembly"
  # The body of one function of the listing, up to the next empty line.
  function_body() {
    awk -v name="<$1>:" 'index($0, name) {found = 1} found && /^$/ {exit} found' "$work_dir/disassembly"
  }

  function_body "$isr_read" > "$work_dir/isr_read"
  [[ -s "$work_dir/isr_read" ]] || fail "$isr_read not found in $elf"

  if grep -q '__wrap_digitalRead' "$work_dir/isr_read"; then
    echo "::notice::$isr_read calls __wrap_digitalRead, the receiver fix is active"
    exit 0
  fi

  # ESPHome 2026.11 or newer: the ISR copy hands over to the pin object, which
  # reads through the interrupt.
  function_body "esphome::libretiny::ArduinoInternalGPIOPin::digital_read()" > "$work_dir/pin_read"
  if grep -q 'gpio_irq_read' "$work_dir/isr_read" "$work_dir/pin_read"; then
    echo "::notice::ESPHome reads interrupt pins with gpio_irq_read itself, the receiver fix is active"
    exit 0
  fi
  cat "$work_dir/isr_read" "$work_dir/pin_read"
  fail "$isr_read neither calls __wrap_digitalRead nor gpio_irq_read"
done

fail "__wrap_digitalRead is not linked into any of: ${elf_files[*]}"
