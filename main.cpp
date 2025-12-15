#include "opensky_client.h"
#include "traveler.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <ctime>

// --- Tiny JSON-ish state load/save (simple, no external dependency) ---
// State file is small. This parser is intentionally lightweight.

static long nowUtc() {
  return (long)std::time(nullptr);
}

static std::string readFile(const std::string& path) {
  std::ifstream in(path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

static void writeFile(const std::string& path, const std::string& data) {
  std::ofstream out(path, std::ios::trunc);
  out << data;
}

static std::string extractJsonString(const std::string& json, const std::string& key, const std::string& def="") {
  std::string pattern = "\"" + key + "\":";
  auto p = json.find(pattern);
  if (p == std::string::npos) return def;
  p += pattern.size();
  while (p < json.size() && std::isspace((unsigned char)json[p])) p++;
  if (p >= json.size()) return def;
  if (json.compare(p, 4, "null") == 0) return def;
  if (json[p] != '"') return def;
  p++;
  auto q = json.find('"', p);
  if (q == std::string::npos) return def;
  return json.substr(p, q - p);
}

static long extractJsonLong(const std::string& json, const std::string& key, long def=0) {
  std::string pattern = "\"" + key + "\":";
  auto p = json.find(pattern);
  if (p == std::string::npos) return def;
  p += pattern.size();
  while (p < json.size() && std::isspace((unsigned char)json[p])) p++;
  if (p >= json.size()) return def;
  if (json.compare(p, 4, "null") == 0) return def;
  size_t q = p;
  while (q < json.size() && (std::isdigit((unsigned char)json[q]) || json[q] == '-')) q++;
  try { return std::stol(json.substr(p, q - p)); } catch (...) { return def; }
}

static int extractJsonInt(const std::string& json, const std::string& key, int def=0) {
  return (int)extractJsonLong(json, key, def);
}

static std::vector<std::string> extractRecentAirports(const std::string& json) {
  std::vector<std::string> out;
  std::string key = "\"recent_airports\":";
  auto p = json.find(key);
  if (p == std::string::npos) return out;
  p = json.find('[', p);
  auto q = json.find(']', p);
  if (p == std::string::npos || q == std::string::npos || q <= p) return out;
  std::string arr = json.substr(p + 1, q - p - 1);

  // Split by quotes
  size_t i = 0;
  while (true) {
    auto a = arr.find('"', i);
    if (a == std::string::npos) break;
    auto b = arr.find('"', a + 1);
    if (b == std::string::npos) break;
    out.push_back(arr.substr(a + 1, b - a - 1));
    i = b + 1;
  }
  return out;
}

static TravelerState loadState(const std::string& path) {
  TravelerState st;
  std::string json = readFile(path);

  st.current_airport = extractJsonString(json, "current_airport", "KCVG");
  st.sim_time_utc    = extractJsonLong(json, "sim_time_utc", 0);
  st.next_event_utc  = extractJsonLong(json, "next_event_utc", 0);
  st.lag_seconds     = extractJsonLong(json, "lag_seconds", 86400);
  st.lookback_hours  = extractJsonInt(json, "lookback_hours", 36);
  st.avoid_recent_n  = extractJsonInt(json, "avoid_recent_n", 10);
  st.recent_airports = extractRecentAirports(json);

  return st;
}

static std::string toJson(const TravelerState& st) {
  std::ostringstream o;
  o << "{\n";
  o << "  \"current_airport\": \"" << st.current_airport << "\",\n";
  o << "  \"sim_time_utc\": " << st.sim_time_utc << ",\n";
  o << "  \"next_event_utc\": " << st.next_event_utc << ",\n";
  o << "  \"lag_seconds\": " << st.lag_seconds << ",\n";
  o << "  \"lookback_hours\": " << st.lookback_hours << ",\n";
  o << "  \"avoid_recent_n\": " << st.avoid_recent_n << ",\n";
  o << "  \"recent_airports\": [";
  for (size_t i = 0; i < st.recent_airports.size(); i++) {
    o << "\"" << st.recent_airports[i] << "\"";
    if (i + 1 < st.recent_airports.size()) o << ", ";
  }
  o << "]\n";
  o << "}\n";
  return o.str();
}

static void appendLogNdjson(const std::string& path,
                            long nowUtc,
                            const TravelerState& before,
                            const HopResult& hop,
                            const TravelerState& after) {
  std::ofstream out(path, std::ios::app);
  out << "{";
  out << "\"logged_at_utc\":" << nowUtc << ",";
  out << "\"from\":\"" << before.current_airport << "\",";
  out << "\"to\":\"" << after.current_airport << "\",";
  out << "\"depart_utc\":" << hop.depart_utc << ",";
  out << "\"arrive_utc\":" << hop.arrive_utc << ",";
  out << "\"icao24\":\"" << hop.flight.icao24 << "\",";
  out << "\"callsign\":\"" << hop.flight.callsign << "\",";
  out << "\"reason\":\"" << hop.reason << "\"";
  out << "}\n";
}

int main() {
  long now = nowUtc();

  TravelerState st = loadState("state.json");

  // Optional OpenSky basic auth from environment variables
  // Add as GitHub secrets: OPENSKY_USER / OPENSKY_PASS if you want.
  const char* user = std::getenv("OPENSKY_USER");
  const char* pass = std::getenv("OPENSKY_PASS");
  //OpenSkyClient client(user ? user : "", pass ? pass : "");

  OpenSkyClient client("", "");

  TravelerEngine engine(client);

  TravelerState before = st;
  HopResult hop = engine.tick(st, now);

  // Save state every run (even if no hop) so next_event advances persistently
  writeFile("state.json", toJson(st));

  if (hop.didHop) {
    appendLogNdjson("trip_log.ndjson", now, before, hop, st);
    std::cout << "HOP: " << before.current_airport << " -> " << st.current_airport
              << " depart=" << hop.depart_utc
              << " arrive=" << hop.arrive_utc
              << " next_event_utc=" << st.next_event_utc
              << "\n";
  } else {
    std::cout << "NO HOP: " << hop.reason
              << " next_event_utc=" << st.next_event_utc
              << " sim_time_utc=" << st.sim_time_utc
              << "\n";
  }

  return 0;
}