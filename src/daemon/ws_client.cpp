#include "ws_client.h"

#include <cpprest/details/basic_types.h>
#include <cpprest/ws_client.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr int default_heartbeat_interval_ms = 5000;
constexpr int default_heartbeat_timeout_ms = 15000;
constexpr std::size_t max_text_message_bytes = 1024 * 1024;
constexpr std::size_t max_connection_reason_bytes = 512;

utility::string_t to_t(const std::string &value) {
  return utility::conversions::to_string_t(value);
}

std::string to_utf8(const utility::string_t &value) {
  return utility::conversions::to_utf8string(value);
}

int normalize_interval(int value, int fallback) {
  return value > 0 ? value : fallback;
}

std::string sanitize_for_error_message(const std::string &text) {
  std::string result;
  result.reserve(std::min(text.size(), max_connection_reason_bytes));
  for (unsigned char ch : text) {
    if (result.size() >= max_connection_reason_bytes) {
      break;
    }
    result.push_back(std::isprint(ch) ? static_cast<char>(ch) : ' ');
  }
  return result;
}

void log_websocket_message_summary(const web::json::value &json,
                                   std::size_t body_size) {
  const auto type_field = to_t("type");
  const auto request_id_field = to_t("request_id");
  std::string type;
  std::string request_id;
  if (json.is_object() && json.has_field(type_field) &&
      json.at(type_field).is_string()) {
    type = to_utf8(json.at(type_field).as_string());
  }
  if (json.is_object() && json.has_field(request_id_field) &&
      json.at(request_id_field).is_string()) {
    request_id = to_utf8(json.at(request_id_field).as_string());
  }

  std::cout << "nmos-sync-daemon websocket message: bytes=" << body_size;
  if (!type.empty()) {
    std::cout << ", type=" << sanitize_for_error_message(type);
  }
  if (!request_id.empty()) {
    std::cout << ", request_id=" << sanitize_for_error_message(request_id);
  }
  std::cout << std::endl;
}

}

namespace seeder::nmos_sync {

void ConnectionResultWaiter::register_request(std::string request_id,
                                              std::string receiver_id) {
  if (request_id.empty()) {
    throw std::runtime_error("connection result request_id is empty");
  }
  if (receiver_id.empty()) {
    throw std::runtime_error("connection result receiver_id is empty");
  }

  auto pending = std::make_shared<PendingResult>();
  pending->receiver_id = std::move(receiver_id);
  std::lock_guard<std::mutex> lock(mutex_);
  if (pending_.find(request_id) != pending_.end()) {
    throw std::runtime_error("connection result request already registered: " +
                             request_id);
  }
  pending_[std::move(request_id)] = std::move(pending);
}

void ConnectionResultWaiter::remove_request(const std::string &request_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  pending_.erase(request_id);
}

void ConnectionResultWaiter::complete(const ConnectionResultMessage &message) {
  std::shared_ptr<PendingResult> pending;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = pending_.find(message.request_id);
    if (found == pending_.end()) {
      return;
    }
    pending = found->second;
    if (pending->completed || pending->canceled) {
      return;
    }
    if (pending->receiver_id != message.receiver_id) {
      return;
    }
    pending->completed = true;
    pending->success = message.success;
    pending->reason = sanitize_for_error_message(message.reason);
  }
  pending->cv.notify_all();
}

void ConnectionResultWaiter::cancel_all(const std::string &reason) {
  std::vector<std::shared_ptr<PendingResult>> pending_results;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_results.reserve(pending_.size());
    for (auto &entry : pending_) {
      auto &pending = entry.second;
      if (pending->completed || pending->canceled) {
        continue;
      }
      pending->canceled = true;
      pending->reason = sanitize_for_error_message(reason);
      pending_results.push_back(pending);
    }
  }

  for (auto &pending : pending_results) {
    pending->cv.notify_all();
  }
}

