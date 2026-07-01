#pragma once

#include <cpprest/details/basic_types.h>
#include <cpprest/json.h>
#include <nmos/json_fields.h>

#include <cstddef>
#include <string>

namespace seeder::nmos_node::internal::resource_factory_detail
{
  inline std::string receiver_multicast_ip_or_default(
      const std::string &configured_ip, const std::string &fallback_ip)
  {
    return configured_ip.empty() ? fallback_ip : configured_ip;
  }

  inline int receiver_port_or_default(
      const int configured_port, const int fallback_port)
  {
    return 0 < configured_port ? configured_port : fallback_port;
  }

  inline void set_receiver_transport_params_leg(
      web::json::value &endpoint, const std::size_t leg_index,
      const std::string &multicast_ip, const std::string &interface_ip,
      const int destination_port)
  {
    endpoint[nmos::fields::transport_params][leg_index]
            [nmos::fields::multicast_ip] =
                web::json::value::string(utility::s2us(multicast_ip));
    endpoint[nmos::fields::transport_params][leg_index]
            [nmos::fields::interface_ip] =
                web::json::value::string(utility::s2us(interface_ip));
    endpoint[nmos::fields::transport_params][leg_index]
            [nmos::fields::destination_port] = destination_port;
  }

  inline void initialize_receiver_transport_params(
      web::json::value &staged, web::json::value &active,
      const std::string &multicast_ip, const std::string &interface_ip,
      const int destination_port)
  {
    set_receiver_transport_params_leg(staged, 0, multicast_ip, interface_ip,
                                      destination_port);
    set_receiver_transport_params_leg(active, 0, multicast_ip, interface_ip,
                                      destination_port);
  }

  inline void initialize_receiver_transport_params(
      web::json::value &staged, web::json::value &active,
      const std::string &multicast_ip, const std::string &interface_ip,
      const int destination_port, const std::string &secondary_multicast_ip,
      const std::string &secondary_interface_ip,
      const int secondary_destination_port)
  {
    initialize_receiver_transport_params(staged, active, multicast_ip,
                                         interface_ip, destination_port);
    set_receiver_transport_params_leg(staged, 1, secondary_multicast_ip,
                                      secondary_interface_ip,
                                      secondary_destination_port);
    set_receiver_transport_params_leg(active, 1, secondary_multicast_ip,
                                      secondary_interface_ip,
                                      secondary_destination_port);
  }

  inline void set_sender_endpoint_enable(web::json::value &endpoint,
                                         const bool primary_enabled,
                                         const bool secondary_enabled)
  {
    endpoint[nmos::fields::master_enable] =
        web::json::value::boolean(primary_enabled);
    auto &transport_params = endpoint[nmos::fields::transport_params];
    if (!transport_params.is_array())
    {
      return;
    }

    auto &transport_params_array = transport_params.as_array();
    if (transport_params_array.size() > 0)
    {
      transport_params_array[0][nmos::fields::rtp_enabled] =
          web::json::value::boolean(primary_enabled);
    }
    if (transport_params_array.size() > 1)
    {
      transport_params_array[1][nmos::fields::rtp_enabled] =
          web::json::value::boolean(secondary_enabled);
    }
  }

  inline void initialize_sender_endpoint_enable(web::json::value &staged,
                                                web::json::value &active,
                                                const bool stream_enabled,
                                                const bool redundancy_enabled)
  {
    const bool secondary_enabled = stream_enabled && redundancy_enabled;
    set_sender_endpoint_enable(staged, stream_enabled, secondary_enabled);
    set_sender_endpoint_enable(active, stream_enabled, secondary_enabled);
  }
}
