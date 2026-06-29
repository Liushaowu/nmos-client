#pragma once

#include "node_resource_factory.h"
#include "node_resource_lifecycle.h"
#include "node_activation_context.h"
#include "node_runtime_types.h"
#include "node_stream_store.h"

#include <nmos/log_gate.h>

#include <mutex>

namespace seeder::nmos_node::internal
{
  struct NodeResourceControllerContext
  {
    StreamStore &stream_store;
    nmos::node_model &node_model;
    nmos::experimental::log_gate *&gate;
    std::mutex &sender_mutex;
    std::mutex &receiver_mutex;
    RuntimeInterfaces &runtime_interfaces;
    std::mutex &runtime_interfaces_mutex;
    utility::string_t &seed_id;
    nmos::id &device_id;
    nmos::connection_sender_transportfile_setter &set_transportfile;
  };

  class NodeResourceController
  {
  public:
    explicit NodeResourceController(NodeResourceControllerContext ctx);

    void add_video_sender(VideoSender video);
    void add_audio_sender(AudioSender audio);
    void add_ancillary_sender(AncillarySender ancillary);
    void add_video_receiver(VideoReceiver video);
    void add_audio_receiver(AudioReceiver audio);
    void add_ancillary_receiver(AncillaryReceiver ancillary);

    void remove_video_sender(std::string id);
    void remove_audio_sender(std::string id);
    void remove_ancillary_sender(std::string id);
    void remove_video_receiver(std::string id);
    void remove_audio_receiver(std::string id);
    void remove_ancillary_receiver(std::string id);

    void update_video_sender(VideoSender video);
    void update_audio_sender(AudioSender audio);
    void update_ancillary_sender(AncillarySender ancillary);
    void update_video_receiver(VideoReceiver video);
    void update_audio_receiver(AudioReceiver audio);
    void update_ancillary_receiver(AncillaryReceiver ancillary);

    void replace_video_sender_resources_for_runtime(const VideoSender &video);
    void replace_audio_sender_resources_for_runtime(const AudioSender &audio);
    void replace_ancillary_sender_resources_for_runtime(
        const AncillarySender &ancillary);
    void replace_video_receiver_resources_for_runtime(const VideoReceiver &video);
    void replace_audio_receiver_resources_for_runtime(const AudioReceiver &audio);
    void replace_ancillary_receiver_resources_for_runtime(
        const AncillaryReceiver &ancillary);

  private:
    using SenderResources = internal::SenderResources;
    using ReceiverResources = internal::ReceiverResources;

    RuntimeInterfaces runtime_interfaces_snapshot() const;
    NodeResourceFactory make_resource_factory() const;
    ResourceLifecycleService lifecycle_service() const;

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

    nmos::id make_video_receiver_resource_id(const std::string &id) const;
    nmos::id make_audio_receiver_resource_id(const std::string &id) const;
    nmos::id make_ancillary_receiver_resource_id(const std::string &id) const;

    void replace_sender_resources(const std::string &description,
                                  SenderResources resources,
                                  bool st2022_7);
    void replace_receiver_resources(const std::string &description,
                                    ReceiverResources resources,
                                    bool st2022_7);

    VideoSender *find_video_sender_by_id(std::string id);
    AudioSender *find_audio_sender_by_id(std::string id);
    AncillarySender *find_ancillary_sender_by_id(std::string id);

    VideoReceiver *find_video_receiver_by_id(std::string id);
    AudioReceiver *find_audio_receiver_by_id(std::string id);
    AncillaryReceiver *find_ancillary_receiver_by_id(std::string id);

    void remove_video_sender_by_sender_id(std::string id);
    void remove_audio_sender_by_sender_id(std::string id);
    void remove_ancillary_sender_by_sender_id(std::string id);
    void remove_video_receiver_by_id(std::string id);
    void remove_audio_receiver_by_id(std::string id);
    void remove_ancillary_receiver_by_id(std::string id);

    StreamStore &stream_store_;
    nmos::node_model &node_model_;
    nmos::experimental::log_gate *&gate_;
    std::mutex &sender_mutex_;
    std::mutex &receiver_mutex_;
    RuntimeInterfaces &runtime_interfaces_;
    std::mutex &runtime_interfaces_mutex_;
    utility::string_t &seed_id_;
    nmos::id &device_id_;
    nmos::connection_sender_transportfile_setter &set_transportfile_;
  };
}
