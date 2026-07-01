#include "node_resource_controller.h"

#include "node_implementation.h"
#include "node_connection_transport_params.h"

#include <cpprest/details/basic_types.h>
#include <nmos/format.h>

#include <utility>

using web::json::value;
using web::json::value_from_elements;

namespace seeder::nmos_node::internal
{
  NodeResourceController::NodeResourceController(
      NodeResourceControllerContext ctx)
      : stream_store_(ctx.stream_store), node_model_(ctx.node_model),
        gate_(ctx.gate), sender_mutex_(ctx.sender_mutex),
        receiver_mutex_(ctx.receiver_mutex),
        runtime_interfaces_(ctx.runtime_interfaces),
        runtime_interfaces_mutex_(ctx.runtime_interfaces_mutex),
        seed_id_(ctx.seed_id), device_id_(ctx.device_id),
        set_transportfile_(ctx.set_transportfile)
  {
  }

  RuntimeInterfaces NodeResourceController::runtime_interfaces_snapshot() const
  {
    std::lock_guard<std::mutex> lock(runtime_interfaces_mutex_);
    return runtime_interfaces_;
  }

  NodeResourceFactory NodeResourceController::make_resource_factory() const
  {
    return NodeResourceFactory(seed_id_, device_id_, node_model_.settings,
                               runtime_interfaces_snapshot());
  }

  ResourceLifecycleService NodeResourceController::lifecycle_service() const
  {
    return ResourceLifecycleService(node_model_);
  }

  NodeResourceController::SenderResources
  NodeResourceController::make_video_sender_resources(
      const VideoSender &video) const
  {
    return make_resource_factory().make_video_sender_resources(video);
  }

  NodeResourceController::SenderResources
  NodeResourceController::make_audio_sender_resources(
      const AudioSender &audio) const
  {
    return make_resource_factory().make_audio_sender_resources(audio);
  }

  NodeResourceController::SenderResources
  NodeResourceController::make_ancillary_sender_resources(
      const AncillarySender &ancillary) const
  {
    return make_resource_factory().make_ancillary_sender_resources(ancillary);
  }

  NodeResourceController::ReceiverResources
  NodeResourceController::make_video_receiver_resources(
      const VideoReceiver &video) const
  {
    return make_resource_factory().make_video_receiver_resources(video);
  }

  NodeResourceController::ReceiverResources
  NodeResourceController::make_audio_receiver_resources(
      const AudioReceiver &audio) const
  {
    return make_resource_factory().make_audio_receiver_resources(audio);
  }

  NodeResourceController::ReceiverResources
  NodeResourceController::make_ancillary_receiver_resources(
      const AncillaryReceiver &ancillary) const
  {
    return make_resource_factory().make_ancillary_receiver_resources(ancillary);
  }

  nmos::id NodeResourceController::make_video_receiver_resource_id(
      const std::string &id) const
  {
    return impl::make_id(seed_id_, nmos::types::receiver, impl::ports::video, id);
  }

  nmos::id NodeResourceController::make_audio_receiver_resource_id(
      const std::string &id) const
  {
    return impl::make_id(seed_id_, nmos::types::receiver, impl::ports::audio, id);
  }

  nmos::id NodeResourceController::make_ancillary_receiver_resource_id(
      const std::string &id) const
  {
    return impl::make_id(seed_id_, nmos::types::receiver, impl::ports::data, id);
  }

