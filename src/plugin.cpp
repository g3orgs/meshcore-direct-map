// SPDX-License-Identifier: MIT
#include "onair_plugin.h"
#include "meshcore_frame.h"
#include "map_server.h"
#include "station_location.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <memory>
#include <string>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;

struct GpsFix {
    double lat = 0, lon = 0;
    Clock::time_point seen{};
    bool valid = false;
};

struct State {
    const OnAirHostApi* host;
    std::atomic<bool> stop{false};
    std::thread gps_thread;
    std::mutex gps_mu;
    GpsFix gps;
    FILE* csv = nullptr;
    uint64_t candidates = 0;
    uint64_t mapped = 0;
    bool warned_missing_metrics = false;
    mdm::MapServer map;
    std::string data_dir;
    std::string session_stamp;
    std::string session_name;
    bool session_error_reported = false;
    std::mutex position_mu;
    std::string position_status;
};

struct ReceiverPosition {
    mdm::StationPosition coords;
    const char* source = "";
    double gps_age = 0;
    bool valid = false;
};

bool staticPositionSelected(const OnAirHostApi* host) {
    return host->get_setting_flag(host->ctx, "use_static_position", 0) != 0;
}

int staticPositionSource(const OnAirHostApi* host) {
    const int value = static_cast<int>(host->get_setting_number(host->ctx, "static_position_source", 0));
    return value == 1 ? 1 : 0;
}

bool manualPosition(const OnAirHostApi* host, mdm::StationPosition& out) {
    char lat[64] = "", lon[64] = "";
    if (host->get_setting_text(host->ctx, "static_lat", lat, sizeof lat) < 0 ||
        host->get_setting_text(host->ctx, "static_lon", lon, sizeof lon) < 0)
        return false;
    return mdm::parseCoordinate(lat, out.lat) && mdm::parseCoordinate(lon, out.lon) &&
           mdm::validPosition(out);
}

bool selectedStaticPosition(const OnAirHostApi* host, mdm::StationPosition& out) {
    return staticPositionSource(host) == 0 ? mdm::readOnAirStationPosition(out) : manualPosition(host, out);
}

void refreshPositionStatus(State* s) {
    const bool fixed = staticPositionSelected(s->host);
    const bool onair = staticPositionSource(s->host) == 0;
    mdm::StationPosition position;
    const bool ready = fixed && selectedStaticPosition(s->host, position);
    s->map.setPositionMode(fixed ? (onair ? "onair" : "manual") : "gps", ready);
    const std::string message = !fixed ? "GPS via gpsd selected" :
        ready ? (onair ? "Fixed OnAir position selected" : "Fixed manual position selected") :
        (onair ? "OnAir station position unavailable" : "Manual position missing or invalid");
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(s->position_mu);
        changed = message != s->position_status;
        if (changed) s->position_status = message;
    }
    if (changed) {
        s->host->status(s->host->ctx, "position", message.c_str(),
                        fixed ? (ready ? ONAIR_LAMP_OK : ONAIR_LAMP_WARN) : ONAIR_LAMP_OFF);
        s->host->log(s->host->ctx, fixed && !ready ? ONAIR_LOG_WARN : ONAIR_LOG_INFO, message.c_str());
    }
}

std::string sessionStamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    if (!localtime_r(&now, &local)) return "unknown-time";
    char stamp[24]{};
    if (!std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &local)) return "unknown-time";
    return stamp;
}

bool ensureSessionFile(State* s) {
    if (s->csv) return true;
    static constexpr char header[] =
        "event_unix_s,freq_hz,snr_db,level_dbfs,crc_present,crc_ok,receiver_lat,receiver_lon,position_age_s,position_source,fingerprint\n";
    for (int suffix = 0; suffix < 1000; ++suffix) {
        const std::string name = "direct-packets-" + s->session_stamp +
            (suffix ? "-" + std::to_string(suffix) : "") + ".csv";
        const std::string path = s->data_dir + "/" + name;
        const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
        if (fd < 0) {
            if (errno == EEXIST) continue;
            break;
        }
        FILE* csv = ::fdopen(fd, "w");
        if (!csv) { ::close(fd); ::unlink(path.c_str()); break; }
        if (std::fputs(header, csv) < 0 || std::fflush(csv) != 0) {
            std::fclose(csv);
            ::unlink(path.c_str());
            break;
        }
        s->csv = csv;
        s->session_name = name;
        s->map.setSessionName(name);
        s->host->log(s->host->ctx, ONAIR_LOG_INFO, ("Writing to " + path).c_str());
        return true;
    }
    if (!s->session_error_reported) {
        s->session_error_reported = true;
        s->host->log(s->host->ctx, ONAIR_LOG_ERROR, "Cannot create a new session CSV");
    }
    return false;
}

