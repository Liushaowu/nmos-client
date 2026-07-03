#include "http_debug_server.h"

#include <cpprest/details/basic_types.h>
#include <cpprest/http_listener.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {

utility::string_t to_t(const std::string &value) {
  return utility::conversions::to_string_t(value);
}

std::string to_utf8(const utility::string_t &value) {
  return utility::conversions::to_utf8string(value);
}

const utility::string_t SettingsPath = to_t("/api/nmos/settings");
const utility::string_t SettingsUpdatePath = to_t("/api/nmos/settings/update");
const utility::string_t DaemonConfigPath = to_t("/api/daemon/config");
const utility::string_t DaemonRestartPath = to_t("/api/daemon/restart");
const utility::string_t DaemonLogsPath = to_t("/api/daemon/logs");
const utility::string_t IndexPath = to_t("/");
constexpr std::streamoff MaxLogTailBytes = 256 * 1024;

web::json::value error_json(const std::string &message) {
  web::json::value object = web::json::value::object();
  object[to_t("error")] = web::json::value::string(to_t(message));
  return object;
}

std::string read_text_file(const std::string &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("failed to open file: " + path);
  }

  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

std::string read_tail_text_file(const std::string &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("failed to open file: " + path);
  }

  stream.seekg(0, std::ios::end);
  const auto end = stream.tellg();
  if (end == std::ifstream::pos_type(-1)) {
    stream.clear();
    stream.seekg(0, std::ios::beg);
  } else {
    const auto size = static_cast<std::streamoff>(end);
    stream.seekg(size > MaxLogTailBytes ? size - MaxLogTailBytes : 0,
                 std::ios::beg);
  }

  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

std::string replacement_utf8() { return "\xEF\xBF\xBD"; }

std::string sanitize_utf8(const std::string &input) {
  std::string output;
  output.reserve(input.size());

  for (std::size_t i = 0; i < input.size();) {
    const auto lead = static_cast<unsigned char>(input[i]);
    if (lead <= 0x7f) {
      output.push_back(input[i++]);
      continue;
    }

    std::size_t length = 0;
    unsigned int codepoint = 0;
    unsigned int minimum = 0;
    if ((lead & 0xe0) == 0xc0) {
      length = 2;
      codepoint = lead & 0x1f;
      minimum = 0x80;
    } else if ((lead & 0xf0) == 0xe0) {
      length = 3;
      codepoint = lead & 0x0f;
      minimum = 0x800;
    } else if ((lead & 0xf8) == 0xf0) {
      length = 4;
      codepoint = lead & 0x07;
      minimum = 0x10000;
    } else {
      output += replacement_utf8();
      ++i;
      continue;
    }

    if (i + length > input.size()) {
      output += replacement_utf8();
      ++i;
      continue;
    }

    bool valid = true;
    for (std::size_t j = 1; j < length; ++j) {
      const auto byte = static_cast<unsigned char>(input[i + j]);
      if ((byte & 0xc0) != 0x80) {
        valid = false;
        break;
      }
      codepoint = (codepoint << 6) | (byte & 0x3f);
    }

    if (!valid || codepoint < minimum || codepoint > 0x10ffff ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
      output += replacement_utf8();
      ++i;
      continue;
    }

    output.append(input, i, length);
    i += length;
  }

  return output;
}

std::string get_required_string_field(const web::json::value &object,
                                      const char *name) {
  const auto field = to_t(name);
  if (!object.is_object() || !object.has_field(field) ||
      object.at(field).is_null()) {
    return {};
  }
  if (!object.at(field).is_string()) {
    throw std::runtime_error(std::string(name) + " must be a string");
  }
  return utility::conversions::to_utf8string(object.at(field).as_string());
}

std::string read_index_html() {
  const std::vector<std::string> candidates{
      "config/windows/config-ui.html",
      "config-ui.html",
  };

  std::vector<std::string> errors;
  for (const auto &candidate : candidates) {
    try {
      return read_text_file(candidate);
    } catch (const std::exception &error) {
      errors.push_back(error.what());
    }
  }

  std::ostringstream message;
  message << "failed to load config UI HTML. Tried:";
  for (const auto &error : errors) {
    message << " " << error << ";";
  }
  throw std::runtime_error(message.str());
}

}

