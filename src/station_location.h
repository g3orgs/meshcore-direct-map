#pragma once

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string>

namespace mdm {

struct StationPosition {
    double lat = 0;
    double lon = 0;
};

inline bool parseCoordinate(const std::string& value, double& out) {
    if (value.empty()) return false;
    char* end = nullptr;
    const double n = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || !std::isfinite(n)) return false;
    while (*end && std::isspace(static_cast<unsigned char>(*end))) ++end;
    if (*end) return false;
    out = n;
    return true;
}

inline bool validPosition(const StationPosition& p) {
    return std::isfinite(p.lat) && std::isfinite(p.lon) &&
           std::abs(p.lat) <= 90 && std::abs(p.lon) <= 180;
}

inline std::string onAirSettingsPath() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return std::string(xdg) + "/onair/settings.conf";
    const char* home = std::getenv("HOME");
    return home && *home ? std::string(home) + "/.config/onair/settings.conf" : std::string();
}

// OnAir 4.3-4.5 have no station-location getter in the plugin ABI. This reads the
// Linux preference file only when the user selects the explicit OnAir fallback.
inline bool readOnAirStationPosition(StationPosition& out) {
    const std::string path = onAirSettingsPath();
    if (path.empty()) return false;
    std::ifstream file(path);
    if (!file) return false;
    StationPosition found;
    bool have_lat = false, have_lon = false;
    bool cleared = false, streamer = false, streamer_hide = true;
    std::string line;
    while (std::getline(file, line)) {
        if (line.rfind("adsbLat=", 0) == 0)
            have_lat = parseCoordinate(line.substr(8), found.lat);
        else if (line.rfind("adsbLon=", 0) == 0)
            have_lon = parseCoordinate(line.substr(8), found.lon);
        else if (line.rfind("adsbLocCleared=", 0) == 0)
            cleared = line.substr(15) == "1";
        else if (line.rfind("streamerMode=", 0) == 0)
            streamer = line.substr(13) == "1";
        else if (line.rfind("streamerHideLocation=", 0) == 0)
            streamer_hide = line.substr(21) != "0";
    }
    if (!have_lat || !have_lon || !validPosition(found) ||
        (found.lat == 0 && found.lon == 0) || cleared || (streamer && streamer_hide))
        return false;
    out = found;
    return true;
}

} // namespace mdm