  void NodeResourceController::replace_sender_resources(
      const std::string &description, SenderResources resources,
      const bool st2022_7)
  {
    const auto sender_id = resources.sender.id;
    if (!lifecycle_service().replace_sender_node_resources(
            std::move(resources.source), std::move(resources.flow),
            std::move(resources.sender)))
    {
      throw node_implementation_init_exception(
          description + " resource update failed!");
    }

    const bool connection_updated = nmos::modify_resource(
        node_model_.connection_resources, sender_id,
        [&](nmos::resource &connection_sender)
        {
          if (connection_sender.data.has_field(nmos::fields::endpoint_active))
          {
            const auto &endpoint_active =
                nmos::fields::endpoint_active(connection_sender.data);
            if (connection_transport_params::active_leg_count_matches(
                    endpoint_active, st2022_7))
            {
              resources.connection_sender.data[nmos::fields::endpoint_active] =
                  endpoint_active;
            }
          }
          connection_sender = std::move(resources.connection_sender);
          auto sender = nmos::find_resource(node_model_.node_resources,
                                            {sender_id, nmos::types::sender});
          if (node_model_.node_resources.end() == sender)
          {
            throw node_implementation_init_exception(
                description + " sender transportfile update failed!");
          }

          auto &endpoint_transportfile =
              connection_sender.data[nmos::fields::endpoint_transportfile];
          set_transportfile_(*sender, connection_sender,
                             endpoint_transportfile);
        });

    if (!connection_updated)
    {
      throw node_implementation_init_exception(description +
                                               " connection update failed!");
    }
  }

  void NodeResourceController::replace_receiver_resources(
      const std::string &description, ReceiverResources resources,
      const bool st2022_7)
  {
    const auto receiver_id = resources.receiver.id;
    if (!lifecycle_service().replace_receiver_node_resource(
            std::move(resources.receiver)))
    {
      throw node_implementation_init_exception(
          description + " resource update failed!");
    }

    const bool connection_updated = nmos::modify_resource(
        node_model_.connection_resources, receiver_id,
        [&](nmos::resource &connection_receiver)
        {
          if (connection_receiver.data.has_field(nmos::fields::endpoint_active))
          {
            const auto &endpoint_active =
                nmos::fields::endpoint_active(connection_receiver.data);
            if (connection_transport_params::active_leg_count_matches(
                    endpoint_active, st2022_7))
            {
              resources.connection_receiver.data[nmos::fields::endpoint_active] =
                  endpoint_active;
            }
          }
          connection_receiver = std::move(resources.connection_receiver);
        });

    if (!connection_updated)
    {
      throw node_implementation_init_exception(description +
                                               " connection update failed!");
    }
  }

  VideoSender *NodeResourceController::find_video_sender_by_id(std::string id)
  {
    return stream_store_.find_video_sender_by_id(id);
  }

  AudioSender *NodeResourceController::find_audio_sender_by_id(std::string id)
  {
    return stream_store_.find_audio_sender_by_id(id);
  }

  AncillarySender *NodeResourceController::find_ancillary_sender_by_id(
      std::string id)
  {
    return stream_store_.find_ancillary_sender_by_id(id);
  }

  VideoReceiver *NodeResourceController::find_video_receiver_by_id(
      std::string id)
  {
    return stream_store_.find_video_receiver_by_id(id);
  }

  AudioReceiver *NodeResourceController::find_audio_receiver_by_id(
      std::string id)
  {
    return stream_store_.find_audio_receiver_by_id(id);
  }

  AncillaryReceiver *NodeResourceController::find_ancillary_receiver_by_id(
      std::string id)
  {
    return stream_store_.find_ancillary_receiver_by_id(id);
  }

  void NodeResourceController::remove_video_sender_by_sender_id(std::string id)
  {
    stream_store_.remove_video_sender_by_sender_id(id);
  }

  void NodeResourceController::remove_audio_sender_by_sender_id(std::string id)
  {
    stream_store_.remove_audio_sender_by_sender_id(id);
  }

  void NodeResourceController::remove_ancillary_sender_by_sender_id(
      std::string id)
  {
    stream_store_.remove_ancillary_sender_by_sender_id(id);
  }

  void NodeResourceController::remove_video_receiver_by_id(std::string id)
  {
    stream_store_.remove_video_receiver_by_id(id);
  }

  void NodeResourceController::remove_audio_receiver_by_id(std::string id)
  {
    stream_store_.remove_audio_receiver_by_id(id);
  }

  void NodeResourceController::remove_ancillary_receiver_by_id(std::string id)
  {
    stream_store_.remove_ancillary_receiver_by_id(id);
  }