namespace seeder::nmos_sync {

HttpDebugServer::HttpDebugServer(std::string url, App &app,
                                 const StateStore &state_store)
    : url_(std::move(url)), app_(app), state_store_(state_store) {}

HttpDebugServer::~HttpDebugServer() { stop(); }

void HttpDebugServer::start() {
  if (url_.empty() || listener_) {
    return;
  }

  listener_ = std::make_unique<web::http::experimental::listener::http_listener>(
      web::uri(to_t(url_)));
  listener_->support(web::http::methods::GET,
                     [this](web::http::http_request request)
                     { handle_get(std::move(request)); });
  listener_->support(web::http::methods::POST,
                     [this](web::http::http_request request)
                     { handle_post(std::move(request)); });
  listener_->support(web::http::methods::PUT,
                     [this](web::http::http_request request)
                     { handle_put(std::move(request)); });
  listener_->support(web::http::methods::PATCH,
                     [this](web::http::http_request request)
                     { handle_patch(std::move(request)); });
  listener_->open().wait();
}

void HttpDebugServer::stop() {
  if (!listener_) {
    return;
  }

  const auto stop_started = std::chrono::steady_clock::now();
  std::cerr << "nmos-sync-daemon debug http stop: closing listener"
            << std::endl;
  listener_->close().wait();
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - stop_started);
  std::cerr << "nmos-sync-daemon debug http stop: listener closed"
            << ", elapsed_ms=" << elapsed.count() << std::endl;
  listener_.reset();
}

void HttpDebugServer::handle_get(web::http::http_request request) {
  const auto path = request.relative_uri().path();
  if (path == IndexPath) {
    handle_index(std::move(request));
    return;
  }
  if (path == to_t("/api/nmos/health")) {
    web::json::value object = web::json::value::object();
    object[to_t("ok")] = web::json::value::boolean(true);
    request.reply(web::http::status_codes::OK, object);
    return;
  }
  if (path == to_t("/api/nmos/status")) {
    request.reply(web::http::status_codes::OK, state_store_.status_json());
    return;
  }
  if (path == SettingsPath) {
    try {
      request.reply(web::http::status_codes::OK, app_.node_settings_json());
    } catch (const std::exception &error) {
      request.reply(web::http::status_codes::InternalError,
                    error_json(error.what()));
    }
    return;
  }
  if (path == to_t("/api/nmos/registries")) {
    try {
      request.reply(web::http::status_codes::OK, app_.available_registries_json());
    } catch (const std::exception &error) {
      request.reply(web::http::status_codes::InternalError,
                    error_json(error.what()));
    }
    return;
  }
  if (path == to_t("/api/nmos/interfaces")) {
    try {
      request.reply(web::http::status_codes::OK, app_.network_interfaces_json());
    } catch (const std::exception &error) {
      request.reply(web::http::status_codes::InternalError,
                    error_json(error.what()));
    }
    return;
  }
  if (path == to_t("/api/nmos/snapshot")) {
    request.reply(web::http::status_codes::OK, state_store_.snapshot_json());
    return;
  }
  if (path == DaemonConfigPath) {
    handle_daemon_config_get(std::move(request));
    return;
  }
  if (path == DaemonLogsPath) {
    handle_daemon_logs_get(std::move(request));
    return;
  }

  request.reply(web::http::status_codes::NotFound);
}

void HttpDebugServer::handle_post(web::http::http_request request) {
  const auto path = request.relative_uri().path();
  if (path == DaemonRestartPath) {
    handle_daemon_restart(std::move(request));
    return;
  }
  if (path != SettingsUpdatePath) {
    request.reply(web::http::status_codes::NotFound);
    return;
  }

  handle_settings_update(std::move(request), true);
}

void HttpDebugServer::handle_put(web::http::http_request request) {
  const auto path = request.relative_uri().path();
  if (path == DaemonConfigPath) {
    handle_daemon_config_put(std::move(request));
    return;
  }
  if (path != SettingsUpdatePath) {
    request.reply(web::http::status_codes::NotFound);
    return;
  }

  handle_settings_update(std::move(request), true);
}

