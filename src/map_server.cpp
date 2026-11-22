// SPDX-License-Identifier: MIT
#include "map_server.h"
#include "map_page.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mdm {
namespace {

constexpr size_t kMaxPoints = 2000;

std::string jsonString(const std::string& s) {
    std::string out = "\"";
    char hex[7];
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 0x20) { std::snprintf(hex, sizeof hex, "\\u%04x", c); out += hex; }
        else out += static_cast<char>(c);
    }
    return out + '"';
}

void sendAll(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) break;
        sent += static_cast<size_t>(n);
    }
}

void response(int fd, int code, const char* mime, const std::string& body) {
    const char* reason = code == 200 ? "OK" : code == 403 ? "Forbidden" : code == 405 ? "Method Not Allowed" : "Not Found";
    std::string header = "HTTP/1.1 " + std::to_string(code) + " " + reason + "\r\n"
        "Content-Type: " + mime + "\r\nContent-Length: " + std::to_string(body.size()) +
        "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n";
    sendAll(fd, header);
    sendAll(fd, body);
}

bool acceptedHost(const std::string& request, int port) {
    const size_t p = request.find("\r\nHost: ");
    if (p == std::string::npos) return false;
    const size_t start = p + 8;
    const size_t end = request.find("\r\n", start);
    if (end == std::string::npos) return false;
    const std::string host = request.substr(start, end - start);
    const std::string suffix = ":" + std::to_string(port);
    return host == "127.0.0.1" + suffix || host == "localhost" + suffix;
}

} // namespace

MapServer::~MapServer() { stop(); }

bool MapServer::start(int preferred_port) {
    if (thread_.joinable()) return true;
    stopping_ = false;
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    int reuse = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int chosen = 0;
    if (preferred_port < 1024 || preferred_port > 65535) preferred_port = 8765;
    for (int p = preferred_port; p <= 65535 && p < preferred_port + 10; ++p) {
        addr.sin_port = htons(static_cast<uint16_t>(p));
        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0) { chosen = p; break; }
    }
    if (!chosen || ::listen(fd, 8) != 0) { ::close(fd); return false; }
    listen_fd_ = fd;
    port_ = chosen;
    try { thread_ = std::thread(&MapServer::serve, this); }
    catch (...) { ::close(fd); listen_fd_ = -1; port_ = 0; return false; }
    return true;
}

void MapServer::stop() {
    stopping_ = true;
    if (thread_.joinable()) thread_.join();
    if (listen_fd_ >= 0) { ::close(listen_fd_); listen_fd_ = -1; }
    port_ = 0;
}

void MapServer::setSimulation(bool enabled) {
    if (simulation_.exchange(enabled) != enabled && enabled) {
        std::lock_guard<std::mutex> lock(mu_);
        demo_started_ = std::chrono::steady_clock::now();
        demo_paused_ = false;
        ++generation_;
    }
}

void MapServer::resetSimulation() {
    std::lock_guard<std::mutex> lock(mu_);
    demo_paused_ = true;
    ++generation_;
}

void MapServer::startSimulation() {
    std::lock_guard<std::mutex> lock(mu_);
    demo_started_ = std::chrono::steady_clock::now();
    demo_paused_ = false;
    ++generation_;
}

void MapServer::add(MapPoint point) {
    std::lock_guard<std::mutex> lock(mu_);
    if (points_.size() == kMaxPoints) points_.erase(points_.begin());
    points_.push_back(std::move(point));
    ++generation_;
}

void MapServer::setGpsFix(bool valid) {
    std::lock_guard<std::mutex> lock(mu_);
    gps_fix_ = valid;
    if (valid) gps_seen_ = std::chrono::steady_clock::now();
}

void MapServer::setPositionMode(std::string mode, bool static_ready) {
    std::lock_guard<std::mutex> lock(mu_);
    position_mode_ = std::move(mode);
    static_ready_ = static_ready;
}

void MapServer::setCounts(uint64_t entries, uint64_t mapped) {
    std::lock_guard<std::mutex> lock(mu_);
    entries_ = entries;
    mapped_ = mapped;
}

void MapServer::setSessionName(std::string name) {
    std::lock_guard<std::mutex> lock(mu_);
    session_name_ = std::move(name);
}