  void NodeResourceController::add_video_sender(VideoSender video)
  {
    bool exists = false;
    {
      std::lock_guard<std::mutex> sender_lock(sender_mutex_);
      exists = nullptr != find_video_sender_by_id(video.id);
    }
    if (exists)
    {
      update_video_sender(std::move(video));
      return;
    }
    nmos::write_lock lock = node_model_.write_lock();
    auto resources = make_video_sender_resources(video);
    const auto source_id = resources.source.id;
    const auto flow_id = resources.flow.id;
    const auto sender_id = resources.sender.id;

    lifecycle_service().erase_sender_resources_if_present(sender_id, source_id,
                                                         flow_id);

    const auto insert_result = lifecycle_service().insert_sender_resources_after(
        0, std::move(resources.source), std::move(resources.flow),
        std::move(resources.sender), std::move(resources.connection_sender),
        *gate_, lock);
    if (insert_result != ResourceInsertResult::success)
    {
      if (insert_result == ResourceInsertResult::source_failed)
        throw node_implementation_init_exception(
            "add video sender source failed!");
      if (insert_result == ResourceInsertResult::flow_failed)
        throw node_implementation_init_exception(
            "add video sender flow failed!");
      if (insert_result == ResourceInsertResult::sender_failed)
        throw node_implementation_init_exception("add video sender failed!");
      throw node_implementation_init_exception(
          "add video sender connection failed!");
    }
    video.sender_id = utility::us2s(sender_id);
    {
      std::lock_guard<std::mutex> sender_lock(sender_mutex_);
      stream_store_.add(std::move(video));
    }
    stream_store_.add_sender_resource_ids(sender_id, source_id, flow_id);
    nmos::modify_resource(node_model_.node_resources, device_id_, ([&](nmos::resource &device)
                                                                   {
                                                                     device.data[nmos::fields::senders] = value_from_elements(stream_store_.sender_ids());
                                                                     device.data[nmos::fields::version] = value(nmos::make_version()); }));
  }

  void NodeResourceController::add_audio_sender(AudioSender audio)
  {
    bool exists = false;
    {
      std::lock_guard<std::mutex> sender_lock(sender_mutex_);
      exists = nullptr != find_audio_sender_by_id(audio.id);
    }
    if (exists)
    {
      update_audio_sender(std::move(audio));
      return;
    }
    nmos::write_lock lock = node_model_.write_lock();
    auto resources = make_audio_sender_resources(audio);
    const auto source_id = resources.source.id;
    const auto flow_id = resources.flow.id;
    const auto sender_id = resources.sender.id;

    lifecycle_service().erase_sender_resources_if_present(sender_id, source_id,
                                                         flow_id);

    const auto insert_result = lifecycle_service().insert_sender_resources_after(
        0, std::move(resources.source), std::move(resources.flow),
        std::move(resources.sender), std::move(resources.connection_sender),
        *gate_, lock);
    if (insert_result != ResourceInsertResult::success)
    {
      if (insert_result == ResourceInsertResult::source_failed)
        throw node_implementation_init_exception(
            "add audio sender source failed!");
      if (insert_result == ResourceInsertResult::flow_failed)
        throw node_implementation_init_exception(
            "add audio sender flow failed!");
      if (insert_result == ResourceInsertResult::sender_failed)
        throw node_implementation_init_exception(
            "insert audio sender failed!");
      throw node_implementation_init_exception(
          "insert audio connection sender failed!");
    }
    audio.sender_id = utility::us2s(sender_id);
    {
      std::lock_guard<std::mutex> sender_lock(sender_mutex_);
      stream_store_.add(std::move(audio));
    }
    stream_store_.add_sender_resource_ids(sender_id, source_id, flow_id);
    nmos::modify_resource(node_model_.node_resources, device_id_, ([&](nmos::resource &device)
                                                                   {
                                                                     device.data[nmos::fields::senders] = value_from_elements(stream_store_.sender_ids());
                                                                     device.data[nmos::fields::version] = value(nmos::make_version()); }));
  }

