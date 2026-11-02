# Troubleshooting

**ltchiptool can't connect**
TX and RX swapped is the most common cause. Then check that the boot pin is
bridged to `3.3V` *before* the RM4 is powered (`IOA0` on the RM4 Mini, see the
device README for other models), that `IOA13` is not pulled to GND and that
the adapter really uses 3.3 V levels.

**The build stops with a `min_version` error**
Your ESPHome is older than 2026.9.0. Update the ESPHome app or
`pip install -U esphome`.

**The device crashes or reboots when sending**
Same cause: firmware built with an ESPHome release before 2026.9.0. Rebuild
with a current version.

**Nothing arrives at the receiver**
Make sure your config still pulls in the whole package, the
[receiver fix](configuration.md#the-receiver-fix) comes with it. Then set `level: DEBUG` in
`logger` and uncomment `dump: raw`, and watch the log while pressing buttons
from about one metre away. If it stays silent, open an issue with the log.

**Air conditioner codes arrive in pieces**
Raise `ir_receiver_idle` to `80ms`.

**The device doesn't show up after flashing**
It opens a hotspot named after the device when it can't connect. Check the
Wi-Fi credentials in your secrets; only 2.4 GHz networks work.

**Two devices have the same MAC and IP address**
If the router shows `00:e0:4c:b7:23:00` for both, their Wi-Fi modules have no
address of their own and the driver gives every unit the same one. Add the
real address of each device, see
[Same MAC address on several devices](configuration.md#same-mac-address-on-several-devices).

**The 433 MHz radio does not answer (RM4 Pro)**
The log says `No reply to command 0x60E` or `The RF module does not answer on
the UART`. The line before or after tells which case it is:
`nothing arrived on the UART` means the module said nothing at all, and
`N bytes arrived on the UART` followed by bytes means it answered, but not in a
frame the component understands. Please open an
[issue](https://github.com/seb5594/ESPHome-Broadlink-RM4-Remote-Firmware/issues)
with the log, set `level: VERBOSE` in `logger` to also see every frame that is
sent. The UART pins (`PA14`/`PA13`) are not configurable: they are the ones the
stock firmware uses.

A line `uart is marked FAILED: SoftwareSerial is not implemented for this chip`
comes from an older version of `rf.yaml` that still declared a `uart:` section.
Update the package and remove that section from your own config.

**`rf_learn` or `rf_find_frequency` finds no transmitter**
Hold the key of the remote about 10 cm from the device for the whole sweep,
which takes a few seconds per pass. Check the log (`level: DEBUG`): every pass
prints its strongest channel and the average. If the strongest channel is only
one or two above the average although the remote is right there, lower
`sweep_margin` in the `bl3361:` section (default 3). A remote that sends only
once per press, with long pauses, is easier to find with `rf_scan` and
`rf_set_frequency`.

**`rf_learn` finds the frequency but no code**
The code has to be seen twice. Press the key briefly, a few times in a row,
once the notification asks for it. A remote that sends a different code every
time (rolling code) never repeats and cannot be learned. A code that is received
but not matched has a different number of pulses or timings each time: look at
the `Received ... pulses` lines.

**The radio answers, but nothing is received**
Test against a known good signal source first, see
[Testing 433 MHz](433mhz.md#testing-without-owning-a-433-mhz-device). The
`rf_scan` action shows whether the radio hears anything at all. If it does but
no code arrives, try `inverted: true` on the `data_pin`.