void HttpDebugServer::handle_patch(web::http::http_request request) {
  const auto path = request.relative_uri().path();
  if (path != SettingsUpdatePath) {
    request.reply(web::http::status_codes::NotFound);
    return;
  }

  handle_settings_update(std::move(request), false);
}

void HttpDebugServer::handle_settings_update(web::http::http_request request,
                                             bool replace_entire_document) {
  try {
    const auto body = request.extract_json().get();
    request.reply(web::http::status_codes::OK,
                  app_.write_node_config(body, replace_entire_document));
  } catch (const std::exception &error) {
    request.reply(web::http::status_codes::BadRequest, error_json(error.what()));
  } catch (...) {
    request.reply(web::http::status_codes::InternalError,
                  error_json("unexpected error"));
  }
}

void HttpDebugServer::handle_daemon_config_get(web::http::http_request request) {
  try {
    request.reply(web::http::status_codes::OK, app_.daemon_config_json());
  } catch (const std::exception &error) {
    request.reply(web::http::status_codes::InternalError,
                  error_json(error.what()));
  }
}

void HttpDebugServer::handle_daemon_config_put(web::http::http_request request) {
  try {
    const auto body = request.extract_json().get();
    request.reply(web::http::status_codes::OK,
                  app_.update_daemon_config(body));
  } catch (const std::exception &error) {
    request.reply(web::http::status_codes::BadRequest, error_json(error.what()));
  }
}

void HttpDebugServer::handle_daemon_restart(web::http::http_request request) {
  try {
    app_.restart_daemon_service();

    web::json::value response = web::json::value::object();
    response[to_t("accepted")] = web::json::value::boolean(true);
    response[to_t("message")] =
        web::json::value::string(to_t("daemon restart initiated"));
    request.reply(web::http::status_codes::OK, response);
  } catch (const std::exception &error) {
    request.reply(web::http::status_codes::InternalError,
                  error_json(error.what()));
  }
}

void HttpDebugServer::handle_daemon_logs_get(web::http::http_request request) {
  try {
    const auto settings = app_.node_settings_json();
    const auto error_log_path = get_required_string_field(settings, "error_log");
    if (error_log_path.empty()) {
      request.reply(web::http::status_codes::NotFound,
                    error_json("error_log is not configured"));
      return;
    }

    const auto query_params =
        web::http::uri::split_query(request.relative_uri().query());
    int offset = 0;
    int limit = 100;
    const auto offset_it = query_params.find(to_t("offset"));
    if (offset_it != query_params.end()) {
      offset = std::max(0, std::stoi(to_utf8(offset_it->second)));
    }
    const auto limit_it = query_params.find(to_t("limit"));
    if (limit_it != query_params.end()) {
      limit = std::max(1, std::min(500, std::stoi(to_utf8(limit_it->second))));
    }

    const auto content = read_tail_text_file(error_log_path);
    std::vector<std::string> all_lines;
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
      all_lines.push_back(line);
    }

    const int total = static_cast<int>(all_lines.size());
    if (offset >= total) {
      offset = std::max(0, total - limit);
    }

    web::json::value result = web::json::value::object();
    web::json::value lines = web::json::value::array();
    const int end = std::min(offset + limit, total);
    for (int i = offset; i < end; ++i) {
      lines[i - offset] =
          web::json::value::string(to_t(sanitize_utf8(all_lines[i])));
    }
    result[to_t("lines")] = std::move(lines);
    result[to_t("total")] = web::json::value::number(total);
    result[to_t("offset")] = web::json::value::number(offset);
    result[to_t("limit")] = web::json::value::number(limit);

    request.reply(web::http::status_codes::OK, result);
  } catch (const std::exception &error) {
    request.reply(web::http::status_codes::InternalError,
                  error_json(error.what()));
  }
}

void HttpDebugServer::handle_index(web::http::http_request request) {
  try {
    web::http::http_response response(web::http::status_codes::OK);
    response.headers().set_content_type(to_t("text/html; charset=utf-8"));
    response.set_body(read_index_html());
    request.reply(response);
  } catch (const std::exception &error) {
    request.reply(web::http::status_codes::InternalError,
                  error_json(error.what()));
  }
}

}
