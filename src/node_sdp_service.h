#pragma once

#include "node_types.h"

#include <cpprest/json.h>
#include <slog/all_in_one.h>

namespace seeder::nmos_node::internal
{
  class NodeSdpService
  {
  public:
    static web::json::value transport_param_with_valid_destination_ip(
        const web::json::value &transport_param);

    static bool transport_param_rtp_enabled(
        const web::json::value &transport_param,
        bool fallback);

    static bool update_video_receiver_from_transport_file(
        VideoReceiver &receiver,
        const web::json::value &transport_file,
        slog::base_gate &gate);

    static bool update_audio_receiver_from_transport_file(
        AudioReceiver &receiver,
        const web::json::value &transport_file,
        slog::base_gate &gate);
  };
}
