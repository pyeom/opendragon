#include "protocol.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace od {

uint8_t checksum(const uint8_t* p, size_t n) {
    uint8_t s = 0;
    for (size_t i = 0; i < n; ++i) s = uint8_t(s + p[i]);
    return uint8_t(0x55 - s);
}

std::vector<uint8_t> build_packet(uint8_t cmd, uint16_t addr, const std::vector<uint8_t>& payload, int length) {
    if (payload.size() > kChunk) throw std::invalid_argument("payload > 10 bytes");
    std::vector<uint8_t> b(kPacketLen, 0);
    b[0] = 0x08;
    b[1] = cmd;
    b[3] = uint8_t(addr >> 8);
    b[4] = uint8_t(addr);
    b[5] = uint8_t(length < 0 ? payload.size() : size_t(length));
    std::copy(payload.begin(), payload.end(), b.begin() + 6);
    b[16] = checksum(b.data(), 16);
    return b;
}

Reply Mouse::request(uint8_t cmd, uint16_t addr, const std::vector<uint8_t>& payload, int length, int timeout_ms,
                     int retries) {
    using clock = std::chrono::steady_clock;
    const auto pkt = build_packet(cmd, addr, payload, length);
    const bool addressed = cmd == kReadFlash || cmd == kWriteFlash;
    for (int attempt = 0; attempt <= retries; ++attempt) {
        t_->drain();
        t_->send(pkt);
        const auto deadline = clock::now() + std::chrono::milliseconds(timeout_ms);
        while (true) {
            int left = int(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now()).count());
            if (left <= 0) break;
            uint8_t buf[64];
            size_t n = t_->recv(buf, sizeof buf, left);
            if (n == 0) break;
            if (n < kPacketLen || (buf[0] != 0x08 && buf[0] != 0x09)) continue;
            Reply r;
            r.cmd = buf[1];
            r.status = buf[2];
            r.addr = uint16_t((buf[3] << 8) | buf[4]);
            r.length = buf[5];
            std::copy(buf + 6, buf + 16, r.payload);
            if (r.cmd == cmd && (!addressed || r.addr == addr)) return r;
        }
    }
    char msg[96];
    std::snprintf(msg, sizeof msg, "no reply to command 0x%02x (is the mouse asleep? move it)", cmd);
    throw OfflineError(msg);
}

bool Mouse::online() {
    Reply r = request(kDeviceOnline);
    return r.length > 0 && r.payload[0] != 0;
}

Battery Mouse::battery() {
    Reply r = request(kBattery);
    if (!r.ok()) throw ProtocolError("battery query not supported");
    Battery b;
    b.raw = r.payload[0];
    b.charging = r.payload[1] != 0;
    // the M916 reports length 2 but still appends the voltage (big endian, mV)
    b.millivolts = (r.payload[2] << 8) | r.payload[3];
    b.level = model_.battery_percent(b.raw, b.millivolts, b.charging);
    return b;
}

std::optional<uint16_t> Mouse::version() {
    Reply r = request(kReadVersion);
    if (!r.ok() || r.length < 2) return std::nullopt;
    return uint16_t((r.payload[0] << 8) | r.payload[1]);
}

std::optional<std::vector<uint8_t>> Mouse::cid_mid() {
    Reply r = request(kReadCidMid);
    if (!r.ok() || r.length < 3) return std::nullopt;
    return std::vector<uint8_t>(r.payload, r.payload + 3);
}

std::vector<uint8_t> Mouse::read_flash(uint16_t addr, size_t size, const Progress& p) {
    std::vector<uint8_t> out;
    out.reserve(size);
    for (size_t off = 0; off < size; off += kChunk) {
        size_t n = std::min(kChunk, size - off);
        Reply r = request(kReadFlash, uint16_t(addr + off), {}, int(n));
        if (!r.ok()) throw ProtocolError("flash read failed at offset " + std::to_string(addr + off));
        out.insert(out.end(), r.payload, r.payload + n);
        if (p) p(off + n, size);
    }
    return out;
}

void Mouse::write_flash(uint16_t addr, const std::vector<uint8_t>& data, const Progress& p) {
    for (size_t off = 0; off < data.size(); off += kChunk) {
        size_t n = std::min(kChunk, data.size() - off);
        std::vector<uint8_t> chunk(data.begin() + off, data.begin() + off + n);
        Reply r = request(kWriteFlash, uint16_t(addr + off), chunk);
        if (!r.ok()) throw ProtocolError("flash write failed at offset " + std::to_string(addr + off));
        if (p) p(off + n, data.size());
    }
}

std::vector<std::unique_ptr<Mouse>> discover() {
    IdList ids;
    for (const Model& m : models())
        for (uint16_t pid : m.pids) ids.emplace_back(m.vid, pid);
    std::vector<std::unique_ptr<Mouse>> out;
    for (auto& node : scan(ids)) {
        const Model* m = find_model(node.vid, node.pid);
        if (!m) continue;
        out.push_back(std::make_unique<Mouse>(open_transport(node, m->feature_reports, m->reply_page), *m, node));
    }
    return out;
}

}  // namespace od
