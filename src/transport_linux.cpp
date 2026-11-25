#include "transport.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace od {
namespace {

std::string slurp(const std::string& p, bool binary = false) {
    std::ifstream f(p, binary ? std::ios::binary : std::ios::in);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Walk the HID report descriptor looking for a Usage Page item with the given value.
bool has_usage_page(const std::string& d, uint16_t page) {
    size_t i = 0;
    while (i < d.size()) {
        auto b = static_cast<uint8_t>(d[i]);
        if (b == 0xFE) {  // long item
            if (i + 1 >= d.size()) break;
            i += 3 + static_cast<uint8_t>(d[i + 1]);
            continue;
        }
        static const int sizes[4] = {0, 1, 2, 4};
        int sz = sizes[b & 3];
        if ((b & 0xFC) == 0x04 && i + sz < d.size()) {
            uint32_t v = 0;
            for (int k = 0; k < sz; ++k) v |= uint32_t(static_cast<uint8_t>(d[i + 1 + k])) << (8 * k);
            if (v == page) return true;
        }
        i += 1 + sz;
    }
    return false;
}

class Hidraw final : public Transport {
public:
    Hidraw(const std::string& path, bool feature) : feature_(feature) {
        fd_ = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd_ < 0) {
            if (errno == EACCES)
                throw std::runtime_error(path + ": permission denied. Install udev/70-opendragon.rules "
                                                "and replug the receiver (or use sudo).");
            throw std::runtime_error(path + ": " + std::strerror(errno));
        }
    }
    ~Hidraw() override { ::close(fd_); }

    void send(const std::vector<uint8_t>& r) override {
        std::vector<uint8_t> buf(r);
        ssize_t n = feature_ ? ::ioctl(fd_, HIDIOCSFEATURE(buf.size()), buf.data())
                             : ::write(fd_, buf.data(), buf.size());
        if (n < 0) throw std::runtime_error(std::string("HID write failed: ") + std::strerror(errno));
    }

    size_t recv(uint8_t* buf, size_t len, int timeout_ms) override {
        pollfd p{fd_, POLLIN, 0};
        if (::poll(&p, 1, std::max(0, timeout_ms)) <= 0) return 0;
        ssize_t n = ::read(fd_, buf, len);
        return n > 0 ? size_t(n) : 0;
    }

    void drain() override {
        uint8_t tmp[64];
        while (::read(fd_, tmp, sizeof tmp) > 0) {}
    }

private:
    int fd_ = -1;
    bool feature_;
};

}  // namespace

std::vector<HidNode> scan(const IdList& ids) {
    std::vector<HidNode> out;
    DIR* d = ::opendir("/sys/class/hidraw");
    if (!d) return out;
    std::vector<std::string> names;
    while (dirent* e = ::readdir(d))
        if (std::strncmp(e->d_name, "hidraw", 6) == 0) names.emplace_back(e->d_name);
    ::closedir(d);
    std::sort(names.begin(), names.end());

    for (const auto& n : names) {
        std::string base = "/sys/class/hidraw/" + n + "/device/";
        std::string uevent = slurp(base + "uevent");
        std::string hid_id, hid_name;
        std::istringstream ls(uevent);
        for (std::string line; std::getline(ls, line);) {
            if (line.rfind("HID_ID=", 0) == 0) hid_id = line.substr(7);
            if (line.rfind("HID_NAME=", 0) == 0) hid_name = line.substr(9);
        }
        unsigned bus = 0, vid = 0, pid = 0;
        if (std::sscanf(hid_id.c_str(), "%x:%x:%x", &bus, &vid, &pid) != 3) continue;
        bool wanted = std::any_of(ids.begin(), ids.end(),
                                  [&](auto& p) { return p.first == vid && p.second == pid; });
        if (!wanted) continue;
        if (!has_usage_page(slurp(base + "report_descriptor", true), kVendorPage)) continue;
        out.push_back({uint16_t(vid), uint16_t(pid), "/dev/" + n, hid_name});
    }
    return out;
}

std::unique_ptr<Transport> open_transport(const HidNode& node, bool use_feature, uint16_t) {
    return std::make_unique<Hidraw>(node.path, use_feature);
}

}  // namespace od
