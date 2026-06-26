#include "app.h"

#include "http_debug_server.h"

#include <chrono>
#include <cpprest/uri.h>
#include <iostream>
#include <thread>

namespace seeder::nmos_sync {

namespace {

utility::string_t to_t(const std::string &value) {
  return utility::conversions::to_string_t(value);
}

std::string to_utf8(const utility::string_t &value) {
  return utility::conversions::to_utf8string(value);
}

web::json::value json_string(const std::string &value) {
  return web::json::value::string(to_t(value));
}

std::string get_string_or_empty(const web::json::value &object,
                                const char *name) {
  const auto field = to_t(name);
  if (!object.is_object() || !object.has_field(field) || object.at(field).is_null()) {
    return {};
  }
  return to_utf8(object.at(field).as_string());
}

int get_int_or_default(const web::json::value &object, const char *name,
                       int fallback) {
  const auto field = to_t(name);
  if (!object.is_object() || !object.has_field(field) || object.at(field).is_null()) {
    return fallback;
  }
  return object.at(field).as_integer();
}

bool get_bool_or_default(const web::json::value &object, const char *name,
                         bool fallback) {
  const auto field = to_t(name);
  if (!object.is_object() || !object.has_field(field) || object.at(field).is_null()) {
    return fallback;
  }
  return object.at(field).as_bool();
}

web::json::value selected_registry_summary(const web::json::value &settings) {
  web::json::value object = web::json::value::object();
  object[to_t("selected_registry_uri")] =
      json_string(get_string_or_empty(settings, "selected_registry_uri"));
  object[to_t("discovery_enabled")] = web::json::value::boolean(
      get_bool_or_default(settings, "discovery_enabled", true));
  object[to_t("registry_address")] =
      json_string(get_string_or_empty(settings, "registry_address"));
  object[to_t("registration_port")] = web::json::value::number(
      get_int_or_default(settings, "registration_port", 3210));
  object[to_t("registry_version")] =
      json_string(get_string_or_empty(settings, "registry_version"));
  object[to_t("highest_pri")] = web::json::value::number(
      get_int_or_default(settings, "highest_pri", 0));
  object[to_t("lowest_pri")] = web::json::value::number(
      get_int_or_default(settings, "lowest_pri", 2147483647));
  return object;
}

void apply_selected_registry_settings(web::json::value &settings) {
  if (!settings.is_object()) {
    throw std::runtime_error("node config payload must be a JSON object");
  }

  const auto selected_registry_uri_field = to_t("selected_registry_uri");
  if (!settings.has_field(selected_registry_uri_field)) {
    return;
  }

  const auto selected_registry_uri_value = settings.at(selected_registry_uri_field);
  if (selected_registry_uri_value.is_null()) {
    settings.as_object().erase(selected_registry_uri_field);
    return;
  }
  if (!selected_registry_uri_value.is_string()) {
    throw std::runtime_error("selected_registry_uri must be a string");
  }

  const auto selected_registry_uri = web::uri(selected_registry_uri_value.as_string());
  if (selected_registry_uri.is_empty()) {
    throw std::runtime_error("selected_registry_uri must not be empty");
  }

  settings[to_t("registry_address")] =
      json_string(to_utf8(selected_registry_uri.host()));
  settings[to_t("registration_port")] =
      web::json::value::number(selected_registry_uri.port());

  const auto path = to_utf8(selected_registry_uri.path());
  const auto slash = path.find_last_of('/');
  if (std::string::npos == slash || slash + 1 >= path.size()) {
    throw std::runtime_error(
        "selected_registry_uri must include /x-nmos/registration/{version}");
  }
  settings[to_t("registry_version")] = json_string(path.substr(slash + 1));

  const bool discovery_enabled =
      get_bool_or_default(settings, "discovery_enabled", false);
  settings[to_t("highest_pri")] =
      web::json::value::number(discovery_enabled ? 0 : 2147483647);
  settings[to_t("lowest_pri")] = web::json::value::number(2147483647);
}

web::json::value merge_objects(const web::json::value &base,
                               const web::json::value &patch) {
  if (!base.is_object() || !patch.is_object()) {
    throw std::runtime_error("node config patch must be a JSON object");
  }

  web::json::value merged = base;
  for (const auto &field : patch.as_object()) {
    if (field.second.is_null()) {
      merged.as_object().erase(field.first);
      continue;
    }

    if (merged.has_field(field.first) && merged.at(field.first).is_object() &&
        field.second.is_object()) {
      merged[field.first] = merge_objects(merged.at(field.first), field.second);
    } else {
      merged[field.first] = field.second;
    }
  }
  return merged;
}

}

App::App(std::string config_path) : config_path_(std::move(config_path)) {}

App::~App() { stop(); }

web::json::value App::node_settings_json() const {
  if (!node_runtime_) {
    throw std::runtime_error("node runtime is not initialized");
  }

  std::lock_guard<std::mutex> lock(node_config_mutex_);
  return node_runtime_->persisted_settings();
}

web::json::value App::available_registries_json() const {
  if (!node_runtime_) {
    throw std::runtime_error("node runtime is not initialized");
  }

  std::lock_guard<std::mutex> lock(node_config_mutex_);
  web::json::value result = web::json::value::object();
  result[to_t("current")] = state_store_.status_json().at(to_t("registry"));
  result[to_t("discovered")] = node_runtime_->discover_registration_apis();
  return result;
}

web::json::value App::update_node_config(const web::json::value &patch,
                                         bool replace_entire_document) {
  if (!node_runtime_) {
    throw std::runtime_error("node runtime is not initialized");
  }
  if (!patch.is_object()) {
    throw std::runtime_error("node config payload must be a JSON object");
  }

  std::lock_guard<std::mutex> lock(node_config_mutex_);
  const auto previous_persisted = node_runtime_->persisted_settings();
  auto next_persisted =
      replace_entire_document ? patch : merge_objects(previous_persisted, patch);
  apply_selected_registry_settings(next_persisted);

  node_runtime_->write_persisted_settings(next_persisted);
  try {
    restart_node_runtime();
  } catch (...) {
    try {
      node_runtime_->write_persisted_settings(previous_persisted);
      restart_node_runtime();
    } catch (const std::exception &rollback_error) {
      std::cerr << "failed to roll back node config after restart failure: "
                << rollback_error.what() << std::endl;
    } catch (...) {
      std::cerr << "failed to roll back node config after restart failure"
                << std::endl;
    }
    throw;
  }

  web::json::value result = web::json::value::object();
  result[to_t("persisted")] = next_persisted;
  result[to_t("effective")] = node_runtime_->effective_settings();
  result[to_t("selected_registry")] = selected_registry_summary(next_persisted);
  return result;
}

void App::restart_node_runtime() {
  if (!node_runtime_) {
    throw std::runtime_error("node runtime is not initialized");
  }

  set_node_state("restarting");
  node_runtime_->stop();
  state_store_.set_registry_status(nmos_node::RegistrationStatus{});
  state_store_.clear_last_snapshot();
  if (!node_runtime_->start()) {
    set_node_state("stopped");
    throw std::runtime_error("failed to restart node runtime with updated config");
  }
  set_node_state("running");
  const auto status = state_store_.status();
  const auto reconnect_revision = status.last_seen_revision > 0 ? status.last_seen_revision : 1;
  schedule_sync(reconnect_revision);
}

int App::run() {
  config_ = DaemonConfig::load_from_file(config_path_);

  state_store_.set_daemon_state("starting");
  node_runtime_ = std::make_unique<NodeRuntime>(config_.node_config_path);
  snapshot_client_ =
      std::make_unique<SnapshotClient>(config_.snapshot_url, config_.pull_timeout_ms);
  ws_client_ =
      std::make_unique<WsClient>(config_.ws_url, config_.reconnect_interval_ms,
                                 config_.ws_heartbeat_interval_ms,
                                 config_.ws_heartbeat_timeout_ms);
  http_debug_server_ =
      std::make_unique<HttpDebugServer>(config_.debug_http_url, *this, state_store_);

  node_runtime_->set_receiver_event_handler(
      [this](const ReceiverEvent &event) { handle_receiver_event(event); });
  node_runtime_->set_sender_event_handler(
      [this](const SenderEvent &event) { handle_sender_event(event); });
  node_runtime_->set_registration_event_handler(
      [this](const RegistrationEvent &event)
      { handle_registration_event(event); });

  WsClientCallbacks callbacks;
  callbacks.on_connected = [this] { handle_ws_connected(); };
  callbacks.on_disconnected = [this] { handle_ws_disconnected(); };
  callbacks.on_snapshot_changed =
      [this](const SnapshotChangedMessage &message)
      {
        state_store_.mark_snapshot_signal_received();
        state_store_.set_last_seen_revision(message.revision);
        {
          std::lock_guard<std::mutex> lock(sync_mutex_);
          force_sync_pending_ = true;
        }
        schedule_sync(message.revision);
      };
  callbacks.on_error = [this](const std::string &message)
  { handle_ws_error(message); };
  ws_client_->set_callbacks(std::move(callbacks));

  set_node_state("starting");
  if (!node_runtime_->start()) {
    state_store_.mark_sync_failed("failed to start node runtime");
    set_node_state("stopped");
    return 1;
  }
  set_node_state("running");

  http_debug_server_->start();

  {
    std::lock_guard<std::mutex> lock(stop_mutex_);
    started_ = true;
  }
  sync_thread_ = std::thread([this] { sync_loop(); });
  ws_client_->start();
  state_store_.set_daemon_state("connecting_ws");

  while (!stop_requested_) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  stop();
  return 0;
}

void App::request_stop() {
  stop_requested_ = true;
  sync_cv_.notify_all();
}

void App::stop() {
  request_stop();

  std::lock_guard<std::mutex> stop_lock(stop_mutex_);
  if (!started_) {
    return;
  }

  const auto stop_started = std::chrono::steady_clock::now();
  const auto log_elapsed = [&stop_started](const char *step) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - stop_started);
    std::cerr << "nmos-sync-daemon stop: " << step
              << ", elapsed_ms=" << elapsed.count() << std::endl;
  };

  log_elapsed("begin");

  if (ws_client_) {
    log_elapsed("stopping websocket client");
    ws_client_->stop();
    log_elapsed("websocket client stopped");
  }
  if (sync_thread_.joinable()) {
    log_elapsed("joining sync thread");
    sync_thread_.join();
    log_elapsed("sync thread joined");
  }
  if (http_debug_server_) {
    log_elapsed("stopping debug http server");
    http_debug_server_->stop();
    log_elapsed("debug http server stopped");
  }
  if (node_runtime_) {
    log_elapsed("stopping node runtime");
    set_node_state("stopping");
    node_runtime_->stop();
    set_node_state("stopped");
    log_elapsed("node runtime stopped");
  }

  state_store_.set_daemon_state("stopped");
  started_ = false;
  log_elapsed("complete");
}

