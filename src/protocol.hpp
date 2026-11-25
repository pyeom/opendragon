// Wire protocol of the CX / Compx 2.4 GHz receivers (see docs/PROTOCOL.md).
#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "models.hpp"
#include "transport.hpp"

namespace od {

constexpr size_t kPacketLen = 17;
constexpr size_t kChunk = 10;  // max payload per flash read/write

enum Cmd : uint8_t {
    kEncryption = 0x01,
    kPcDriverStatus = 0x02,
    kDeviceOnline = 0x03,
    kBattery = 0x04,
    kDongleEnterPair = 0x05,
    kGetPairState = 0x06,
    kWriteFlash = 0x07,
    kReadFlash = 0x08,
    kClearSetting = 0x09,
    kStatusChanged = 0x0A,
    kGetCurrentConfig = 0x0E,
    kSetCurrentConfig = 0x0F,
    kReadCidMid = 0x10,
    kReadVersion = 0x12,
    kSetLongRange = 0x16,
    kGetLongRange = 0x17,
};

// (0x55 - sum) & 0xff: used for packets and for every field stored in flash.
uint8_t checksum(const uint8_t* p, size_t n);

std::vector<uint8_t> build_packet(uint8_t cmd, uint16_t addr = 0, const std::vector<uint8_t>& payload = {},
                                  int length = -1);

struct Reply {
    uint8_t cmd = 0, status = 0, length = 0;
    uint16_t addr = 0;
    uint8_t payload[kChunk] = {};
    bool ok() const { return status == 0; }
    std::vector<uint8_t> data() const { return {payload, payload + std::min<size_t>(length, kChunk)}; }
};

struct ProtocolError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
// The receiver is there but the mouse is asleep / off: only receiver-level commands answer.
struct OfflineError : ProtocolError {
    using ProtocolError::ProtocolError;
};

struct Battery {
    int level = 0;  // percent
    bool charging = false;
    int millivolts = 0;
    int raw = 0;
};

using Progress = std::function<void(size_t done, size_t total)>;

class Mouse {
public:
    Mouse(std::unique_ptr<Transport> t, const Model& m, HidNode node)
        : t_(std::move(t)), model_(m), node_(std::move(node)) {}

    const Model& model() const { return model_; }
    const HidNode& node() const { return node_; }

    Reply request(uint8_t cmd, uint16_t addr = 0, const std::vector<uint8_t>& payload = {}, int length = -1,
                  int timeout_ms = 400, int retries = 2);

    bool online();
    Battery battery();
    std::optional<uint16_t> version();
    std::optional<std::vector<uint8_t>> cid_mid();

    std::vector<uint8_t> read_flash(uint16_t addr, size_t size, const Progress& p = {});
    void write_flash(uint16_t addr, const std::vector<uint8_t>& data, const Progress& p = {});

private:
    std::unique_ptr<Transport> t_;
    const Model& model_;
    HidNode node_;
};

// Find every supported receiver that is plugged in.
std::vector<std::unique_ptr<Mouse>> discover();

}  // namespace od
