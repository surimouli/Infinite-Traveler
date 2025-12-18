#pragma once
#include "opensky_client.h"
#include <string>
#include <vector>

struct TravelerState {
  std::string current_airport;
  long sim_time_utc = 0;
  long next_event_utc = 0;
  long lag_seconds = 86400;
  int lookback_hours = 36;
  int avoid_recent_n = 10;

  std::string personality = "chaotic";

  std::vector<std::string> recent_airports;
};

struct HopResult {
  bool didHop = false;
  Flight flight;
  long depart_utc = 0;
  long arrive_utc = 0;
  std::string reason;
};

class TravelerEngine {
 public:
  explicit TravelerEngine(OpenSkyClient client);
  HopResult tick(TravelerState& st, long nowUtc);

 private:
  OpenSkyClient client_;
  bool isRecentlyVisited(const TravelerState& st, const std::string& airport) const;
  void pushRecent(TravelerState& st, const std::string& airport) const;

  double scoreFlight(const TravelerState& st, const Flight& f) const;
};