void ConnectionResultWaiter::wait_for_result(
    const std::string &request_id, std::chrono::milliseconds timeout) {
  std::shared_ptr<PendingResult> pending;
  std::unique_lock<std::mutex> lock(mutex_);
  const auto found = pending_.find(request_id);
  if (found == pending_.end()) {
    throw std::runtime_error("connection result request not registered: " +
                             request_id);
  }
  pending = found->second;

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  const bool ready = pending->cv.wait_until(lock, deadline, [&pending] {
    return pending->completed || pending->canceled;
  });

  if (!ready) {
    pending_.erase(request_id);
    throw std::runtime_error(std::to_string(timeout.count()) +
                             "ms timeout waiting for connection result (request: " +
                             request_id + ")");
  }

  const bool completed = pending->completed;
  const bool canceled = pending->canceled;
  const bool success = pending->success;
  std::string reason = pending->reason;
  pending_.erase(request_id);
  lock.unlock();

  if (completed) {
    if (success) {
      return;
    }
    if (reason.empty()) {
      reason = "The destination device refused connection (request: " +
              request_id + ").";
    }
    throw std::runtime_error(reason);
  }
  if (canceled) {
    if (reason.empty()) {
      reason = "The device has disconnected from the connection (request: " +
              request_id + ").";
    }
    throw std::runtime_error(reason);
  }
}

WsClient::WsClient(std::string ws_url, int reconnect_interval_ms,
                   int heartbeat_interval_ms, int heartbeat_timeout_ms)
    : ws_url_(std::move(ws_url)),
      reconnect_interval_ms_(reconnect_interval_ms),
      heartbeat_interval_ms_(normalize_interval(
          heartbeat_interval_ms, default_heartbeat_interval_ms)),
      heartbeat_timeout_ms_(normalize_interval(
          heartbeat_timeout_ms, default_heartbeat_timeout_ms)),
      last_activity_(std::chrono::steady_clock::now()) {}

WsClient::~WsClient() { stop(); }

void WsClient::set_callbacks(WsClientCallbacks callbacks) {
  std::lock_guard<std::mutex> lock(callbacks_mutex_);
  callbacks_ = std::move(callbacks);
}

void WsClient::start() {
  if (receive_worker_.joinable() || send_worker_.joinable() ||
      heartbeat_worker_.joinable()) {
    return;
  }
  stop_requested_ = false;
  receive_worker_ = std::thread([this] { run(); });
  send_worker_ = std::thread([this] { send_loop(); });
  heartbeat_worker_ = std::thread([this] { heartbeat_loop(); });
}

void WsClient::stop() {
  const auto stop_started = std::chrono::steady_clock::now();
  const auto log_elapsed = [&stop_started](const char *step) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - stop_started);
    std::cerr << "nmos-sync-daemon websocket stop: " << step
              << ", elapsed_ms=" << elapsed.count() << std::endl;
  };

  log_elapsed("begin");
  stop_requested_ = true;
  connection_results_.cancel_all("websocket client stopped");
  send_queue_cv_.notify_all();
  std::shared_ptr<web::websockets::client::websocket_callback_client> client;
  {
    std::lock_guard<std::mutex> lock(client_mutex_);
    client = client_;
  }
  if (client) {
    try {
      log_elapsed("requesting active client close");
      client
          ->close(web::websockets::client::websocket_close_status::going_away,
                  to_t("nmos-sync-daemon stopping"))
          .then([client](pplx::task<void> close_task) {
            try {
              close_task.get();
              std::cerr << "nmos-sync-daemon websocket stop: active client closed"
                        << std::endl;
            } catch (const std::exception &error) {
              std::cerr << "nmos-sync-daemon websocket stop: close error: "
                        << error.what() << std::endl;
            } catch (...) {
              std::cerr << "nmos-sync-daemon websocket stop: unknown close error"
                        << std::endl;
            }
          });
      log_elapsed("active client close requested");
    } catch (const std::exception &error) {
      report_error(error.what());
    } catch (...) {
      report_error("unknown websocket close error");
    }
  }
  if (receive_worker_.joinable()) {
    log_elapsed("joining receive worker");
    receive_worker_.join();
    log_elapsed("receive worker joined");
  }
  if (send_worker_.joinable()) {
    log_elapsed("joining send worker");
    send_worker_.join();
    log_elapsed("send worker joined");
  }
  if (heartbeat_worker_.joinable()) {
    log_elapsed("joining heartbeat worker");
    heartbeat_worker_.join();
    log_elapsed("heartbeat worker joined");
  }
  log_elapsed("complete");
}

bool WsClient::send_json(const web::json::value &message) {
  return queue_message(
      QueuedMessage{QueuedMessage::Type::text, to_utf8(message.serialize())});
}