void App::schedule_sync(std::int64_t revision) {
  const auto status = state_store_.status();
  const bool needs_resync_without_newer_revision =
      status.protection_mode || !status.streams_registered;
  {
    std::lock_guard<std::mutex> lock(sync_mutex_);
    if (!force_sync_pending_ && revision <= status.last_applied_revision &&
        !needs_resync_without_newer_revision) {
      return;
    }

    sync_pending_ = true;
    if (revision > pending_revision_) {
      pending_revision_ = revision;
    }
    last_signal_time_ = std::chrono::steady_clock::now();
    next_sync_not_before_ = std::chrono::steady_clock::time_point{};
  }
  sync_cv_.notify_all();
}

void App::sync_loop() {
  std::unique_lock<std::mutex> lock(sync_mutex_);
  while (!stop_requested_) {
    sync_cv_.wait(lock, [this] { return stop_requested_ || sync_pending_; });
    if (stop_requested_) {
      return;
    }

    auto deadline = last_signal_time_ +
                    std::chrono::milliseconds(config_.snapshot_debounce_ms);
    if (sync_cv_.wait_until(lock, deadline, [this, deadline] {
          return stop_requested_ || last_signal_time_ > deadline;
        })) {
      continue;
    }

    const bool is_retry_attempt =
        next_sync_not_before_ != std::chrono::steady_clock::time_point{};
    if (is_retry_attempt &&
        std::chrono::steady_clock::now() < next_sync_not_before_) {
      if (sync_cv_.wait_until(lock, next_sync_not_before_, [this] {
            return stop_requested_ ||
                   next_sync_not_before_ == std::chrono::steady_clock::time_point{};
          })) {
        continue;
      }
      std::cerr << "nmos-sync-daemon retrying sync after delay, revision="
                << pending_revision_ << std::endl;
    }

    const auto revision = pending_revision_;
    sync_pending_ = false;
    force_sync_pending_ = false;
    pending_revision_ = 0;
    next_sync_not_before_ = std::chrono::steady_clock::time_point{};
    lock.unlock();
    perform_sync(revision, is_retry_attempt);
    lock.lock();
  }
}

