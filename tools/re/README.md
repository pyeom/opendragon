# Reverse-engineering tools

Scripts used to work out the protocol from the official Windows software. They are not needed to
use opendragon; they are here so the work can be repeated for other mice.

Requirements: Windows, Python 3, `pip install frida-tools hidapi pefile capstone`.

| File | What it does |
|------|--------------|
| `probe.py` | Send single protocol commands to a receiver with hidapi. |
| `session.py` | Start a vendor app under Frida and log its traffic. |
| `hidhook.js` | Frida agent that logs WriteFile/ReadFile/HidD_* calls on HID handles. |
| `spoof.js` | Frida agent that rewrites the battery level the app receives. |
| `single.js` | Frida agent that lets a second copy of the M901P-KS app start. |
| `carve.py` | Extract PE files embedded in another binary (the M916 app embeds its native `HIDUsb.dll` via Costura). |
| `dis.py` | Disassemble an exported function, with imports and strings annotated. |
| `xref.py` | Find code references to a string. |

## Typical workflow

1. Copy the vendor app's install folder somewhere writable and rename the `.exe`. Most of these apps
   allow only one instance and the installed one often runs elevated, so Frida can't attach to it.
2. Close the installed app, then run the copy under Frida:

   ```
   python session.py "copy\OemDrv_re.exe" traffic.log hidhook.js
   ```

3. Change one setting at a time in the app and look at the `0x07` (write flash) packets in the log.

The M916-PRO-1K app is .NET and not obfuscated: `ilspycmd` gives readable C#, and `carve.py`
extracts the native `HIDUsb.dll` where the packets are built. The flash layout comes from its
`ProtocolDataParser` and `MacroKeyToBuffer` exports.

Some vendor apps keep a list of supported product IDs in a config file (`Config.ini`, with values
base64-encoded). If your receiver's PID is missing, add it to the copy's config and the app will
talk to it.
