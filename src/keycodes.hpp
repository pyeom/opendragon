// Names <-> HID codes used by shortcuts and macros.
#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace od {

// Event "type" nibble as stored in flash (HID_CODE_TYPE in the vendor app)
enum EvType : uint8_t { kEvModifier = 0, kEvKey = 1, kEvMedia = 2, kEvPower = 3, kEvMouse = 4, kEvMoveXY = 5 };

struct KeyCode {
    EvType type;
    uint16_t code;  // modifier bitmask, keyboard usage, consumer usage or mouse button mask
};

// Accepts: a..z, 0..9, f1..f24, enter, esc, space, ..., ctrl/shift/alt/super (+ l/r variants),
// media keys (volup, voldown, mute, play, next, prev, stop, calc, mail, browser, ...),
// mouse buttons (mouse_left, mouse_right, mouse_middle, mouse_back, mouse_forward),
// or raw codes: key:0x2c, media:0xe9.
std::optional<KeyCode> parse_key(const std::string& name);
std::string key_name(EvType type, uint16_t code);

}  // namespace od