  void NodeResourceController::add_ancillary_sender(AncillarySender ancillary)
  {
    bool exists = false;
    {
      std::lock_guard<std::mutex> sender_lock(sender_mutex_);
      exists = nullptr != find_ancillary_sender_by_id(ancillary.id);
    }
    if (exists)
    {
      update_ancillary_sender(std::move(ancillary));
      return;
    }
    nmos::write_lock lock = node_model_.write_lock();
    auto resources = make_ancillary_sender_resources(ancillary);
    const auto source_id = resources.source.id;
    const auto flow_id = resources.flow.id;
    const auto sender_id = resources.sender.id;

    const auto insert_result = lifecycle_service().insert_sender_resources_after(
        0, std::move(resources.source), std::move(resources.flow),
        std::move(resources.sender), std::move(resources.connection_sender),
        *gate_, lock);
    if (insert_result != ResourceInsertResult::success)
    {
      if (insert_result == ResourceInsertResult::source_failed)
        throw node_implementation_init_exception(
            " add ancillary sender source failed!");
      if (insert_result == ResourceInsertResult::flow_failed)
        throw node_implementation_init_exception(
            "add ancillary sender flow failed!");
      if (insert_result == ResourceInsertResult::sender_failed)
        throw node_implementation_init_exception(
            "add ancillary sender failed!");
      throw node_implementation_init_exception(
          "add ancillary sender connection failed!");
    }
    ancillary.sender_id = utility::us2s(sender_id);
    {
      std::lock_guard<std::mutex> sender_lock(sender_mutex_);
      stream_store_.add(std::move(ancillary));
    }
    stream_store_.add_sender_resource_ids(sender_id, source_id, flow_id);
    nmos::modify_resource(node_model_.node_resources, device_id_, ([&](nmos::resource &device)
                                                                   {
                                                                     device.data[nmos::fields::senders] = value_from_elements(stream_store_.sender_ids());
                                                                     device.data[nmos::fields::version] = value(nmos::make_version()); }));
  }

  void NodeResourceController::add_video_receiver(VideoReceiver video)
  {
    bool exists = false;
    {
      std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
      exists = nullptr != find_video_receiver_by_id(video.id);
    }
    if (exists)
    {
      update_video_receiver(std::move(video));
      return;
    }
    nmos::write_lock lock = node_model_.write_lock();
    auto resources = make_video_receiver_resources(video);
    const auto receiver_id = resources.receiver.id;

    lifecycle_service().erase_receiver_resources_if_present(receiver_id);

    const auto insert_result = lifecycle_service().insert_receiver_resources_after(
        0, std::move(resources.receiver), std::move(resources.connection_receiver),
        *gate_, lock);
    if (insert_result != ResourceInsertResult::success)
    {
      if (insert_result == ResourceInsertResult::receiver_failed)
        throw node_implementation_init_exception(
            "insert video receiver failed!");
      throw node_implementation_init_exception(
          "insert video connection receiver failed!");
    }
    {
      std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
      stream_store_.add(std::move(video), receiver_id);
    }
    stream_store_.add_receiver_id(receiver_id);
    nmos::modify_resource(node_model_.node_resources, device_id_, ([&](nmos::resource &device)
                                                                   {
                                                                     device.data[nmos::fields::receivers] = value_from_elements(stream_store_.receiver_ids());
                                                                     device.data[nmos::fields::version] = value(nmos::make_version()); }));
  }

  void NodeResourceController::add_audio_receiver(AudioReceiver audio)
  {
    bool exists = false;
    {
      std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
      exists = nullptr != find_audio_receiver_by_id(audio.id);
    }
    if (exists)
    {
      update_audio_receiver(std::move(audio));
      return;
    }
    nmos::write_lock lock = node_model_.write_lock();
    auto resources = make_audio_receiver_resources(audio);
    const auto receiver_id = resources.receiver.id;

    lifecycle_service().erase_receiver_resources_if_present(receiver_id);

    const auto insert_result = lifecycle_service().insert_receiver_resources_after(
        0, std::move(resources.receiver), std::move(resources.connection_receiver),
        *gate_, lock);
    if (insert_result != ResourceInsertResult::success)
    {
      if (insert_result == ResourceInsertResult::receiver_failed)
        throw node_implementation_init_exception(
            "add audio receiver failed!");
      throw node_implementation_init_exception(
          "add audio receiver connection failed!");
    }
    {
      std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
      stream_store_.add(std::move(audio), receiver_id);
    }
    stream_store_.add_receiver_id(receiver_id);
    nmos::modify_resource(node_model_.node_resources, device_id_, ([&](nmos::resource &device)
                                                                   {
                                                                     device.data[nmos::fields::receivers] = value_from_elements(stream_store_.receiver_ids());
                                                                     device.data[nmos::fields::version] = value(nmos::make_version()); }));
  }

