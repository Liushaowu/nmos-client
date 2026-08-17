#pragma once

#include <cpprest/basic_utils.h>
#include <cpprest/host_utils.h>
#include <cpprest/json_ops.h>
#include <nmos/json_fields.h>

#include <algorithm>
#include <string>
#include <vector>

namespace seeder::nmos_node::internal::resource_factory_detail
{
  struct RuntimeInterfaceLeg
  {
    web::hosts::experimental::host_interface interface;
    std::string ip;
    bool selected = false;
  };

  struct RuntimeInterfaceSelection
  {
    RuntimeInterfaceLeg primary;
    RuntimeInterfaceLeg redundancy;
  };

  inline std::string first_interface_address(
      const web::hosts::experimental::host_interface &interface)
  {
    return interface.addresses.empty()
               ? std::string{}
               : utility::conversions::to_utf8string(interface.addresses.front());
  }

  inline bool interface_has_address(
      const web::hosts::experimental::host_interface &interface,
      const std::string &address)
  {
    return !address.empty() &&
           std::any_of(interface.addresses.begin(), interface.addresses.end(),
                       [&](const utility::string_t &interface_address)
                       {
                         return utility::conversions::to_utf8string(
                                    interface_address) == address;
                       });
  }

  inline RuntimeInterfaceLeg make_runtime_interface_leg(
      const web::hosts::experimental::host_interface &interface,
      const std::string &ip)
  {
    return RuntimeInterfaceLeg{interface, ip, true};
  }

  inline RuntimeInterfaceLeg find_interface_by_exact_address(
      const std::vector<web::hosts::experimental::host_interface> &interfaces,
      const std::string &configured_ip)
  {
    for (const auto &interface : interfaces)
    {
      if (interface_has_address(interface, configured_ip))
      {
        return make_runtime_interface_leg(interface, configured_ip);
      }
    }
    return RuntimeInterfaceLeg{};
  }

  inline RuntimeInterfaceLeg select_runtime_interface_leg(
      const std::vector<web::hosts::experimental::host_interface> &interfaces,
      const std::string &configured_ip)
  {
    if (auto leg = find_interface_by_exact_address(interfaces, configured_ip);
        leg.selected)
      return leg;
    for (const auto &interface : interfaces)
    {
      if (!interface.addresses.empty())
      {
        return make_runtime_interface_leg(interface,
                                          first_interface_address(interface));
      }
    }
    if (!interfaces.empty())
    {
      return make_runtime_interface_leg(interfaces.front(), std::string{});
    }
    return RuntimeInterfaceLeg{};
  }

  inline RuntimeInterfaceLeg select_redundancy_runtime_interface_leg(
      const std::vector<web::hosts::experimental::host_interface> &interfaces,
      const std::string &configured_ip, const RuntimeInterfaceLeg &primary)
  {
    if (auto leg = find_interface_by_exact_address(interfaces, configured_ip);
        leg.selected)
      return leg;
    for (const auto &interface : interfaces)
    {
      if (!interface.addresses.empty() &&
          (!primary.selected || interface.name != primary.interface.name))
      {
        return make_runtime_interface_leg(interface,
                                          first_interface_address(interface));
      }
    }
    if (primary.selected)
    {
      return primary;
    }
    return select_runtime_interface_leg(interfaces, std::string{});
  }

  inline RuntimeInterfaceSelection select_runtime_interfaces(
      const std::vector<web::hosts::experimental::host_interface> &interfaces,
      const std::string &primary_source_ip,
      const std::string &redundancy_source_ip,
      const bool redundancy_enabled)
  {
    RuntimeInterfaceSelection selection;
    selection.primary = select_runtime_interface_leg(interfaces, primary_source_ip);
    if (redundancy_enabled)
    {
      selection.redundancy = select_redundancy_runtime_interface_leg(
          interfaces, redundancy_source_ip, selection.primary);
    }
    return selection;
  }

  inline std::vector<utility::string_t> selected_interface_names(
      const RuntimeInterfaceSelection &selection, const bool redundancy_enabled)
  {
    std::vector<utility::string_t> names;
    if (selection.primary.selected)
    {
      names.push_back(selection.primary.interface.name);
    }
    if (redundancy_enabled && selection.redundancy.selected &&
        (!selection.primary.selected ||
         selection.redundancy.interface.name != selection.primary.interface.name))
    {
      names.push_back(selection.redundancy.interface.name);
    }
    return names;
  }

  inline web::json::value interface_address_constraint(
      const RuntimeInterfaceLeg &leg)
  {
    return web::json::value_of({{nmos::fields::constraint_enum,
                                 leg.selected ? web::json::value_from_elements(
                                                    leg.interface.addresses)
                                              : web::json::value::array()}});
  }
}