int configuredPort(const OnAirHostApi* host) {
    char text[16] = "8765";
    host->get_setting_text(host->ctx, "map_port", text, sizeof text);
    char* end = nullptr;
    const long port = std::strtol(text, &end, 10);
    return end != text && *end == 0 && port >= 1024 && port <= 65535 ? static_cast<int>(port) : 8765;
}

void writeMapLauncher(const std::string& dir, int port) {
    const std::string url = "http://127.0.0.1:" + std::to_string(port) + "/";
    const std::string path = dir + "/map-open.html";
    if (FILE* f = std::fopen(path.c_str(), "w")) {
        std::fprintf(f, "<!doctype html><html lang=\"de\"><meta charset=\"utf-8\">"
                        "<meta http-equiv=\"refresh\" content=\"0;url=%s\">"
                        "<title>MeshCore Direct Map öffnen</title><p><a href=\"%s\">Karte öffnen</a></p></html>",
                     url.c_str(), url.c_str());
        std::fclose(f);
    }
}

bool jsonNumber(const std::string& line, const char* key, double& value) {
    std::string needle = std::string("\"") + key + "\"";
    size_t p = line.find(needle);
    if (p == std::string::npos) return false;
    p = line.find(':', p + needle.size());
    if (p == std::string::npos) return false;
    char* end = nullptr;
    value = std::strtod(line.c_str() + p + 1, &end);
    return end != line.c_str() + p + 1 && std::isfinite(value);
}

void applyGpsLine(State* s, const std::string& line) {
    if (line.find("\"class\":\"TPV\"") == std::string::npos) return;
    double mode = 0, lat = 0, lon = 0;
    const bool good = jsonNumber(line, "mode", mode) && mode >= 2 &&
                      jsonNumber(line, "lat", lat) && jsonNumber(line, "lon", lon) &&
                      std::abs(lat) <= 90 && std::abs(lon) <= 180;
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(s->gps_mu);
        changed = s->gps.valid != good;
        s->gps.valid = good;
        if (good) {
            s->gps.lat = lat;
            s->gps.lon = lon;
            s->gps.seen = Clock::now();
        }
    }
    s->map.setGpsFix(good);
    if (changed) {
        s->host->log(s->host->ctx, ONAIR_LOG_INFO, good ? "GPS fix acquired" : "GPS fix lost");
        s->host->status(s->host->ctx, "gps", good ? "GPS fix available" : "No GPS fix",
                        good ? ONAIR_LAMP_OK : ONAIR_LAMP_WARN);
    }
}

void expireGps(State* s) {
    bool expired = false;
    {
        std::lock_guard<std::mutex> lock(s->gps_mu);
        if (s->gps.valid && Clock::now() - s->gps.seen > std::chrono::seconds(10)) {
            s->gps.valid = false;
            expired = true;
        }
    }
    if (expired) {
        s->map.setGpsFix(false);
        s->host->log(s->host->ctx, ONAIR_LOG_INFO, "GPS fix expired");
        s->host->status(s->host->ctx, "gps", "No GPS fix", ONAIR_LAMP_WARN);
    }
}

