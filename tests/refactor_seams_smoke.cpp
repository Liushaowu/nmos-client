#include "node_callback_dispatcher.h"
#include "node_connection_validation.h"
#include "node_connection_transport_params.h"
#include "daemon/dto.h"
#include "daemon/config.h"
#include "daemon/ws_client.h"
#include "node_sdp_service.h"
#include "node_server_runtime.h"
#include "node_stream_store.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <nmos/activation_mode.h>
#include <nmos/connection_resources.h>
#include <nmos/json_fields.h>
#include <nmos/log_gate.h>
#include <stdexcept>
#include <sstream>
#include <string>
#include <thread>

using namespace seeder::nmos_node;
using namespace seeder::nmos_sync;

namespace
{
  void check_stream_store_sender_ids()
  {
    internal::StreamStore store;
    store.add_sender_resource_ids(U("sender-1"), U("source-1"), U("flow-1"));

    assert(store.has_sender_id(U("sender-1")));
    assert(1 == store.sender_ids().size());
    assert(1 == store.source_ids().size());
    assert(1 == store.flow_ids().size());

    store.remove_sender_resource_ids(U("sender-1"), U("source-1"), U("flow-1"));
    assert(!store.has_sender_id(U("sender-1")));
    assert(store.sender_ids().empty());
    assert(store.source_ids().empty());
    assert(store.flow_ids().empty());
  }

  void check_stream_store_stream_cache()
  {
    internal::StreamStore store;
    VideoSender sender;
    sender.id = "video-a";
    sender.sender_id = "sender-a";
    store.add(sender);

    assert(nullptr != store.find_video_sender_by_id("video-a"));
    assert(nullptr != store.find_video_sender_by_sender_id("sender-a"));

    sender.name = "renamed";
    assert(store.replace(sender));
    assert("renamed" == store.find_video_sender_by_id("video-a")->name);

    store.remove_video_sender_by_sender_id("sender-a");
    assert(nullptr == store.find_video_sender_by_id("video-a"));
  }

  void check_callback_dispatcher_snapshots()
  {
    internal::CallbackDispatcher dispatcher;
    bool called = false;
    dispatcher.set_update_audio_sender_callback(
        [&](const AudioSender &)
        {
          called = true;
        });

    const auto callbacks = dispatcher.sender_callbacks();
    assert(static_cast<bool>(callbacks.audio));

    AudioSender audio;
    callbacks.audio(audio);
    assert(called);
  }

  void check_callback_dispatcher_receiver_validation_handler()
  {
    internal::CallbackDispatcher dispatcher;
    bool called = false;
    dispatcher.set_receiver_connection_validation_handler(
        [&](const ReceiverEvent &)
        {
          called = true;
        });

    const auto handler = dispatcher.receiver_connection_validation_handler();
    assert(static_cast<bool>(handler));
    handler(ReceiverEvent{VideoReceiver{}});
    assert(called);
  }

  void check_sdp_transport_helpers()
  {
    auto missing_ip = web::json::value::object();
    const auto with_default_ip =
        internal::NodeSdpService::transport_param_with_valid_destination_ip(
            missing_ip);
    assert(U("0.0.0.0") ==
           with_default_ip.at(nmos::fields::destination_ip).as_string());

    auto enabled = web::json::value::object();
    enabled[nmos::fields::rtp_enabled] = web::json::value::boolean(true);
    assert(internal::NodeSdpService::transport_param_rtp_enabled(enabled, false));

    auto absent = web::json::value::object();
    assert(internal::NodeSdpService::transport_param_rtp_enabled(absent, true));
  }

  void check_connection_handler_helpers()
  {
    using TransportParamsState = internal::connection_transport_params::State;

    auto endpoint = web::json::value::object();
    endpoint[nmos::fields::transport_params] = web::json::value::array(1);
    assert(internal::connection_transport_params::active_leg_count_matches(
        endpoint, false));
    assert(!internal::connection_transport_params::active_leg_count_matches(
        endpoint, true));

    const auto &transport_params = endpoint.at(nmos::fields::transport_params);
    assert(internal::connection_transport_params::is_array(
        transport_params));
    assert(!internal::connection_transport_params::is_empty(
        transport_params));
    assert(TransportParamsState::ready ==
           internal::connection_transport_params::state(
               transport_params));

    const auto empty_transport_params = web::json::value::array();
    assert(TransportParamsState::empty ==
           internal::connection_transport_params::state(
               empty_transport_params));

    const auto invalid_transport_params = web::json::value::object();
    assert(TransportParamsState::not_array ==
           internal::connection_transport_params::state(
               invalid_transport_params));
  }

