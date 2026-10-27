#pragma once

#include <cpprest/json.h>
#include <cpprest/uri.h>
#include <optional>
#include <stdexcept>
#include <string>

namespace seeder::nmos_sync {

struct SenderQuery {
  std::optional<std::size_t> page;
  std::size_t page_size{50};
  std::string keyword;
  std::string type;
  static SenderQuery parse(const utility::string_t &query);
};

class RegistryUnavailable : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Query URL includes /x-nmos/query/{version}. Fetches all upstream pages before
// applying local filters and pagination; SDP is fetched only for returned items.
class RegistryClient {
public:
  explicit RegistryClient(std::string query_url, int timeout_ms = 3000);
  web::json::value senders(const SenderQuery &query) const;

private:
  web::json::value collection(const std::string &resource) const;
  web::json::value with_sdp(web::json::value sender) const;
  std::string query_url_;
  int timeout_ms_;
};

}
