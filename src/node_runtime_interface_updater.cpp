#include "node_runtime_interface_updater.h"

#include <nmos/json_fields.h>
#include <nmos/resources.h>
#include <nmos/type.h>

#include <utility>

namespace seeder::nmos_node::internal
{
  NodeRuntimeInterfaceUpdater::NodeRuntimeInterfaceUpdater(
      NodeRuntimeInterfaceUpdaterContext ctx)
      : runtime_interfaces_(ctx.runtime_interfaces),
        runtime_interfaces_mutex_(ctx.runtime_interfaces_mutex),
        stream_store_(ctx.stream_store), sender_mutex_(ctx.sender_mutex),
        receiver_mutex_(ctx.receiver_mutex), node_model_(ctx.node_model),
        set_transportfile_(ctx.set_transportfile),
        resource_controller_(ctx.resource_controller)
  {
  }

  NodeRuntimeInterfaceUpdater::SenderSnapshot
  NodeRuntimeInterfaceUpdater::sender_snapshot() const
  {
    std::lock_guard<std::mutex> sender_lock(sender_mutex_);
    return SenderSnapshot{stream_store_.video_senders(),
                          stream_store_.audio_senders(),
                          stream_store_.ancillary_senders()};
  }

  NodeRuntimeInterfaceUpdater::ReceiverSnapshot
  NodeRuntimeInterfaceUpdater::receiver_snapshot() const
  {
    std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
    return ReceiverSnapshot{stream_store_.video_receivers(),
                            stream_store_.audio_receivers(),
                            stream_store_.ancillary_receivers()};
  }

  void NodeRuntimeInterfaceUpdater::set_runtime_interfaces(
      RuntimeInterfaces interfaces)
  {
    nmos::write_lock lock = node_model_.write_lock();
    {
      std::lock_guard<std::mutex> runtime_interfaces_lock(
          runtime_interfaces_mutex_);
      runtime_interfaces_ = std::move(interfaces);
    }

    const auto senders = sender_snapshot();
    const auto receivers = receiver_snapshot();
    refresh_resources(senders, receivers);
    refresh_transportfiles();
  }

  void NodeRuntimeInterfaceUpdater::refresh_resources(
      const SenderSnapshot &senders, const ReceiverSnapshot &receivers)
  {
    for (const auto &video : senders.videos)
    {
      resource_controller_.replace_video_sender_resources_for_runtime(video);
    }
    for (const auto &audio : senders.audios)
    {
      resource_controller_.replace_audio_sender_resources_for_runtime(audio);
    }
    for (const auto &ancillary : senders.ancillaries)
    {
      resource_controller_.replace_ancillary_sender_resources_for_runtime(
          ancillary);
    }
    for (const auto &video : receivers.videos)
    {
      resource_controller_.replace_video_receiver_resources_for_runtime(video);
    }
    for (const auto &audio : receivers.audios)
    {
      resource_controller_.replace_audio_receiver_resources_for_runtime(audio);
    }
    for (const auto &ancillary : receivers.ancillaries)
    {
      resource_controller_.replace_ancillary_receiver_resources_for_runtime(
          ancillary);
    }
  }

  void NodeRuntimeInterfaceUpdater::refresh_transportfiles()
  {
    for (const auto &sender_id : stream_store_.sender_ids())
    {
      auto sender = nmos::find_resource(node_model_.node_resources,
                                        {sender_id, nmos::types::sender});
      if (node_model_.node_resources.end() == sender)
      {
        continue;
      }

      nmos::modify_resource(node_model_.connection_resources, sender_id,
                            [&](nmos::resource &connection_sender)
                            {
                              auto &endpoint_transportfile =
                                  connection_sender.data[nmos::fields::endpoint_transportfile];
                              set_transportfile_(*sender, connection_sender,
                                                 endpoint_transportfile);
                            });
    }
  }
}
