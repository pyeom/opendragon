// In-memory copy of the mouse flash with typed accessors (layout in docs/PROTOCOL.md).
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "keycodes.hpp"
#include "models.hpp"

namespace od {

namespace addr {
constexpr uint16_t kRate = 0x00, kDpiCount = 0x02, kDpiCurrent = 0x04, kDpi = 0x0C, kDpiColor = 0x2C;
constexpr uint16_t kDpiLedMode = 0x4C, kDpiLedBrightness = 0x4E, kDpiLedSpeed = 0x50, kDpiLedEnable = 0x52;
constexpr uint16_t kKeys = 0x60, kLedBar = 0xA0, kLedBarEnable = 0xA7;
constexpr uint16_t kDebounce = 0xA9, kMotionSync = 0xAB, kLedOffTime = 0xAD, kLinearCorrection = 0xAF,
                   kRipple = 0xB1, kMoveOffLed = 0xB3, kSleepCustom = 0xB5, kSleepTime = 0xB7, kPowerSaving = 0xB9;
constexpr uint16_t kShortcuts = 0x100, kShortcutSize = 0x20;
constexpr uint16_t kMacros = 0x300, kMacroSize = 0x180;
constexpr uint16_t kSettingsEnd = 0x300;  // config + shortcuts
constexpr uint16_t kFlashEnd = 0x1B00;    // + 16 macros
}  // namespace addr

constexpr int kButtons = 16;
constexpr int kDpiSlots = 8;
constexpr int kMaxMacroEvents = 70;
constexpr int kMaxShortcutEvents = 6;

enum KeyFnType : uint8_t {
    kFnDisabled = 0, kFnMouse = 1, kFnDpi = 2, kFnHScroll = 3, kFnFire = 4, kFnShortcut = 5,
    kFnMacro = 6, kFnReportRate = 7, kFnLight = 8, kFnProfile = 9, kFnSniper = 10,
};

struct KeyFn {
    uint8_t type = 0, p1 = 0, p2 = 0;
};

struct Event {
    uint8_t state = 0;  // 0 press, 1 release, 2 neither (move)
    EvType type = kEvKey;
    uint16_t code = 0;
    uint16_t delay_ms = 0;  // delay after this event
};

struct Macro {
    std::string name;
    std::vector<Event> events;
};

class FlashImage {
public:
    explicit FlashImage(std::vector<uint8_t> bytes) : b_(std::move(bytes)) {}
    const std::vector<uint8_t>& bytes() const { return b_; }
    size_t size() const { return b_.size(); }
    uint8_t at(size_t a) const { return a < b_.size() ? b_[a] : 0xFF; }

    // value bytes followed by a (0x55 - sum) checksum byte
    std::optional<std::vector<uint8_t>> field(uint16_t a, size_t n) const;
    std::optional<uint8_t> byte(uint16_t a) const;
    void set_field(uint16_t a, const std::vector<uint8_t>& v);
    void patch(uint16_t a, const std::vector<uint8_t>& v);

    KeyFn key(int i) const;
    void set_key(int i, KeyFn k);

    std::vector<Event> shortcut(int i) const;
    void set_shortcut(int i, const std::vector<Event>& ev);

    // needs macro bytes present in the image (size >= kFlashEnd or loaded on demand)
    std::optional<Macro> macro(int i) const;
    void set_macro(int i, const Macro& m);

private:
    void ensure(size_t n);
    std::vector<uint8_t> b_;
};

// Returns changed [start, end) ranges between two images, merging nearby edits.
std::vector<std::pair<uint16_t, uint16_t>> diff_ranges(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b);

std::string describe_key(const FlashImage& img, int i);
std::string rate_name(uint8_t r);
std::optional<uint8_t> rate_code(int hz);

}  // namespace od
