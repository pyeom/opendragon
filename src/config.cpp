#include "config.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

#include "protocol.hpp"

namespace od {

void FlashImage::ensure(size_t n) {
    if (b_.size() < n) b_.resize(n, 0xFF);
}

std::optional<std::vector<uint8_t>> FlashImage::field(uint16_t a, size_t n) const {
    if (size_t(a) + n + 1 > b_.size()) return std::nullopt;
    if (checksum(&b_[a], n) != b_[a + n]) return std::nullopt;
    return std::vector<uint8_t>(b_.begin() + a, b_.begin() + a + n);
}

std::optional<uint8_t> FlashImage::byte(uint16_t a) const {
    auto f = field(a, 1);
    if (!f) return std::nullopt;
    return (*f)[0];
}

void FlashImage::set_field(uint16_t a, const std::vector<uint8_t>& v) {
    ensure(a + v.size() + 1);
    std::copy(v.begin(), v.end(), b_.begin() + a);
    b_[a + v.size()] = checksum(v.data(), v.size());
}

void FlashImage::patch(uint16_t a, const std::vector<uint8_t>& v) {
    ensure(a + v.size());
    std::copy(v.begin(), v.end(), b_.begin() + a);
}

KeyFn FlashImage::key(int i) const {
    auto f = field(uint16_t(addr::kKeys + 4 * i), 3);
    if (!f) return {0xFF, 0xFF, 0xFF};
    return {(*f)[0], (*f)[1], (*f)[2]};
}

void FlashImage::set_key(int i, KeyFn k) { set_field(uint16_t(addr::kKeys + 4 * i), {k.type, k.p1, k.p2}); }

static uint8_t pack_flags(const Event& e) {
    uint8_t f = uint8_t(e.type) & 0x0F;
    if (e.state == 0) f |= 0x80;
    else if (e.state == 1) f |= 0x40;
    return f;
}

static Event unpack(uint8_t flags, uint8_t lo, uint8_t hi) {
    Event e;
    e.type = EvType(flags & 0x0F);
    e.state = (flags & 0x80) ? 0 : (flags & 0x40) ? 1 : 2;
    e.code = uint16_t(lo | (hi << 8));
    return e;
}

std::vector<Event> FlashImage::shortcut(int i) const {
    const size_t a = addr::kShortcuts + addr::kShortcutSize * size_t(i);
    std::vector<Event> out;
    if (a + addr::kShortcutSize > b_.size()) return out;
    uint8_t n = b_[a];
    if (n == 0 || n > kMaxShortcutEvents) return out;
    if (checksum(&b_[a], 1 + 3 * n) != b_[a + 1 + 3 * n]) return out;
    for (int k = 0; k < n; ++k) out.push_back(unpack(b_[a + 1 + 3 * k], b_[a + 2 + 3 * k], b_[a + 3 + 3 * k]));
    return out;
}

void FlashImage::set_shortcut(int i, const std::vector<Event>& ev) {
    if (ev.size() > kMaxShortcutEvents) throw std::invalid_argument("a shortcut takes at most 3 keys");
    const size_t a = addr::kShortcuts + addr::kShortcutSize * size_t(i);
    ensure(a + addr::kShortcutSize);
    std::fill(b_.begin() + a, b_.begin() + a + addr::kShortcutSize, 0xFF);
    b_[a] = uint8_t(ev.size());
    for (size_t k = 0; k < ev.size(); ++k) {
        b_[a + 1 + 3 * k] = pack_flags(ev[k]);
        b_[a + 2 + 3 * k] = uint8_t(ev[k].code);
        b_[a + 3 + 3 * k] = uint8_t(ev[k].code >> 8);
    }
    b_[a + 1 + 3 * ev.size()] = checksum(&b_[a], 1 + 3 * ev.size());
}

std::optional<Macro> FlashImage::macro(int i) const {
    const size_t a = addr::kMacros + addr::kMacroSize * size_t(i);
    if (a + 0x20 > b_.size()) return std::nullopt;
    uint8_t n = b_[a + 0x1F];
    if (n == 0 || n > kMaxMacroEvents || a + 0x21 + 5 * n > b_.size()) return std::nullopt;
    if (checksum(&b_[a + 0x1F], 1 + 5 * n) != b_[a + 0x20 + 5 * n]) return std::nullopt;
    Macro m;
    uint8_t len = std::min<uint8_t>(b_[a], 30);
    m.name.assign(reinterpret_cast<const char*>(&b_[a + 1]), len);
    for (int k = 0; k < n; ++k) {
        size_t e = a + 0x20 + 5 * k;
        Event ev = unpack(b_[e], b_[e + 1], b_[e + 2]);
        ev.delay_ms = uint16_t((b_[e + 3] << 8) | b_[e + 4]);
        m.events.push_back(ev);
    }
    return m;
}

void FlashImage::set_macro(int i, const Macro& m) {
    if (m.events.empty() || m.events.size() > kMaxMacroEvents)
        throw std::invalid_argument("a macro needs 1 to 70 events");
    const size_t a = addr::kMacros + addr::kMacroSize * size_t(i);
    ensure(a + addr::kMacroSize);
    std::fill(b_.begin() + a, b_.begin() + a + addr::kMacroSize, 0xFF);
    std::string name = m.name.substr(0, 30);
    b_[a] = uint8_t(name.size());
    std::fill(b_.begin() + a + 1, b_.begin() + a + 31, 0x00);
    std::copy(name.begin(), name.end(), b_.begin() + a + 1);
    b_[a + 0x1F] = uint8_t(m.events.size());
    for (size_t k = 0; k < m.events.size(); ++k) {
        size_t e = a + 0x20 + 5 * k;
        b_[e] = pack_flags(m.events[k]);
        b_[e + 1] = uint8_t(m.events[k].code);
        b_[e + 2] = uint8_t(m.events[k].code >> 8);
        b_[e + 3] = uint8_t(m.events[k].delay_ms >> 8);
        b_[e + 4] = uint8_t(m.events[k].delay_ms);
    }
    b_[a + 0x20 + 5 * m.events.size()] = checksum(&b_[a + 0x1F], 1 + 5 * m.events.size());
}

std::vector<std::pair<uint16_t, uint16_t>> diff_ranges(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    std::vector<std::pair<uint16_t, uint16_t>> out;
    size_t n = std::max(a.size(), b.size());
    auto get = [](const std::vector<uint8_t>& v, size_t i) { return i < v.size() ? v[i] : 0xFF; };
    for (size_t i = 0; i < n; ++i) {
        if (get(a, i) == get(b, i)) continue;
        if (!out.empty() && i - out.back().second <= 3) out.back().second = uint16_t(i + 1);
        else out.emplace_back(uint16_t(i), uint16_t(i + 1));
    }
    return out;
}

std::string rate_name(uint8_t r) {
    switch (r) {
        case 0x01: return "1000 Hz";
        case 0x02: return "500 Hz";
        case 0x04: return "250 Hz";
        case 0x08: return "125 Hz";
        case 0x10: return "2000 Hz";
        case 0x20: return "4000 Hz";
        default: return "unknown";
    }
}

std::optional<uint8_t> rate_code(int hz) {
    switch (hz) {
        case 125: return 0x08;
        case 250: return 0x04;
        case 500: return 0x02;
        case 1000: return 0x01;
        case 2000: return 0x10;
        case 4000: return 0x20;
        default: return std::nullopt;
    }
}

static std::string describe_events(const std::vector<Event>& ev) {
    // render press events only: "ctrl+shift+a"
    std::string s;
    for (auto& e : ev) {
        if (e.state != 0) continue;
        if (!s.empty()) s += "+";
        if (e.type == kEvModifier) {
            std::string m;
            for (int bit = 0; bit < 8; ++bit)
                if (e.code & (1 << bit)) m += (m.empty() ? "" : "+") + key_name(kEvModifier, uint16_t(1 << bit));
            s += m;
        } else {
            s += key_name(e.type, e.code);
        }
    }
    return s.empty() ? "(empty)" : s;
}

std::string describe_key(const FlashImage& img, int i) {
    KeyFn k = img.key(i);
    char buf[96];
    switch (k.type) {
        case kFnDisabled: return "disabled";
        case kFnMouse:
            switch (k.p1) {
                case 0x01: return "left";
                case 0x02: return "right";
                case 0x04: return "middle";
                case 0x08: return "back";
                case 0x10: return "forward";
            }
            std::snprintf(buf, sizeof buf, "mouse:0x%02x", k.p1);
            return buf;
        case kFnDpi: return k.p1 == 1 ? "dpi-cycle" : k.p1 == 2 ? "dpi-up" : k.p1 == 3 ? "dpi-down" : "dpi:?";
        case kFnHScroll: return k.p1 == 1 ? "scroll-left" : "scroll-right";
        case kFnFire:
            std::snprintf(buf, sizeof buf, "fire:%d:%d", k.p1, k.p2);
            return buf;
        case kFnShortcut: return "key:" + describe_events(img.shortcut(i));
        case kFnMacro: {
            auto m = img.macro(k.p1);
            std::snprintf(buf, sizeof buf, "macro #%d x%d", k.p1, k.p2);
            return std::string(buf) + (m ? " \"" + m->name + "\" (" + std::to_string(m->events.size()) + " events)"
                                         : "");
        }
        case kFnReportRate: return "rate-cycle";
        case kFnLight: return "light-toggle";
        case kFnProfile: return "profile-cycle";
        case kFnSniper:
            std::snprintf(buf, sizeof buf, "sniper:raw=0x%02x", k.p1);
            return buf;
        case 0xFF: return "(bad checksum)";
    }
    std::snprintf(buf, sizeof buf, "unknown(%d,%d,%d)", k.type, k.p1, k.p2);
    return buf;
}

}  // namespace od
