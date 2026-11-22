#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mdm {

struct MapPoint {
    double lat = 0;
    double lon = 0;
    double value = 0;
    double time = 0;
    bool is_level = false;
    bool crc_verified = false;
    std::string fingerprint;
    std::string position_source = "gps";
};

class MapServer {
public:
    MapServer() = default;
    ~MapServer();
    MapServer(const MapServer&) = delete;
    MapServer& operator=(const MapServer&) = delete;

    bool start(int preferred_port = 8765);
    void stop();
    int port() const { return port_.load(); }
    void setSimulation(bool enabled);
    void resetSimulation();
    void startSimulation();
    bool simulation() const { return simulation_.load(); }
    void add(MapPoint point);
    void setGpsFix(bool valid);
    void setPositionMode(std::string mode, bool static_ready);
    void setCounts(uint64_t entries, uint64_t mapped);
    void setSessionName(std::string name);
    std::string pointsJson() const;

private:
    void serve();
    std::atomic<bool> stopping_{false};
    std::atomic<bool> simulation_{false};
    std::atomic<int> port_{0};
    int listen_fd_ = -1;
    std::thread thread_;
    mutable std::mutex mu_;
    std::vector<MapPoint> points_;
    std::chrono::steady_clock::time_point gps_seen_{};
    bool gps_fix_ = false;
    std::string position_mode_ = "gps";
    bool static_ready_ = false;
    uint64_t entries_ = 0;
    uint64_t mapped_ = 0;
    std::string session_name_;
    std::chrono::steady_clock::time_point demo_started_{};
    uint64_t generation_ = 0;
    bool demo_paused_ = false;
};

} // namespace mdm