void WsClient::send_json_and_wait_for_connection_result(
    const web::json::value &message, const std::string &request_id,
    const std::string &receiver_id, std::chrono::milliseconds timeout) {
  connection_results_.register_request(request_id, receiver_id);
  try {
    if (!send_text_now(to_utf8(message.serialize()))) {
      connection_results_.remove_request(request_id);
      throw std::runtime_error(
          "failed to send connection result request_id " + request_id);
    }
  } catch (const std::exception &error) {
    connection_results_.remove_request(request_id);
    throw std::runtime_error(
        "failed to send connection result request_id " + request_id + ": " +
        error.what());
  } catch (...) {
    connection_results_.remove_request(request_id);
    throw std::runtime_error(
        "failed to send connection result request_id " + request_id);
  }
  connection_results_.wait_for_result(request_id, timeout);
}

bool WsClient::queue_message(QueuedMessage message) {
  if (!connected_) {
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(send_queue_mutex_);
    send_queue_.push_back(std::move(message));
  }
  send_queue_cv_.notify_one();
  return true;
}

bool WsClient::send_text_now(const std::string &payload) {
  if (!connected_) {
    return false;
  }

  std::shared_ptr<web::websockets::client::websocket_callback_client> client;
  {
    std::lock_guard<std::mutex> lock(client_mutex_);
    client = client_;
  }
  if (!client || !connected_) {
    return false;
  }

  web::websockets::client::websocket_outgoing_message outgoing;
  outgoing.set_utf8_message(payload);
  std::lock_guard<std::mutex> send_lock(send_mutex_);
  client->send(outgoing).get();
  return true;
}

bool WsClient::is_connected() const { return connected_.load(); }

void WsClient::run() {
  while (!stop_requested_) {
    try {
      auto client = std::make_shared<web::websockets::client::websocket_callback_client>();
      client->set_message_handler(
          [this](const web::websockets::client::websocket_incoming_message &incoming) {
            if (stop_requested_) {
              return;
            }
            try {
              {
                std::lock_guard<std::mutex> lock(activity_mutex_);
                last_activity_ = std::chrono::steady_clock::now();
              }
              if (incoming.message_type() ==
                  web::websockets::client::websocket_message_type::pong) {
                return;
              }
              if (incoming.message_type() !=
                  web::websockets::client::websocket_message_type::text_message) {
                return;
              }

              const auto body = incoming.extract_string().get();
              if (body.empty()) {
                return;
              }
              if (body.size() > max_text_message_bytes) {
                report_error("websocket message too large");
                close_active_client();
                return;
              }
              auto json = web::json::value::parse(body);
              log_websocket_message_summary(json, body.size());

              const auto message_type = [&json] {
                const auto type_field = to_t("type");
                if (json.is_object() && json.has_field(type_field) &&
                    json.at(type_field).is_string()) {
                  return to_utf8(json.at(type_field).as_string());
                }
                return std::string{};
              }();
              if (message_type == "connection.validation.result") {
                try {
                  const auto result = connection_validation_result_message_from_json(json);
                  if (result.has_value()) {
                    connection_results_.complete(*result);
                  }
                } catch (const std::exception &error) {
                  report_error(std::string("invalid connection.validation.result message: ") +
                               sanitize_for_error_message(error.what()));
                  return;
                }
              }
              const auto changed = snapshot_changed_message_from_json(json);
              if (changed.has_value()) {
                std::function<void(const SnapshotChangedMessage &message)>
                    on_snapshot_changed;
                {
                  std::lock_guard<std::mutex> lock(callbacks_mutex_);
                  on_snapshot_changed = callbacks_.on_snapshot_changed;
                }
                if (on_snapshot_changed) {
                  on_snapshot_changed(*changed);
                }
              }
            } catch (const std::exception &error) {
              report_error(error.what());
              close_active_client();
            } catch (...) {
              report_error("unknown websocket message handling error");
              close_active_client();
            }
          });
      client->set_close_handler(
          [this](web::websockets::client::websocket_close_status,
                 const utility::string_t &, const std::error_code &) {
            connection_closed_ = true;
            connection_results_.cancel_all("websocket disconnected");
          });
      client->connect(web::uri(to_t(ws_url_))).wait();
      {
        std::lock_guard<std::mutex> lock(client_mutex_);
        client_ = client;
      }
      {
        std::lock_guard<std::mutex> lock(activity_mutex_);
        last_activity_ = std::chrono::steady_clock::now();
      }
      connected_ = true;

      std::function<void()> on_connected;
      {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        on_connected = callbacks_.on_connected;
      }
      if (on_connected) {
        on_connected();
      }

      while (!stop_requested_ && !connection_closed_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    } catch (const std::exception &error) {
      report_error(error.what());
    }

    connection_closed_ = false;
    const bool was_connected = connected_.exchange(false);
    {
      std::lock_guard<std::mutex> lock(client_mutex_);
      client_.reset();
    }
    {
      std::lock_guard<std::mutex> lock(send_queue_mutex_);
      send_queue_.clear();
    }
    send_queue_cv_.notify_all();
    connection_results_.cancel_all("websocket disconnected");
    if (was_connected) {
      std::function<void()> on_disconnected;
      {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        on_disconnected = callbacks_.on_disconnected;
      }
      if (on_disconnected) {
        on_disconnected();
      }
    }

    if (!stop_requested_) {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(reconnect_interval_ms_));
    }
  }
}