  void check_runtime_helper_linkage()
  {
    internal::NodeServerRuntime::log_stop_elapsed(
        std::chrono::steady_clock::now(), "smoke");
  }

  void check_connection_result_dto()
  {
    auto success = web::json::value::object();
    success[U("type")] = web::json::value::string(U("connection.validation.result"));
    success[U("request_id")] = web::json::value::string(U("req-1"));
    success[U("receiver_id")] = web::json::value::string(U("rx-1"));
    success[U("success")] = web::json::value::boolean(true);

    const auto parsed_success = connection_validation_result_message_from_json(success);
    assert(parsed_success.has_value());
    assert("req-1" == parsed_success->request_id);
    assert("rx-1" == parsed_success->receiver_id);
    assert(parsed_success->success);
    assert(parsed_success->reason.empty());

    auto failure = web::json::value::object();
    failure[U("type")] = web::json::value::string(U("connection.validation.result"));
    failure[U("request_id")] = web::json::value::string(U("req-2"));
    failure[U("receiver_id")] = web::json::value::string(U("rx-2"));
    failure[U("success")] = web::json::value::boolean(false);
    failure[U("reason")] = web::json::value::string(U("activation rejected"));

    const auto parsed_failure = connection_validation_result_message_from_json(failure);
    assert(parsed_failure.has_value());
    assert(!parsed_failure->success);
    assert("rx-2" == parsed_failure->receiver_id);
    assert("activation rejected" == parsed_failure->reason);

    auto ignored = web::json::value::object();
    ignored[U("type")] = web::json::value::string(U("data.changed"));
    assert(!connection_validation_result_message_from_json(ignored).has_value());

    auto invalid = web::json::value::object();
    invalid[U("type")] = web::json::value::string(U("connection.validation.result"));
    invalid[U("success")] = web::json::value::boolean(true);
    try
    {
      connection_validation_result_message_from_json(invalid);
      assert(false);
    }
    catch (const std::runtime_error &)
    {
    }

    VideoReceiver receiver;
    receiver.id = "rx-1";
    const auto message = make_receiver_video_observed_validation_message(
        receiver, "req-3", "rx-1");
    assert(U("receiver.video.observed_validation") ==
           message.at(U("type")).as_string());
    assert(U("req-3") == message.at(U("request_id")).as_string());
    assert(U("rx-1") == message.at(U("receiver_id")).as_string());
  }

  void check_connection_result_waiter()
  {
    ConnectionResultWaiter waiter;
    waiter.register_request("req-success", "rx-success");
    std::thread success_thread([&waiter]
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      waiter.complete(ConnectionResultMessage{"req-success", "rx-success",
                                              true, {}});
    });
    waiter.wait_for_result("req-success", std::chrono::milliseconds(20));
    success_thread.join();

    waiter.register_request("req-failure", "rx-failure");
    waiter.complete(
        ConnectionResultMessage{"req-failure", "rx-failure", false,
                                "activation rejected"});
    try
    {
      waiter.wait_for_result("req-failure", std::chrono::milliseconds(20));
      assert(false);
    }
    catch (const std::runtime_error &error)
    {
      assert(std::string(error.what()).find("activation rejected") !=
             std::string::npos);
    }

    waiter.register_request("req-empty-failure", "rx-empty-failure");
    waiter.complete(ConnectionResultMessage{"req-empty-failure",
                                            "rx-empty-failure", false, {}});
    try
    {
      waiter.wait_for_result("req-empty-failure",
                             std::chrono::milliseconds(20));
      assert(false);
    }
    catch (const std::runtime_error &error)
    {
      assert(std::string(error.what()).find("req-empty-failure") !=
             std::string::npos);
    }

