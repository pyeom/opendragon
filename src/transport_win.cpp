// Windows backend (hid.dll + SetupAPI). Only used to test against the mice on a Windows machine;
// the real target is Linux (transport_linux.cpp).
#include "transport.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace od {
namespace {

struct Caps {
    uint16_t vid = 0, pid = 0, page = 0;
    USHORT in_len = 0, out_len = 0, feat_len = 0;
    std::string product;
};

bool query(const std::string& path, Caps& c) {
    HANDLE h = CreateFileA(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    HIDD_ATTRIBUTES a{};
    a.Size = sizeof a;
    bool ok = HidD_GetAttributes(h, &a);
    PHIDP_PREPARSED_DATA pp = nullptr;
    if (ok && HidD_GetPreparsedData(h, &pp)) {
        HIDP_CAPS caps{};
        HidP_GetCaps(pp, &caps);
        c.page = caps.UsagePage;
        c.in_len = caps.InputReportByteLength;
        c.out_len = caps.OutputReportByteLength;
        c.feat_len = caps.FeatureReportByteLength;
        HidD_FreePreparsedData(pp);
    }
    wchar_t buf[128] = {};
    if (HidD_GetProductString(h, buf, sizeof buf)) {
        for (wchar_t* p = buf; *p; ++p) c.product += char(*p < 128 ? *p : '?');
    }
    c.vid = a.VendorID;
    c.pid = a.ProductID;
    CloseHandle(h);
    return ok;
}

std::vector<std::string> all_hid_paths() {
    std::vector<std::string> out;
    GUID g;
    HidD_GetHidGuid(&g);
    HDEVINFO set = SetupDiGetClassDevsA(&g, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return out;
    SP_DEVICE_INTERFACE_DATA ifd{};
    ifd.cbSize = sizeof ifd;
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &g, i, &ifd); ++i) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailA(set, &ifd, nullptr, 0, &need, nullptr);
        std::vector<char> mem(need);
        auto* det = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_A*>(mem.data());
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
        if (SetupDiGetDeviceInterfaceDetailA(set, &ifd, det, need, nullptr, nullptr)) out.emplace_back(det->DevicePath);
    }
    SetupDiDestroyDeviceInfoList(set);
    return out;
}

std::string iface_of(std::string p) {
    std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    auto i = p.find("&mi_");
    return i == std::string::npos ? "" : p.substr(i, 6);
}

class WinHid final : public Transport {
public:
    WinHid(const HidNode& node, bool feature, uint16_t reply_page) : feature_(feature) {
        Caps c;
        query(node.path, c);
        out_len_ = feature ? c.feat_len : c.out_len;
        tx_ = CreateFileA(node.path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          nullptr, OPEN_EXISTING, 0, nullptr);
        if (tx_ == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot open " + node.path);

        std::string rx_path = node.path;
        if (reply_page != kVendorPage) {
            for (auto& p : all_hid_paths()) {
                Caps rc;
                if (query(p, rc) && rc.vid == node.vid && rc.pid == node.pid && rc.page == reply_page &&
                    iface_of(p) == iface_of(node.path)) {
                    rx_path = p;
                    break;
                }
            }
        }
        Caps rc;
        query(rx_path, rc);
        in_len_ = rc.in_len ? rc.in_len : 64;
        rx_ = CreateFileA(rx_path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                          OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (rx_ == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot open " + rx_path);
        ev_ = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        rbuf_.resize(in_len_);
    }
    ~WinHid() override {
        CancelIo(rx_);
        CloseHandle(rx_);
        CloseHandle(tx_);
        CloseHandle(ev_);
    }

    void send(const std::vector<uint8_t>& r) override {
        std::vector<uint8_t> buf(std::max<size_t>(out_len_, r.size()), 0);
        std::copy(r.begin(), r.end(), buf.begin());
        BOOL ok;
        if (feature_) {
            ok = HidD_SetFeature(tx_, buf.data(), ULONG(buf.size()));
        } else {
            DWORD n = 0;
            ok = WriteFile(tx_, buf.data(), DWORD(buf.size()), &n, nullptr);
        }
        if (!ok) throw std::runtime_error("HID write failed (error " + std::to_string(GetLastError()) + ")");
    }

    size_t recv(uint8_t* buf, size_t len, int timeout_ms) override {
        if (!pending_) {
            ResetEvent(ev_);
            ov_ = OVERLAPPED{};
            ov_.hEvent = ev_;
            if (!ReadFile(rx_, rbuf_.data(), DWORD(rbuf_.size()), nullptr, &ov_) && GetLastError() != ERROR_IO_PENDING)
                return 0;
            pending_ = true;
        }
        if (WaitForSingleObject(ev_, DWORD(std::max(0, timeout_ms))) != WAIT_OBJECT_0) return 0;
        DWORD n = 0;
        pending_ = false;
        if (!GetOverlappedResult(rx_, &ov_, &n, FALSE)) return 0;
        n = std::min<DWORD>(n, DWORD(len));
        std::copy(rbuf_.begin(), rbuf_.begin() + n, buf);
        return n;
    }

    void drain() override {
        uint8_t tmp[64];
        while (recv(tmp, sizeof tmp, 0) > 0) {}
    }

private:
    bool feature_;
    HANDLE tx_ = INVALID_HANDLE_VALUE, rx_ = INVALID_HANDLE_VALUE, ev_ = nullptr;
    OVERLAPPED ov_{};
    bool pending_ = false;
    USHORT out_len_ = 0, in_len_ = 0;
    std::vector<uint8_t> rbuf_;
};

}  // namespace

std::vector<HidNode> scan(const IdList& ids) {
    std::vector<HidNode> out;
    for (auto& p : all_hid_paths()) {
        Caps c;
        if (!query(p, c) || c.page != kVendorPage) continue;
        for (auto& [v, pid] : ids)
            if (v == c.vid && pid == c.pid) out.push_back({c.vid, c.pid, p, c.product});
    }
    return out;
}

std::unique_ptr<Transport> open_transport(const HidNode& node, bool use_feature, uint16_t reply_page) {
    return std::make_unique<WinHid>(node, use_feature, reply_page);
}

}  // namespace od
