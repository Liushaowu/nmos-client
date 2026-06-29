#pragma once

#include <nmos/id.h>
#include <nmos/model.h>
#include <nmos/mutex.h>
#include <nmos/resource.h>
#include <nmos/resources.h>

#include <slog/all_in_one.h>

namespace seeder::nmos_node::internal
{
  enum class ResourceInsertResult
  {
    success,
    source_failed,
    flow_failed,
    sender_failed,
    receiver_failed,
    connection_failed
  };

  class ResourceLifecycleService
  {
  public:
    explicit ResourceLifecycleService(nmos::node_model &model);

    bool insert_after(unsigned int milliseconds,
                      nmos::resources &resources,
                      nmos::resource &&resource,
                      slog::base_gate &gate,
                      nmos::write_lock &lock);

    bool remove_after(unsigned int milliseconds,
                      nmos::resources &resources,
                      const nmos::id &id,
                      slog::base_gate &gate,
                      nmos::write_lock &lock);

    void remove_sender_resources_after(unsigned int milliseconds,
                                       const nmos::id &sender_id,
                                       const nmos::id &source_id,
                                       const nmos::id &flow_id,
                                       slog::base_gate &gate,
                                       nmos::write_lock &lock);

    void remove_receiver_resources_after(unsigned int milliseconds,
                                         const nmos::id &receiver_id,
                                         slog::base_gate &gate,
                                         nmos::write_lock &lock);

    void erase_if_present(nmos::resources &resources, const nmos::id &id);

    void erase_sender_resources_if_present(const nmos::id &sender_id,
                                           const nmos::id &source_id,
                                           const nmos::id &flow_id);

    void erase_receiver_resources_if_present(const nmos::id &receiver_id);

    ResourceInsertResult insert_sender_resources_after(
        unsigned int milliseconds,
        nmos::resource &&source,
        nmos::resource &&flow,
        nmos::resource &&sender,
        nmos::resource &&connection_sender,
        slog::base_gate &gate,
        nmos::write_lock &lock);

    ResourceInsertResult insert_receiver_resources_after(
        unsigned int milliseconds,
        nmos::resource &&receiver,
        nmos::resource &&connection_receiver,
        slog::base_gate &gate,
        nmos::write_lock &lock);

    bool replace_node_resource(nmos::resource replacement);
    bool replace_connection_resource(nmos::resource replacement);

    bool replace_sender_node_resources(nmos::resource &&source,
                                       nmos::resource &&flow,
                                       nmos::resource &&sender);

    bool replace_receiver_node_resource(nmos::resource &&receiver);

  private:
    nmos::node_model &model_;
  };
}
