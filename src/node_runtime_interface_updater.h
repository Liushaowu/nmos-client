#pragma once

#include "node_resource_controller.h"
#include "node_runtime_types.h"
#include "node_stream_store.h"

#include <nmos/model.h>

#include <mutex>
#include <vector>

namespace seeder::nmos_node::internal
{
  struct NodeRuntimeInterfaceUpdaterContext
  {
    RuntimeInterfaces &runtime_interfaces;
    std::mutex &runtime_interfaces_mutex;
    StreamStore &stream_store;
    std::mutex &sender_mutex;
    std::mutex &receiver_mutex;
    nmos::node_model &node_model;
    nmos::connection_sender_transportfile_setter &set_transportfile;
    NodeResourceController &resource_controller;
  };

  class NodeRuntimeInterfaceUpdater
  {
  public:
    explicit NodeRuntimeInterfaceUpdater(NodeRuntimeInterfaceUpdaterContext ctx);

    void set_runtime_interfaces(RuntimeInterfaces interfaces);

  private:
    struct SenderSnapshot
    {
      std::vector<VideoSender> videos;
      std::vector<AudioSender> audios;
      std::vector<AncillarySender> ancillaries;
    };

    struct ReceiverSnapshot
    {
      std::vector<VideoReceiver> videos;
      std::vector<AudioReceiver> audios;
      std::vector<AncillaryReceiver> ancillaries;
    };

    SenderSnapshot sender_snapshot() const;
    ReceiverSnapshot receiver_snapshot() const;
    void refresh_resources(const SenderSnapshot &senders,
                           const ReceiverSnapshot &receivers);
    void refresh_transportfiles();

    RuntimeInterfaces &runtime_interfaces_;
    std::mutex &runtime_interfaces_mutex_;
    StreamStore &stream_store_;
    std::mutex &sender_mutex_;
    std::mutex &receiver_mutex_;
    nmos::node_model &node_model_;
    nmos::connection_sender_transportfile_setter &set_transportfile_;
    NodeResourceController &resource_controller_;
  };
}
