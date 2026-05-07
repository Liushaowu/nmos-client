#include "snapshot_client.h"

#include <cpprest/http_client.h>

#include <stdexcept>

namespace seeder::nmos_sync
{

  SnapshotClient::SnapshotClient(std::string snapshot_url, int timeout_ms)
      : snapshot_url_(std::move(snapshot_url)), timeout_ms_(timeout_ms) {}

  SnapshotDto SnapshotClient::fetch_snapshot() const
  {
    web::http::client::http_client_config config;
    config.set_timeout(std::chrono::milliseconds(timeout_ms_));
    web::http::client::http_client client(
        utility::conversions::to_string_t(snapshot_url_), config);

    auto response = client.request(web::http::methods::GET).get();
    if (response.status_code() != web::http::status_codes::OK)
    {
      throw std::runtime_error("snapshot fetch failed with status: " +
                               std::to_string(response.status_code()));
    }
    // std::string response_body = response.extract_string().get();
    auto json = response.extract_json().get();
    std::string response_body  = json.serialize();
    return snapshot_from_json(json);
  }

}