  void NodeResourceController::add_ancillary_receiver(AncillaryReceiver ancillary)
  {
    bool exists = false;
    {
      std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
      exists = nullptr != find_ancillary_receiver_by_id(ancillary.id);
    }
    if (exists)
    {
      update_ancillary_receiver(std::move(ancillary));
      return;
    }
    nmos::write_lock lock = node_model_.write_lock();
    auto resources = make_ancillary_receiver_resources(ancillary);
    const auto receiver_id = resources.receiver.id;

    lifecycle_service().erase_receiver_resources_if_present(receiver_id);

    const auto insert_result = lifecycle_service().insert_receiver_resources_after(
        0, std::move(resources.receiver), std::move(resources.connection_receiver),
        *gate_, lock);
    if (insert_result != ResourceInsertResult::success)
    {
      if (insert_result == ResourceInsertResult::receiver_failed)
        throw node_implementation_init_exception(
            "add ancillary receiver failed!");
      throw node_implementation_init_exception(
          "add ancillary receiver connection failed!");
    }
    {
      std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
      stream_store_.add(std::move(ancillary), receiver_id);
    }
    stream_store_.add_receiver_id(receiver_id);
    nmos::modify_resource(node_model_.node_resources, device_id_, ([&](nmos::resource &device)
                                                                   {
                                                                     device.data[nmos::fields::receivers] = value_from_elements(stream_store_.receiver_ids());
                                                                     device.data[nmos::fields::version] = value(nmos::make_version()); }));
  }

  void NodeResourceController::remove_video_sender(std::string id)
  {
    bool exists = false;
    {
      std::lock_guard<std::mutex> sender_lock(sender_mutex_);
      exists = nullptr != find_video_sender_by_id(id);
    }
    if (!exists)
    {
      slog::log<slog::severities::error>(*gate_, SLOG_FLF)
          << nmos::stash_category(impl::categories::node_implementation)
          << "remove video sender not found video sender id:" << id;
      return;
    }
    nmos::write_lock lock = node_model_.write_lock();

    const auto source_v_id =
        impl::make_id(seed_id_, nmos::types::source, impl::ports::video, id);
    const auto flow_v_id =
        impl::make_id(seed_id_, nmos::types::flow, impl::ports::video, id);
    const auto sender_v_id =
        impl::make_id(seed_id_, nmos::types::sender, impl::ports::video, id);

    lifecycle_service().remove_sender_resources_after(
        0, sender_v_id, source_v_id, flow_v_id, *gate_, lock);

    {
      std::lock_guard<std::mutex> sender_lock(sender_mutex_);
      remove_video_sender_by_sender_id(utility::us2s(sender_v_id));
    }

    stream_store_.remove_sender_resource_ids(sender_v_id, source_v_id,
                                             flow_v_id);
    nmos::modify_resource(node_model_.node_resources, device_id_, ([&](nmos::resource &device)
                                                                   {
                                                                     device.data[nmos::fields::senders] = value_from_elements(stream_store_.sender_ids());
                                                                     device.data[nmos::fields::version] = value(nmos::make_version()); }));
    slog::log<slog::severities::warning>(*gate_, SLOG_FLF)
        << nmos::stash_category(impl::categories::node_implementation)
        << "Remove id:" << id << "  sender_id:" << sender_v_id;
  }

