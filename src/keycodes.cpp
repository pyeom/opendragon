#include "keycodes.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace od {
namespace {

struct Named {
    const char* name;
    EvType type;
    uint16_t code;
};

const std::vector<Named>& table() {
    static std::vector<Named> t = [] {
        std::vector<Named> v = {
            {"enter", kEvKey, 0x28}, {"esc", kEvKey, 0x29}, {"backspace", kEvKey, 0x2A}, {"tab", kEvKey, 0x2B},
            {"space", kEvKey, 0x2C}, {"minus", kEvKey, 0x2D}, {"equal", kEvKey, 0x2E}, {"lbracket", kEvKey, 0x2F},
            {"rbracket", kEvKey, 0x30}, {"backslash", kEvKey, 0x31}, {"semicolon", kEvKey, 0x33},
            {"apostrophe", kEvKey, 0x34}, {"grave", kEvKey, 0x35}, {"comma", kEvKey, 0x36}, {"dot", kEvKey, 0x37},
            {"slash", kEvKey, 0x38}, {"capslock", kEvKey, 0x39}, {"printscreen", kEvKey, 0x46},
            {"scrolllock", kEvKey, 0x47}, {"pause", kEvKey, 0x48}, {"insert", kEvKey, 0x49}, {"home", kEvKey, 0x4A},
            {"pageup", kEvKey, 0x4B}, {"delete", kEvKey, 0x4C}, {"end", kEvKey, 0x4D}, {"pagedown", kEvKey, 0x4E},
            {"right", kEvKey, 0x4F}, {"left", kEvKey, 0x50}, {"down", kEvKey, 0x51}, {"up", kEvKey, 0x52},
            {"numlock", kEvKey, 0x53}, {"kp_divide", kEvKey, 0x54}, {"kp_multiply", kEvKey, 0x55},
            {"kp_minus", kEvKey, 0x56}, {"kp_plus", kEvKey, 0x57}, {"kp_enter", kEvKey, 0x58},
            {"kp_dot", kEvKey, 0x63}, {"menu", kEvKey, 0x65},
            // modifiers (bitmask)
            {"ctrl", kEvModifier, 0x01}, {"lctrl", kEvModifier, 0x01}, {"shift", kEvModifier, 0x02},
            {"lshift", kEvModifier, 0x02}, {"alt", kEvModifier, 0x04}, {"lalt", kEvModifier, 0x04},
            {"super", kEvModifier, 0x08}, {"lsuper", kEvModifier, 0x08}, {"win", kEvModifier, 0x08},
            {"meta", kEvModifier, 0x08}, {"rctrl", kEvModifier, 0x10}, {"rshift", kEvModifier, 0x20},
            {"ralt", kEvModifier, 0x40}, {"altgr", kEvModifier, 0x40}, {"rsuper", kEvModifier, 0x80},
            // consumer page
            {"volup", kEvMedia, 0xE9}, {"voldown", kEvMedia, 0xEA}, {"mute", kEvMedia, 0xE2},
            {"play", kEvMedia, 0xCD}, {"next", kEvMedia, 0xB5}, {"prev", kEvMedia, 0xB6}, {"stop", kEvMedia, 0xB7},
            {"calc", kEvMedia, 0x192}, {"mail", kEvMedia, 0x18A}, {"mediaplayer", kEvMedia, 0x183},
            {"computer", kEvMedia, 0x194}, {"browser", kEvMedia, 0x223}, {"search", kEvMedia, 0x221},
            {"browser_back", kEvMedia, 0x224}, {"browser_forward", kEvMedia, 0x225},
            {"browser_refresh", kEvMedia, 0x227}, {"browser_stop", kEvMedia, 0x226},
            {"favorites", kEvMedia, 0x22A},
            // mouse buttons inside macros
            {"mouse_left", kEvMouse, 0x01}, {"mouse_right", kEvMouse, 0x02}, {"mouse_middle", kEvMouse, 0x04},
            {"mouse_back", kEvMouse, 0x08}, {"mouse_forward", kEvMouse, 0x10},
        };
        static std::vector<std::string> owned;  // storage for generated names
        owned.reserve(80);
        for (int i = 0; i < 26; ++i) owned.push_back(std::string(1, char('a' + i)));
        for (int i = 1; i <= 9; ++i) owned.push_back(std::to_string(i));
        owned.push_back("0");
        for (int i = 1; i <= 24; ++i) owned.push_back("f" + std::to_string(i));
        for (int i = 1; i <= 9; ++i) owned.push_back("kp" + std::to_string(i));
        owned.push_back("kp0");
        size_t k = 0;
        for (int i = 0; i < 26; ++i) v.push_back({owned[k++].c_str(), kEvKey, uint16_t(0x04 + i)});
        for (int i = 0; i < 10; ++i) v.push_back({owned[k++].c_str(), kEvKey, uint16_t(0x1E + i)});
        for (int i = 0; i < 24; ++i)
            v.push_back({owned[k++].c_str(), kEvKey, uint16_t(i < 12 ? 0x3A + i : 0x68 + (i - 12))});
        for (int i = 0; i < 10; ++i) v.push_back({owned[k++].c_str(), kEvKey, uint16_t(0x59 + i)});
        return v;
    }();
    return t;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

}  // namespace

std::optional<KeyCode> parse_key(const std::string& raw) {
    std::string n = lower(raw);
    for (auto& e : table())
        if (n == e.name) return KeyCode{e.type, e.code};
    auto colon = n.find(':');
    if (colon != std::string::npos) {
        std::string kind = n.substr(0, colon), val = n.substr(colon + 1);
        char* end = nullptr;
        unsigned long c = std::strtoul(val.c_str(), &end, 0);
        if (!val.empty() && *end == '\0' && c <= 0xFFFF) {
            if (kind == "key") return KeyCode{kEvKey, uint16_t(c)};
            if (kind == "media") return KeyCode{kEvMedia, uint16_t(c)};
            if (kind == "mod") return KeyCode{kEvModifier, uint16_t(c)};
        }
    }
    return std::nullopt;
}

std::string key_name(EvType type, uint16_t code) {
    for (auto& e : table())
        if (e.type == type && e.code == code) return e.name;
    char buf[32];
    const char* kind = type == kEvMedia ? "media" : type == kEvModifier ? "mod" : type == kEvMouse ? "mouse" : "key";
    std::snprintf(buf, sizeof buf, "%s:0x%x", kind, code);
    return buf;
}

}  // namespace od