    waiter.register_request("req-cancel", "rx-cancel");
    waiter.cancel_all("websocket disconnected");
    try
    {
      waiter.wait_for_result("req-cancel", std::chrono::milliseconds(20));
      assert(false);
    }
    catch (const std::runtime_error &error)
    {
      assert(std::string(error.what()).find("websocket disconnected") !=
             std::string::npos);
    }

    waiter.register_request("req-timeout", "rx-timeout");
    try
    {
      waiter.wait_for_result("req-timeout", std::chrono::milliseconds(1));
      assert(false);
    }
    catch (const std::runtime_error &error)
    {
      const std::string message = error.what();
      assert(message.find("1ms timeout") != std::string::npos);
      assert(message.find("req-timeout") != std::string::npos);
    }

    waiter.register_request("req-complete-before-cancel",
                            "rx-complete-before-cancel");
    waiter.complete(
        ConnectionResultMessage{"req-complete-before-cancel",
                                "rx-complete-before-cancel", true, {}});
    waiter.cancel_all("websocket disconnected");
    waiter.wait_for_result("req-complete-before-cancel",
                           std::chrono::milliseconds(20));

    waiter.register_request("req-failure-before-cancel",
                            "rx-failure-before-cancel");
    waiter.complete(ConnectionResultMessage{"req-failure-before-cancel",
                                            "rx-failure-before-cancel", false,
                                            "activation rejected"});
    waiter.cancel_all("websocket disconnected");
    try
    {
      waiter.wait_for_result("req-failure-before-cancel",
                             std::chrono::milliseconds(20));
      assert(false);
    }
    catch (const std::runtime_error &error)
    {
      assert(std::string(error.what()).find("activation rejected") !=
             std::string::npos);
    }

    waiter.register_request("req-first-result-wins", "rx-first-result-wins");
    waiter.complete(ConnectionResultMessage{"req-first-result-wins",
                                            "rx-first-result-wins", true, {}});
    waiter.complete(ConnectionResultMessage{"req-first-result-wins",
                                            "rx-first-result-wins", false,
                                            "late failure"});
    waiter.wait_for_result("req-first-result-wins",
                           std::chrono::milliseconds(20));

    waiter.register_request("req-duplicate", "rx-duplicate");
    try
    {
      waiter.register_request("req-duplicate", "rx-duplicate");
      assert(false);
    }
    catch (const std::runtime_error &error)
    {
      assert(std::string(error.what()).find("already registered") !=
             std::string::npos);
    }
    waiter.complete(ConnectionResultMessage{"req-duplicate", "rx-duplicate",
                                            true, {}});
    waiter.wait_for_result("req-duplicate", std::chrono::milliseconds(20));