  void NodeResourceController::remove_audio_sender(std::string id)
  {
    bool exists = false;
    {
      std::lock_guard<std::mutex> sender_lock(sender_mutex_);
      exists = nullptr != find_audio_sender_by_id(id);
    }
    if (!exists)
    {
      slog::log<slog::severities::error>(*gate_, SLOG_FLF)
          << nmos::stash_category(impl::categories::node_implementation)
          << "remove audio sender not found audio sender id:" << id;
      return;
    }
    nmos::write_lock lock = node_model_.write_lock();

    const auto source_a_id =
        impl::make_id(seed_id_, nmos::types::source, impl::ports::audio, id);
    const auto flow_a_id =
        impl::make_id(seed_id_, nmos::types::flow, impl::ports::audio, id);
    const auto sender_a_id =
        impl::make_id(seed_id_, nmos::types::sender, impl::ports::audio, id);

    lifecycle_service().remove_sender_resources_after(
        0, sender_a_id, source_a_id, flow_a_id, *gate_, lock);

    {
      std::lock_guard<std::mutex> sender_lock(sender_mutex_);
      remove_audio_sender_by_sender_id(utility::us2s(sender_a_id));
    }

    stream_store_.remove_sender_resource_ids(sender_a_id, source_a_id,
                                             flow_a_id);
    nmos::modify_resource(node_model_.node_resources, device_id_, ([&](nmos::resource &device)
                                                                   {
                                                                     device.data[nmos::fields::senders] = value_from_elements(stream_store_.sender_ids());
                                                                     device.data[nmos::fields::version] = value(nmos::make_version()); }));
    slog::log<slog::severities::warning>(*gate_, SLOG_FLF)
        << nmos::stash_category(impl::categories::node_implementation)
        << "Remove id:" << id << "  sender_id:" << sender_a_id;
  }

  void NodeResourceController::remove_ancillary_sender(std::string id)
  {
    bool exists = false;
    {
      std::lock_guard<std::mutex> sender_lock(sender_mutex_);
      exists = nullptr != find_ancillary_sender_by_id(id);
    }
    if (!exists)
    {
      slog::log<slog::severities::error>(*gate_, SLOG_FLF)
          << nmos::stash_category(impl::categories::node_implementation)
          << "remove ancillary sender not found ancillary sender id:" << id;
      return;
    }
    nmos::write_lock lock = node_model_.write_lock();

    const auto source_id =
        impl::make_id(seed_id_, nmos::types::source, impl::ports::data, id);
    const auto flow_id =
        impl::make_id(seed_id_, nmos::types::flow, impl::ports::data, id);
    const auto sender_id =
        impl::make_id(seed_id_, nmos::types::sender, impl::ports::data, id);

    lifecycle_service().remove_sender_resources_after(
        0, sender_id, source_id, flow_id, *gate_, lock);

    {
      std::lock_guard<std::mutex> sender_lock(sender_mutex_);
      remove_ancillary_sender_by_sender_id(utility::us2s(sender_id));
    }

    stream_store_.remove_sender_resource_ids(sender_id, source_id, flow_id);

    slog::log<slog::severities::warning>(*gate_, SLOG_FLF)
        << nmos::stash_category(impl::categories::node_implementation)
        << "Remove id:" << id << "  sender_id:" << sender_id;

    nmos::modify_resource(node_model_.node_resources, device_id_, ([&](nmos::resource &device)
                                                                   {
                                                                     device.data[nmos::fields::senders] = value_from_elements(stream_store_.sender_ids());
                                                                     device.data[nmos::fields::version] = value(nmos::make_version()); }));
  }

  void NodeResourceController::remove_video_receiver(std::string id)
  {
    nmos::write_lock lock = node_model_.write_lock();
    std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
    VideoReceiver *video = find_video_receiver_by_id(id);
    if (!video)
    {
      slog::log<slog::severities::error>(*gate_, SLOG_FLF)
          << nmos::stash_category(impl::categories::node_implementation)
          << "remove video receiver not found video receiver id:" << id;
      return;
    }
    const auto receiver_id = make_video_receiver_resource_id(id);
    stream_store_.remove_receiver_id(receiver_id);

    nmos::modify_resource(node_model_.node_resources, device_id_, ([&](nmos::resource &device)
                                                                   {
                                                                     device.data[nmos::fields::receivers] = value_from_elements(stream_store_.receiver_ids());
                                                                     device.data[nmos::fields::version] = value(nmos::make_version()); }));
    node_model_.notify();
    lifecycle_service().remove_receiver_resources_after(
        0, receiver_id, *gate_, lock);
    remove_video_receiver_by_id(id);
  }

