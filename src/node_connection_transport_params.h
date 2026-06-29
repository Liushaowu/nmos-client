#pragma once

#include <cpprest/json.h>

namespace seeder::nmos_node::internal::connection_transport_params
{
  enum class State
  {
    ready,
    not_array,
    empty
  };

  bool active_leg_count_matches(const web::json::value &endpoint_active,
                                bool st2022_7);

  State state(const web::json::value &transport_params);

  bool is_array(const web::json::value &transport_params);

  bool is_empty(const web::json::value &transport_params);
}
