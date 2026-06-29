#include "node_connection_transport_params.h"

#include <nmos/json_fields.h>

namespace seeder::nmos_node::internal::connection_transport_params
{
  bool active_leg_count_matches(const web::json::value &endpoint_active,
                                const bool st2022_7)
  {
    if (!endpoint_active.is_object() ||
        !endpoint_active.has_field(nmos::fields::transport_params))
    {
      return false;
    }

    const auto &transport_params =
        nmos::fields::transport_params(endpoint_active);
    return transport_params.is_array() &&
           transport_params.as_array().size() == (st2022_7 ? 2 : 1);
  }

  bool is_array(const web::json::value &transport_params)
  {
    return transport_params.is_array();
  }

  State state(const web::json::value &transport_params)
  {
    if (!is_array(transport_params))
    {
      return State::not_array;
    }

    if (is_empty(transport_params))
    {
      return State::empty;
    }

    return State::ready;
  }

  bool is_empty(const web::json::value &transport_params)
  {
    return 0 == transport_params.as_array().size();
  }
}
