#include "traveler.h"

#include <algorithm>
#include <random>
#include <sstream>
#include <iostream>

TravelerEngine::TravelerEngine(OpenSkyClient client) : client_(std::move(client)) {}

bool TravelerEngine::isRecentlyVisited(const TravelerState& st, const std::string& airport) const {
  for (const auto& a : st.recent_airports) {
    if (a == airport) return true;
  }
  return false;
}

void TravelerEngine::pushRecent(TravelerState& st, const std::string& airport) const {
  st.recent_airports.push_back(airport);
  if ((int)st.recent_airports.size() > st.avoid_recent_n) {
    st.recent_airports.erase(st.recent_airports.begin());
  }
}

HopResult TravelerEngine::tick(TravelerState& st, long nowUtc) {
  HopResult out;

  // If it's not time yet, do nothing.
  if (st.next_event_utc > 0 && nowUtc < st.next_event_utc) {
    out.reason = "Not time yet.";
    return out;
  }

  // Initialize sim_time_utc on first run: set traveler time to (now - lag)
  if (st.sim_time_utc == 0) {
    st.sim_time_utc = nowUtc - st.lag_seconds;
  }

  // We will look in a window ending near sim_time_utc + a bit, but OpenSky airport endpoints
  // are historical; simplest: query [sim_time - lookback, sim_time + lookahead]
  long windowEnd = st.sim_time_utc + 12 * 3600; // small lookahead in story time
  long windowBegin = windowEnd - (long)st.lookback_hours * 3600;

  // OpenSky limit: window <= 2 days (172800 seconds). Ensure it.
  if (windowEnd - windowBegin > 172800) {
    windowBegin = windowEnd - 172800;
  }

  std::vector<Flight> flights;
  try {
    flights = client_.getDepartures(st.current_airport, windowBegin, windowEnd);
  } catch (const std::exception& e) {
    out.reason = std::string("OpenSky error: ") + e.what();
    return out;
  }

  // Filter flights departing after sim_time_utc
  std::vector<Flight> candidates;
  candidates.reserve(flights.size());
  for (const auto& f : flights) {
    if (f.estDepartureAirport != st.current_airport) continue;
    if (f.firstSeen < st.sim_time_utc) continue;
    if (f.estArrivalAirport.empty()) continue;
    candidates.push_back(f);
  }

  if (candidates.empty()) {
    // Move story time forward a bit and try again next tick.
    // This is our “keep searching” mechanism without burning API calls too often.
    st.sim_time_utc += 3 * 3600;          // advance story time 3 hours
    st.next_event_utc = nowUtc + 5 * 60;  // re-check in 5 minutes
    out.reason = "No candidates in window; advanced story time + scheduled recheck.";
    return out;
  }

  // Find earliest departure time among candidates
  long minDepart = candidates[0].firstSeen;
  for (const auto& f : candidates) minDepart = std::min(minDepart, f.firstSeen);

  // Collect all with that earliest departure time
  std::vector<Flight> earliest;
  for (const auto& f : candidates) {
    if (f.firstSeen == minDepart) earliest.push_back(f);
  }

  // Apply anti-loop preference: prefer destinations not visited recently
  std::vector<Flight> nonLoop;
  for (const auto& f : earliest) {
    if (!isRecentlyVisited(st, f.estArrivalAirport)) nonLoop.push_back(f);
  }

  const std::vector<Flight>& pickFrom = nonLoop.empty() ? earliest : nonLoop;

  // Random tie-break
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<size_t> dist(0, pickFrom.size() - 1);
  Flight chosen = pickFrom[dist(gen)];

  long departUtc = chosen.firstSeen;
  long arriveUtc = (chosen.lastSeen > 0 ? chosen.lastSeen : (departUtc + 2 * 3600));

  // Real-time waiting: compute real time until "arrival"
  long flightDuration = std::max(60L, arriveUtc - departUtc); // at least 60 sec
  st.next_event_utc = nowUtc + flightDuration;

  // Update traveler story state to arrival moment
  st.sim_time_utc = arriveUtc;
  st.current_airport = chosen.estArrivalAirport;

  pushRecent(st, chosen.estArrivalAirport);

  out.didHop = true;
  out.flight = chosen;
  out.depart_utc = departUtc;
  out.arrive_utc = arriveUtc;
  out.reason = nonLoop.empty()
                 ? "Hopped (earliest + random tie-break)."
                 : "Hopped (earliest + random tie-break, avoided recent airport).";
  return out;
}