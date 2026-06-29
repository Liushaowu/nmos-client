#include "node_resource_lifecycle.h"

#include <boost/chrono/duration.hpp>
#include <nmos/slog.h>

#include <utility>

namespace seeder::nmos_node::internal
{
  ResourceLifecycleService::ResourceLifecycleService(nmos::node_model &model)
      : model_(model)
  {
  }

  bool ResourceLifecycleService::insert_after(unsigned int milliseconds,
                                              nmos::resources &resources,
                                              nmos::resource &&resource,
                                              slog::base_gate &gate,
                                              nmos::write_lock &lock)
  {
    if (nmos::details::wait_for(model_.shutdown_condition, lock,
                                boost::chrono::milliseconds(milliseconds),
                                [&]
                                { return model_.shutdown; }))
    {
      return false;
    }

    const std::pair<nmos::id, nmos::type> id_type{resource.id, resource.type};
    const bool success = nmos::insert_resource(resources, std::move(resource)).second;

    if (success)
    {
      slog::log<slog::severities::info>(gate, SLOG_FLF)
          << "Updated node_model_ with " << id_type;
    }
    else
    {
      slog::log<slog::severities::severe>(gate, SLOG_FLF)
          << "Model update error: " << id_type;
    }

    slog::log<slog::severities::too_much_info>(gate, SLOG_FLF)
        << "Notifying node behaviour thread";
    model_.notify();
    return success;
  }

  bool ResourceLifecycleService::remove_after(unsigned int milliseconds,
                                              nmos::resources &resources,
                                              const nmos::id &id,
                                              slog::base_gate &gate,
                                              nmos::write_lock &lock)
  {
    if (nmos::details::wait_for(model_.shutdown_condition, lock,
                                boost::chrono::milliseconds(milliseconds),
                                [&]
                                { return model_.shutdown; }))
    {
      return false;
    }

    const bool success = nmos::erase_resource(resources, id);
    if (success)
    {
      slog::log<slog::severities::info>(gate, SLOG_FLF)
          << "Remove node_model_ with " << id;
    }
    else
    {
      slog::log<slog::severities::severe>(gate, SLOG_FLF)
          << "Model Remove error: " << id;
    }

    slog::log<slog::severities::too_much_info>(gate, SLOG_FLF)
        << "Notifying node behaviour thread";
    model_.notify();
    return success;
  }

  void ResourceLifecycleService::remove_sender_resources_after(
      const unsigned int milliseconds,
      const nmos::id &sender_id,
      const nmos::id &source_id,
      const nmos::id &flow_id,
      slog::base_gate &gate,
      nmos::write_lock &lock)
  {
    remove_after(milliseconds, model_.node_resources, sender_id, gate, lock);
    remove_after(milliseconds, model_.connection_resources, sender_id, gate, lock);
    remove_after(milliseconds, model_.node_resources, source_id, gate, lock);
    remove_after(milliseconds, model_.node_resources, flow_id, gate, lock);
  }

  void ResourceLifecycleService::remove_receiver_resources_after(
      const unsigned int milliseconds,
      const nmos::id &receiver_id,
      slog::base_gate &gate,
      nmos::write_lock &lock)
  {
    remove_after(milliseconds, model_.connection_resources, receiver_id, gate,
                 lock);
    remove_after(milliseconds, model_.node_resources, receiver_id, gate, lock);
  }

  void ResourceLifecycleService::erase_if_present(nmos::resources &resources,
                                                  const nmos::id &id)
  {
    nmos::erase_resource(resources, id);
  }

  void ResourceLifecycleService::erase_sender_resources_if_present(
      const nmos::id &sender_id,
      const nmos::id &source_id,
      const nmos::id &flow_id)
  {
    erase_if_present(model_.connection_resources, sender_id);
    erase_if_present(model_.node_resources, sender_id);
    erase_if_present(model_.node_resources, flow_id);
    erase_if_present(model_.node_resources, source_id);
  }

  void ResourceLifecycleService::erase_receiver_resources_if_present(
      const nmos::id &receiver_id)
  {
    erase_if_present(model_.connection_resources, receiver_id);
    erase_if_present(model_.node_resources, receiver_id);
  }

  ResourceInsertResult ResourceLifecycleService::insert_sender_resources_after(
      const unsigned int milliseconds,
      nmos::resource &&source,
      nmos::resource &&flow,
      nmos::resource &&sender,
      nmos::resource &&connection_sender,
      slog::base_gate &gate,
      nmos::write_lock &lock)
  {
    const auto source_id = source.id;
    const auto flow_id = flow.id;
    const auto sender_id = sender.id;

    if (!insert_after(milliseconds, model_.node_resources,
                      std::move(source), gate, lock))
    {
      return ResourceInsertResult::source_failed;
    }
    if (!insert_after(milliseconds, model_.node_resources,
                      std::move(flow), gate, lock))
    {
      erase_if_present(model_.node_resources, source_id);
      return ResourceInsertResult::flow_failed;
    }
    if (!insert_after(milliseconds, model_.node_resources,
                      std::move(sender), gate, lock))
    {
      erase_if_present(model_.node_resources, flow_id);
      erase_if_present(model_.node_resources, source_id);
      return ResourceInsertResult::sender_failed;
    }
    if (!insert_after(milliseconds, model_.connection_resources,
                      std::move(connection_sender), gate, lock))
    {
      erase_if_present(model_.node_resources, sender_id);
      erase_if_present(model_.node_resources, flow_id);
      erase_if_present(model_.node_resources, source_id);
      return ResourceInsertResult::connection_failed;
    }
    return ResourceInsertResult::success;
  }

  ResourceInsertResult ResourceLifecycleService::insert_receiver_resources_after(
      const unsigned int milliseconds,
      nmos::resource &&receiver,
      nmos::resource &&connection_receiver,
      slog::base_gate &gate,
      nmos::write_lock &lock)
  {
    const auto receiver_id = receiver.id;
    if (!insert_after(milliseconds, model_.node_resources,
                      std::move(receiver), gate, lock))
    {
      return ResourceInsertResult::receiver_failed;
    }
    if (!insert_after(milliseconds, model_.connection_resources,
                      std::move(connection_receiver), gate, lock))
    {
      erase_if_present(model_.node_resources, receiver_id);
      return ResourceInsertResult::connection_failed;
    }
    return ResourceInsertResult::success;
  }

  bool ResourceLifecycleService::replace_node_resource(
      nmos::resource replacement)
  {
    const auto resource_id = replacement.id;
    return nmos::modify_resource(
        model_.node_resources, resource_id,
        [&](nmos::resource &resource)
        {
          resource = std::move(replacement);
        });
  }

  bool ResourceLifecycleService::replace_connection_resource(
      nmos::resource replacement)
  {
    const auto resource_id = replacement.id;
    return nmos::modify_resource(
        model_.connection_resources, resource_id,
        [&](nmos::resource &resource)
        {
          resource = std::move(replacement);
        });
  }

  bool ResourceLifecycleService::replace_sender_node_resources(
      nmos::resource &&source,
      nmos::resource &&flow,
      nmos::resource &&sender)
  {
    const bool source_updated = replace_node_resource(std::move(source));
    const bool flow_updated = replace_node_resource(std::move(flow));
    const bool sender_updated = replace_node_resource(std::move(sender));
    return source_updated && flow_updated && sender_updated;
  }

  bool ResourceLifecycleService::replace_receiver_node_resource(
      nmos::resource &&receiver)
  {
    return replace_node_resource(std::move(receiver));
  }
}
