#include "traveler.h"

#include <algorithm>
#include <random>
#include <cmath>

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

// --- NEW: score function ---
double TravelerEngine::scoreFlight(const TravelerState& st, const Flight& f) const {
  // Features we can compute for free from OpenSky:
  // - duration (proxy for distance/scenic)
  // - novelty (avoid recent airports)
  // - slight randomness
  long dur = 0;
  if (f.lastSeen > 0 && f.firstSeen > 0) dur = std::max(0L, f.lastSeen - f.firstSeen);
  double durationHours = dur / 3600.0;

  bool novel = !isRecentlyVisited(st, f.estArrivalAirport);

  // Base preference: novelty helps everyone a little
  double noveltyScore = novel ? 1.0 : -0.5;

  // Normalize duration into a gentle curve
  // short ~ 0-2h, medium 2-5h, long 5h+
  double shortnessScore = std::exp(-durationHours / 2.0);          // high for short flights
  double longnessScore  = std::min(1.0, durationHours / 6.0);      // climbs up to ~1 by 6h

  // Tiny jitter to avoid deterministic loops
  static thread_local std::mt19937 gen(std::random_device{}());
  std::uniform_real_distribution<double> jitterDist(-0.05, 0.05);
  double jitter = jitterDist(gen);

  // Personality weights
  double wNovel = 0.6;
  double wShort = 0.2;
  double wLong  = 0.2;
  double wJit   = 0.1;

  if (st.personality == "chaotic") {
    // Chaotic: novelty + randomness
    wNovel = 0.8; wShort = 0.1; wLong = 0.1; wJit = 0.25;
  } else if (st.personality == "budget") {
    // Budget: prefer shorter flights + avoid repeats
    wNovel = 0.5; wShort = 0.6; wLong = 0.0; wJit = 0.08;
  } else if (st.personality == "scenic") {
    // Scenic: prefer longer flights (proxy), still likes novelty
    wNovel = 0.5; wShort = 0.0; wLong = 0.7; wJit = 0.08;
  }

  return (wNovel * noveltyScore) + (wShort * shortnessScore) + (wLong * longnessScore) + (wJit * jitter);
}

HopResult TravelerEngine::tick(TravelerState& st, long nowUtc) {
  HopResult out;

  if (st.next_event_utc > 0 && nowUtc < st.next_event_utc) {
    out.reason = "Not time yet.";
    return out;
  }

  if (st.sim_time_utc == 0) {
    st.sim_time_utc = nowUtc - st.lag_seconds;
  }

  long windowEnd = st.sim_time_utc + 12 * 3600;
  long windowBegin = windowEnd - (long)st.lookback_hours * 3600;

  if (windowEnd - windowBegin > 172800) {
    windowBegin = windowEnd - 172800;
  }

  // OpenSky partition rule: max 2 UTC calendar days
  auto dayIndex = [](long t) -> long { return t / 86400; };
  long endDay = dayIndex(windowEnd);
  long beginDay = dayIndex(windowBegin);
  if (endDay - beginDay > 1) {
    windowBegin = (endDay - 1) * 86400;
  }

  std::vector<Flight> flights;
  try {
    flights = client_.getDepartures(st.current_airport, windowBegin, windowEnd);
  } catch (const std::exception& e) {
    out.reason = std::string("OpenSky error: ") + e.what();
    return out;
  }

  // Candidates must depart from current airport, be after sim_time, have arrival, and NOT be a self-hop.
  std::vector<Flight> candidates;
  candidates.reserve(flights.size());
  for (const auto& f : flights) {
    if (f.estDepartureAirport != st.current_airport) continue;
    if (f.firstSeen < st.sim_time_utc) continue;
    if (f.estArrivalAirport.empty()) continue;
    if (f.estArrivalAirport == st.current_airport) continue; // IMPORTANT: prevent KMCO->KMCO
    candidates.push_back(f);
  }

  if (candidates.empty()) {
    st.sim_time_utc += 3 * 3600;
    st.next_event_utc = nowUtc + 5 * 60;
    out.reason = "No candidates in window; advanced story time + scheduled recheck.";
    return out;
  }

  // --- NEW: personality-based scoring instead of earliest-only ---
  // Score each candidate, take best, but keep a little randomness for exploration.
  struct Scored {
    Flight f;
    double score;
  };
  std::vector<Scored> scored;
  scored.reserve(candidates.size());
  for (const auto& f : candidates) {
    scored.push_back({f, scoreFlight(st, f)});
  }

  std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
    return a.score > b.score;
  });

  // Exploration: 10% chance pick randomly among top 5 (keeps it adventurous)
  static thread_local std::mt19937 gen(std::random_device{}());
  std::uniform_real_distribution<double> coin(0.0, 1.0);

  size_t topK = std::min<size_t>(5, scored.size());
  size_t chosenIdx = 0;

  if (coin(gen) < 0.10 && topK > 1) {
    std::uniform_int_distribution<size_t> pick(0, topK - 1);
    chosenIdx = pick(gen);
  }

  Flight chosen = scored[chosenIdx].f;

  long departUtc = chosen.firstSeen;
  long arriveUtc = (chosen.lastSeen > 0 ? chosen.lastSeen : (departUtc + 2 * 3600));

  long flightDuration = std::max(60L, arriveUtc - departUtc);
  st.next_event_utc = nowUtc + flightDuration;

  st.sim_time_utc = arriveUtc;
  st.current_airport = chosen.estArrivalAirport;
  pushRecent(st, chosen.estArrivalAirport);

  out.didHop = true;
  out.flight = chosen;
  out.depart_utc = departUtc;
  out.arrive_utc = arriveUtc;

  out.reason = "Hopped (personality scoring: " + st.personality + ").";
  return out;
}