#include "state_store.h"

#include <cpprest/details/basic_types.h>

namespace {

utility::string_t to_t(const std::string &value) {
  return utility::conversions::to_string_t(value);
}

web::json::value json_string(const std::string &text) {
  return web::json::value::string(to_t(text));
}

}

namespace seeder::nmos_sync {

void StateStore::set_daemon_state(const std::string &state) {
  std::lock_guard<std::mutex> lock(mutex_);
  status_.daemon_state = state;
}

void StateStore::set_node_state(const std::string &state) {
  std::lock_guard<std::mutex> lock(mutex_);
  status_.node_state = state;
}

void StateStore::set_last_seen_revision(std::int64_t revision) {
  std::lock_guard<std::mutex> lock(mutex_);
  status_.last_seen_revision = revision;
}

void StateStore::mark_snapshot_signal_received() {
  std::lock_guard<std::mutex> lock(mutex_);
  status_.has_received_snapshot_changed = true;
}

void StateStore::mark_sync_failed(const std::string &message) {
  std::lock_guard<std::mutex> lock(mutex_);
  status_.last_sync_error = message;
}

void StateStore::mark_apply_success(std::int64_t applied_revision,
                                    const SnapshotDto &snapshot,
                                    bool streams_registered) {
  std::lock_guard<std::mutex> lock(mutex_);
  status_.last_applied_revision = applied_revision;
  status_.last_seen_revision = applied_revision;
  status_.last_sync_error.clear();
  status_.streams_registered = streams_registered;
  status_.protection_mode = false;
  status_.has_received_snapshot_changed = true;
  last_snapshot_ = snapshot;
}

void StateStore::mark_drained_due_to_ws_disconnect() {
  std::lock_guard<std::mutex> lock(mutex_);
  status_.streams_registered = false;
  status_.protection_mode = true;
  pending_drain_notification_ = true;
}

void StateStore::set_last_snapshot(const SnapshotDto &snapshot) {
  std::lock_guard<std::mutex> lock(mutex_);
  last_snapshot_ = snapshot;
}

void StateStore::clear_last_snapshot() {
  std::lock_guard<std::mutex> lock(mutex_);
  last_snapshot_.reset();
}

void StateStore::set_registry_status(const nmos_node::RegistrationStatus &status) {
  std::lock_guard<std::mutex> lock(mutex_);
  registry_status_.connected = status.connected;
  registry_status_.uri = status.uri;
  registry_status_.scheme = status.scheme;
  registry_status_.host = status.host;
  registry_status_.port = status.port;
  registry_status_.version = status.version;
}

StatusSnapshot StateStore::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return status_;
}

RegistrySnapshot StateStore::registry_status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return registry_status_;
}

std::optional<SnapshotDto> StateStore::last_snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return last_snapshot_;
}

web::json::value StateStore::status_json() const {
  const auto snapshot = status();
  const auto registry = registry_status();
  web::json::value object = web::json::value::object();
  object[to_t("daemon_state")] = json_string(snapshot.daemon_state);
  object[to_t("node_state")] = json_string(snapshot.node_state);
  object[to_t("last_seen_revision")] =
      web::json::value::number(snapshot.last_seen_revision);
  object[to_t("last_applied_revision")] =
      web::json::value::number(snapshot.last_applied_revision);
  object[to_t("last_sync_error")] = json_string(snapshot.last_sync_error);
  object[to_t("streams_registered")] =
      web::json::value::boolean(snapshot.streams_registered);
  object[to_t("protection_mode")] =
      web::json::value::boolean(snapshot.protection_mode);
  object[to_t("has_received_snapshot_changed")] =
      web::json::value::boolean(snapshot.has_received_snapshot_changed);
  web::json::value registry_object = web::json::value::object();
  registry_object[to_t("connected")] =
      web::json::value::boolean(registry.connected);
  registry_object[to_t("uri")] = json_string(registry.uri);
  registry_object[to_t("scheme")] = json_string(registry.scheme);
  registry_object[to_t("host")] = json_string(registry.host);
  registry_object[to_t("port")] = web::json::value::number(registry.port);
  registry_object[to_t("version")] = json_string(registry.version);
  object[to_t("registry")] = std::move(registry_object);
  return object;
}

web::json::value StateStore::snapshot_json() const {
  const auto snapshot = last_snapshot();
  if (!snapshot.has_value()) {
    return web::json::value::null();
  }
  return snapshot_to_json(*snapshot);
}

bool StateStore::consume_pending_drain_notification() {
  std::lock_guard<std::mutex> lock(mutex_);
  const bool pending = pending_drain_notification_;
  pending_drain_notification_ = false;
  return pending;
}

}
