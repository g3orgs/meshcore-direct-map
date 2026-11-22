#include "map_server.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cassert>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

std::string request(int port, const char* path, const char* host, const char* method = "GET", const char* extra = "") {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0);
    const std::string text = std::string(method) + " " + path + " HTTP/1.1\r\nHost: " + host + "\r\n" + extra + "\r\n";
    assert(::send(fd, text.data(), text.size(), 0) == static_cast<ssize_t>(text.size()));
    std::string reply;
    char buf[4096];
    ssize_t n;
    while ((n = ::recv(fd, buf, sizeof buf, 0)) > 0) reply.append(buf, static_cast<size_t>(n));
    ::close(fd);
    return reply;
}

int main() {
    mdm::MapServer server;
    if (!server.start()) return 77; // restricted test sandboxes may deny loopback binds
    const int port = server.port();
    assert(port >= 8765 && port < 8775);
    const std::string host = "127.0.0.1:" + std::to_string(port);
    assert(request(port, "/", host.c_str()).find("MeshCore Direct Map") != std::string::npos);
    assert(request(port, "/api/points", host.c_str()).find("\"simulation\":false") != std::string::npos);
    server.setGpsFix(true);
    server.setCounts(3, 2);
    server.setSessionName("direct-packets-20261122-130000.csv");
    const std::string status = request(port, "/api/points", host.c_str());
    assert(status.find("\"gps_fix\":true") != std::string::npos);
    assert(status.find("\"entries\":3,\"mapped\":2") != std::string::npos);
    assert(status.find("direct-packets-20261122-130000.csv") != std::string::npos);
    server.setPositionMode("manual", true);
    const std::string fixed = request(port, "/api/points", host.c_str());
    assert(fixed.find("\"position_mode\":\"manual\",\"position_ready\":true") != std::string::npos);
    server.setSimulation(true);
    const std::string empty = request(port, "/api/points", host.c_str());
    assert(empty.find("\"simulation\":true") != std::string::npos);
    assert(empty.find("\"points\":[]") != std::string::npos);
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    const std::string demo = request(port, "/api/points", host.c_str());
    assert(demo.find("\"fingerprint\":\"simulated\"") != std::string::npos);
    assert(request(port, "/api/reset", host.c_str(), "POST", "X-MDM-Action: reset\r\n").find("\"reset\":true") != std::string::npos);
    const std::string reset = request(port, "/api/points", host.c_str());
    assert(reset != demo);
    assert(reset.find("\"points\":[]") != std::string::npos);
    std::this_thread::sleep_for(std::chrono::milliseconds(2200));
    assert(request(port, "/api/points", host.c_str()).find("\"points\":[]") != std::string::npos);
    assert(request(port, "/api/start", host.c_str(), "POST", "X-MDM-Action: start\r\n").find("\"started\":true") != std::string::npos);
    assert(request(port, "/api/points", host.c_str()).find("\"points\":[]") != std::string::npos);
    std::this_thread::sleep_for(std::chrono::milliseconds(2200));
    const std::string grown = request(port, "/api/points", host.c_str());
    assert(std::count(grown.begin(), grown.end(), '{') > std::count(reset.begin(), reset.end(), '{'));
    assert(request(port, "/api/points", "example.com").find("403 Forbidden") != std::string::npos);
    server.setSimulation(false);
    mdm::MapPoint point;
    point.lat = 51.33; point.lon = 12.38; point.value = 9.2; point.is_level = false;
    point.position_source = "onair";
    server.add(point);
    assert(request(port, "/api/points", host.c_str()).find("\"metric\":\"SNR dB\"") != std::string::npos);
    assert(request(port, "/api/points", host.c_str()).find("\"position_source\":\"onair\"") != std::string::npos);
    if (std::getenv("MDM_VISUAL_TEST")) {
        server.setSimulation(true);
        std::this_thread::sleep_for(std::chrono::seconds(45));
    }
    server.stop();
    assert(server.start(18865));
    assert(server.port() >= 18865 && server.port() < 18875);
    const std::string custom_host = "127.0.0.1:" + std::to_string(server.port());
    assert(request(server.port(), "/api/points", custom_host.c_str()).find("200 OK") != std::string::npos);
    server.stop();
}