void WsClient::heartbeat_loop() {
  while (!stop_requested_) {
    auto remaining_sleep = heartbeat_interval_ms_;
    while (!stop_requested_ && remaining_sleep > 0) {
      const auto sleep_ms = std::min(remaining_sleep, 100);
      std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
      remaining_sleep -= sleep_ms;
    }
    if (stop_requested_) {
      return;
    }
    if (!connected_) {
      continue;
    }

    const auto now = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_activity;
    {
      std::lock_guard<std::mutex> lock(activity_mutex_);
      last_activity = last_activity_;
    }

    const auto idle_for = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_activity);
    if (idle_for > std::chrono::milliseconds(heartbeat_timeout_ms_)) {
      report_error("websocket heartbeat timeout");
      close_active_client();
      continue;
    }

    if (!queue_message(
            QueuedMessage{QueuedMessage::Type::ping, "nmos-sync-heartbeat"})) {
      report_error("failed to queue websocket ping");
      close_active_client();
    }
  }
}

void WsClient::send_loop() {
  while (!stop_requested_) {
    QueuedMessage message;
    {
      std::unique_lock<std::mutex> lock(send_queue_mutex_);
      send_queue_cv_.wait(lock, [this] {
        return stop_requested_ || !send_queue_.empty();
      });
      if (stop_requested_) {
        return;
      }
      message = std::move(send_queue_.front());
      send_queue_.pop_front();
    }

    std::shared_ptr<web::websockets::client::websocket_callback_client> client;
    {
      std::lock_guard<std::mutex> lock(client_mutex_);
      client = client_;
    }

    if (!client || !connected_) {
      continue;
    }

    try {
      web::websockets::client::websocket_outgoing_message outgoing;
      if (message.type == QueuedMessage::Type::ping) {
        outgoing.set_ping_message(message.payload);
      } else {
        outgoing.set_utf8_message(message.payload);
      }
      std::lock_guard<std::mutex> send_lock(send_mutex_);
      client->send(outgoing).then([client](pplx::task<void> send_task) {
        try {
          send_task.get();
        } catch (const std::exception &error) {
          std::cerr << "nmos-sync-daemon websocket send error: "
                    << error.what() << std::endl;
        } catch (...) {
          std::cerr << "nmos-sync-daemon websocket unknown send error"
                    << std::endl;
        }
      });
    } catch (const std::exception &error) {
      report_error(error.what());
      close_active_client();
    }
  }
}

void WsClient::report_error(const std::string &message) {
  std::function<void(const std::string &message)> on_error;
  {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    on_error = callbacks_.on_error;
  }
  if (on_error) {
    on_error(message);
  }
}

void WsClient::close_active_client() {
  std::shared_ptr<web::websockets::client::websocket_callback_client> client;
  {
    std::lock_guard<std::mutex> lock(client_mutex_);
    client = client_;
  }
  if (!client) {
    return;
  }
  try {
    client
        ->close(web::websockets::client::websocket_close_status::going_away,
                to_t("nmos-sync-daemon reconnecting"))
        .then([client](pplx::task<void> close_task) {
          try {
            close_task.get();
          } catch (const std::exception &error) {
            std::cerr << "nmos-sync-daemon websocket close error: "
                      << error.what() << std::endl;
          } catch (...) {
            std::cerr << "nmos-sync-daemon websocket unknown close error"
                      << std::endl;
          }
        });
  } catch (const std::exception &error) {
    report_error(error.what());
  } catch (...) {
    report_error("unknown websocket close error");
  }
}

}
