# Receiver protocol

How the Redragon M916-PRO-1K and M901P-KS talk to their configuration software. Everything here was
worked out from the official Windows apps (M916-PRO-1K v1.0.0.4: .NET with a native `HIDUsb.dll`
embedded through Costura; M901P-KS: MFC `OemDrv.exe` + `Lowerdev.dll`), confirmed with Frida traces
of the apps and then by sending the commands to the mice directly.

The receivers are made by CX and Compx and are used by other brands too, so other mice may speak the
same protocol.

## Devices

| Mouse | USB ID | Chips | Requests | Replies |
|-------|--------|-------|----------|---------|
| M916-PRO-1K | `3554:F5D5` receiver (the vendor app only lists `F55D`, `F55F`; `F55E` is the cable) | CX receiver, PAW3395 sensor, CH32V305 MCU | output report 8, interface 1, usage page `0xFF02` | input report 8, same collection |
| M901P-KS | `25A7:FA07` receiver, `25A7:FA08` cable ("2.4G Dual Mode Mouse") | Compx receiver, PAW3335 sensor | feature report 8, interface 1, usage page `0xFF02` | input report **9**, usage page `0xFF01`, same interface |

On Linux both collections are on the same `/dev/hidrawN` node (one node per USB interface). opendragon
picks the node whose report descriptor contains `Usage Page 0xFF02`.

## Packet

Requests and replies are 17 bytes:

```
[0]      report id    0x08 (replies: 0x08 on CX, 0x09 on Compx)
[1]      command
[2]      status       0 = ok, 1 = unsupported / error
[3..4]   address      big endian, flash commands only
[5]      length       payload bytes in use (max 10)
[6..15]  payload
[16]     checksum     (0x55 - sum(bytes[0..15])) & 0xFF
```

## Commands

| ID | Name | Answered by | Notes |
|----|------|-------------|-------|
| `0x01` | encryption | receiver | 4 random bytes, length 8; 6-byte answer |
| `0x02` | PC driver status | receiver | `01` = vendor app running |
| `0x03` | device online | receiver | `data[0] = 1` when the mouse is awake and linked |
| `0x04` | battery | mouse | `level, charging, mV_hi, mV_lo` (see below) |
| `0x05` / `0x06` | enter pairing / pairing state | receiver | |
| `0x07` | **write flash** | mouse | address + up to 10 bytes |
| `0x08` | **read flash** | mouse | address + length (up to 10) |
| `0x09` | factory reset | mouse | |
| `0x0A` | status changed | event | |
| `0x0E` / `0x0F` | get / set profile | mouse | M916: profile 0 |
| `0x10` | read CID/MID | mouse | M916: `17 05 39` (CID 23, MID 5) |
| `0x12` | firmware version | mouse | M916: `02 00` |
| `0x14` / `0x15` | set / get receiver RGB | receiver | not on these receivers |
| `0x16` / `0x17` | set / get long-range mode | mouse | |

The M901P-KS firmware answers `status = 1` to `0x0E`, `0x10`, `0x12`, `0x15` and `0x17`.

When the mouse is asleep only the receiver answers (`0x01`–`0x03`). Everything else times out until
the mouse is moved.

### Battery

* M916-PRO-1K: `64 00 10 6E` = level 100, not charging, 4206 mV. The reply says length 2 but the
  voltage is there anyway. The vendor app maps the voltage to a percentage with a 21-point curve
  (0 % to 100 % in 5 % steps: 3050, 3420, 3480 … 4110 mV).
* M901P-KS: `07 00` = 70 %. The level is 0–10 and there is no voltage. Confirmed by feeding fake
  values to the vendor app.

## Flash layout

Most values are followed by their own check byte, `0x55 - sum(value bytes)`.