void App::perform_sync(std::int64_t revision, bool is_retry_attempt) {
  if (stop_requested_) {
    return;
  }

  std::lock_guard<std::mutex> reconcile_lock(reconcile_mutex_);
  state_store_.set_daemon_state("resyncing");
  try {
    const auto current_snapshot = state_store_.last_snapshot();
    auto snapshot = snapshot_client_->fetch_snapshot();
    reconcile_engine_.apply_snapshot(current_snapshot, snapshot, *node_runtime_);
    state_store_.mark_apply_success(revision, snapshot, snapshot.has_any_streams());
    state_store_.set_daemon_state("running");
    if (is_retry_attempt) {
      std::cerr << "nmos-sync-daemon sync retry succeeded, revision="
                << revision << std::endl;
    }
  } catch (const std::exception &error) {
    state_store_.mark_sync_failed(error.what());
    state_store_.set_daemon_state("degraded");
    if (ws_client_ && ws_client_->is_connected()) {
      ws_client_->send_json(make_sync_failed_message(
          SyncFailedMessage{revision, error.what()}));
    }
    {
      std::lock_guard<std::mutex> lock(sync_mutex_);
      sync_pending_ = true;
      if (revision > pending_revision_) {
        pending_revision_ = revision;
      }
      last_signal_time_ = std::chrono::steady_clock::now();
      next_sync_not_before_ =
          last_signal_time_ +
          std::chrono::milliseconds(config_.reconnect_interval_ms);
    }
    std::cerr << "nmos-sync-daemon sync failed, scheduling retry in "
              << config_.reconnect_interval_ms << "ms, revision=" << revision
              << ", error=" << error.what() << std::endl;
    sync_cv_.notify_all();
  }
}

