#include "http_debug_server.h"

#include <cpprest/details/basic_types.h>
#include <cpprest/http_listener.h>

#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {

utility::string_t to_t(const std::string &value) {
  return utility::conversions::to_string_t(value);
}

const utility::string_t SettingsPath = to_t("/api/nmos/settings");
const utility::string_t SettingsUpdatePath = to_t("/api/nmos/settings/update");

web::json::value error_json(const std::string &message) {
  web::json::value object = web::json::value::object();
  object[to_t("error")] = web::json::value::string(to_t(message));
  return object;
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
      request.reply(web::http::status_codes::InternalError, error_json(error.what()));
    }
    return;
  }
  if (path == to_t("/api/nmos/registries")) {
    try {
      request.reply(web::http::status_codes::OK, app_.available_registries_json());
    } catch (const std::exception &error) {
      request.reply(web::http::status_codes::InternalError, error_json(error.what()));
    }
    return;
  }
  if (path == to_t("/api/nmos/snapshot")) {
    request.reply(web::http::status_codes::OK, state_store_.snapshot_json());
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

}
