// opendragon: configure Redragon wireless mice that use CX / Compx 2.4 GHz receivers.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#define OD_ISATTY(fd) _isatty(fd)
#else
#include <unistd.h>
#define OD_ISATTY(fd) isatty(fd)
#endif

#include "config.hpp"
#include "keycodes.hpp"
#include "protocol.hpp"

using namespace od;
namespace fs = std::filesystem;

namespace {

constexpr const char* kVersion = "0.1.0";

struct Args {
    std::string device;  // model prefix, list index, or empty
    bool yes = false;
    bool json = false, waybar = false;
    std::vector<std::string> pos;
};

[[noreturn]] void die(const std::string& m) {
    std::cerr << "error: " << m << "\n";
    std::exit(1);
}

void usage() {
    std::cout << "opendragon " << kVersion << R"( - configure Redragon wireless mice (M916-PRO-1K, M901P-KS)

usage: opendragon [-d DEVICE] COMMAND [ARGS]

  list                          receivers found and whether the mouse is awake
  info                          model, firmware, battery
  battery [--json|--waybar]     battery of every connected mouse
  show                          settings stored in the mouse
  rate HZ                       polling rate: 125, 250, 500, 1000
  dpi LIST                      DPI levels, e.g. 800,1600,3200 (up to 8)
  dpi-current N                 active DPI level (1..n)
  dpi-color N RRGGBB            indicator color for DPI level N
  bind BUTTON ACTION            remap a button (1..16, see 'show'). Actions:
      left right middle back forward disabled
      dpi-cycle dpi-up dpi-down scroll-left scroll-right rate-cycle profile-cycle
      fire:SPEED:COUNT
      key:ctrl+shift+a  key:f13  key:volup       shortcut, up to 3 keys
      macro:FILE[:REPEAT]                        see docs/MACROS.md
  macro-show BUTTON             print a button's macro in file format
  backup [FILE]                 dump the whole flash (default: ~/.local/state/opendragon)
  restore FILE [--yes]          write a dump back
  raw CMD [ADDR] [HEX|LEN]      send a raw command (debugging)

options:
  -d, --device m916|m901|N      pick a mouse when more than one is connected
)";
}