  void NodeResourceController::remove_audio_receiver(std::string id)
  {
    nmos::write_lock lock = node_model_.write_lock();
    std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
    AudioReceiver *audio = find_audio_receiver_by_id(id);
    if (!audio)
    {
      slog::log<slog::severities::error>(*gate_, SLOG_FLF)
          << nmos::stash_category(impl::categories::node_implementation)
          << "remove audio receiver not found audio receiver id:" << id;
      return;
    }
    const auto receiver_id = make_audio_receiver_resource_id(id);
    stream_store_.remove_receiver_id(receiver_id);
    nmos::modify_resource(node_model_.node_resources, device_id_, ([&](nmos::resource &device)
                                                                   {
                                                                     device.data[nmos::fields::receivers] = value_from_elements(stream_store_.receiver_ids());
                                                                     device.data[nmos::fields::version] = value(nmos::make_version()); }));
    node_model_.notify();
    lifecycle_service().remove_receiver_resources_after(
        0, receiver_id, *gate_, lock);
    remove_audio_receiver_by_id(id);
  }

  void NodeResourceController::remove_ancillary_receiver(std::string id)
  {
    nmos::write_lock lock = node_model_.write_lock();
    std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
    AncillaryReceiver *ancillary = find_ancillary_receiver_by_id(id);
    if (!ancillary)
    {
      slog::log<slog::severities::error>(*gate_, SLOG_FLF)
          << nmos::stash_category(impl::categories::node_implementation)
          << "remove ancillary receiver not found ancillary receiver id:" << id;
      return;
    }
    const auto receiver_id = make_ancillary_receiver_resource_id(id);
    stream_store_.remove_receiver_id(receiver_id);
    nmos::modify_resource(node_model_.node_resources, device_id_, ([&](nmos::resource &device)
                                                                   {
                                                                     device.data[nmos::fields::receivers] = value_from_elements(stream_store_.receiver_ids());
                                                                     device.data[nmos::fields::version] = value(nmos::make_version()); }));
    node_model_.notify();
    lifecycle_service().remove_receiver_resources_after(
        0, receiver_id, *gate_, lock);
    remove_ancillary_receiver_by_id(id);
  }

  void NodeResourceController::update_video_sender(VideoSender video)
  {
    bool exists = false;
    {
      std::lock_guard<std::mutex> sender_lock(sender_mutex_);
      exists = nullptr != find_video_sender_by_id(video.id);
    }
    if (!exists)
    {
      add_video_sender(std::move(video));
      return;
    }

    nmos::write_lock lock = node_model_.write_lock();
    const auto sender_id = impl::make_id(
        seed_id_, nmos::types::sender, impl::ports::video, video.id);
    auto resources = make_video_sender_resources(video);
    replace_sender_resources("update video sender", std::move(resources),
                             video.redundancy.present);
    video.sender_id = utility::us2s(sender_id);
    {
      std::lock_guard<std::mutex> sender_lock(sender_mutex_);
      stream_store_.replace(video);
    }
    node_model_.notify();
  }

  void NodeResourceController::update_audio_sender(AudioSender audio)
  {
    bool exists = false;
    {
      std::lock_guard<std::mutex> sender_lock(sender_mutex_);
      exists = nullptr != find_audio_sender_by_id(audio.id);
    }
    if (!exists)
    {
      add_audio_sender(std::move(audio));
      return;
    }

    nmos::write_lock lock = node_model_.write_lock();
    const auto sender_id = impl::make_id(
        seed_id_, nmos::types::sender, impl::ports::audio, audio.id);
    auto resources = make_audio_sender_resources(audio);
    replace_sender_resources("update audio sender", std::move(resources),
                             audio.redundancy.present);
    audio.sender_id = utility::us2s(sender_id);
    {
      std::lock_guard<std::mutex> sender_lock(sender_mutex_);
      stream_store_.replace(audio);
    }
    node_model_.notify();
  }

