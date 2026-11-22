#include "onair_plugin.h"
#include "station_location.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

extern "C" const OnAirPluginVTable* onair_plugin_entry(uint32_t);

namespace {
std::string dir;
std::string map_url;
bool simulate_setting = false;
bool use_static_position = false;
int static_source = 0;
std::string manual_lat;
std::string manual_lon;
int mapped_points = 0;
double mapped_lat = 0, mapped_lon = 0;
int dataDir(void*, char* out, uint32_t cap) {
    if (dir.size() + 1 > cap) return -1;
    std::memcpy(out, dir.c_str(), dir.size() + 1);
    return 0;
}
void log(void*, int32_t, const char* message) {
    const std::string line = message ? message : "";
    const std::string prefix = "Map server running: ";
    if (line.rfind(prefix, 0) == 0) map_url = line.substr(prefix.size());
}
int setting(void*, const char* key, char* out, uint32_t cap) {
    const char* value = std::strcmp(key, "map_port") == 0 ? "18865" :
        std::strcmp(key, "static_lat") == 0 ? manual_lat.c_str() :
        std::strcmp(key, "static_lon") == 0 ? manual_lon.c_str() : "01";
    if (std::strlen(value) + 1 > cap) return -1;
    std::memcpy(out, value, std::strlen(value) + 1);
    return 0;
}
double settingNumber(void*, const char* key, double def) {
    return std::strcmp(key, "static_position_source") == 0 ? static_source : def;
}
int settingFlag(void*, const char* key, int32_t def) {
    if (std::strcmp(key, "simulate") == 0) return simulate_setting;
    if (std::strcmp(key, "use_static_position") == 0) return use_static_position;
    return def;
}
int status(void*, const char*, const char*, int32_t) { return 0; }
int mapPoint(void*, const char*, const char*, double lat, double lon) {
    ++mapped_points; mapped_lat = lat; mapped_lon = lon; return 0;
}
std::vector<std::filesystem::path> sessions() {
    std::vector<std::filesystem::path> found;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("direct-packets-", 0) == 0 && entry.path().extension() == ".csv")
            found.push_back(entry.path());
    }
    std::sort(found.begin(), found.end());
    return found;
}
void event(const OnAirPluginVTable* vt, void* state, const char* hex, double crc) {
    OnAirField fields[] = {
        {"kind", 1, 0, "meshcore"}, {"hex", 1, 0, hex},
        {"snr_db", 0, 12, nullptr}, {"level_dbfs", 0, -44, nullptr},
        {"crc_present", 0, 1, nullptr}, {"crc_ok", 0, crc, nullptr}
    };
    OnAirEvent e{sizeof(OnAirEvent), "lora.packet", "LoRa", 1000, 869618000, 6, fields};
    vt->on_event(state, &e);
}
}

