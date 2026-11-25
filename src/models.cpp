#include "models.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace od {
namespace {

// PAW3335 register values from the vendor's driver_sensor.h, index = dpi/100 - 1 (100..16000).
constexpr std::array<uint8_t, 160> kPaw3335 = {
    0x00, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x10, 0x11, 0x12, 0x13, 0x14,
    0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x23, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A,
    0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x32, 0x33, 0x34, 0x35, 0x36, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3E, 0x3F, 0x40,
    0x41, 0x42, 0x44, 0x45, 0x46, 0x47, 0x48, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x50, 0x51, 0x52, 0x53, 0x54, 0x56,
    0x57, 0x58, 0x59, 0x5A, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x62, 0x63, 0x64, 0x65, 0x66, 0x68, 0x69, 0x6A, 0x6B,
    0x6C, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x74, 0x75, 0x76, 0x77, 0x78, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x80, 0x81,
    0x82, 0x83, 0x84, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8C, 0x8D, 0x8E, 0x8F, 0x90, 0x92, 0x93, 0x94, 0x95, 0x96,
    0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9E, 0x9F, 0xA0, 0xA1, 0xA2, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xAA, 0xAB, 0xAC,
    0xAD, 0xAE, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD,
};

}  // namespace

std::optional<DpiRaw> Model::encode_dpi(int dpi) const {
    if (dpi < dpi_min || dpi > dpi_max || dpi % dpi_step) return std::nullopt;
    if (sensor == Sensor::PAW3395) {
        // 10-bit value v = dpi/50 - 1; bits 8-9 of X go to ext[3:2], of Y to ext[7:6]
        int v = dpi / 50 - 1;
        uint8_t hi = uint8_t((v >> 8) & 3);
        return DpiRaw{uint8_t(v), uint8_t(v), uint8_t((hi << 2) | (hi << 6))};
    }
    uint8_t r = kPaw3335[size_t(dpi / 100 - 1)];
    return DpiRaw{r, r, 0};
}

std::optional<int> Model::decode_dpi(const DpiRaw& r) const {
    if (sensor == Sensor::PAW3395) return (((r.ext >> 2) & 3) << 8 | r.x) * 50 + 50;
    auto it = std::find(kPaw3335.begin(), kPaw3335.end(), r.x);
    if (it == kPaw3335.end()) return std::nullopt;
    return int(it - kPaw3335.begin() + 1) * 100;
}

int Model::battery_percent(int raw, int mv, bool charging) const {
    if (mv > 0 && battery_mv.size() == 21) {
        // same interpolation as the vendor app (BatteryHandle.BatVoltageToLevel)
        if (mv >= battery_mv.back()) return charging ? 99 : 100;
        for (size_t i = 0; i < battery_mv.size(); ++i) {
            if (mv < battery_mv[i]) {
                if (i == 0) return 0;
                int step = (battery_mv[i] - battery_mv[i - 1]) / 5;
                return int((mv - battery_mv[i - 1]) / std::max(1, step) + int(i - 1) * 5);
            }
        }
    }
    if (battery_in_tenths && raw <= 10) return raw * 10;
    return std::min(raw, 100);
}

const std::vector<Model>& models() {
    static const std::vector<Model> list = {
        {"Redragon M916-PRO-1K", "m916-pro-1k", 0x3554, {0xF5D5, 0xF55D, 0xF55F, 0xF55E},
         /*feature reports*/ false, /*reply page*/ 0xFF02, Sensor::PAW3395, 50, 26000, 50, 1000,
         {3050, 3420, 3480, 3540, 3600, 3660, 3720, 3760, 3800, 3840, 3880,
          3920, 3940, 3960, 3980, 4000, 4020, 4040, 4060, 4080, 4110},
         /*tenths*/ false, /*extended*/ true},
        {"Redragon M901P-KS", "m901p-ks", 0x25A7, {0xFA07, 0xFA08},
         /*feature reports*/ true, /*reply page*/ 0xFF01, Sensor::PAW3335, 100, 16000, 100, 1000,
         {}, /*tenths*/ true, /*extended*/ false},
    };
    return list;
}

const Model* find_model(uint16_t vid, uint16_t pid) {
    for (const Model& m : models())
        if (m.vid == vid && std::find(m.pids.begin(), m.pids.end(), pid) != m.pids.end()) return &m;
    return nullptr;
}

}  // namespace od
