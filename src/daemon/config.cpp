#include "config.h"

#include <cpprest/details/basic_types.h>
#include <cpprest/json.h>
#include <cpprest/uri.h>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

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

std::string trim_trailing_slashes(std::string value) {
  while (!value.empty() && value.back() == '/') {
    value.pop_back();
  }
  return value;
}

std::string normalize_device_server(const std::string &value) {
  const auto uri = web::uri(to_t(value));
  if (uri.is_empty() || uri.scheme().empty() || uri.host().empty()) {
    throw std::runtime_error(
        "device_server must be an absolute HTTP(S) URL with a host");
  }
  if (uri.scheme() != U("http") && uri.scheme() != U("https")) {
    throw std::runtime_error("device_server must use http:// or https://");
  }
  if ((!uri.path().empty() && uri.path() != U("/")) ||
      !uri.query().empty() || !uri.fragment().empty()) {
    throw std::runtime_error("device_server must be an HTTP(S) origin");
  }

  return trim_trailing_slashes(to_utf8(uri.to_string()));
}

std::string append_path(const std::string &base, const char *path) {
  return trim_trailing_slashes(base) + path;
}

}

namespace seeder::nmos_sync {

web::json::value DaemonConfig::to_json() const {
  web::json::value obj = web::json::value::object();
  obj[to_t("node_config_path")] =
      web::json::value::string(to_t(node_config_path));
  obj[to_t("device_server")] = web::json::value::string(to_t(device_server));
  obj[to_t("pull_timeout_ms")] = web::json::value::number(pull_timeout_ms);
  obj[to_t("reconnect_interval_ms")] =
      web::json::value::number(reconnect_interval_ms);
  obj[to_t("ws_heartbeat_interval_ms")] =
      web::json::value::number(ws_heartbeat_interval_ms);
  obj[to_t("ws_heartbeat_timeout_ms")] =
      web::json::value::number(ws_heartbeat_timeout_ms);
  obj[to_t("snapshot_debounce_ms")] =
      web::json::value::number(snapshot_debounce_ms);
  obj[to_t("debug_http_url")] =
      web::json::value::string(to_t(debug_http_url));
  return obj;
}

std::string DaemonConfig::snapshot_url() const {
  return append_path(device_server, "/api/data/nmos");
}

std::string DaemonConfig::ws_url() const {
  if (device_server.rfind("http://", 0) == 0) {
    return append_path("ws://" + device_server.substr(7), "/ws/nmos");
  }
  if (device_server.rfind("https://", 0) == 0) {
    return append_path("wss://" + device_server.substr(8), "/ws/nmos");
  }
  throw std::runtime_error("device_server must use http:// or https://");
}

void DaemonConfig::save_to_file(const std::string &file_path,
                                const web::json::value &json) {
  if (!json.is_object()) {
    throw std::runtime_error("daemon config must be a JSON object");
  }

  const auto serialized = json.serialize();
  std::ofstream stream(file_path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    throw std::runtime_error("failed to open daemon config file for writing: " +
                             file_path);
  }
  const auto utf8 = to_utf8(serialized);
  stream.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
  if (!stream) {
    throw std::runtime_error("failed to write daemon config file: " +
                             file_path);
  }
}

DaemonConfig DaemonConfig::from_json(const web::json::value &json) {
  DaemonConfig config;
  config.node_config_path = require_string(json, "node_config_path");
  config.device_server = normalize_device_server(
      require_string(json, "device_server"));
  config.pull_timeout_ms = get_int_or(json, "pull_timeout_ms", 3000);
  config.reconnect_interval_ms =
      get_int_or(json, "reconnect_interval_ms", 1000);
  config.ws_heartbeat_interval_ms =
      get_int_or(json, "ws_heartbeat_interval_ms", 5000);
  config.ws_heartbeat_timeout_ms =
      get_int_or(json, "ws_heartbeat_timeout_ms", 15000);
  config.snapshot_debounce_ms =
      get_int_or(json, "snapshot_debounce_ms", 300);
  config.debug_http_url =
      get_string_or(json, "debug_http_url", "http:/" "/127.0.0.1:8081");
  return config;
}

DaemonConfig DaemonConfig::load_from_file(const std::string &file_path) {
  std::ifstream stream(file_path);
  if (!stream) {
    throw std::runtime_error("failed to open daemon config file: " + file_path);
  }

  std::ostringstream buffer;
  buffer << stream.rdbuf();
  auto json = web::json::value::parse(
      utility::conversions::to_string_t(buffer.str()));
  return DaemonConfig::from_json(json);
}

}
