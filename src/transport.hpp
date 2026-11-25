// Raw HID access: Linux hidraw, or Windows hid.dll (used for testing on the RE laptop).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace od {

constexpr uint16_t kVendorPage = 0xFF02;  // command collection on both receivers

struct HidNode {
    uint16_t vid = 0;
    uint16_t pid = 0;
    std::string path;     // /dev/hidrawN or Windows device path of the 0xFF02 collection
    std::string product;
};

class Transport {
public:
    virtual ~Transport() = default;
    virtual void send(const std::vector<uint8_t>& report) = 0;
    // Returns number of bytes read (0 on timeout). Report id is buf[0].
    virtual size_t recv(uint8_t* buf, size_t len, int timeout_ms) = 0;
    virtual void drain() = 0;
};

using IdList = std::vector<std::pair<uint16_t, uint16_t>>;

std::vector<HidNode> scan(const IdList& ids);

// use_feature: send requests as FEATURE reports (Compx) instead of OUTPUT reports (CX).
// reply_page:  usage page of the collection carrying replies (only matters on Windows,
//              where every top-level collection is a separate device).
std::unique_ptr<Transport> open_transport(const HidNode& node, bool use_feature, uint16_t reply_page);

}  // namespace od