void App::handle_ws_connected() {
  state_store_.set_daemon_state("connected_waiting_snapshot");
  if (state_store_.consume_pending_drain_notification() && ws_client_) {
    ws_client_->send_json(make_streams_drained_message());
  }
  const auto status = state_store_.status();
  const auto reconnect_revision =
      status.last_seen_revision > 0 ? status.last_seen_revision : 1;
  schedule_sync(reconnect_revision);
}

void App::handle_ws_disconnected() {
  state_store_.set_daemon_state("draining");
  std::lock_guard<std::mutex> reconcile_lock(reconcile_mutex_);
  reconcile_engine_.drain_all(state_store_.last_snapshot(), *node_runtime_);
  state_store_.mark_drained_due_to_ws_disconnect();
  state_store_.clear_last_snapshot();
  state_store_.set_daemon_state("degraded");
}

void App::handle_ws_error(const std::string &message) {
  state_store_.mark_sync_failed(message);
}

void App::handle_sender_event(const SenderEvent &event) {
  if (!ws_client_ || !ws_client_->is_connected()) {
    return;
  }

  std::visit(
      [&](const auto &payload) {
        using Payload = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<Payload, nmos_node::VideoSender>) {
          ws_client_->send_json(
              make_sender_video_observed_changed_message(payload));
        } else if constexpr (std::is_same_v<Payload, nmos_node::AudioSender>) {
          ws_client_->send_json(
              make_sender_audio_observed_changed_message(payload));
        } else {
          ws_client_->send_json(
              make_sender_ancillary_observed_changed_message(payload));
        }
      },
      event.payload);
}

void App::handle_receiver_event(const ReceiverEvent &event) {
  if (!ws_client_ || !ws_client_->is_connected()) {
    return;
  }

  std::visit(
      [&](const auto &payload) {
        using Payload = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<Payload, nmos_node::VideoReceiver>) {
          ws_client_->send_json(
              make_receiver_video_observed_changed_message(payload));
        } else if constexpr (std::is_same_v<Payload, nmos_node::AudioReceiver>) {
          ws_client_->send_json(
              make_receiver_audio_observed_changed_message(payload));
        } else {
          ws_client_->send_json(
              make_receiver_ancillary_observed_changed_message(payload));
        }
      },
      event.payload);
}

void App::handle_registration_event(const RegistrationEvent &event) {
  state_store_.set_registry_status(event.status);
}

void App::set_node_state(const std::string &state) {
  state_store_.set_node_state(state);
  if (ws_client_ && ws_client_->is_connected()) {
    ws_client_->send_json(
        make_node_lifecycle_message(last_node_state_, state));
  }
  last_node_state_ = state;
}

}
