#include "ws_client.h"

#include <cpprest/details/basic_types.h>
#include <cpprest/ws_client.h>

#include <chrono>
#include <iostream>
#include <utility>
#include <thread>

namespace {

utility::string_t to_t(const std::string &value) {
  return utility::conversions::to_string_t(value);
}

std::string to_utf8(const utility::string_t &value) {
  return utility::conversions::to_utf8string(value);
}

}

namespace seeder::nmos_sync {

WsClient::WsClient(std::string ws_url, int reconnect_interval_ms)
    : ws_url_(std::move(ws_url)),
      reconnect_interval_ms_(reconnect_interval_ms) {}

WsClient::~WsClient() { stop(); }

void WsClient::set_callbacks(WsClientCallbacks callbacks) {
  std::lock_guard<std::mutex> lock(callbacks_mutex_);
  callbacks_ = std::move(callbacks);
}

void WsClient::start() {
  if (receive_worker_.joinable() || send_worker_.joinable()) {
    return;
  }
  stop_requested_ = false;
  receive_worker_ = std::thread([this] { run(); });
  send_worker_ = std::thread([this] { send_loop(); });
}

void WsClient::stop() {
  stop_requested_ = true;
  send_queue_cv_.notify_all();
  std::shared_ptr<web::websockets::client::websocket_client> client;
  {
    std::lock_guard<std::mutex> lock(client_mutex_);
    client = client_;
  }
  if (client) {
    try {
      client->close().wait();
    } catch (...) {
    }
  }
  if (receive_worker_.joinable()) {
    receive_worker_.join();
  }
  if (send_worker_.joinable()) {
    send_worker_.join();
  }
}

bool WsClient::send_json(const web::json::value &message) {
  if (!connected_) {
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(send_queue_mutex_);
    send_queue_.push_back(to_utf8(message.serialize()));
  }
  send_queue_cv_.notify_one();
  return true;
}

bool WsClient::is_connected() const { return connected_.load(); }

void WsClient::run() {
  while (!stop_requested_) {
    try {
      auto client = std::make_shared<web::websockets::client::websocket_client>();
      client->connect(web::uri(to_t(ws_url_))).wait();
      {
        std::lock_guard<std::mutex> lock(client_mutex_);
        client_ = client;
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

      while (!stop_requested_) {
        std::shared_ptr<web::websockets::client::websocket_client> active_client;
        {
          std::lock_guard<std::mutex> lock(client_mutex_);
          active_client = client_;
        }
        if (!active_client) {
          break;
        }

        auto incoming = active_client->receive().get();
        const auto body = incoming.extract_string().get();
        std::cout << "nmos-sync-daemon websocket message: " << body << std::endl;
        auto json = web::json::value::parse(body);
        const auto changed = snapshot_changed_message_from_json(json);
        if (changed.has_value()) {
          std::function<void(const SnapshotChangedMessage &message)> on_snapshot_changed;
          {
            std::lock_guard<std::mutex> lock(callbacks_mutex_);
            on_snapshot_changed = callbacks_.on_snapshot_changed;
          }
          if (on_snapshot_changed) {
            on_snapshot_changed(*changed);
          }
        }
      }
    } catch (const std::exception &error) {
      report_error(error.what());
    }

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

void WsClient::send_loop() {
  while (!stop_requested_) {
    std::string payload;
    {
      std::unique_lock<std::mutex> lock(send_queue_mutex_);
      send_queue_cv_.wait(lock, [this] {
        return stop_requested_ || !send_queue_.empty();
      });
      if (stop_requested_) {
        return;
      }
      payload = std::move(send_queue_.front());
      send_queue_.pop_front();
    }

    std::shared_ptr<web::websockets::client::websocket_client> client;
    {
      std::lock_guard<std::mutex> lock(client_mutex_);
      client = client_;
    }

    if (!client || !connected_) {
      continue;
    }

    try {
      web::websockets::client::websocket_outgoing_message outgoing;
      outgoing.set_utf8_message(payload);
      client->send(outgoing).wait();
    } catch (const std::exception &error) {
      report_error(error.what());
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

}