    waiter.register_request("req-bound", "rx-bound");
    waiter.complete(ConnectionResultMessage{"req-bound", "wrong-rx", true, {}});
    try
    {
      waiter.wait_for_result("req-bound", std::chrono::milliseconds(1));
      assert(false);
    }
    catch (const std::runtime_error &error)
    {
      assert(std::string(error.what()).find("1ms timeout") !=
             std::string::npos);
    }
  }

  void check_ws_client_wait_requires_connected_socket()
  {
    WsClient client("ws://127.0.0.1:1", 1, 1, 1);
    try
    {
      client.send_json_and_wait_for_connection_result(
          web::json::value::object(), "req-not-connected",
          "rx-not-connected", std::chrono::milliseconds(5));
      assert(false);
    }
    catch (const std::runtime_error &error)
    {
      assert(std::string(error.what()).find("req-not-connected") !=
             std::string::npos);
    }
  }

  web::json::value make_daemon_config_with_device_apis(
      const std::string &device_http_api, const std::string &device_ws_api)
  {
    web::json::value config = web::json::value::object();
    config[U("node_config_path")] = web::json::value::string(U("node.json"));
    config[U("device_http_api")] = web::json::value::string(
        utility::conversions::to_string_t(device_http_api));
    config[U("device_ws_api")] = web::json::value::string(
        utility::conversions::to_string_t(device_ws_api));
    return config;
  }

  void check_daemon_config_device_apis_derivation()
  {
    const auto temp = std::filesystem::temp_directory_path();
    const auto localhost_path = temp / "nmos-daemon-localhost-http-ws.json";
    const auto remote_path = temp / "nmos-daemon-remote-https-wss.json";
    const auto port_slash_path = temp / "nmos-daemon-port-slash.json";

    DaemonConfig::save_to_file(
        localhost_path.string(),
        make_daemon_config_with_device_apis("http://127.0.0.1",
                                                 "ws://127.0.0.1"));
    const auto localhost_config =
        DaemonConfig::load_from_file(localhost_path.string());
    assert("http://127.0.0.1" == localhost_config.device_http_api);
    assert("ws://127.0.0.1" == localhost_config.device_ws_api);
    assert("http://127.0.0.1/api/data/nmos" ==
           localhost_config.snapshot_url());
    assert("ws://127.0.0.1/ws/nmos" == localhost_config.ws_url());

    DaemonConfig::save_to_file(
        remote_path.string(),
        make_daemon_config_with_device_apis("https://example.com",
                                                 "wss://example.com"));
    const auto remote_config =
        DaemonConfig::load_from_file(remote_path.string());
    assert("https://example.com/api/data/nmos" ==
           remote_config.snapshot_url());
    assert("wss://example.com/ws/nmos" == remote_config.ws_url());

    DaemonConfig::save_to_file(
        port_slash_path.string(),
        make_daemon_config_with_device_apis("https://example.com:8443/",
                                                 "wss://example.com:8443/"));
    const auto port_slash_config =
        DaemonConfig::load_from_file(port_slash_path.string());
    assert("https://example.com:8443" == port_slash_config.device_http_api);
    assert("wss://example.com:8443" == port_slash_config.device_ws_api);
    assert("https://example.com:8443/api/data/nmos" ==
           port_slash_config.snapshot_url());
    assert("wss://example.com:8443/ws/nmos" == port_slash_config.ws_url());

    const auto saved = port_slash_config.to_json();
    assert(saved.has_field(U("device_http_api")));
    assert(saved.has_field(U("device_ws_api")));
    assert(!saved.has_field(U("device_server")));
    assert(!saved.has_field(U("snapshot_url")));
    assert(!saved.has_field(U("ws_url")));

    try
    {
      web::json::value missing_device_http_api = web::json::value::object();
      missing_device_http_api[U("node_config_path")] =
          web::json::value::string(U("node.json"));
      missing_device_http_api[U("device_ws_api")] =
          web::json::value::string(U("ws://127.0.0.1"));
      DaemonConfig::from_json(missing_device_http_api);
      assert(false);
    }
    catch (const std::runtime_error &error)
    {
      assert(std::string(error.what()).find("device_http_api") !=
             std::string::npos);
    }

    try
    {
      web::json::value missing_device_ws_api = web::json::value::object();
      missing_device_ws_api[U("node_config_path")] =
          web::json::value::string(U("node.json"));
      missing_device_ws_api[U("device_http_api")] =
          web::json::value::string(U("http://127.0.0.1"));
      DaemonConfig::from_json(missing_device_ws_api);
      assert(false);
    }
    catch (const std::runtime_error &error)
    {
      assert(std::string(error.what()).find("device_ws_api") !=
             std::string::npos);
    }

    try
    {
      web::json::value old_config = web::json::value::object();
      old_config[U("node_config_path")] =
          web::json::value::string(U("node.json"));
      old_config[U("device_server")] =
          web::json::value::string(U("http://127.0.0.1"));
      DaemonConfig::from_json(old_config);
      assert(false);
    }
    catch (const std::runtime_error &error)
    {
      assert(std::string(error.what()).find("device_http_api") !=
             std::string::npos);
    }

    try
    {
      DaemonConfig::from_json(
          make_daemon_config_with_device_apis("ws://127.0.0.1",
                                                   "ws://127.0.0.1"));
      assert(false);
    }
    catch (const std::runtime_error &error)
    {
      assert(std::string(error.what()).find("http:// or https://") !=
             std::string::npos);
    }

    try
    {
      DaemonConfig::from_json(
          make_daemon_config_with_device_apis("http://127.0.0.1",
                                                   "http://127.0.0.1"));
      assert(false);
    }
    catch (const std::runtime_error &error)
    {
      assert(std::string(error.what()).find("ws:// or wss://") !=
             std::string::npos);
    }

    try
    {
      DaemonConfig::from_json(
          make_daemon_config_with_device_apis("http:///api",
                                                   "ws://127.0.0.1"));
      assert(false);
    }
    catch (const std::runtime_error &error)
    {
      assert(std::string(error.what()).find("host") != std::string::npos);
    }

    try
    {
      DaemonConfig::from_json(
          make_daemon_config_with_device_apis("https://example.com/base",
                                                   "wss://example.com"));
      assert(false);
    }
    catch (const std::runtime_error &error)
    {
      assert(std::string(error.what()).find("origin") != std::string::npos);
    }
  }

  web::json::value make_immediate_receiver_endpoint_staged()
  {
    auto endpoint = web::json::value::object();
    endpoint[nmos::fields::master_enable] = web::json::value::boolean(true);
    endpoint[nmos::fields::activation][nmos::fields::mode] =
        web::json::value::string(nmos::activation_modes::activate_immediate.name);
    endpoint[nmos::fields::transport_params] = web::json::value::array(1);
    endpoint[nmos::fields::transport_params][0][nmos::fields::interface_ip] =
        web::json::value::string(U("192.0.0.1"));
    endpoint[nmos::fields::transport_params][0][nmos::fields::multicast_ip] =
        web::json::value::string(U("0.0.0.0"));
    endpoint[nmos::fields::transport_params][0][nmos::fields::destination_port] =
        web::json::value::number(5004);
    endpoint[nmos::fields::transport_params][0][nmos::fields::rtp_enabled] =
        web::json::value::boolean(true);
    return endpoint;
  }

  internal::ActivationContext make_validation_context(
      internal::StreamStore &store,
      internal::CallbackDispatcher &callbacks,
      nmos::experimental::log_gate *&gate,
      std::mutex &receiver_mutex,
      std::mutex &sender_mutex,
      nmos::id &node_id,
      int &ptp_domain_number,
      RuntimeInterfaces &runtime_interfaces,
      std::mutex &runtime_interfaces_mutex)
  {
    return internal::ActivationContext{store, callbacks, gate, receiver_mutex,
                                       sender_mutex, node_id,
                                       ptp_domain_number,
                                       runtime_interfaces,
                                       runtime_interfaces_mutex};
  }

  void check_connection_validator_ignores_sender_resources()
  {
    internal::StreamStore store;
    internal::CallbackDispatcher callbacks;
    bool called = false;
    callbacks.set_receiver_connection_validation_handler(
        [&](const ReceiverEvent &)
        {
          called = true;
        });

    nmos::experimental::log_model log_model;
    std::ostringstream error_log;
    std::ostringstream access_log;
    nmos::experimental::log_gate log_gate(error_log, access_log, log_model);
    auto *gate_ptr = &log_gate;
    std::mutex receiver_mutex;
    std::mutex sender_mutex;
    std::mutex runtime_interfaces_mutex;
    RuntimeInterfaces runtime_interfaces;
    nmos::id node_id = U("node-1");
    int ptp_domain_number = 127;
    nmos::settings settings;

    const auto validator = internal::make_connection_resource_patch_validator(
        settings, make_validation_context(store, callbacks, gate_ptr,
                                          receiver_mutex, sender_mutex, node_id,
                                          ptp_domain_number, runtime_interfaces,
                                          runtime_interfaces_mutex));

    auto sender = nmos::resource(nmos::api_version{1, 3}, nmos::types::sender,
                                 web::json::value::object(), U("sender-1"),
                                 true);
    auto connection_sender = nmos::make_connection_rtp_sender(U("sender-1"), false);
    validator(sender, connection_sender, make_immediate_receiver_endpoint_staged(),
              log_gate);
    assert(!called);
  }

  void check_connection_validator_rejects_receiver_immediate_activation()
  {
    internal::StreamStore store;
    VideoReceiver receiver;
    receiver.id = "rx-1";
    receiver.name = "Receiver 1";
    store.add(receiver, U("receiver-1"));

    internal::CallbackDispatcher callbacks;
    bool called = false;
    callbacks.set_receiver_connection_validation_handler(
        [&](const ReceiverEvent &event)
        {
          called = true;
          const auto *video = std::get_if<VideoReceiver>(&event.payload);
          assert(video);
          assert(video->enable);
          assert("192.0.0.1" == video->source_ip);
          assert("0.0.0.0" == video->ip);
          assert(5004 == video->port);
          throw std::runtime_error("backend rejected receiver activation");
        });

    nmos::experimental::log_model log_model;
    std::ostringstream error_log;
    std::ostringstream access_log;
    nmos::experimental::log_gate log_gate(error_log, access_log, log_model);
    auto *gate_ptr = &log_gate;
    std::mutex receiver_mutex;
    std::mutex sender_mutex;
    std::mutex runtime_interfaces_mutex;
    RuntimeInterfaces runtime_interfaces;
    nmos::id node_id = U("node-1");
    int ptp_domain_number = 127;
    nmos::settings settings;

    const auto validator = internal::make_connection_resource_patch_validator(
        settings, make_validation_context(store, callbacks, gate_ptr,
                                          receiver_mutex, sender_mutex, node_id,
                                          ptp_domain_number, runtime_interfaces,
                                          runtime_interfaces_mutex));

    auto receiver_resource = nmos::resource(nmos::api_version{1, 3},
                                            nmos::types::receiver,
                                            web::json::value::object(),
                                            U("receiver-1"), true);
    auto connection_receiver =
        nmos::make_connection_rtp_receiver(U("receiver-1"), false);
    try
    {
      validator(receiver_resource, connection_receiver,
                make_immediate_receiver_endpoint_staged(), log_gate);
      assert(false);
    }
    catch (const web::json::json_exception &error)
    {
      assert(called);
      assert(std::string(error.what()).find(
                 "backend rejected receiver activation") != std::string::npos);
    }
  }

  void check_connection_validator_rejects_invalid_receiver_transport_params()
  {
    internal::StreamStore store;
    VideoReceiver receiver;
    receiver.id = "rx-1";
    store.add(receiver, U("receiver-1"));

    internal::CallbackDispatcher callbacks;
    bool called = false;
    callbacks.set_receiver_connection_validation_handler(
        [&](const ReceiverEvent &)
        {
          called = true;
        });

    nmos::experimental::log_model log_model;
    std::ostringstream error_log;
    std::ostringstream access_log;
    nmos::experimental::log_gate log_gate(error_log, access_log, log_model);
    auto *gate_ptr = &log_gate;
    std::mutex receiver_mutex;
    std::mutex sender_mutex;
    std::mutex runtime_interfaces_mutex;
    RuntimeInterfaces runtime_interfaces;
    nmos::id node_id = U("node-1");
    int ptp_domain_number = 127;
    nmos::settings settings;

    const auto validator = internal::make_connection_resource_patch_validator(
        settings, make_validation_context(store, callbacks, gate_ptr,
                                          receiver_mutex, sender_mutex, node_id,
                                          ptp_domain_number, runtime_interfaces,
                                          runtime_interfaces_mutex));

    auto endpoint = make_immediate_receiver_endpoint_staged();
    endpoint[nmos::fields::transport_params] = web::json::value::object();
    auto receiver_resource = nmos::resource(nmos::api_version{1, 3},
                                            nmos::types::receiver,
                                            web::json::value::object(),
                                            U("receiver-1"), true);
    auto connection_receiver =
        nmos::make_connection_rtp_receiver(U("receiver-1"), false);
    try
    {
      validator(receiver_resource, connection_receiver, endpoint, log_gate);
      assert(false);
    }
    catch (const web::json::json_exception &error)
    {
      assert(!called);
      assert(std::string(error.what()).find("transport_params") !=
             std::string::npos);
      assert(std::string(error.what()).find("not an array") !=
             std::string::npos);
    }
  }
}

int main()
{
  check_stream_store_sender_ids();
  check_stream_store_stream_cache();
  check_callback_dispatcher_snapshots();
  check_callback_dispatcher_receiver_validation_handler();
  check_sdp_transport_helpers();
  check_connection_handler_helpers();
  check_runtime_helper_linkage();
  check_connection_result_dto();
  check_connection_result_waiter();
  check_ws_client_wait_requires_connected_socket();
  check_daemon_config_device_apis_derivation();
  check_connection_validator_ignores_sender_resources();
  check_connection_validator_rejects_receiver_immediate_activation();
  check_connection_validator_rejects_invalid_receiver_transport_params();
  return 0;
}
