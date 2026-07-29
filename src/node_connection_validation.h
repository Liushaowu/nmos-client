#pragma once

#include "node_activation_context.h"

#include <nmos/connection_api.h>
#include <nmos/settings.h>

namespace seeder::nmos_node::internal
{
  nmos::details::connection_resource_patch_validator
  make_connection_resource_patch_validator(const nmos::settings &settings,
                                           ActivationContext ctx);
}
