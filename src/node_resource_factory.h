#pragma once

#include "node_types.h"

#include <cpprest/host_utils.h>
#include <cpprest/json.h>
#include <nmos/id.h>
#include <nmos/resource.h>

#include <string>
#include <vector>

namespace seeder::nmos_node::internal
{
  struct SenderResources
  {
    nmos::resource source;
    nmos::resource flow;
    nmos::resource sender;
    nmos::resource connection_sender;
  };

  struct ReceiverResources
  {
    nmos::resource receiver;
    nmos::resource connection_receiver;
  };

  class NodeResourceFactory
  {
  public:
    NodeResourceFactory(
        utility::string_t seed_id, nmos::id device_id,
        web::json::value settings,
        std::vector<web::hosts::experimental::host_interface> runtime_interfaces);

    nmos::id make_video_receiver_resource_id(const std::string &id) const;
    nmos::id make_audio_receiver_resource_id(const std::string &id) const;
    nmos::id make_ancillary_receiver_resource_id(const std::string &id) const;

    SenderResources make_video_sender_resources(const VideoSender &video) const;
    SenderResources make_audio_sender_resources(const AudioSender &audio) const;
    SenderResources make_ancillary_sender_resources(
        const AncillarySender &ancillary) const;

    ReceiverResources make_video_receiver_resources(
        const VideoReceiver &video) const;
    ReceiverResources make_audio_receiver_resources(
        const AudioReceiver &audio) const;
    ReceiverResources make_ancillary_receiver_resources(
        const AncillaryReceiver &ancillary) const;

  private:
    utility::string_t seed_id_;
    nmos::id device_id_;
    web::json::value settings_;
    std::vector<web::hosts::experimental::host_interface> runtime_interfaces_;
  };
}