void gpsLoop(State* s) {
    while (!s->stop.load()) {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd >= 0) {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(2947);
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0) {
                timeval timeout{0, 300000};
                ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
                static constexpr char watch[] = "?WATCH={\"enable\":true,\"json\":true};\n";
                ::send(fd, watch, sizeof watch - 1, MSG_NOSIGNAL);
                std::string pending;
                char buf[2048];
                while (!s->stop.load()) {
                    const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
                    if (n == 0) break;
                    if (n < 0) { expireGps(s); continue; } // receive timeout; allows prompt shutdown
                    pending.append(buf, static_cast<size_t>(n));
                    if (pending.size() > 16384) pending.clear();
                    size_t pos;
                    while ((pos = pending.find('\n')) != std::string::npos) {
                        applyGpsLine(s, pending.substr(0, pos));
                        pending.erase(0, pos + 1);
                    }
                }
            }
            ::close(fd);
        }
        expireGps(s);
        for (int i = 0; i < 5 && !s->stop.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

const OnAirField* field(const OnAirEvent* e, const char* key) {
    if (!e || !e->fields) return nullptr;
    for (uint32_t i = 0; i < e->n_fields && i < 32; ++i)
        if (e->fields[i].key && std::strcmp(e->fields[i].key, key) == 0)
            return &e->fields[i];
    return nullptr;
}

void* create(const OnAirHostApi* host, const char*) {
    if (!host || !host->data_dir || !host->log) return nullptr;
    try {
        char dir[1024]{};
        if (host->data_dir(host->ctx, dir, sizeof dir) != 0) {
            host->log(host->ctx, ONAIR_LOG_ERROR, "Filesystem permission is required");
            return nullptr;
        }
        auto s = std::make_unique<State>();
        s->host = host;
        s->data_dir = dir;
        s->session_stamp = sessionStamp();
        s->map.setSimulation(host->get_setting_flag(host->ctx, "simulate", 0) != 0);
        refreshPositionStatus(s.get());
        const int requested_port = configuredPort(host);
        if (s->map.start(requested_port)) {
            const std::string url = "Map server running: http://127.0.0.1:" + std::to_string(s->map.port()) + "/";
            host->log(host->ctx, ONAIR_LOG_INFO, url.c_str());
            writeMapLauncher(s->data_dir, s->map.port());
            host->status(host->ctx, "map_server", url.c_str(), ONAIR_LAMP_OK);
        } else {
            const std::string message = "Local map server could not start (requested port " +
                std::to_string(requested_port) + " and next available candidates)";
            host->log(host->ctx, ONAIR_LOG_ERROR, message.c_str());
        }
        host->log(host->ctx, ONAIR_LOG_INFO, "New session: CSV is created with the first direct packet");
        host->log(host->ctx, ONAIR_LOG_INFO, "GPS fix: no; session entries: 0; map points: 0");
        host->status(host->ctx, "gps", "No GPS fix", ONAIR_LAMP_WARN);
        host->status(host->ctx, "direct", "0 session entries; 0 mapped", ONAIR_LAMP_WARN);
        s->gps_thread = std::thread(gpsLoop, s.get());
        return s.release();
    } catch (...) {
        host->log(host->ctx, ONAIR_LOG_ERROR, "Plugin initialization failed");
        return nullptr;
    }
}

void destroy(void* p) {
    auto* s = static_cast<State*>(p);
    if (!s) return;
    s->stop = true;
    s->map.stop();
    if (s->gps_thread.joinable()) s->gps_thread.join();
    if (s->csv) std::fclose(s->csv);
    delete s;
}

void onEvent(void* p, const OnAirEvent* e) {
    auto* s = static_cast<State*>(p);
    if (!s || !e || !e->type || std::strcmp(e->type, "lora.packet") != 0) return;
    try {
        const auto* kind = field(e, "kind");
        const auto* raw = field(e, "hex");
        if (!kind || !kind->is_text || !kind->s || std::strcmp(kind->s, "meshcore") != 0 ||
            !raw || !raw->is_text || !raw->s) return;

        mdm::Frame frame;
        if (!mdm::parseFrame(raw->s, frame)) return;
        char setting[16] = "01";
        s->host->get_setting_text(s->host->ctx, "channel_hash", setting, sizeof setting);
        if (std::strlen(setting) != 2 || mdm::hexValue(setting[0]) < 0 || mdm::hexValue(setting[1]) < 0)
            return;
        const uint8_t channel = static_cast<uint8_t>((mdm::hexValue(setting[0]) << 4) | mdm::hexValue(setting[1]));
        if (!mdm::isDirectGroup(frame, channel)) return;

        const OnAirField* snr = field(e, "snr_db");
        const OnAirField* level = field(e, "level_dbfs");
        const OnAirField* crc_present = field(e, "crc_present");
        const OnAirField* crc_ok = field(e, "crc_ok");
        const bool has_crc = crc_present && crc_ok && !crc_present->is_text && !crc_ok->is_text;
        if (has_crc && crc_present->d != 0 && crc_ok->d == 0) return;
        const bool has_level = level && !level->is_text && std::isfinite(level->d);
        if ((!has_crc || !has_level) && !s->warned_missing_metrics) {
            s->warned_missing_metrics = true;
            s->host->log(s->host->ctx, ONAIR_LOG_WARN,
                "OnAir event lacks CRC or level_dbfs; using SNR as provisional map metric when GPS is available");
        }

        ReceiverPosition position;
        if (staticPositionSelected(s->host)) {
            position.valid = selectedStaticPosition(s->host, position.coords);
            position.source = staticPositionSource(s->host) == 0 ? "onair" : "manual";
            refreshPositionStatus(s);
        } else {
            GpsFix fix;
            {
                std::lock_guard<std::mutex> lock(s->gps_mu);
                fix = s->gps;
            }
            position.gps_age = fix.valid ? std::chrono::duration<double>(Clock::now() - fix.seen).count() : 0;
            position.valid = fix.valid && position.gps_age <= 10;
            position.coords = {fix.lat, fix.lon};
            position.source = "gps";
        }
        if (!ensureSessionFile(s)) return;
        std::fprintf(s->csv, "%.3f,%.0f,", e->time, e->freq_hz);
        if (snr && !snr->is_text && std::isfinite(snr->d)) std::fprintf(s->csv, "%.3f", snr->d);
        std::fputc(',', s->csv);
        if (has_level) std::fprintf(s->csv, "%.3f", level->d);
        std::fputc(',', s->csv);
        if (has_crc) std::fprintf(s->csv, "%d,%d", crc_present->d != 0, crc_ok->d != 0);
        else std::fputc(',', s->csv);
        std::fputc(',', s->csv);
        if (position.valid) {
            std::fprintf(s->csv, "%.7f,%.7f,", position.coords.lat, position.coords.lon);
            if (std::strcmp(position.source, "gps") == 0)
                std::fprintf(s->csv, "%.2f", position.gps_age);
            std::fprintf(s->csv, ",%s", position.source);
        } else std::fputs(",,,", s->csv);
        std::fprintf(s->csv, ",%s\n", frame.fingerprint.c_str());
        if (std::fflush(s->csv) != 0 || std::ferror(s->csv)) {
            s->host->log(s->host->ctx, ONAIR_LOG_ERROR, "Could not write session CSV");
            return;
        }
        ++s->candidates;

        const bool has_snr = snr && !snr->is_text && std::isfinite(snr->d);
        if (position.valid && (has_level || has_snr)) {
            mdm::MapPoint point;
            point.lat = position.coords.lat;
            point.lon = position.coords.lon;
            point.value = has_level ? level->d : snr->d;
            point.time = e->time;
            point.is_level = has_level;
            point.crc_verified = has_crc && crc_present->d != 0 && crc_ok->d != 0;
            point.fingerprint = frame.fingerprint;
            point.position_source = position.source;
            s->map.add(std::move(point));
            char id[48], label[96];
            std::snprintf(id, sizeof id, "direct-%llu", static_cast<unsigned long long>(s->candidates));
            std::snprintf(label, sizeof label, "Direct candidate 0x%02X %.1f %s", channel,
                          has_level ? level->d : snr->d, has_level ? "dBFS" : "SNR dB");
            s->host->map_point(s->host->ctx, id, label, position.coords.lat, position.coords.lon);
            ++s->mapped;
        }
        s->map.setCounts(s->candidates, s->mapped);
        char status[128];
        std::snprintf(status, sizeof status, "%llu session entries; %llu mapped",
            static_cast<unsigned long long>(s->candidates), static_cast<unsigned long long>(s->mapped));
        s->host->status(s->host->ctx, "direct", status, s->mapped ? ONAIR_LAMP_OK : ONAIR_LAMP_WARN);
        s->host->log(s->host->ctx, ONAIR_LOG_INFO, status);
    } catch (...) {
        s->host->log(s->host->ctx, ONAIR_LOG_ERROR, "Failed to process lora.packet");
    }
}

void settingChanged(void* p, const char* key) {
    auto* s = static_cast<State*>(p);
    if (s && key && std::strcmp(key, "simulate") == 0)
        s->map.setSimulation(s->host->get_setting_flag(s->host->ctx, "simulate", 0) != 0);
    if (s && key && (std::strcmp(key, "use_static_position") == 0 ||
                     std::strcmp(key, "static_position_source") == 0 ||
                     std::strcmp(key, "static_lat") == 0 ||
                     std::strcmp(key, "static_lon") == 0))
        refreshPositionStatus(s);
}

const OnAirPluginVTable kVt = {
    sizeof(OnAirPluginVTable), ONAIR_PLUGIN_ABI_VERSION, ONAIR_CAP_EVENTS | ONAIR_CAP_SETTINGS,
    "meshcore-direct-map", create, destroy, nullptr, nullptr, onEvent, nullptr, settingChanged
};
} // namespace

extern "C" ONAIR_EXPORT const OnAirPluginVTable* onair_plugin_entry(uint32_t host_abi) {
    return host_abi >= ONAIR_PLUGIN_ABI_VERSION ? &kVt : nullptr;
}
