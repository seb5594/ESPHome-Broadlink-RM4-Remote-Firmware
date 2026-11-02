# RF module protocol (BL3361)

Part of [ESPHome-Broadlink-RM4](../README.md). **Everything on this page is
experimental**: it was read out of a flash dump of a stock RM4 Pro and has not
been confirmed against a live device yet. If you own an RM4 Pro, a logic
analyzer or an RTL-SDR can settle most open points, see
[What is not confirmed](#what-is-not-confirmed).

- [How the radio is wired](#how-the-radio-is-wired)
- [The UART protocol](#the-uart-protocol)
- [Receiving and transmitting](#receiving-and-transmitting)
- [Frequencies](#frequencies)
- [Where the findings come from](#where-the-findings-come-from)
- [What is not confirmed](#what-is-not-confirmed)

## How the radio is wired

The RM4 Pro has two chips. The Realtek RTL8720CF handles Wi-Fi and the IR part
and is the one ESPHome runs on. The 433 MHz radio is a separate module,
**BL3361-1**. The stock firmware talks to it over two connections:

| Connection | What goes over it |
|---|---|
| UART0 of the RTL8720CF, TX `PA14` and RX `PA13` | Commands: tune, switch between receive and transmit, read the signal strength. |
| One raw data pin, `PA20` | The radio signal itself. In receive mode the module puts the demodulated OOK signal on it; in transmit mode it keys its transmitter with the level the Wi-Fi chip drives onto it. |

The stock firmware switches no other pin for the module: there is no reset
line and no power switch in the profile or in the driver. `PA20` comes from the
hardware profile that is stored in the flash, see
[the pin map in the RM4 Pro page](devices/rm4-pro.md#pin-map-and-how-it-was-verified).
This likely explains why phdindota saw only noise on that pin: nothing
had configured the module, so its receiver was not tuned and not even switched
on.

## The UART protocol

115200 baud, 8 data bits, even parity, 1 stop bit, on `PA14` (TX) and `PA13`
(RX). The stock firmware opens the port in one function (`uart_drv_open`): for
port 0 it passes pin 14 as TX and pin 13 as RX to the UART driver, and the RF
driver asks for port 0 with baud rate 115200, parity value 2 (`UartParityEven`
in the SDK), 8 data bits and one stop bit.

The component does not use ESPHome's `uart` component for this. On the RTL8720C,
UART0 can be routed to two pairs of pins (`PA11`/`PA12` and `PA13`/`PA14`), so
LibreTiny leaves `PIN_SERIAL0_TX` and `PIN_SERIAL0_RX` undefined, and ESPHome
only matches pins against exactly these names. `PA14`/`PA13` therefore end up on
a software serial that does not exist on this chip, and the writes go to the
log UART instead. The component opens the port itself through the SDK of the chip
and names the pins. It does not use the `Serial0` class of LibreTiny either: the
receive interrupt behind it does not always run for the last byte of a reply, so
that byte stays in the FIFO until the next one arrives and every reply looks one
byte short. The port is read by polling instead.

Every request is one frame,
and the module answers with a frame whose command number is the request's plus
one. The stock driver waits up to 300 ms for the answer.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | Magic, the bytes `A5 A5 5A 5A` |
| 4 | 2 | Checksum, little endian |
| 6 | 2 | Command |
| 8 | 2 | Payload length |
| 10 | 2 | Reserved, 0 |
| 12 | n | Payload |

The checksum is `0xBEAF` plus the sum of all bytes of the frame, computed with
the checksum field set to 0, truncated to 16 bits. Example: the frame for
command `0x60E` (idle) without payload is `A5 A5 5A 5A C1 C0 0E 06 00 00 00 00`.

| Command | Payload | Meaning |
|---|---|---|
| `0x600` | tuning blob, 204 bytes | Load the transceiver settings for a frequency |
| `0x602` | channel, direction | Select the channel; direction `1` = receive, `2` = transmit |
| `0x604` | threshold | Signal strength threshold (only the stock frequency scan uses it) |
| `0x606` | none | Read the signal strength, the answer holds one byte |
| `0x608` | none | Enter receive mode |
| `0x60A` | none | Enter transmit mode |
| `0x60E` | none | Idle; the stock firmware sends it once at start-up and when it closes the module |

Every change of mode runs the same three steps: `0x600` with the tuning blob,
then `0x608` or `0x60A`, then `0x602` with the channel and direction.

## Receiving and transmitting

- **Receive.** After the three steps above the module drives `PA20` with the
  demodulated signal. Without a transmitter in range the output is a stream of
  short random pulses, because the receiver has no squelch. The
  [`bl3361` component](../esphome-broadlink-rm4-remote-firmware/components/bl3361)
  measures the pulses with an edge interrupt, merges everything shorter than
  100 µs into its neighbours and cuts the stream into frames at every pause
  longer than 15 ms. Frames with fewer than 24 pulses are dropped as noise.
- **Transmit.** The firmware drives `PA20` itself, high for a mark and low for
  a space, and the module keys its transmitter with it (OOK). The
  component does this with timed delays; the Wi-Fi interrupts add some jitter,
  which is why a code is sent several times by default.
- One component owns the pin for both directions. ESPHome's own
  `remote_receiver` and `remote_transmitter` are not used on purpose: on the
  RTL8720C an interrupt and a GPIO output cannot be active on the same pad, and
  the transmitter drives its pad through the PWM peripheral.

## Frequencies

The module tunes to 305-339.999 MHz and 430-439.999 MHz. The stock firmware
turns a frequency into a request of the form *(base frequency, step, 20
channels)*:

| Range | Step | Base |
|---|---|---|
| 315.000-315.999 MHz and 433.000-433.999 MHz | 10 kHz | frequency rounded down to 100 kHz |
| the rest of the two bands | 100 kHz | frequency rounded down to 1 MHz |

433.92 MHz therefore becomes base 433.900 MHz, step 10 kHz, channel 2. The
tuning blob holds 96 register/value pairs for the transceiver chip, the
automatic frequency control threshold and two fixed bytes. Eight of the
registers (the synthesizer words) depend on the base frequency and come from a
table with one row per supported base.

The `rf_scan` action reads the signal strength of all 20 channels of the
current request, which is the quickest way to see where a transmitter really
sits. Its window is 200 kHz wide for 315 and 433 MHz and 2 MHz wide elsewhere,
`rf_set_frequency` moves it.

## Where the findings come from

The firmware image has no symbols, but it contains log strings with function
names. The driver is a table of seven operations (`bl3361_init`,
`bl3361_open`, `bl3361_set_chan`, `bl3361_get_rssi`,
`bl3361_set_rssi_threshold` and so on), and its log strings name the
quantities used above (`basefreq`, `step`, `channels`). Following the code
from these strings gave the frame layout, the checksum, the command numbers,
the order of the steps and the tuning rules. The register values are data read
from the image.

Per the project's rules, no code was copied: the component re-implements the
behaviour from the description above, with the logic folded together and
rewritten. The host tests in `components/bl3361/test` check it against golden
frames and tuning blobs that were derived independently, so a wrong table
index or checksum would show up.

## What is not confirmed

Treat the following as open until somebody has tested it on a device:

- The **idle level and polarity** of the data pin. If a transmitter is in
  range but nothing is received, set `inverted: true` on the `data_pin`.
- Whether the module needs **longer settling times** than the 10 ms the
  component waits after a mode change.
- The **scale of the signal strength**. It is a raw unsigned byte, assumed to
  grow with the signal.
- The **timing accuracy** of transmitting. It is software-timed.
- The exact **meaning of some profile values** (`[9, 0, 90]` in the `rf`
  section of the hardware profile). Only the data pin is used.

Findings, logic-analyzer captures of the UART while the stock firmware learns
a code and RTL-SDR recordings of what the RM4 Pro transmits are very welcome in
an [issue](https://github.com/seb5594/ESPHome-Broadlink-RM4-Remote-Firmware/issues).
