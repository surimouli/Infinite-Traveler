#pragma once
#include <string>
#include <vector>

struct Flight {
  std::string icao24;
  std::string callsign;
  std::string estDepartureAirport;
  std::string estArrivalAirport;
  long firstSeen = 0; // unix seconds UTC
  long lastSeen  = 0; // unix seconds UTC
};

class OpenSkyClient {
 public:
  explicit OpenSkyClient(std::string bearerToken = "");

  // Fetch departures from an airport in [begin,end] unix seconds (UTC).
  std::vector<Flight> getDepartures(const std::string& airportIcao,
                                   long beginUtc,
                                   long endUtc);

 private:
  std::string bearerToken_;
  std::string httpGet(const std::string& url);
};