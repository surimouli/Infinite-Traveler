#include "opensky_client.h"

#include <curl/curl.h>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <cctype>
#include <utility>

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  size_t total = size * nmemb;
  std::string* s = static_cast<std::string*>(userp);
  s->append(static_cast<char*>(contents), total);
  return total;
}

OpenSkyClient::OpenSkyClient(std::string bearerToken)
  : bearerToken_(std::move(bearerToken)) {}

std::string OpenSkyClient::httpGet(const std::string& url) {
  CURL* curl = curl_easy_init();
  if (!curl) throw std::runtime_error("curl_easy_init failed");

  std::string response;

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

  // Add Bearer token header if provided
  struct curl_slist* headers = nullptr;
  if (!bearerToken_.empty()) {
    std::string auth = "Authorization: Bearer " + bearerToken_;
    headers = curl_slist_append(headers, auth.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  }

  CURLcode res = curl_easy_perform(curl);
  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

  if (headers) curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    std::ostringstream oss;
    oss << "curl_easy_perform failed: " << curl_easy_strerror(res);
    throw std::runtime_error(oss.str());
  }

  // ✅ IMPORTANT FIX:
  // OpenSky sometimes returns 404 with body [] meaning "no data for this window".
  // Treat it as a valid empty response instead of a fatal error.
  if (http_code == 404) {
    return "[]";
  }

  if (http_code != 200) {
    std::ostringstream oss;
    oss << "HTTP " << http_code << " from OpenSky. Response: " << response;
    throw std::runtime_error(oss.str());
  }

  return response;
}

// ---- ultra-light parsing helpers ----
static std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace((unsigned char)s[a])) a++;
  while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
  return s.substr(a, b - a);
}

static std::string extractStringField(const std::string& obj, const std::string& key) {
  std::string pattern = "\"" + key + "\":";
  size_t p = obj.find(pattern);
  if (p == std::string::npos) return "";
  p += pattern.size();
  while (p < obj.size() && std::isspace((unsigned char)obj[p])) p++;
  if (p >= obj.size()) return "";
  if (obj.compare(p, 4, "null") == 0) return "";
  if (obj[p] != '"') return "";
  p++;
  size_t q = obj.find('"', p);
  if (q == std::string::npos) return "";
  return obj.substr(p, q - p);
}

static long extractLongField(const std::string& obj, const std::string& key) {
  std::string pattern = "\"" + key + "\":";
  size_t p = obj.find(pattern);
  if (p == std::string::npos) return 0;
  p += pattern.size();
  while (p < obj.size() && std::isspace((unsigned char)obj[p])) p++;
  if (p >= obj.size()) return 0;
  if (obj.compare(p, 4, "null") == 0) return 0;
  size_t q = p;
  while (q < obj.size() && (std::isdigit((unsigned char)obj[q]) || obj[q] == '-')) q++;
  try {
    return std::stol(obj.substr(p, q - p));
  } catch (...) {
    return 0;
  }
}

std::vector<Flight> OpenSkyClient::getDepartures(const std::string& airportIcao,
                                                 long beginUtc,
                                                 long endUtc) {
  std::ostringstream url;
  url << "https://opensky-network.org/api/flights/departure"
      << "?airport=" << airportIcao
      << "&begin=" << beginUtc
      << "&end=" << endUtc;

  std::string json = httpGet(url.str());

  // Response is a JSON array of objects. We'll split by { ... } blocks.
  std::vector<Flight> flights;
  size_t pos = 0;
  while (true) {
    size_t a = json.find('{', pos);
    if (a == std::string::npos) break;
    size_t b = json.find('}', a);
    if (b == std::string::npos) break;

    std::string obj = json.substr(a, b - a + 1);

    Flight f;
    f.icao24 = extractStringField(obj, "icao24");
    f.callsign = trim(extractStringField(obj, "callsign"));
    f.estDepartureAirport = extractStringField(obj, "estDepartureAirport");
    f.estArrivalAirport = extractStringField(obj, "estArrivalAirport");
    f.firstSeen = extractLongField(obj, "firstSeen");
    f.lastSeen = extractLongField(obj, "lastSeen");

    if (!f.estDepartureAirport.empty() && !f.estArrivalAirport.empty() && f.firstSeen > 0) {
      flights.push_back(std::move(f));
    }

    pos = b + 1;
  }

  return flights;
}  // ✅ MISSING BRACE FIXED HERE