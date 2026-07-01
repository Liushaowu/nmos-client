#include "http_debug_server.h"

#include <cpprest/details/basic_types.h>
#include <cpprest/http_listener.h>

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

const utility::string_t SettingsPath = to_t("/api/nmos/settings");
const utility::string_t SettingsUpdatePath = to_t("/api/nmos/settings/update");
const utility::string_t DaemonConfigPath = to_t("/api/daemon/config");
const utility::string_t IndexPath = to_t("/");

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

std::string read_index_html() {
  const std::vector<std::string> candidates{
      "config-ui.html",
      "config/windows/config-ui.html",
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
  if (path == to_t("/api/nmos/snapshot")) {
    request.reply(web::http::status_codes::OK, state_store_.snapshot_json());
    return;
  }
  if (path == DaemonConfigPath) {
    handle_daemon_config_get(std::move(request));
    return;
  }

  request.reply(web::http::status_codes::NotFound);
}

void HttpDebugServer::handle_post(web::http::http_request request) {
  const auto path = request.relative_uri().path();
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
                  app_.update_node_config(body, replace_entire_document));
  } catch (const std::exception &error) {
    request.reply(web::http::status_codes::BadRequest, error_json(error.what()));
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
