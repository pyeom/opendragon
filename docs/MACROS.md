# Macros

Macros are stored in the mouse, so they keep working on any computer without software. Each button
has its own slot with room for 70 events. An event is a key or button press or release, followed by
an optional delay in milliseconds.

## File format

```text
# comments start with #
name Grenade combo      # optional, max 30 characters (defaults to the file name)
down shift              # press
tap g 40                # press and release g, then wait 40 ms
delay 100               # add 100 ms to the previous event's delay
up shift 10             # release, then wait 10 ms
click left 50           # mouse button: left, right, middle, back, forward
type hello world        # letters, digits and spaces (uppercase letters cost 4 events)
```

Bind it to button 4:

```sh
opendragon bind 4 macro:grenade.txt
opendragon bind 4 macro:grenade.txt:3     # repeat 3 times per press
opendragon macro-show 4                   # read it back from the mouse
```

## Shortcuts

Combinations of up to 3 keys don't need a file:

```sh
opendragon bind 6 key:ctrl+shift+m
opendragon bind 7 key:volup
opendragon bind 8 key:f13      # then in Hyprland: bind = , F13, exec, ...
```

## Key names

| Group | Names |
|-------|-------|
| Letters, digits | `a`…`z`, `0`…`9` |
| Function keys | `f1`…`f24` |
| Editing | `enter esc backspace tab space insert delete home end pageup pagedown` |
| Arrows | `up down left right` |
| Symbols | `minus equal lbracket rbracket backslash semicolon apostrophe grave comma dot slash` |
| Locks, misc | `capslock numlock scrolllock printscreen pause menu` |
| Keypad | `kp0`…`kp9 kp_plus kp_minus kp_multiply kp_divide kp_enter kp_dot` |
| Modifiers | `ctrl shift alt super` and `lctrl rctrl lshift rshift lalt ralt altgr lsuper rsuper` |
| Media | `volup voldown mute play next prev stop` |
| Apps | `calc mail mediaplayer computer browser search favorites` |
| Browser | `browser_back browser_forward browser_refresh browser_stop` |
| Mouse (macros) | `mouse_left mouse_right mouse_middle mouse_back mouse_forward` |
| Raw codes | `key:0x2c` (keyboard usage), `media:0xe9` (consumer usage), `mod:0x05` (modifier mask) |

Key names follow the US layout: `semicolon` sends the key that is `;` on a US keyboard, whatever your
layout prints for it.
