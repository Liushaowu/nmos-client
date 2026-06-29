#include "node_resource_factory.h"

#include "node_implementation.h"

#include <nmos/format.h>

#include <utility>

namespace seeder::nmos_node::internal
{
  NodeResourceFactory::NodeResourceFactory(
      utility::string_t seed_id, nmos::id device_id, web::json::value settings,
      std::vector<web::hosts::experimental::host_interface> runtime_interfaces)
      : seed_id_(std::move(seed_id)), device_id_(std::move(device_id)),
        settings_(std::move(settings)),
        runtime_interfaces_(std::move(runtime_interfaces))
  {
  }

  nmos::id NodeResourceFactory::make_video_receiver_resource_id(
      const std::string &id) const
  {
    return impl::make_id(seed_id_, nmos::types::receiver, impl::ports::video, id);
  }

  nmos::id NodeResourceFactory::make_audio_receiver_resource_id(
      const std::string &id) const
  {
    return impl::make_id(seed_id_, nmos::types::receiver, impl::ports::audio, id);
  }

  nmos::id NodeResourceFactory::make_ancillary_receiver_resource_id(
      const std::string &id) const
  {
    return impl::make_id(seed_id_, nmos::types::receiver, impl::ports::data, id);
  }
}