std::string hex(const std::vector<uint8_t>& v) {
    std::string s;
    char b[4];
    for (auto x : v) {
        std::snprintf(b, sizeof b, "%02x ", x);
        s += b;
    }
    if (!s.empty()) s.pop_back();
    return s;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

int to_int(const std::string& s, int base = 10) {
    char* e = nullptr;
    long v = std::strtol(s.c_str(), &e, base);
    if (s.empty() || *e) die("not a number: " + s);
    return int(v);
}

std::vector<uint8_t> parse_rgb(const std::string& s) {
    std::string h = !s.empty() && s[0] == '#' ? s.substr(1) : s;
    if (h.size() != 6) die("bad color (use RRGGBB): " + s);
    int v = to_int(h, 16);
    return {uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
}

fs::path state_dir() {
    fs::path base;
    if (const char* x = std::getenv("XDG_STATE_HOME"); x && *x) base = x;
    else if (const char* h = std::getenv("HOME")) base = fs::path(h) / ".local" / "state";
    else if (const char* u = std::getenv("LOCALAPPDATA")) base = u;
    else base = ".";
    fs::path d = base / "opendragon";
    fs::create_directories(d);
    return d;
}

std::string timestamp() {
    std::time_t t = std::time(nullptr);
    char b[32];
    std::strftime(b, sizeof b, "%Y%m%d-%H%M%S", std::localtime(&t));
    return b;
}

void progress(size_t done, size_t total) {
    static const bool tty = OD_ISATTY(2);
    if (!tty) return;
    std::fprintf(stderr, "\r  %3zu%%", done * 100 / std::max<size_t>(1, total));
    if (done >= total) std::fprintf(stderr, "\r      \r");
}

void write_file(const fs::path& p, const std::vector<uint8_t>& bytes) {
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    if (!f) die("cannot write " + p.string());
}

// ---------------------------------------------------------------- device selection

std::vector<std::unique_ptr<Mouse>> g_mice;

Mouse& pick(const Args& a) {
    if (g_mice.empty()) die("no supported receiver found (plugged in? permissions? see README)");
    if (a.device.empty()) {
        if (g_mice.size() > 1) {
            std::string names;
            for (auto& m : g_mice) names += " " + m->model().slug;
            die("several mice connected, pick one with -d:" + names);
        }
        return *g_mice[0];
    }
    for (size_t i = 0; i < g_mice.size(); ++i)
        if (a.device == std::to_string(i + 1)) return *g_mice[i];

    // The same mouse can show up twice (receiver + USB cable): prefer the one that is awake.
    std::vector<Mouse*> hits;
    for (auto& m : g_mice)
        if (m->model().slug.rfind(lower(a.device), 0) == 0) hits.push_back(m.get());
    if (hits.empty()) die("no such device: " + a.device);
    if (hits.size() > 1)
        for (Mouse* m : hits) {
            try {
                if (m->online()) return *m;
            } catch (const std::exception&) {
            }
        }
    return *hits[0];
}

void require_awake(Mouse& m) {
    if (!m.online()) die(m.model().name + " is asleep or off: move it and try again");
}

// Settings and shortcuts, plus the macros that buttons point to.
FlashImage load(Mouse& m, bool with_macros) {
    require_awake(m);
    auto bytes = m.read_flash(0, addr::kSettingsEnd, progress);
    FlashImage img(bytes);
    if (!with_macros) return img;

    std::vector<uint8_t> all = bytes;
    all.resize(addr::kFlashEnd, 0xFF);
    for (int i = 0; i < kButtons; ++i) {
        KeyFn k = img.key(i);
        if (k.type != kFnMacro || k.p1 >= kButtons) continue;
        uint16_t a = uint16_t(addr::kMacros + addr::kMacroSize * k.p1);
        auto head = m.read_flash(a, 0x20);
        std::copy(head.begin(), head.end(), all.begin() + a);
        int n = std::min<int>(head[0x1F], kMaxMacroEvents);
        if (n > 0) {
            auto body = m.read_flash(uint16_t(a + 0x20), size_t(5 * n + 1));
            std::copy(body.begin(), body.end(), all.begin() + a + 0x20);
        }
    }
    return FlashImage(all);
}

// Writes the difference between two images and reads it back.
//  - Settings and shortcuts (< 0x300) are written byte-range by byte-range.
//  - A macro must be written in one contiguous run from the start of its slot (0x21 + 5n bytes);
//    scattered writes corrupt it. Slots cannot be erased (0xFF writes are ignored), so an emptied
//    slot is left as is. This matches what the vendor software does.
// The previous settings are saved to the state directory first.
void commit(Mouse& m, const FlashImage& before, const FlashImage& after) {
    const auto& A = before.bytes();
    const auto& B = after.bytes();
    std::vector<uint8_t> settings_a(A.begin(), A.begin() + std::min<size_t>(A.size(), addr::kSettingsEnd));
    std::vector<uint8_t> settings_b(B.begin(), B.begin() + std::min<size_t>(B.size(), addr::kSettingsEnd));
    auto writes = diff_ranges(settings_a, settings_b);

    int skipped = 0;
    for (int i = 0; i < kButtons; ++i) {
        size_t base = addr::kMacros + addr::kMacroSize * size_t(i);
        if (B.size() < base + addr::kMacroSize) break;
        bool same = A.size() >= base + addr::kMacroSize &&
                    std::equal(B.begin() + long(base), B.begin() + long(base + addr::kMacroSize), A.begin() + long(base));
        if (same) continue;
        uint8_t n = B[base + 0x1F];
        if (n == 0 || n > kMaxMacroEvents) {
            ++skipped;
            continue;
        }
        writes.emplace_back(uint16_t(base), uint16_t(base + 0x21 + 5 * n));
    }
    if (skipped) std::cerr << "note: " << skipped << " empty macro slot(s) left untouched (they can't be erased)\n";
    if (writes.empty()) {
        std::cout << "nothing to change\n";
        return;
    }

    fs::path bk = state_dir() / (m.model().slug + "-" + timestamp() + "-auto.bin");
    write_file(bk, settings_a);
    for (auto [s, e] : writes) m.write_flash(s, std::vector<uint8_t>(B.begin() + s, B.begin() + e));
    for (auto [s, e] : writes) {
        auto rb = m.read_flash(s, size_t(e - s));
        if (!std::equal(rb.begin(), rb.end(), B.begin() + s)) {
            char where[8];
            std::snprintf(where, sizeof where, "0x%04x", s);
            die(std::string("read-back mismatch at ") + where + "; previous settings saved in " + bk.string());
        }
    }
    std::cout << "saved (previous settings: " << bk.string() << ")\n";
}

// ---------------------------------------------------------------- macros

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream ss(s);
    while (std::getline(ss, cur, sep)) out.push_back(cur);
    return out;
}

KeyCode need_key(const std::string& n) {
    auto k = parse_key(n);
    if (!k) die("unknown key: " + n);
    return *k;
}

Macro parse_macro_file(const fs::path& p) {
    std::ifstream f(p);
    if (!f) die("cannot open " + p.string());
    Macro m;
    m.name = p.stem().string();
    int lineno = 0;
    for (std::string line; std::getline(f, line);) {
        ++lineno;
        if (auto h = line.find('#'); h != std::string::npos) line.erase(h);
        std::istringstream ls(line);
        std::string verb, arg, ms;
        if (!(ls >> verb)) continue;
        verb = lower(verb);
        ls >> arg >> ms;
        std::string rest;
        std::getline(ls, rest);

        auto fail = [&](const std::string& msg) { die(p.string() + ":" + std::to_string(lineno) + ": " + msg); };
        auto delay = [&] { return ms.empty() ? uint16_t(0) : uint16_t(std::clamp(to_int(ms), 0, 65535)); };
        auto push = [&](uint8_t state, KeyCode k, uint16_t d) { m.events.push_back({state, k.type, k.code, d}); };
        auto need_arg = [&] {
            if (arg.empty()) fail("missing key");
        };

        if (verb == "name") {
            m.name = arg + (ms.empty() ? "" : " " + ms) + rest;
        } else if (verb == "down" || verb == "press") {
            need_arg();
            push(0, need_key(arg), delay());
        } else if (verb == "up" || verb == "release") {
            need_arg();
            push(1, need_key(arg), delay());
        } else if (verb == "tap" || verb == "click") {
            need_arg();
            std::string k = verb == "click" && arg.rfind("mouse_", 0) != 0 ? "mouse_" + arg : arg;
            push(0, need_key(k), 20);
            push(1, need_key(k), delay());
        } else if (verb == "delay" || verb == "wait") {
            if (m.events.empty()) fail("'delay' needs an event before it");
            m.events.back().delay_ms = uint16_t(std::min(65535, m.events.back().delay_ms + to_int(arg)));
        } else if (verb == "type") {
            std::string text = arg + (ms.empty() ? "" : " " + ms) + rest;
            for (char c : text) {
                std::string k = c == ' ' ? "space" : std::string(1, char(std::tolower((unsigned char)c)));
                bool upper = std::isupper((unsigned char)c);
                if (upper) push(0, need_key("shift"), 5);
                push(0, need_key(k), 15);
                push(1, need_key(k), 15);
                if (upper) push(1, need_key("shift"), 5);
            }
        } else {
            fail("unknown command '" + verb + "' (use down, up, tap, click, delay, type, name)");
        }
    }
    if (m.events.size() > size_t(kMaxMacroEvents))
        die("macro has " + std::to_string(m.events.size()) + " events; the limit is 70");
    return m;
}

std::string event_key(const Event& e) {
    if (e.type != kEvModifier) return key_name(e.type, e.code);
    std::string k;
    for (int b = 0; b < 8; ++b)
        if (e.code & (1 << b)) k += (k.empty() ? "" : "+") + key_name(kEvModifier, uint16_t(1 << b));
    return k;
}

void print_macro(const Macro& m) {
    std::cout << "name " << m.name << "\n";
    for (auto& e : m.events) {
        std::cout << (e.state == 0 ? "down " : e.state == 1 ? "up " : "move ") << event_key(e);
        if (e.delay_ms) std::cout << " " << e.delay_ms;
        std::cout << "\n";
    }
}

// key:ctrl+shift+a -> press in order, release in reverse order (same as the vendor software)
std::vector<Event> parse_shortcut(const std::string& spec) {
    std::vector<KeyCode> keys;
    for (auto& part : split(spec, '+')) keys.push_back(need_key(part));
    if (keys.empty() || keys.size() > 3) die("a shortcut takes 1 to 3 keys");
    std::vector<Event> ev;
    for (auto& k : keys) ev.push_back({0, k.type, k.code, 0});
    for (auto it = keys.rbegin(); it != keys.rend(); ++it) ev.push_back({1, it->type, it->code, 0});
    return ev;
}

// ---------------------------------------------------------------- commands

void cmd_list() {
    if (g_mice.empty()) {
        std::cout << "no supported receiver found\n";
        return;
    }
    for (size_t i = 0; i < g_mice.size(); ++i) {
        auto& m = *g_mice[i];
        std::string st;
        try {
            st = m.online() ? "awake" : "asleep/off";
        } catch (const std::exception& e) {
            st = e.what();
        }
        std::printf("%zu  %-22s %04x:%04x  %-11s %s\n", i + 1, m.model().name.c_str(), m.node().vid, m.node().pid,
                    st.c_str(), m.node().path.c_str());
    }
}

std::string battery_text(const Battery& b) {
    std::string s = std::to_string(b.level) + "%";
    if (b.charging) s += " (charging)";
    if (b.millivolts) s += ", " + std::to_string(b.millivolts) + " mV";
    return s;
}

void cmd_info(Mouse& m) {
    std::cout << "model:     " << m.model().name << "\n";
    std::printf("receiver:  %04x:%04x %s\n", m.node().vid, m.node().pid, m.node().path.c_str());
    bool on = m.online();
    std::cout << "state:     " << (on ? "awake" : "asleep/off") << "\n";
    if (!on) return;
    try {
        std::cout << "battery:   " << battery_text(m.battery()) << "\n";
    } catch (const std::exception& e) {
        std::cout << "battery:   " << e.what() << "\n";
    }
    if (auto v = m.version()) std::printf("firmware:  %x.%02x\n", *v >> 8, *v & 0xFF);
    if (auto c = m.cid_mid()) std::printf("CID/MID:   %d/%d (type 0x%02x)\n", (*c)[0], (*c)[1], (*c)[2]);
}

void cmd_battery(const Args& a) {
    struct Row {
        std::string slug, name, text;
        int level = -1;
        bool charging = false;
    };
    std::vector<Row> rows;
    for (auto& mp : g_mice) {
        Row r{mp->model().slug, mp->model().name, "asleep"};
        try {
            if (mp->online()) {
                Battery b = mp->battery();
                r.level = b.level;
                r.charging = b.charging;
                r.text = battery_text(b);
            }
        } catch (const std::exception&) {
            r.text = "no answer";
        }
        rows.push_back(r);
    }

    if (a.waybar) {
        std::string text, tip;
        int lowest = 101;
        for (auto& r : rows) {
            std::string tag = r.slug.substr(0, 4);
            for (auto& ch : tag) ch = char(std::toupper((unsigned char)ch));
            text += (text.empty() ? "" : "  ") + tag + " " + (r.level >= 0 ? std::to_string(r.level) + "%" : "-");
            tip += (tip.empty() ? "" : "\\n") + r.name + ": " + r.text;
            if (r.level >= 0) lowest = std::min(lowest, r.level);
        }
        const char* cls = rows.empty() ? "disconnected" : lowest <= 15 ? "critical" : lowest <= 30 ? "warning" : "normal";
        std::printf("{\"text\":\"%s\",\"tooltip\":\"%s\",\"percentage\":%d,\"class\":\"%s\"}\n", text.c_str(),
                    tip.c_str(), lowest > 100 ? 0 : lowest, cls);
        return;
    }
    if (a.json) {
        std::cout << "[";
        for (size_t i = 0; i < rows.size(); ++i)
            std::printf("%s{\"device\":\"%s\",\"level\":%d,\"charging\":%s}", i ? "," : "", rows[i].slug.c_str(),
                        rows[i].level, rows[i].charging ? "true" : "false");
        std::cout << "]\n";
        return;
    }
    if (rows.empty()) std::cout << "no supported receiver found\n";
    for (auto& r : rows) std::cout << r.name << ": " << r.text << "\n";
}

void cmd_show(Mouse& m) {
    FlashImage img = load(m, true);
    const Model& md = m.model();
    auto b = [&](uint16_t a) {
        auto v = img.byte(a);
        return v ? int(*v) : -1;
    };
    auto onoff = [&](uint16_t a) { return b(a) > 0 ? "on" : "off"; };

    std::cout << md.name << "\n";
    std::cout << "  polling rate:  " << (b(addr::kRate) >= 0 ? rate_name(uint8_t(b(addr::kRate))) : "?") << "\n";
    int count = b(addr::kDpiCount), cur = b(addr::kDpiCurrent);
    std::cout << "  DPI levels:    " << count << " (active: " << cur + 1 << ")\n";
    for (int i = 0; i < (count > 0 ? std::min(count, kDpiSlots) : kDpiSlots); ++i) {
        auto d = img.field(uint16_t(addr::kDpi + 4 * i), 3);
        auto c = img.field(uint16_t(addr::kDpiColor + 4 * i), 3);
        std::string dpi = "?";
        if (d) {
            auto v = md.decode_dpi({(*d)[0], (*d)[1], (*d)[2]});
            dpi = v ? std::to_string(*v) : "raw " + hex(*d);
        }
        char col[8] = "------";
        if (c) std::snprintf(col, sizeof col, "%02x%02x%02x", (*c)[0], (*c)[1], (*c)[2]);
        std::printf("    %c %d: %6s  #%s\n", i == cur ? '*' : ' ', i + 1, dpi.c_str(), col);
    }
    if (md.has_extended_config) {
        std::printf("  debounce:      %d ms\n", b(addr::kDebounce));
        std::printf("  motion sync:   %s\n", onoff(addr::kMotionSync));
        std::printf("  angle snap:    %s\n", onoff(addr::kLinearCorrection));
        std::printf("  ripple ctrl:   %s\n", onoff(addr::kRipple));
    }
    std::cout << "  buttons:\n";
    for (int i = 0; i < kButtons; ++i) std::printf("    %2d: %s\n", i + 1, describe_key(img, i).c_str());
}

KeyFn parse_action(const std::string& act, int idx, FlashImage& before, FlashImage& img, Mouse& m) {
    static const std::map<std::string, KeyFn> simple = {
        {"disabled", {kFnDisabled, 0, 0}},     {"left", {kFnMouse, 1, 0}},
        {"right", {kFnMouse, 2, 0}},           {"middle", {kFnMouse, 4, 0}},
        {"back", {kFnMouse, 8, 0}},            {"forward", {kFnMouse, 0x10, 0}},
        {"dpi-cycle", {kFnDpi, 1, 0}},         {"dpi-up", {kFnDpi, 2, 0}},
        {"dpi-down", {kFnDpi, 3, 0}},          {"scroll-left", {kFnHScroll, 1, 0}},
        {"scroll-right", {kFnHScroll, 2, 0}},  {"rate-cycle", {kFnReportRate, 0, 0}},
        {"profile-cycle", {kFnProfile, 0, 0}},
    };
    std::string a = lower(act);
    if (auto it = simple.find(a); it != simple.end()) return it->second;

    if (a.rfind("fire:", 0) == 0) {
        auto p = split(a, ':');
        if (p.size() != 3) die("use fire:SPEED:COUNT");
        return {kFnFire, uint8_t(to_int(p[1])), uint8_t(to_int(p[2]))};
    }
    if (a.rfind("key:", 0) == 0) {
        img.set_shortcut(idx, parse_shortcut(act.substr(4)));
        return {kFnShortcut, 0, 0};
    }
    if (a.rfind("macro:", 0) == 0) {
        std::string file = act.substr(6);
        int reps = 1;
        auto c = file.rfind(':');
        if (c != std::string::npos && c + 1 < file.size() &&
            std::all_of(file.begin() + long(c) + 1, file.end(), [](unsigned char ch) { return std::isdigit(ch); })) {
            reps = to_int(file.substr(c + 1));
            file = file.substr(0, c);
        }
        Macro mac = parse_macro_file(file);
        // Start from what is really in the slot so commit() sees an accurate diff.
        uint16_t slot = uint16_t(addr::kMacros + addr::kMacroSize * idx);
        auto cur = m.read_flash(slot, addr::kMacroSize);
        before.patch(slot, cur);
        img.patch(slot, cur);
        img.set_macro(idx, mac);
        return {kFnMacro, uint8_t(idx), uint8_t(std::clamp(reps, 1, 255))};
    }
    die("unknown action: " + act + " (see --help)");
}

int button_index(const std::string& s) {
    int n = to_int(s);
    if (n < 1 || n > kButtons) die("button must be 1..16");
    return n - 1;
}

void cmd_raw(Mouse& m, const Args& a) {
    if (a.pos.size() < 2) die("usage: raw CMD [ADDR] [HEX|LEN]");
    uint8_t cmd = uint8_t(to_int(a.pos[1], 16));
    uint16_t ad = a.pos.size() > 2 ? uint16_t(to_int(a.pos[2], 16)) : 0;
    std::vector<uint8_t> payload;
    int len = -1;
    if (a.pos.size() > 3) {
        const std::string& h = a.pos[3];
        if (h.size() <= 2) len = to_int(h, 16);
        else
            for (size_t i = 0; i + 1 < h.size(); i += 2) payload.push_back(uint8_t(to_int(h.substr(i, 2), 16)));
    }
    std::cout << "-> " << hex(build_packet(cmd, ad, payload, len)) << "\n";
    Reply r = m.request(cmd, ad, payload, len);
    std::printf("<- cmd %02x status %02x addr %04x len %d data %s\n", r.cmd, r.status, r.addr, r.length,
                hex(std::vector<uint8_t>(r.payload, r.payload + 10)).c_str());
}

void cmd_restore(const Args& a) {
    if (a.pos.size() < 2) die("usage: restore FILE");
    Mouse& m = pick(a);
    std::ifstream f(a.pos[1], std::ios::binary);
    if (!f) die("cannot open " + a.pos[1]);
    std::vector<uint8_t> want((std::istreambuf_iterator<char>(f)), {});
    if (want.size() != addr::kFlashEnd && want.size() != addr::kSettingsEnd)
        die("unexpected file size (" + std::to_string(want.size()) + " bytes)");
    if (!a.yes) {
        std::cout << "This overwrites the settings of " << m.model().name << ". Continue? [y/N] ";
        std::string ans;
        std::getline(std::cin, ans);
        if (lower(ans) != "y") return;
    }
    require_awake(m);
    FlashImage before(m.read_flash(0, want.size(), progress));
    commit(m, before, FlashImage(want));
}

// Commands that change settings: load, edit, commit.
void cmd_edit(const Args& a) {
    const std::string& c = a.pos[0];
    auto need = [&](size_t n, const char* use) {
        if (a.pos.size() < n) die(std::string("usage: ") + use);
    };
    if (c != "rate" && c != "dpi" && c != "dpi-current" && c != "dpi-color" && c != "bind")
        die("unknown command: " + c + " (see --help)");

    Mouse& m = pick(a);
    const Model& md = m.model();
    FlashImage before = load(m, c == "bind");
    FlashImage img = before;

    if (c == "rate") {
        need(2, "rate HZ");
        int hz = to_int(a.pos[1]);
        auto code = rate_code(hz);
        if (!code || hz > md.max_rate_hz) die("supported rates: 125 250 500 " + std::to_string(md.max_rate_hz));
        img.set_field(addr::kRate, {*code});
    } else if (c == "dpi") {
        need(2, "dpi 800,1600,...");
        auto parts = split(a.pos[1], ',');
        if (parts.empty() || parts.size() > size_t(kDpiSlots)) die("between 1 and 8 levels");
        for (size_t i = 0; i < parts.size(); ++i) {
            auto r = md.encode_dpi(to_int(parts[i]));
            if (!r)
                die("DPI " + parts[i] + " out of range (" + std::to_string(md.dpi_min) + "-" +
                    std::to_string(md.dpi_max) + ", step " + std::to_string(md.dpi_step) + ")");
            img.set_field(uint16_t(addr::kDpi + 4 * i), {r->x, r->y, r->ext});
        }
        img.set_field(addr::kDpiCount, {uint8_t(parts.size())});
        auto cur = img.byte(addr::kDpiCurrent);
        if (!cur || *cur >= parts.size()) img.set_field(addr::kDpiCurrent, {0});
    } else if (c == "dpi-current") {
        need(2, "dpi-current N");
        int n = to_int(a.pos[1]);
        auto count = img.byte(addr::kDpiCount);
        if (n < 1 || n > (count ? *count : kDpiSlots)) die("level out of range");
        img.set_field(addr::kDpiCurrent, {uint8_t(n - 1)});
    } else if (c == "dpi-color") {
        need(3, "dpi-color N RRGGBB");
        int n = to_int(a.pos[1]);
        if (n < 1 || n > kDpiSlots) die("level out of range");
        img.set_field(uint16_t(addr::kDpiColor + 4 * (n - 1)), parse_rgb(a.pos[2]));
    } else if (c == "bind") {
        need(3, "bind BUTTON ACTION");
        int i = button_index(a.pos[1]);
        img.set_key(i, parse_action(a.pos[2], i, before, img, m));
    }
    commit(m, before, img);
}

}  // namespace

