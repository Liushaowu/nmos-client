#include "node_callback_dispatcher.h"
#include "node_connection_transport_params.h"
#include "node_sdp_service.h"
#include "node_server_runtime.h"
#include "node_stream_store.h"

#include <cassert>
#include <chrono>
#include <nmos/json_fields.h>
#include <string>

using namespace seeder::nmos_node;

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
}

int main()
{
  check_stream_store_sender_ids();
  check_stream_store_stream_cache();
  check_callback_dispatcher_snapshots();
  check_sdp_transport_helpers();
  check_connection_handler_helpers();
  check_runtime_helper_linkage();
  return 0;
}
