// Supported mice and their per-model differences.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace od {

enum class Sensor { PAW3395, PAW3335 };

struct DpiRaw {
    uint8_t x = 0, y = 0, ext = 0;
};

struct Model {
    std::string name;
    std::string slug;  // used for backups / --device
    uint16_t vid;
    std::vector<uint16_t> pids;
    bool feature_reports;  // Compx: requests are feature reports
    uint16_t reply_page;   // usage page that carries the replies
    Sensor sensor;
    int dpi_min, dpi_max, dpi_step;
    int max_rate_hz;
    std::vector<int> battery_mv;  // discharge curve, 21 points (0..100 % in 5 % steps); empty = no voltage
    bool battery_in_tenths;       // battery level reported as 0..10 instead of 0..100
    bool has_extended_config;     // 0xA9..0xBA: debounce, motion sync, angle snap, ripple control, ...

    std::optional<DpiRaw> encode_dpi(int dpi) const;
    std::optional<int> decode_dpi(const DpiRaw& r) const;
    int battery_percent(int raw, int mv, bool charging) const;
};

const std::vector<Model>& models();
const Model* find_model(uint16_t vid, uint16_t pid);

}  // namespace od
