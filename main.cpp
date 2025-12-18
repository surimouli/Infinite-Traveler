#include "opensky_client.h"
#include "traveler.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <ctime>
#include <cctype>
#include <cstdlib>

static long nowUtc() { return (long)std::time(nullptr); }

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

static long extractJsonHopCount(const std::string& json) {
  return extractJsonLong(json, "hop_count", 0);
}

static TravelerState loadState(const std::string& path, long& hopCount) {
  TravelerState st;
  std::string json = readFile(path);

  st.current_airport = extractJsonString(json, "current_airport", "KCVG");
  st.sim_time_utc    = extractJsonLong(json, "sim_time_utc", 0);
  st.next_event_utc  = extractJsonLong(json, "next_event_utc", 0);
  st.lag_seconds     = extractJsonLong(json, "lag_seconds", 86400);
  st.lookback_hours  = extractJsonInt(json, "lookback_hours", 36);
  st.avoid_recent_n  = extractJsonInt(json, "avoid_recent_n", 10);
  st.recent_airports = extractRecentAirports(json);

  st.personality     = extractJsonString(json, "personality", "chaotic");
  hopCount           = extractJsonHopCount(json);

  return st;
}

static std::string toJson(const TravelerState& st, long hopCount) {
  std::ostringstream o;
  o << "{\n";
  o << "  \"current_airport\": \"" << st.current_airport << "\",\n";
  o << "  \"sim_time_utc\": " << st.sim_time_utc << ",\n";
  o << "  \"next_event_utc\": " << st.next_event_utc << ",\n";
  o << "  \"lag_seconds\": " << st.lag_seconds << ",\n";
  o << "  \"lookback_hours\": " << st.lookback_hours << ",\n";
  o << "  \"avoid_recent_n\": " << st.avoid_recent_n << ",\n";
  o << "  \"hop_count\": " << hopCount << ",\n";
  o << "  \"personality\": \"" << st.personality << "\",\n";
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
                            long loggedAtUtc,
                            const TravelerState& before,
                            const HopResult& hop,
                            const TravelerState& after,
                            long hopNumber) {
  std::ofstream out(path, std::ios::app);
  out << "{";
  out << "\"hop\":" << hopNumber << ",";
  out << "\"logged_at_utc\":" << loggedAtUtc << ",";
  out << "\"from\":\"" << before.current_airport << "\",";
  out << "\"to\":\"" << after.current_airport << "\",";
  out << "\"depart_utc\":" << hop.depart_utc << ",";
  out << "\"arrive_utc\":" << hop.arrive_utc << ",";
  out << "\"icao24\":\"" << hop.flight.icao24 << "\",";
  out << "\"callsign\":\"" << hop.flight.callsign << "\",";
  out << "\"reason\":\"" << hop.reason << "\"";
  out << "}\n";
}

static std::string formatUtc(long t) {
  std::time_t tt = (std::time_t)t;
  std::tm g{};
#if defined(_WIN32)
  gmtime_s(&g, &tt);
#else
  g = *std::gmtime(&tt);
#endif
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M UTC", &g);
  return std::string(buf);
}

static void writeLatestCaption(const std::string& from,
                               const std::string& to,
                               long hopNumber,
                               long departUtc,
                               long arriveUtc,
                               const std::string& callsign,
                               const std::string& personality,
                               const std::string& vibeLine) {
  std::ostringstream c;
  c << "Hop #" << hopNumber << " ✈️\n";
  c << from << " → " << to << "\n";
  if (!callsign.empty()) c << "Flight: " << callsign << "\n";
  c << "Depart: " << formatUtc(departUtc) << "\n";
  c << "Arrive: " << formatUtc(arriveUtc) << "\n\n";
  c << "Personality: " << personality << "\n";
  c << vibeLine << "\n\n";
  c << "#airport #travel #aviation #wanderlust #planespotting\n";
  writeFile("latest_caption.txt", c.str());
}

static void writeLatestPostJson(const TravelerState& before,
                                const TravelerState& after,
                                const HopResult& hop,
                                long hopNumber) {
  std::ostringstream j;
  j << "{\n";
  j << "  \"hop\": " << hopNumber << ",\n";
  j << "  \"from\": \"" << before.current_airport << "\",\n";
  j << "  \"to\": \"" << after.current_airport << "\",\n";
  j << "  \"depart_utc\": " << hop.depart_utc << ",\n";
  j << "  \"arrive_utc\": " << hop.arrive_utc << ",\n";
  j << "  \"icao24\": \"" << hop.flight.icao24 << "\",\n";
  j << "  \"callsign\": \"" << hop.flight.callsign << "\",\n";
  j << "  \"reason\": \"" << hop.reason << "\"\n";
  j << "}\n";
  writeFile("latest_post.json", j.str());
}

int main() {
  long now = nowUtc();

  long hopCount = 0;
  TravelerState st = loadState("state.json", hopCount);

  const char* token = std::getenv("OPENSKY_TOKEN");
  if (!token || std::string(token).empty()) {
    std::cout << "NO HOP: Missing OPENSKY_TOKEN env var.\n";
    return 0;
  }

  OpenSkyClient client(token);
  TravelerEngine engine(client);

  TravelerState before = st;
  HopResult hop = engine.tick(st, now);

  if (hop.didHop) {
    hopCount += 1;
    appendLogNdjson("trip_log.ndjson", now, before, hop, st, hopCount);

    std::string vibe;
    if (st.personality == "chaotic") vibe = "Current mood: unhinged boarding pass energy.";
    else if (st.personality == "budget") vibe = "Current mood: saving money like it’s a sport.";
    else if (st.personality == "scenic") vibe = "Current mood: window seat supremacy.";
    else vibe = "Current mood: gate snacks + main character energy.";

    writeLatestCaption(before.current_airport, st.current_airport, hopCount,
                       hop.depart_utc, hop.arrive_utc, hop.flight.callsign,
                       st.personality, vibe);

    writeLatestPostJson(before, st, hop, hopCount);

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

  writeFile("state.json", toJson(st, hopCount));
  return 0;
}