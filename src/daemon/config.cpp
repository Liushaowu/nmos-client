#include "config.h"

#include <cpprest/details/basic_types.h>
#include <cpprest/json.h>

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace {

utility::string_t to_t(const std::string &value) {
  return utility::conversions::to_string_t(value);
}

std::string to_utf8(const utility::string_t &value) {
  return utility::conversions::to_utf8string(value);
}

std::string require_string(const web::json::value &object, const char *name) {
  const auto field = to_t(name);
  if (!object.is_object() || !object.has_field(field) ||
      object.at(field).is_null()) {
    throw std::runtime_error(std::string("missing required config field: ") +
                             name);
  }
  return to_utf8(object.at(field).as_string());
}

int get_int_or(const web::json::value &object, const char *name, int fallback) {
  const auto field = to_t(name);
  if (!object.is_object() || !object.has_field(field) ||
      object.at(field).is_null()) {
    return fallback;
  }
  return object.at(field).as_integer();
}

std::string get_string_or(const web::json::value &object, const char *name,
                          const std::string &fallback) {
  const auto field = to_t(name);
  if (!object.is_object() || !object.has_field(field) ||
      object.at(field).is_null()) {
    return fallback;
  }
  return to_utf8(object.at(field).as_string());
}

}

namespace seeder::nmos_sync {

DaemonConfig DaemonConfig::load_from_file(const std::string &file_path) {
  std::ifstream stream(file_path);
  if (!stream) {
    throw std::runtime_error("failed to open daemon config file: " + file_path);
  }

  std::ostringstream buffer;
  buffer << stream.rdbuf();
  auto json = web::json::value::parse(
      utility::conversions::to_string_t(buffer.str()));

  DaemonConfig config;
  config.node_config_path = require_string(json, "node_config_path");
  config.snapshot_url = require_string(json, "snapshot_url");
  config.ws_url = require_string(json, "ws_url");
  config.pull_timeout_ms = get_int_or(json, "pull_timeout_ms", 3000);
  config.reconnect_interval_ms =
      get_int_or(json, "reconnect_interval_ms", 1000);
  config.snapshot_debounce_ms =
      get_int_or(json, "snapshot_debounce_ms", 300);
  config.debug_http_url =
      get_string_or(json, "debug_http_url", "http:/" "/127.0.0.1:8081");
  return config;
}

}