| Address | Content |
|---------|---------|
| `0x00` | polling rate (`0x01` 1000 Hz, `0x02` 500, `0x04` 250, `0x08` 125, `0x10` 2000, `0x20` 4000) |
| `0x02` | number of DPI levels |
| `0x04` | active DPI level (index) |
| `0x06`, `0x08`, `0x0A` | X spindown, Y spindown, lift-off distance |
| `0x0C + 4i` (i < 8) | DPI level i: `x, y, ext, chk` |
| `0x2C + 4i` (i < 8) | indicator color for level i: `r, g, b, chk` |
| `0x4C`…`0x53` | DPI indicator: mode, brightness, speed, enabled (each + chk) |
| `0x60 + 4i` (i < 16) | button i: `type, p1, p2, chk` |
| `0xA0` | lighting: `mode, r, g, b, speed, brightness, chk`; `0xA7` enabled + chk |
| `0xA9`…`0xBA` | M916 only: debounce, motion sync, lights-off time, angle snapping, ripple control, lights off while moving, custom sleep, sleep time, power saving (each + chk) |
| `0x100 + 0x20i` | keyboard shortcut for button i |
| `0x300 + 0x180i` | macro for button i |

### DPI values

* PAW3395 (M916): `v = dpi / 50 - 1`, 10 bits. `x = y = v & 0xFF`, and the two high bits go to
  `ext` as `(hi << 2) | (hi << 6)`. 26000 DPI → `07 07 88`.
* PAW3335 (M901): a non-linear table from the vendor's `driver_sensor.h` (1000 → `0x0B`,
  2000 → `0x17`, 16000 → `0xBD`), `ext = 0`. The full table is in `src/models.cpp`.

### Button functions

| type | function | p1, p2 |
|------|----------|--------|
| 0 | disabled | |
| 1 | mouse button | mask: 1 left, 2 right, 4 middle, 8 back, 0x10 forward |
| 2 | DPI | 1 cycle, 2 up, 3 down |
| 3 | horizontal scroll | 1 left, 2 right |
| 4 | rapid fire | speed, count |
| 5 | keyboard shortcut | stored at `0x100 + 0x20i` |
| 6 | macro | p1 = slot (same as the button index), p2 = repeat count |
| 7 | cycle polling rate | |
| 8 | toggle lighting | |
| 9 | cycle profile | |
| 10 | DPI lock | p1 = DPI value |

### Events (shortcuts and macros)

Each event is `flags, code_lo, code_hi`. `flags = 0x80 (press) | 0x40 (release) | type`, where type
is 0 modifier (mask: 1 LCtrl, 2 LShift, 4 LAlt, 8 LGUI, 0x10 RCtrl, 0x20 RShift, 0x40 RAlt, 0x80 RGUI),
1 keyboard usage, 2 consumer usage (16 bits), 3 power, 4 mouse button, 5 move XY.

**Shortcut** (`0x100 + 0x20i`): `count, count × event, chk`, with `chk = 0x55 - sum(bytes[0 .. 3·count])`.
The vendor app presses the keys in order and releases them in reverse, so up to 3 keys fit.
Example from the M901: `02 81 1E 00 41 1E 00 55` types `1`.

**Macro** (`0x300 + 0x180i`):

```
[0x00]        name length
[0x01..0x1E]  name
[0x1F]        event count n (max 70)
[0x20 + 5k]   event k: flags, code_lo, code_hi, delay_hi, delay_lo   (delay in ms after the event)
[0x20 + 5n]   chk = 0x55 - sum(bytes[0x1F .. 0x20 + 5n))
```

A macro has to be written in one go, in order, from the start of its slot (`0x21 + 5n` bytes in
10-byte chunks). Writes elsewhere in the slot leave it corrupted, and writing `0xFF` doesn't erase
anything. The vendor app never erases slots either: unbinding a macro only changes the button type.
Both mice use this format.

## Behaviour notes

* Writes take effect immediately; no reconnect or power cycle needed.
* The mouse keeps a copy of its settings in RAM and writes it back to flash when you use its own
  buttons. On the M901, the DPI button rewrites `0x04`, and the button on the bottom (which cycles
  the polling rate and blinks the LEDs) rewrites `0x00`, the lighting block and `0xA9`.
* The M901P-KS's lighting ignores this block. Writes to `0xA0`, `0xA7` and `0x4C` are stored but the
  LEDs don't change, over the receiver or the cable, before or after a power cycle. The vendor app
  sends exactly the same writes and fails the same way.
* The M916-PRO-1K vendor app hides lighting settings entirely (`DisplayLight=0`).
