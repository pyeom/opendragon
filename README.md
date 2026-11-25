<p align="center">
  <img src="assets/icon.svg" width="96" height="96" alt="">
</p>

<h1 align="center">opendragon</h1>

<p align="center">
  Battery, DPI, button mapping and macros for Redragon wireless mice on Linux.<br>
  No vendor software, no Wine, no dependencies.
</p>

<p align="center">
  <a href="https://github.com/pyeom/opendragon/actions/workflows/ci.yml"><img src="https://github.com/pyeom/opendragon/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="MIT license"></a>
</p>

---

Redragon only ships configuration software for Windows. Under Linux these mice still move and click
fine, but you can't see the battery level or change DPI, buttons or macros. opendragon talks to the
2.4 GHz receiver directly and writes the same settings the official app does. Settings live in the
mouse, so they stay when you plug it into another computer.

```console
$ opendragon battery
Redragon M916-PRO-1K: 100%, 4188 mV
Redragon M901P-KS: 70%

$ opendragon -d m916 show
Redragon M916-PRO-1K
  polling rate:  1000 Hz
  DPI levels:    5 (active: 1)
    * 1:   1000  #ff0000
      2:   2000  #0000ff
      3:   4000  #00ff00
      4:   8000  #ff00ff
      5:  26000  #ffff00
  debounce:      8 ms
  motion sync:   on
  angle snap:    off
  ripple ctrl:   off
  buttons:
     1: left
     2: right
     3: middle
     4: back
     5: forward
     6: dpi-cycle
     ...
```

## Supported mice

| Mouse | Receiver | Battery | DPI / rate | Buttons | Macros |
|-------|----------|:-------:|:----------:|:-------:|:------:|
| Redragon M916-PRO-1K | `3554:f5d5` | yes, with voltage | yes | yes | yes |
| Redragon M901P-KS | `25a7:fa07` (cable: `25a7:fa08`) | yes | yes | yes | yes |

Both were tested on real hardware. Lighting effects can't be changed: the M901P-KS ignores the
lighting settings, even when they're written by the official app, and the M916-PRO-1K app has no
lighting options at all.

The receivers come from two OEMs (CX and Compx) that supply other brands too, so other mice may well
use the same protocol. If yours has one of these vendor IDs, see [Adding a mouse](#adding-a-mouse).

## Install

**Arch / CachyOS / Manjaro**

```sh
git clone https://github.com/pyeom/opendragon
cd opendragon/packaging/arch
makepkg -si
```

**Other distributions** (needs CMake and a C++17 compiler)

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
```

Both install a udev rule that gives the logged-in user access to the receivers. Reload the rules
and replug the receiver once:

```sh
sudo udevadm control --reload && sudo udevadm trigger
```

## Usage

```sh
opendragon list                       # receivers found, and whether each mouse is awake
opendragon battery                    # all connected mice
opendragon info                       # firmware version and battery
opendragon show                       # everything stored in the mouse

opendragon rate 1000                  # polling rate: 125 250 500 1000
opendragon dpi 800,1600,3200          # up to 8 levels
opendragon dpi-current 2
opendragon dpi-color 1 ff0000         # DPI indicator color per level

opendragon bind 4 back                # remap buttons (numbers as listed by 'show')
opendragon bind 7 key:ctrl+c          # keyboard shortcut, up to 3 keys
opendragon bind 8 key:volup           # media keys
opendragon bind 9 macro:combo.txt     # macro from a text file
opendragon bind 5 fire:20:3           # rapid fire

opendragon backup                     # full flash dump to ~/.local/state/opendragon/
opendragon restore dump.bin
```

With more than one mouse connected, pick one with `-d m916`, `-d m901` or `-d 2` (the number from
`list`). The mouse must be awake: if a command says it's asleep, move it and try again.

Macros are plain text files; the format is in [docs/MACROS.md](docs/MACROS.md):

```text
name Grenade
down shift
tap g 40
up shift
```

### Waybar

```jsonc
"custom/mouse": {
  "exec": "opendragon battery --waybar",
  "return-type": "json",
  "interval": 120
}
```

Shows `M916 100%  M901 70%` with details in the tooltip. The `warning` and `critical` classes kick
in at 30% and 15%. `opendragon battery --json` gives machine-readable output for other bars.

## Safety

Every command that writes:

- saves the current settings to `~/.local/state/opendragon/` first,
- writes only the bytes that change,
- reads them back and stops if anything doesn't match.

`opendragon restore` puts a saved file back.

## How it works

The receivers expose a vendor HID collection that takes 17-byte commands to read and write a 7 KB
settings area in the mouse's flash. The protocol, the flash layout and the quirks found along the way
are documented in [docs/PROTOCOL.md](docs/PROTOCOL.md). It was worked out by decompiling the official
apps and tracing them with Frida. The scripts used for that are in [tools/re](tools/re).

## Adding a mouse

1. Check whether the receiver has a vendor collection on usage page `0xFF02`:
   `python tools/re/probe.py list` (needs `pip install hidapi`).
2. Ask it whether the mouse is online: `python tools/re/probe.py <vid> 03`. A 17-byte reply means
   it speaks this protocol.
3. Add the model to `src/models.cpp`: USB IDs, request style (output or feature reports), sensor
   DPI encoding and battery format.
4. Check `opendragon show` against what the official app displays, then open a pull request.

`docs/dumps/` has full flash dumps of both supported mice; the unit tests run against them
(`ctest --test-dir build`).

## Disclaimer

Not affiliated with or endorsed by Redragon. Writing to your mouse's flash is at your own risk,
though every write is backed up and verified.

## License

[MIT](LICENSE)