int main(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        if (s == "-h" || s == "--help") {
            usage();
            return 0;
        }
        if (s == "--version") {
            std::cout << kVersion << "\n";
            return 0;
        }
        if ((s == "-d" || s == "--device") && i + 1 < argc) a.device = argv[++i];
        else if (s == "-y" || s == "--yes") a.yes = true;
        else if (s == "--json") a.json = true;
        else if (s == "--waybar") a.waybar = true;
        else a.pos.push_back(s);
    }
    if (a.pos.empty()) {
        usage();
        return 1;
    }

    try {
        g_mice = discover();
        const std::string& c = a.pos[0];
        if (c == "list") cmd_list();
        else if (c == "battery") cmd_battery(a);
        else if (c == "info") cmd_info(pick(a));
        else if (c == "show") cmd_show(pick(a));
        else if (c == "raw") cmd_raw(pick(a), a);
        else if (c == "restore") cmd_restore(a);
        else if (c == "backup") {
            Mouse& m = pick(a);
            require_awake(m);
            fs::path p = a.pos.size() > 1 ? fs::path(a.pos[1]) : state_dir() / (m.model().slug + "-" + timestamp() + ".bin");
            write_file(p, m.read_flash(0, addr::kFlashEnd, progress));
            std::cout << "saved " << p.string() << "\n";
        } else if (c == "macro-show") {
            if (a.pos.size() < 2) die("usage: macro-show BUTTON");
            Mouse& m = pick(a);
            FlashImage img = load(m, true);
            int i = button_index(a.pos[1]);
            KeyFn k = img.key(i);
            if (k.type != kFnMacro) die("button " + a.pos[1] + " has no macro (" + describe_key(img, i) + ")");
            auto mac = img.macro(k.p1);
            if (!mac) die("the stored macro is invalid");
            print_macro(*mac);
        } else {
            cmd_edit(a);
        }
    } catch (const std::exception& e) {
        die(e.what());
    }
    return 0;
}
