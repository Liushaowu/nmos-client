#include "node_runtime_interface_updater.h"

#include "node_resource_factory_interfaces.h"

#include <nmos/json_fields.h>
#include <nmos/log_gate.h>
#include <nmos/node_interfaces.h>
#include <nmos/node_resource.h>
#include <nmos/resources.h>
#include <nmos/slog.h>
#include <nmos/type.h>

#include <algorithm>
#include <cctype>
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
        resource_controller_(ctx.resource_controller),
        node_id_(ctx.node_id), gate_(ctx.gate)
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
    refresh_node_interfaces();
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

  void NodeRuntimeInterfaceUpdater::refresh_node_interfaces()
  {
    RuntimeInterfaces interfaces;
    {
      std::lock_guard<std::mutex> lock(runtime_interfaces_mutex_);
      interfaces = runtime_interfaces_;
    }

    if (interfaces.empty())
    {
      return;
    }

    const auto node_interfaces =
        nmos::experimental::node_interfaces(interfaces);

    // 将 chassis_id 和 port_id 强制转为小写，并将冒号替换为连字符（NMOS 规范要求）
    auto normalized_interfaces = node_interfaces;
    for (auto &entry : normalized_interfaces)
    {
      auto normalize_mac = [](utility::string_t &s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        std::replace(s.begin(), s.end(), U(':'), U('-'));
      };
      normalize_mac(entry.second.chassis_id);
      normalize_mac(entry.second.port_id);
    }

    const auto interfaces_json =
        nmos::make_node_interfaces(normalized_interfaces);

    nmos::modify_resource(node_model_.node_resources, node_id_,
                          [&](nmos::resource &node)
                          {
                            node.data[nmos::fields::interfaces] =
                                interfaces_json;
                          });

    if (gate_ && *gate_)
    {
      slog::log<slog::severities::info>(**gate_, SLOG_FLF)
          << "Updated node interfaces from runtime devices: "
          << interfaces_json.serialize();
    }
  }
}
