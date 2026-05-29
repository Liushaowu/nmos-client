#pragma once

#include "config.h"
#include "node_runtime.h"
#include "reconcile_engine.h"
#include "snapshot_client.h"
#include "state_store.h"
#include "ws_client.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include <cpprest/json.h>

namespace seeder::nmos_sync {

class HttpDebugServer;

class App {
public:
  explicit App(std::string config_path);
  ~App();

  int run();
  void stop();
  web::json::value node_settings_json() const;
  web::json::value available_registries_json() const;
  web::json::value update_node_config(const web::json::value &patch,
                                      bool replace_entire_document);

private:
  void restart_node_runtime();
  void schedule_sync(std::int64_t revision);
  void sync_loop();
  void perform_sync(std::int64_t revision, bool is_retry_attempt);
  void handle_ws_connected();
  void handle_ws_disconnected();
  void handle_ws_error(const std::string &message);
  void handle_receiver_event(const ReceiverEvent &event);
  void handle_registration_event(const RegistrationEvent &event);
  void set_node_state(const std::string &state);

  std::string config_path_;
  std::atomic<bool> stop_requested_{false};
  bool started_{false};

  DaemonConfig config_;
  StateStore state_store_;
  std::unique_ptr<NodeRuntime> node_runtime_;
  std::unique_ptr<SnapshotClient> snapshot_client_;
  std::unique_ptr<WsClient> ws_client_;
  std::unique_ptr<HttpDebugServer> http_debug_server_;
  ReconcileEngine reconcile_engine_;

  std::thread sync_thread_;
  std::mutex sync_mutex_;
  std::condition_variable sync_cv_;
  mutable std::mutex node_config_mutex_;
  bool sync_pending_{false};
  bool force_sync_pending_{false};
  std::int64_t pending_revision_{0};
  std::chrono::steady_clock::time_point last_signal_time_{};
  std::chrono::steady_clock::time_point next_sync_not_before_{};
  std::mutex reconcile_mutex_;
  std::string last_node_state_{"stopped"};
};

}