int main() {
    char tmpl[] = "/tmp/mdm-test-XXXXXX";
    const char* tmp = mkdtemp(tmpl);
    assert(tmp);
    dir = tmp;
    { std::ofstream legacy(dir + "/direct-packets.csv"); legacy << "legacy file stays untouched\n"; }
    OnAirHostApi host{};
    host.struct_size = sizeof host;
    host.abi_version = ONAIR_PLUGIN_ABI_VERSION;
    host.log = log;
    host.get_setting_text = setting;
    host.get_setting_number = settingNumber;
    host.get_setting_flag = settingFlag;
    host.status = status;
    host.map_point = mapPoint;
    host.data_dir = dataDir;
    const auto* vt = onair_plugin_entry(ONAIR_PLUGIN_ABI_VERSION);
    assert(vt && std::strcmp(vt->id, "meshcore-direct-map") == 0);
    void* state = vt->create(&host, "");
    assert(state);
    assert(sessions().empty()); // a session file is created only with a real candidate
    if (!map_url.empty()) {
        const std::string prefix = "http://127.0.0.1:";
        assert(map_url.rfind(prefix, 0) == 0);
        const int port = std::stoi(map_url.substr(prefix.size()));
        assert(port >= 18865 && port < 18875);
        std::ifstream launcher(dir + "/map-open.html");
        std::string html((std::istreambuf_iterator<char>(launcher)), std::istreambuf_iterator<char>());
        assert(html.find("href=\"" + map_url + "\"") != std::string::npos);
    }
    event(vt, state, "15011101aabbcc", 1); // relayed
    event(vt, state, "150001aabbcc", 0);  // bad CRC
    event(vt, state, "150001aabbcc", 1);  // direct candidate
    vt->destroy(state);
    auto files = sessions();
    assert(files.size() == 1);
    std::ifstream csv(files[0]);
    std::string line;
    int rows = 0;
    while (std::getline(csv, line)) {
        assert(std::count(line.begin(), line.end(), ',') == 10);
        ++rows;
    }
    assert(rows == 2); // header plus only the valid direct candidate
    state = vt->create(&host, "");
    assert(state);
    event(vt, state, "150001aabbcc", 1);
    vt->destroy(state);
    files = sessions();
    assert(files.size() == 2); // a second activation never appends to the first file

    simulate_setting = true;
    state = vt->create(&host, "");
    assert(state);
    vt->destroy(state);
    assert(sessions().size() == 2); // fictional points never create a measurement file
    simulate_setting = false;

    const auto config = std::filesystem::path(dir) / "onair";
    std::filesystem::create_directories(config);
    assert(setenv("XDG_CONFIG_HOME", dir.c_str(), 1) == 0);
    {
        std::ofstream prefs(config / "settings.conf");
        prefs << "adsbLat=51.324\nadsbLon=12.57\n";
    }
    use_static_position = true;
    state = vt->create(&host, "");
    assert(state);
    const int before_onair = mapped_points;
    event(vt, state, "150001aabbcc", 1);
    vt->destroy(state);
    assert(mapped_points == before_onair + 1 && mapped_lat == 51.324 && mapped_lon == 12.57);
    files = sessions();
    assert(files.size() == 3);
    {
        bool found = false;
        for (const auto& file : files) {
            std::ifstream csv(file);
            std::string header, row;
            std::getline(csv, header); std::getline(csv, row);
            assert(header.find("receiver_lat,receiver_lon,position_age_s,position_source") != std::string::npos);
            found |= row.find("51.3240000,12.5700000,,onair,") != std::string::npos;
        }
        assert(found);
    }

    { std::ofstream prefs(config / "settings.conf");
      prefs << "adsbLat=51.324\nadsbLon=12.57\nadsbLocCleared=1\n"; }
    state = vt->create(&host, "");
    assert(state);
    const int before_cleared = mapped_points;
    event(vt, state, "150001aabbcc", 1);
    vt->destroy(state);
    assert(mapped_points == before_cleared);

    { std::ofstream prefs(config / "settings.conf");
      prefs << "adsbLat=51.324\nadsbLon=12.57\nstreamerMode=1\n"; }
    mdm::StationPosition hidden;
    assert(!mdm::readOnAirStationPosition(hidden));

    static_source = 1;
    manual_lat = "51.5";
    manual_lon = "12.25";
    state = vt->create(&host, "");
    assert(state);
    const int before_manual = mapped_points;
    event(vt, state, "150001aabbcc", 1);
    vt->destroy(state);
    assert(mapped_points == before_manual + 1 && mapped_lat == 51.5 && mapped_lon == 12.25);
    files = sessions();
    assert(files.size() == 5);
    {
        bool found = false;
        for (const auto& file : files) {
            std::ifstream csv(file);
            std::string header, row;
            std::getline(csv, header); std::getline(csv, row);
            found |= row.find("51.5000000,12.2500000,,manual,") != std::string::npos;
        }
        assert(found);
    }

    use_static_position = false;
    state = vt->create(&host, "");
    assert(state);
    const int before_gps_only = mapped_points;
    event(vt, state, "150001aabbcc", 1);
    vt->destroy(state);
    assert(mapped_points == before_gps_only); // OnAir position is ignored without the checkbox
    std::ifstream legacy(dir + "/direct-packets.csv");
    std::string old;
    std::getline(legacy, old);
    assert(old == "legacy file stays untouched");
    std::filesystem::remove_all(dir);
}