std::string MapServer::pointsJson() const {
    std::vector<MapPoint> points;
    const bool sim = simulation_.load();
    uint64_t generation = 0;
    uint64_t entries = 0, mapped = 0;
    bool paused = false;
    bool gps_fix = false;
    bool position_ready = false;
    std::string position_mode;
    std::string session;
    {
        std::lock_guard<std::mutex> lock(mu_);
        generation = generation_;
        paused = demo_paused_;
        entries = entries_;
        mapped = mapped_;
        session = session_name_;
        gps_fix = gps_fix_ && std::chrono::duration<double>(
            std::chrono::steady_clock::now() - gps_seen_).count() <= 10.0;
        position_mode = position_mode_;
        position_ready = position_mode == "gps" ? gps_fix : static_ready_;
        if (!sim) points = points_;
        else if (!paused) {
            const auto sec = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - demo_started_).count();
            const int count = std::clamp(static_cast<int>(sec), 0, 24);
            constexpr double pi = 3.141592653589793;
            for (int i = 0; i < count; ++i) {
                const double angle = i * (2 * pi / 24);
                MapPoint p;
                p.lat = 51.337 + 0.010 * std::sin(angle) + 0.0015 * std::sin(3 * angle);
                p.lon = 12.375 + 0.015 * std::cos(angle);
                const double dlat = (p.lat - 51.34) * 111.0;
                const double dlon = (p.lon - 12.382) * 70.0;
                p.value = -96 + 57 * std::exp(-(dlat * dlat + dlon * dlon) / 0.6);
                p.time = 0;
                p.is_level = true;
                p.crc_verified = true;
                p.fingerprint = "simulated";
                p.position_source = "simulation";
                points.push_back(std::move(p));
            }
        }
    }
    std::string out = std::string("{\"simulation\":") + (sim ? "true" : "false") +
        ",\"paused\":" + (paused ? "true" : "false") +
        ",\"gps_fix\":" + (gps_fix ? "true" : "false") +
        ",\"position_mode\":" + jsonString(position_mode) +
        ",\"position_ready\":" + (position_ready ? "true" : "false") +
        ",\"entries\":" + std::to_string(entries) +
        ",\"mapped\":" + std::to_string(mapped) +
        ",\"session\":" + jsonString(session) +
        ",\"generation\":" + std::to_string(generation) + ",\"points\":[";
    char row[256];
    for (size_t i = 0; i < points.size(); ++i) {
        const auto& p = points[i];
        if (i) out += ',';
        std::snprintf(row, sizeof row,
            "{\"lat\":%.7f,\"lon\":%.7f,\"value\":%.2f,\"time\":%.3f,\"metric\":\"%s\",\"crc_verified\":%s,\"fingerprint\":",
            p.lat, p.lon, p.value, p.time, p.is_level ? "dBFS" : "SNR dB", p.crc_verified ? "true" : "false");
        out += row;
        out += jsonString(p.fingerprint);
        out += ",\"position_source\":";
        out += jsonString(p.position_source);
        out += '}';
    }
    return out + "]}";
}

void MapServer::serve() {
    while (!stopping_) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(listen_fd_, &fds);
        timeval wait{0, 200000};
        const int ready = ::select(listen_fd_ + 1, &fds, nullptr, nullptr, &wait);
        if (ready <= 0) continue;
        const int client = ::accept(listen_fd_, nullptr, nullptr);
        if (client < 0) continue;
        timeval timeout{1, 0};
        ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
        ::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
        char buf[4096];
        const ssize_t n = ::recv(client, buf, sizeof buf - 1, 0);
        if (n > 0) {
            buf[n] = 0;
            const std::string request(buf, static_cast<size_t>(n));
            if (!acceptedHost(request, port_)) response(client, 403, "text/plain; charset=utf-8", "Forbidden");
            else if (request.rfind("GET /api/points ", 0) == 0)
                response(client, 200, "application/json; charset=utf-8", pointsJson());
            else if (request.rfind("POST /api/reset ", 0) == 0 &&
                     request.find("\r\nX-MDM-Action: reset\r\n") != std::string::npos) {
                resetSimulation();
                response(client, 200, "application/json; charset=utf-8", "{\"reset\":true}");
            }
            else if (request.rfind("POST /api/start ", 0) == 0 &&
                     request.find("\r\nX-MDM-Action: start\r\n") != std::string::npos) {
                startSimulation();
                response(client, 200, "application/json; charset=utf-8", "{\"started\":true}");
            }
            else if (request.rfind("GET / ", 0) == 0)
                response(client, 200, "text/html; charset=utf-8", kMapPage);
            else if (request.rfind("GET ", 0) != 0)
                response(client, 405, "text/plain; charset=utf-8", "GET only");
            else response(client, 404, "text/plain; charset=utf-8", "Not found");
        }
        ::close(client);
    }
}

} // namespace mdm