  void NodeResourceController::update_ancillary_sender(AncillarySender ancillary)
  {
    bool exists = false;
    {
      std::lock_guard<std::mutex> sender_lock(sender_mutex_);
      exists = nullptr != find_ancillary_sender_by_id(ancillary.id);
    }
    if (!exists)
    {
      add_ancillary_sender(std::move(ancillary));
      return;
    }

    nmos::write_lock lock = node_model_.write_lock();
    const auto sender_id = impl::make_id(
        seed_id_, nmos::types::sender, impl::ports::data, ancillary.id);
    auto resources = make_ancillary_sender_resources(ancillary);
    replace_sender_resources("update ancillary sender", std::move(resources),
                             ancillary.redundancy.present);
    ancillary.sender_id = utility::us2s(sender_id);
    {
      std::lock_guard<std::mutex> sender_lock(sender_mutex_);
      stream_store_.replace(ancillary);
    }
    node_model_.notify();
  }

  void NodeResourceController::update_video_receiver(VideoReceiver video)
  {
    bool exists = false;
    {
      std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
      exists = nullptr != find_video_receiver_by_id(video.id);
    }
    if (!exists)
    {
      add_video_receiver(std::move(video));
      return;
    }

    nmos::write_lock lock = node_model_.write_lock();
    auto resources = make_video_receiver_resources(video);
    replace_receiver_resources("update video receiver", std::move(resources),
                               video.redundancy.present);
    {
      std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
      stream_store_.replace(video);
    }
    node_model_.notify();
  }

  void NodeResourceController::update_audio_receiver(AudioReceiver audio)
  {
    bool exists = false;
    {
      std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
      exists = nullptr != find_audio_receiver_by_id(audio.id);
    }
    if (!exists)
    {
      add_audio_receiver(std::move(audio));
      return;
    }

    nmos::write_lock lock = node_model_.write_lock();
    auto resources = make_audio_receiver_resources(audio);
    replace_receiver_resources("update audio receiver", std::move(resources),
                               audio.redundancy.present);
    {
      std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
      stream_store_.replace(audio);
    }
    node_model_.notify();
  }

  void NodeResourceController::update_ancillary_receiver(
      AncillaryReceiver ancillary)
  {
    bool exists = false;
    {
      std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
      exists = nullptr != find_ancillary_receiver_by_id(ancillary.id);
    }
    if (!exists)
    {
      add_ancillary_receiver(std::move(ancillary));
      return;
    }

    nmos::write_lock lock = node_model_.write_lock();
    auto resources = make_ancillary_receiver_resources(ancillary);
    replace_receiver_resources("update ancillary receiver",
                               std::move(resources), ancillary.redundancy.present);
    {
      std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
      stream_store_.replace(ancillary);
    }
    node_model_.notify();
  }

  void NodeResourceController::replace_video_sender_resources_for_runtime(
      const VideoSender &video)
  {
    replace_sender_resources("runtime interfaces update video sender",
                             make_video_sender_resources(video),
                             video.redundancy.present);
  }

  void NodeResourceController::replace_audio_sender_resources_for_runtime(
      const AudioSender &audio)
  {
    replace_sender_resources("runtime interfaces update audio sender",
                             make_audio_sender_resources(audio),
                             audio.redundancy.present);
  }

  void NodeResourceController::replace_ancillary_sender_resources_for_runtime(
      const AncillarySender &ancillary)
  {
    replace_sender_resources("runtime interfaces update ancillary sender",
                             make_ancillary_sender_resources(ancillary),
                             ancillary.redundancy.present);
  }

  void NodeResourceController::replace_video_receiver_resources_for_runtime(
      const VideoReceiver &video)
  {
    replace_receiver_resources("runtime interfaces update video receiver",
                               make_video_receiver_resources(video),
                               video.redundancy.present);
  }

  void NodeResourceController::replace_audio_receiver_resources_for_runtime(
      const AudioReceiver &audio)
  {
    replace_receiver_resources("runtime interfaces update audio receiver",
                               make_audio_receiver_resources(audio),
                               audio.redundancy.present);
  }

  void NodeResourceController::replace_ancillary_receiver_resources_for_runtime(
      const AncillaryReceiver &ancillary)
  {
    replace_receiver_resources("runtime interfaces update ancillary receiver",
                               make_ancillary_receiver_resources(ancillary),
                               ancillary.redundancy.present);
  }
}
