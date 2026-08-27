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
  // ============================================================
  // StreamStore accessor — dispatches to the right per-type method
  // ============================================================
  namespace
  {
    template <typename Tag>
    struct StoreAccessor;

    template <>
    struct StoreAccessor<VideoTag>
    {
      static auto *find_sender(StreamStore &s, const std::string &id)
      {
        return s.find_video_sender_by_id(id);
      }
      static void add_sender(StreamStore &s, VideoSender v)
      {
        s.add(std::move(v));
      }
      static void replace_sender(StreamStore &s, const VideoSender &v)
      {
        s.replace(v);
      }
      static void remove_sender(StreamStore &s, const std::string &id)
      {
        s.remove_video_sender_by_sender_id(id);
      }
      static auto *find_receiver(StreamStore &s, const std::string &id)
      {
        return s.find_video_receiver_by_id(id);
      }
      static void add_receiver(StreamStore &s, VideoReceiver r,
                               const nmos::id &rid)
      {
        s.add(std::move(r), rid);
      }
      static void replace_receiver(StreamStore &s, const VideoReceiver &r)
      {
        s.replace(r);
      }
      static void remove_receiver(StreamStore &s, const std::string &id)
      {
        s.remove_video_receiver_by_id(id);
      }
    };

    template <>
    struct StoreAccessor<AudioTag>
    {
      static auto *find_sender(StreamStore &s, const std::string &id)
      {
        return s.find_audio_sender_by_id(id);
      }
      static void add_sender(StreamStore &s, AudioSender v)
      {
        s.add(std::move(v));
      }
      static void replace_sender(StreamStore &s, const AudioSender &v)
      {
        s.replace(v);
      }
      static void remove_sender(StreamStore &s, const std::string &id)
      {
        s.remove_audio_sender_by_sender_id(id);
      }
      static auto *find_receiver(StreamStore &s, const std::string &id)
      {
        return s.find_audio_receiver_by_id(id);
      }
      static void add_receiver(StreamStore &s, AudioReceiver r,
                               const nmos::id &rid)
      {
        s.add(std::move(r), rid);
      }
      static void replace_receiver(StreamStore &s, const AudioReceiver &r)
      {
        s.replace(r);
      }
      static void remove_receiver(StreamStore &s, const std::string &id)
      {
        s.remove_audio_receiver_by_id(id);
      }
    };

    template <>
    struct StoreAccessor<AncillaryTag>
    {
      static auto *find_sender(StreamStore &s, const std::string &id)
      {
        return s.find_ancillary_sender_by_id(id);
      }
      static void add_sender(StreamStore &s, AncillarySender v)
      {
        s.add(std::move(v));
      }
      static void replace_sender(StreamStore &s, const AncillarySender &v)
      {
        s.replace(v);
      }
      static void remove_sender(StreamStore &s, const std::string &id)
      {
        s.remove_ancillary_sender_by_sender_id(id);
      }
      static auto *find_receiver(StreamStore &s, const std::string &id)
      {
        return s.find_ancillary_receiver_by_id(id);
      }
      static void add_receiver(StreamStore &s, AncillaryReceiver r,
                               const nmos::id &rid)
      {
        s.add(std::move(r), rid);
      }
      static void replace_receiver(StreamStore &s, const AncillaryReceiver &r)
      {
        s.replace(r);
      }
      static void remove_receiver(StreamStore &s, const std::string &id)
      {
        s.remove_ancillary_receiver_by_id(id);
      }
    };

    // ---- factory dispatch helpers ----

    template <typename Tag>
    SenderResources make_sender_resources(const NodeResourceFactory &factory,
                                          const typename MediaTraits<Tag>::Sender &item)
    {
      if constexpr (std::is_same_v<Tag, VideoTag>)
        return factory.make_video_sender_resources(item);
      else if constexpr (std::is_same_v<Tag, AudioTag>)
        return factory.make_audio_sender_resources(item);
      else
        return factory.make_ancillary_sender_resources(item);
    }

    template <typename Tag>
    ReceiverResources make_receiver_resources(const NodeResourceFactory &factory,
                                              const typename MediaTraits<Tag>::Receiver &item)
    {
      if constexpr (std::is_same_v<Tag, VideoTag>)
        return factory.make_video_receiver_resources(item);
      else if constexpr (std::is_same_v<Tag, AudioTag>)
        return factory.make_audio_receiver_resources(item);
      else
        return factory.make_ancillary_receiver_resources(item);
    }

    template <typename Tag>
    nmos::id make_receiver_resource_id(const utility::string_t &seed_id,
                                       const std::string &id)
    {
      return impl::make_id(seed_id, nmos::types::receiver,
                           MediaTraits<Tag>::port(), id);
    }
  } // namespace

  // ============================================================
  // Constructor & simple helpers
  // ============================================================

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

  bool NodeResourceController::has_sender_resources(
      const nmos::id &sender_id,
      const nmos::id &source_id,
      const nmos::id &flow_id) const
  {
    return node_model_.node_resources.end() !=
               nmos::find_resource(node_model_.node_resources,
                                   {source_id, nmos::types::source}) &&
           node_model_.node_resources.end() !=
               nmos::find_resource(node_model_.node_resources,
                                   {flow_id, nmos::types::flow}) &&
           node_model_.node_resources.end() !=
               nmos::find_resource(node_model_.node_resources,
                                   {sender_id, nmos::types::sender}) &&
           node_model_.connection_resources.end() !=
               nmos::find_resource(node_model_.connection_resources,
                                   {sender_id, nmos::types::sender});
  }

  // ============================================================
  // Template implementations — add / remove / update
  // ============================================================

  template <typename Tag>
  void NodeResourceController::add_sender_impl(
      typename MediaTraits<Tag>::Sender sender)
  {
    bool exists = false;
    {
      std::lock_guard<std::mutex> sender_lock(sender_mutex_);
      exists = nullptr !=
               StoreAccessor<Tag>::find_sender(stream_store_, sender.id);
    }
    if (exists)
    {
      update_sender_impl<Tag>(std::move(sender));
      return;
    }
    nmos::write_lock lock = node_model_.write_lock();
    auto resources =
        make_sender_resources<Tag>(make_resource_factory(), sender);
    const auto source_id = resources.source.id;
    const auto flow_id   = resources.flow.id;
    const auto sender_id = resources.sender.id;

    // 将 sender 提前加入 stream_store，确保 auto_resolver 能查到
    // 若资源插入失败，需要回滚（remove）
    sender.sender_id = utility::us2s(sender_id);
    {
      std::lock_guard<std::mutex> sender_lock(sender_mutex_);
      StoreAccessor<Tag>::add_sender(stream_store_, std::move(sender));
    }

    lifecycle_service().erase_sender_resources_if_present(sender_id, source_id,
                                                          flow_id);

    const auto insert_result = lifecycle_service().insert_sender_resources_after(
        0, std::move(resources.source), std::move(resources.flow),
        std::move(resources.sender), std::move(resources.connection_sender),
        *gate_, lock);
    if (insert_result != ResourceInsertResult::success)
    {
      // 回滚：资源插入失败，从 stream_store 移除已添加的 sender
      {
        std::lock_guard<std::mutex> sender_lock(sender_mutex_);
        StoreAccessor<Tag>::remove_sender(stream_store_,
                                          utility::us2s(sender_id));
      }
      const auto *name = MediaTraits<Tag>::name();
      if (insert_result == ResourceInsertResult::source_failed)
        throw node_implementation_init_exception(
            std::string("add ") + name + " sender source failed!");
      if (insert_result == ResourceInsertResult::flow_failed)
        throw node_implementation_init_exception(
            std::string("add ") + name + " sender flow failed!");
      if (insert_result == ResourceInsertResult::sender_failed)
        throw node_implementation_init_exception(
            std::string("add ") + name + " sender failed!");
      throw node_implementation_init_exception(
          std::string("add ") + name + " sender connection failed!");
    }

    stream_store_.add_sender_resource_ids(sender_id, source_id, flow_id);
    nmos::modify_resource(
        node_model_.node_resources, device_id_,
        ([&](nmos::resource &device) {
          device.data[nmos::fields::senders] =
              value_from_elements(stream_store_.sender_ids());
          device.data[nmos::fields::version] =
              value(nmos::make_version());
        }));
  }

  template <typename Tag>
  void NodeResourceController::remove_sender_impl(std::string id)
  {
    bool exists = false;
    {
      std::lock_guard<std::mutex> sender_lock(sender_mutex_);
      exists = nullptr != StoreAccessor<Tag>::find_sender(stream_store_, id);
    }
    if (!exists)
    {
      slog::log<slog::severities::error>(*gate_, SLOG_FLF)
          << nmos::stash_category(impl::categories::node_implementation)
          << "remove " << MediaTraits<Tag>::name()
          << " sender not found, id:" << id;
      return;
    }
    nmos::write_lock lock = node_model_.write_lock();

    const auto source_id = impl::make_id(
        seed_id_, nmos::types::source, MediaTraits<Tag>::port(), id);
    const auto flow_id = impl::make_id(
        seed_id_, nmos::types::flow, MediaTraits<Tag>::port(), id);
    const auto sender_id = impl::make_id(
        seed_id_, nmos::types::sender, MediaTraits<Tag>::port(), id);

    lifecycle_service().remove_sender_resources_after(
        0, sender_id, source_id, flow_id, *gate_, lock);

    {
      std::lock_guard<std::mutex> sender_lock(sender_mutex_);
      StoreAccessor<Tag>::remove_sender(stream_store_,
                                        utility::us2s(sender_id));
    }

    stream_store_.remove_sender_resource_ids(sender_id, source_id, flow_id);
    nmos::modify_resource(
        node_model_.node_resources, device_id_,
        ([&](nmos::resource &device) {
          device.data[nmos::fields::senders] =
              value_from_elements(stream_store_.sender_ids());
          device.data[nmos::fields::version] =
              value(nmos::make_version());
        }));
    slog::log<slog::severities::warning>(*gate_, SLOG_FLF)
        << nmos::stash_category(impl::categories::node_implementation)
        << "Remove id:" << id << "  sender_id:" << sender_id;
  }

  template <typename Tag>
  void NodeResourceController::update_sender_impl(
      typename MediaTraits<Tag>::Sender sender)
  {
    bool exists = false;
    {
      std::lock_guard<std::mutex> sender_lock(sender_mutex_);
      exists = nullptr !=
               StoreAccessor<Tag>::find_sender(stream_store_, sender.id);
    }
    if (!exists)
    {
      add_sender_impl<Tag>(std::move(sender));
      return;
    }

    nmos::write_lock lock = node_model_.write_lock();
    if (!ensure_sender_resources_for_update_impl<Tag>(sender, lock))
    {
      return;
    }
    const auto sender_id = impl::make_id(
        seed_id_, nmos::types::sender, MediaTraits<Tag>::port(), sender.id);
    auto resources = make_sender_resources<Tag>(make_resource_factory(), sender);
    replace_sender_resources(
        std::string("update ") + MediaTraits<Tag>::name() + " sender",
        std::move(resources), sender.redundancy.present);
    sender.sender_id = utility::us2s(sender_id);
    {
      std::lock_guard<std::mutex> sender_lock(sender_mutex_);
      StoreAccessor<Tag>::replace_sender(stream_store_, sender);
    }
    node_model_.notify();
  }

  template <typename Tag>
  void NodeResourceController::add_receiver_impl(
      typename MediaTraits<Tag>::Receiver receiver)
  {
    bool exists = false;
    {
      std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
      exists = nullptr !=
               StoreAccessor<Tag>::find_receiver(stream_store_, receiver.id);
    }
    if (exists)
    {
      update_receiver_impl<Tag>(std::move(receiver));
      return;
    }
    nmos::write_lock lock = node_model_.write_lock();
    auto resources =
        make_receiver_resources<Tag>(make_resource_factory(), receiver);
    const auto receiver_id = resources.receiver.id;

    // 将 receiver 提前加入 stream_store，确保 auto_resolver 能查到
    // 若资源插入失败，需要回滚（remove）
    {
      std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
      StoreAccessor<Tag>::add_receiver(stream_store_, std::move(receiver),
                                       receiver_id);
    }

    lifecycle_service().erase_receiver_resources_if_present(receiver_id);

    const auto insert_result =
        lifecycle_service().insert_receiver_resources_after(
            0, std::move(resources.receiver),
            std::move(resources.connection_receiver), *gate_, lock);
    if (insert_result != ResourceInsertResult::success)
    {
      // 回滚：资源插入失败，从 stream_store 移除已添加的 receiver
      {
        std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
        StoreAccessor<Tag>::remove_receiver(stream_store_, receiver_id);
      }
      const auto *name = MediaTraits<Tag>::name();
      if (insert_result == ResourceInsertResult::receiver_failed)
        throw node_implementation_init_exception(
            std::string("add ") + name + " receiver failed!");
      throw node_implementation_init_exception(
          std::string("add ") + name + " receiver connection failed!");
    }

    stream_store_.add_receiver_id(receiver_id);
    nmos::modify_resource(
        node_model_.node_resources, device_id_,
        ([&](nmos::resource &device) {
          device.data[nmos::fields::receivers] =
              value_from_elements(stream_store_.receiver_ids());
          device.data[nmos::fields::version] =
              value(nmos::make_version());
        }));
  }

  template <typename Tag>
  void NodeResourceController::remove_receiver_impl(std::string id)
  {
    nmos::write_lock lock = node_model_.write_lock();
    std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);

    auto *item = StoreAccessor<Tag>::find_receiver(stream_store_, id);
    if (!item)
    {
      slog::log<slog::severities::error>(*gate_, SLOG_FLF)
          << nmos::stash_category(impl::categories::node_implementation)
          << "remove " << MediaTraits<Tag>::name()
          << " receiver not found, id:" << id;
      return;
    }
    const auto receiver_id =
        make_receiver_resource_id<Tag>(seed_id_, id);
    stream_store_.remove_receiver_id(receiver_id);

    nmos::modify_resource(
        node_model_.node_resources, device_id_,
        ([&](nmos::resource &device) {
          device.data[nmos::fields::receivers] =
              value_from_elements(stream_store_.receiver_ids());
          device.data[nmos::fields::version] =
              value(nmos::make_version());
        }));
    node_model_.notify();
    lifecycle_service().remove_receiver_resources_after(
        0, receiver_id, *gate_, lock);
    StoreAccessor<Tag>::remove_receiver(stream_store_, id);
  }

  template <typename Tag>
  void NodeResourceController::update_receiver_impl(
      typename MediaTraits<Tag>::Receiver receiver)
  {
    bool exists = false;
    {
      std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
      exists = nullptr !=
               StoreAccessor<Tag>::find_receiver(stream_store_, receiver.id);
    }
    if (!exists)
    {
      add_receiver_impl<Tag>(std::move(receiver));
      return;
    }

    nmos::write_lock lock = node_model_.write_lock();
    auto resources =
        make_receiver_resources<Tag>(make_resource_factory(), receiver);
    replace_receiver_resources(
        std::string("update ") + MediaTraits<Tag>::name() + " receiver",
        std::move(resources), true);
    {
      std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
      StoreAccessor<Tag>::replace_receiver(stream_store_, receiver);
    }
    node_model_.notify();
  }

  // ============================================================
  // ensure_*_sender_resources_for_update (template)
  // ============================================================

  template <typename Tag>
  bool NodeResourceController::ensure_sender_resources_for_update_impl(
      const typename MediaTraits<Tag>::Sender &item, nmos::write_lock &lock)
  {
    const auto source_id = impl::make_id(
        seed_id_, nmos::types::source, MediaTraits<Tag>::port(), item.id);
    const auto flow_id = impl::make_id(
        seed_id_, nmos::types::flow, MediaTraits<Tag>::port(), item.id);
    const auto sender_id = impl::make_id(
        seed_id_, nmos::types::sender, MediaTraits<Tag>::port(), item.id);
    if (has_sender_resources(sender_id, source_id, flow_id))
    {
      return true;
    }

    slog::log<slog::severities::warning>(*gate_, SLOG_FLF)
        << nmos::stash_category(impl::categories::node_implementation)
        << MediaTraits<Tag>::name()
        << " sender resources incomplete before update, rebuilding id:"
        << item.id;
    lifecycle_service().erase_sender_resources_if_present(sender_id, source_id,
                                                          flow_id);
    stream_store_.remove_sender_resource_ids(sender_id, source_id, flow_id);
    {
      std::lock_guard<std::mutex> sender_lock(sender_mutex_);
      StoreAccessor<Tag>::remove_sender(stream_store_,
                                        utility::us2s(sender_id));
    }

    auto resources =
        make_sender_resources<Tag>(make_resource_factory(), item);
    auto rebuilt = item;
    rebuilt.sender_id = utility::us2s(sender_id);
    {
      std::lock_guard<std::mutex> sender_lock(sender_mutex_);
      StoreAccessor<Tag>::add_sender(stream_store_, std::move(rebuilt));
    }

    const auto insert_result = lifecycle_service().insert_sender_resources_after(
        0, std::move(resources.source), std::move(resources.flow),
        std::move(resources.sender), std::move(resources.connection_sender),
        *gate_, lock);
    if (insert_result != ResourceInsertResult::success)
    {
      {
        std::lock_guard<std::mutex> sender_lock(sender_mutex_);
        StoreAccessor<Tag>::remove_sender(stream_store_,
                                          utility::us2s(sender_id));
      }
      throw node_implementation_init_exception(
          std::string("rebuild ") + MediaTraits<Tag>::name() +
          " sender resources failed!");
    }

    stream_store_.add_sender_resource_ids(sender_id, source_id, flow_id);
    nmos::modify_resource(
        node_model_.node_resources, device_id_,
        ([&](nmos::resource &device) {
          device.data[nmos::fields::senders] =
              value_from_elements(stream_store_.sender_ids());
          device.data[nmos::fields::version] =
              value(nmos::make_version());
        }));
    return false;
  }

  // ============================================================
  // replace_*_for_runtime (template)
  // ============================================================

  template <typename Tag>
  void NodeResourceController::replace_sender_resources_for_runtime_impl(
      const typename MediaTraits<Tag>::Sender &item)
  {
    const auto source_id = impl::make_id(
        seed_id_, nmos::types::source, MediaTraits<Tag>::port(), item.id);
    const auto flow_id = impl::make_id(
        seed_id_, nmos::types::flow, MediaTraits<Tag>::port(), item.id);
    const auto sender_id = impl::make_id(
        seed_id_, nmos::types::sender, MediaTraits<Tag>::port(), item.id);
    if (!has_sender_resources(sender_id, source_id, flow_id))
    {
      slog::log<slog::severities::warning>(*gate_, SLOG_FLF)
          << nmos::stash_category(impl::categories::node_implementation)
          << "runtime interfaces update skipped incomplete "
          << MediaTraits<Tag>::name() << " sender id:" << item.id;
      return;
    }
    replace_sender_resources(
        std::string("runtime interfaces update ") +
            MediaTraits<Tag>::name() + " sender",
        make_sender_resources<Tag>(make_resource_factory(), item),
        true);
  }

  template <typename Tag>
  void NodeResourceController::replace_receiver_resources_for_runtime_impl(
      const typename MediaTraits<Tag>::Receiver &item)
  {
    replace_receiver_resources(
        std::string("runtime interfaces update ") +
            MediaTraits<Tag>::name() + " receiver",
        make_receiver_resources<Tag>(make_resource_factory(), item),
        true);
  }

  // ============================================================
  // Explicit template instantiations
  // ============================================================

  template void NodeResourceController::add_sender_impl<VideoTag>(VideoSender);
  template void NodeResourceController::add_sender_impl<AudioTag>(AudioSender);
  template void NodeResourceController::add_sender_impl<AncillaryTag>(AncillarySender);

  template void NodeResourceController::remove_sender_impl<VideoTag>(std::string);
  template void NodeResourceController::remove_sender_impl<AudioTag>(std::string);
  template void NodeResourceController::remove_sender_impl<AncillaryTag>(std::string);

  template void NodeResourceController::update_sender_impl<VideoTag>(VideoSender);
  template void NodeResourceController::update_sender_impl<AudioTag>(AudioSender);
  template void NodeResourceController::update_sender_impl<AncillaryTag>(AncillarySender);

  template void NodeResourceController::add_receiver_impl<VideoTag>(VideoReceiver);
  template void NodeResourceController::add_receiver_impl<AudioTag>(AudioReceiver);
  template void NodeResourceController::add_receiver_impl<AncillaryTag>(AncillaryReceiver);

  template void NodeResourceController::remove_receiver_impl<VideoTag>(std::string);
  template void NodeResourceController::remove_receiver_impl<AudioTag>(std::string);
  template void NodeResourceController::remove_receiver_impl<AncillaryTag>(std::string);

  template void NodeResourceController::update_receiver_impl<VideoTag>(VideoReceiver);
  template void NodeResourceController::update_receiver_impl<AudioTag>(AudioReceiver);
  template void NodeResourceController::update_receiver_impl<AncillaryTag>(AncillaryReceiver);

  template bool NodeResourceController::ensure_sender_resources_for_update_impl<VideoTag>(
      const VideoSender &, nmos::write_lock &);
  template bool NodeResourceController::ensure_sender_resources_for_update_impl<AudioTag>(
      const AudioSender &, nmos::write_lock &);
  template bool NodeResourceController::ensure_sender_resources_for_update_impl<AncillaryTag>(
      const AncillarySender &, nmos::write_lock &);

  template void NodeResourceController::replace_sender_resources_for_runtime_impl<VideoTag>(
      const VideoSender &);
  template void NodeResourceController::replace_sender_resources_for_runtime_impl<AudioTag>(
      const AudioSender &);
  template void NodeResourceController::replace_sender_resources_for_runtime_impl<AncillaryTag>(
      const AncillarySender &);

  template void NodeResourceController::replace_receiver_resources_for_runtime_impl<VideoTag>(
      const VideoReceiver &);
  template void NodeResourceController::replace_receiver_resources_for_runtime_impl<AudioTag>(
      const AudioReceiver &);
  template void NodeResourceController::replace_receiver_resources_for_runtime_impl<AncillaryTag>(
      const AncillaryReceiver &);

  // ============================================================
  // Public API — thin delegates to template impls
  // ============================================================

  void NodeResourceController::add_video_sender(VideoSender video)
  {
    add_sender_impl<VideoTag>(std::move(video));
  }
  void NodeResourceController::add_audio_sender(AudioSender audio)
  {
    add_sender_impl<AudioTag>(std::move(audio));
  }
  void NodeResourceController::add_ancillary_sender(AncillarySender ancillary)
  {
    add_sender_impl<AncillaryTag>(std::move(ancillary));
  }

  void NodeResourceController::remove_video_sender(std::string id)
  {
    remove_sender_impl<VideoTag>(std::move(id));
  }
  void NodeResourceController::remove_audio_sender(std::string id)
  {
    remove_sender_impl<AudioTag>(std::move(id));
  }
  void NodeResourceController::remove_ancillary_sender(std::string id)
  {
    remove_sender_impl<AncillaryTag>(std::move(id));
  }

  void NodeResourceController::update_video_sender(VideoSender video)
  {
    update_sender_impl<VideoTag>(std::move(video));
  }
  void NodeResourceController::update_audio_sender(AudioSender audio)
  {
    update_sender_impl<AudioTag>(std::move(audio));
  }
  void NodeResourceController::update_ancillary_sender(AncillarySender ancillary)
  {
    update_sender_impl<AncillaryTag>(std::move(ancillary));
  }

  void NodeResourceController::add_video_receiver(VideoReceiver video)
  {
    add_receiver_impl<VideoTag>(std::move(video));
  }
  void NodeResourceController::add_audio_receiver(AudioReceiver audio)
  {
    add_receiver_impl<AudioTag>(std::move(audio));
  }
  void NodeResourceController::add_ancillary_receiver(AncillaryReceiver ancillary)
  {
    add_receiver_impl<AncillaryTag>(std::move(ancillary));
  }

  void NodeResourceController::remove_video_receiver(std::string id)
  {
    remove_receiver_impl<VideoTag>(std::move(id));
  }
  void NodeResourceController::remove_audio_receiver(std::string id)
  {
    remove_receiver_impl<AudioTag>(std::move(id));
  }
  void NodeResourceController::remove_ancillary_receiver(std::string id)
  {
    remove_receiver_impl<AncillaryTag>(std::move(id));
  }

  void NodeResourceController::update_video_receiver(VideoReceiver video)
  {
    update_receiver_impl<VideoTag>(std::move(video));
  }
  void NodeResourceController::update_audio_receiver(AudioReceiver audio)
  {
    update_receiver_impl<AudioTag>(std::move(audio));
  }
  void NodeResourceController::update_ancillary_receiver(
      AncillaryReceiver ancillary)
  {
    update_receiver_impl<AncillaryTag>(std::move(ancillary));
  }

  void NodeResourceController::replace_video_sender_resources_for_runtime(
      const VideoSender &video)
  {
    replace_sender_resources_for_runtime_impl<VideoTag>(video);
  }
  void NodeResourceController::replace_audio_sender_resources_for_runtime(
      const AudioSender &audio)
  {
    replace_sender_resources_for_runtime_impl<AudioTag>(audio);
  }
  void NodeResourceController::replace_ancillary_sender_resources_for_runtime(
      const AncillarySender &ancillary)
  {
    replace_sender_resources_for_runtime_impl<AncillaryTag>(ancillary);
  }

  void NodeResourceController::replace_video_receiver_resources_for_runtime(
      const VideoReceiver &video)
  {
    replace_receiver_resources_for_runtime_impl<VideoTag>(video);
  }
  void NodeResourceController::replace_audio_receiver_resources_for_runtime(
      const AudioReceiver &audio)
  {
    replace_receiver_resources_for_runtime_impl<AudioTag>(audio);
  }
  void NodeResourceController::replace_ancillary_receiver_resources_for_runtime(
      const AncillaryReceiver &ancillary)
  {
    replace_receiver_resources_for_runtime_impl<AncillaryTag>(ancillary);
  }

  // ============================================================
  // replace_sender / replace_receiver (unchanged)
  // ============================================================

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

              // 将新 staged 中 rtp_enabled 同步到保留的 active 端点
              auto &preserved_active =
                  resources.connection_receiver
                      .data[nmos::fields::endpoint_active];
              auto &new_staged =
                  resources.connection_receiver
                      .data[nmos::fields::endpoint_staged];
              if (preserved_active.has_field(nmos::fields::transport_params) &&
                  new_staged.has_field(nmos::fields::transport_params))
              {
                auto &active_tp =
                    preserved_active[nmos::fields::transport_params];
                auto &staged_tp =
                    new_staged[nmos::fields::transport_params];
                if (active_tp.is_array() && staged_tp.is_array())
                {
                  auto &active_tp_arr = active_tp.as_array();
                  auto &staged_tp_arr = staged_tp.as_array();
                  for (size_t i = 0;
                       i < active_tp_arr.size() && i < staged_tp_arr.size();
                       ++i)
                  {
                    if (staged_tp_arr[i].has_field(
                            nmos::fields::rtp_enabled))
                    {
                      active_tp_arr[i][nmos::fields::rtp_enabled] =
                          staged_tp_arr[i][nmos::fields::rtp_enabled];
                    }
                  }
                }
              }
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

  // ============================================================
  // StreamStore pass-throughs (unchanged)
  // ============================================================

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

} // namespace seeder::nmos_node::internal