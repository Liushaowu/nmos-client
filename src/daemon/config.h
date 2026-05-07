#pragma once

#include <string>

namespace seeder::nmos_sync {

struct DaemonConfig {
  std::string node_config_path;
  std::string snapshot_url;
  std::string ws_url;
  int pull_timeout_ms = 3000;
  int reconnect_interval_ms = 1000;
  int snapshot_debounce_ms = 300;
  std::string debug_http_url = "http:/" "/127.0.0.1:8081";

  static DaemonConfig load_from_file(const std::string &file_path);
};

}
