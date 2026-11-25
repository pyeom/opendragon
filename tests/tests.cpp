// Offline unit tests against the real flash dumps in docs/dumps.
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include "config.hpp"
#include "models.hpp"
#include "protocol.hpp"

using namespace od;

static int failures = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);     \
            ++failures;                                                  \
        }                                                                \
    } while (0)

static std::vector<uint8_t> load(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

static const Model& model(const char* slug) {
    for (auto& m : models())
        if (m.slug == slug) return m;
    std::abort();
}

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "docs/dumps";

    // packet: battery request captured from the vendor app
    auto pkt = build_packet(kBattery);
    CHECK(pkt.size() == 17 && pkt[0] == 0x08 && pkt[1] == 0x04 && pkt[16] == 0x49);
    // flash read 0x00a0 len 8 captured from OemDrv: 08 08 00 00 a0 08 .. 9d
    pkt = build_packet(kReadFlash, 0xA0, {}, 8);
    CHECK(pkt[3] == 0x00 && pkt[4] == 0xA0 && pkt[5] == 8 && pkt[16] == 0x9d);

    // ---- M916 dump
    const Model& m916 = model("m916-pro-1k");
    FlashImage a(load(dir + "/m916-pro-1k_flash.bin"));
    CHECK(a.size() == 0x1B00);
    CHECK(a.byte(addr::kRate) == 0x01);
    CHECK(a.byte(addr::kDpiCount) == 5);
    int want916[5] = {1000, 2000, 4000, 8000, 26000};
    for (int i = 0; i < 5; ++i) {
        auto f = a.field(uint16_t(addr::kDpi + 4 * i), 3);
        CHECK(f.has_value());
        if (f) CHECK(m916.decode_dpi({(*f)[0], (*f)[1], (*f)[2]}) == want916[i]);
        auto enc = m916.encode_dpi(want916[i]);
        CHECK(enc && f && enc->x == (*f)[0] && enc->ext == (*f)[2]);
    }
    CHECK(describe_key(a, 0) == "left");
    CHECK(describe_key(a, 1) == "right");
    CHECK(describe_key(a, 5) == "dpi-cycle");
    CHECK(m916.battery_percent(100, 4206, false) == 100);
    CHECK(m916.battery_percent(0, 3800, false) == 40);

    // ---- M901 dump
    const Model& m901 = model("m901p-ks");
    FlashImage b(load(dir + "/m901p-ks_flash.bin"));
    CHECK(b.byte(addr::kRate) == 0x08);
    int want901[5] = {1000, 2000, 4000, 8000, 16000};
    for (int i = 0; i < 5; ++i) {
        auto f = b.field(uint16_t(addr::kDpi + 4 * i), 3);
        CHECK(f && m901.decode_dpi({(*f)[0], (*f)[1], (*f)[2]}) == want901[i]);
    }
    CHECK(describe_key(b, 0) == "key:1");
    CHECK(describe_key(b, 1) == "key:2");
    CHECK(describe_key(b, 7) == "left");
    CHECK(describe_key(b, 11) == "fire:20:3");

    // ---- shortcut round trip reproduces the vendor encoding exactly
    FlashImage c(std::vector<uint8_t>(addr::kFlashEnd, 0xFF));
    c.set_shortcut(0, {{0, kEvKey, 0x1E, 0}, {1, kEvKey, 0x1E, 0}});
    for (int i = 0; i < 8; ++i) CHECK(c.at(addr::kShortcuts + i) == b.at(addr::kShortcuts + i));

    // ---- macro round trip
    Macro mac{"test", {{0, kEvModifier, 0x01, 10}, {0, kEvKey, 0x06, 30}, {1, kEvKey, 0x06, 0}, {1, kEvModifier, 1, 5}}};
    c.set_macro(3, mac);
    auto back = c.macro(3);
    CHECK(back && back->name == "test" && back->events.size() == 4);
    CHECK(back && back->events[1].code == 0x06 && back->events[1].delay_ms == 30 && back->events[2].state == 1);

    // ---- diff only touches what changed
    FlashImage d = a;
    d.set_field(addr::kRate, {0x02});
    auto r = diff_ranges(a.bytes(), d.bytes());
    CHECK(r.size() == 1 && r[0].first == 0 && r[0].second == 2);

    std::printf(failures ? "%d test(s) failed\n" : "all tests passed\n", failures);
    return failures ? 1 : 0;
}
