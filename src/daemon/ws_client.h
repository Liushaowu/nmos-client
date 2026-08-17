#pragma once

#include "dto.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace web::websockets::client {
class websocket_callback_client;
class websocket_client;
}

namespace seeder::nmos_sync {

class ConnectionResultWaiter {
public:
  void register_request(std::string request_id, std::string receiver_id);
  void remove_request(const std::string &request_id);
  void complete(const ConnectionResultMessage &message);
  void cancel_all(const std::string &reason);
  void wait_for_result(const std::string &request_id,
                       std::chrono::milliseconds timeout);

private:
  struct PendingResult {
    std::string receiver_id;
    bool completed = false;
    bool canceled = false;
    bool success = false;
    std::string reason;
    std::condition_variable cv;
  };

  std::mutex mutex_;
  std::map<std::string, std::shared_ptr<PendingResult>> pending_;
};

struct WsClientCallbacks {
  std::function<void()> on_connected;
  std::function<void()> on_disconnected;
  std::function<void(const SnapshotChangedMessage &message)> on_snapshot_changed;
  std::function<void(const std::string &message)> on_error;
};

class WsClient {
public:
  WsClient(std::string ws_url, int reconnect_interval_ms,
           int heartbeat_interval_ms, int heartbeat_timeout_ms);
  ~WsClient();

  void set_callbacks(WsClientCallbacks callbacks);
  void start();
  void stop();

  bool send_json(const web::json::value &message);
  void send_json_and_wait_for_connection_result(
      const web::json::value &message, const std::string &request_id,
      const std::string &receiver_id, std::chrono::milliseconds timeout);
  bool is_connected() const;

private:
  struct QueuedMessage {
    enum class Type { text, ping };

    Type type = Type::text;
    std::string payload;
  };

  void run();
  void send_loop();
  void heartbeat_loop();
  void report_error(const std::string &message);
  void close_active_client();
  bool queue_message(QueuedMessage message);
  bool send_text_now(const std::string &payload);

  std::string ws_url_;
  int reconnect_interval_ms_;
  int heartbeat_interval_ms_;
  int heartbeat_timeout_ms_;
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> connected_{false};
  std::atomic<bool> connection_closed_{false};
  std::thread receive_worker_;
  std::thread send_worker_;
  std::thread heartbeat_worker_;
  mutable std::mutex client_mutex_;
  std::shared_ptr<web::websockets::client::websocket_callback_client> client_;
  std::mutex send_mutex_;
  mutable std::mutex activity_mutex_;
  std::chrono::steady_clock::time_point last_activity_;
  std::mutex send_queue_mutex_;
  std::condition_variable send_queue_cv_;
  std::deque<QueuedMessage> send_queue_;
  ConnectionResultWaiter connection_results_;
  WsClientCallbacks callbacks_;
  std::mutex callbacks_mutex_;
};

}
