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
The log says `The RF module does not answer on the UART`. The radio is
experimental and its UART pins are the least certain part of the setup, see
[RF module protocol](rf-protocol.md#what-is-not-confirmed). Check that
`rf.yaml` is in the package list, then try the other pad pair of UART0:
`pin_rf_uart_tx: PA11` and `pin_rf_uart_rx: PA12`. Please report what you find
in an [issue](https://github.com/seb5594/ESPHome-Broadlink-RM4-Remote-Firmware/issues).

**The radio answers, but nothing is received**
Test against a known good signal source first, see
[Testing 433 MHz](433mhz.md#testing-without-owning-a-433-mhz-device). The
`rf_scan` action shows whether the radio hears anything at all. If it does but
no code arrives, try `inverted: true` on the `data_pin`.
