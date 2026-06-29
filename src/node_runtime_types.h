#pragma once

#include <cpprest/host_utils.h>
#include <cpprest/json.h>

#include <vector>

namespace seeder
{
  namespace nmos_node
  {
    using NodeSettingsJson = web::json::value;
    using RuntimeInterface = web::hosts::experimental::host_interface;
    using RuntimeInterfaces = std::vector<RuntimeInterface>;
  }
}
