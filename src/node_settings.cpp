#include "node_settings.h"

#include <arpa/inet.h>
#include <cpprest/basic_utils.h>
#include <cpprest/asyncrt_utils.h>
#include <ifaddrs.h>
#include <mdns/service_discovery.h>
#include <netinet/in.h>
#include <nmos/mdns.h>
#include <nmos/version.h>
#include <sys/socket.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace seeder::nmos_node::internal
{
  namespace
  {
    std::string to_utf8_string(const utility::string_t &value)
    {
      return utility::conversions::to_utf8string(value);
    }

    bool is_ipv4_literal(const std::string &value)
    {
      in_addr address{};
      return 1 == inet_pton(AF_INET, value.c_str(), &address);
    }

    std::string resolve_interface_ipv4_address(const std::string &interface_name)
    {
      ifaddrs *interfaces = nullptr;
      if (0 != getifaddrs(&interfaces))
      {
        throw std::runtime_error(
            "Failed to enumerate network interfaces while resolving interfaces entry '" +
            interface_name + "': " + std::strerror(errno));
      }

      std::string resolved_address;
      for (auto interface = interfaces; nullptr != interface; interface = interface->ifa_next)
      {
        if (nullptr == interface->ifa_addr || nullptr == interface->ifa_name ||
            interface_name != interface->ifa_name ||
            AF_INET != interface->ifa_addr->sa_family)
        {
          continue;
        }

        char address_buffer[INET_ADDRSTRLEN] = {};
        const auto *socket_address =
            reinterpret_cast<const sockaddr_in *>(interface->ifa_addr);
        if (nullptr == inet_ntop(AF_INET, &socket_address->sin_addr,
                                 address_buffer, sizeof(address_buffer)))
        {
          freeifaddrs(interfaces);
          throw std::runtime_error(
              "Failed to convert IPv4 address for interfaces entry '" +
              interface_name + "'");
        }

        resolved_address = address_buffer;
        break;
      }

      freeifaddrs(interfaces);
      return resolved_address;
    }

    void set_host_addresses(web::json::value &settings,
                            const std::vector<std::string> &host_addresses)
    {
      auto normalized_host_addresses =
          web::json::value::array(host_addresses.size());
      for (size_t index = 0; index < host_addresses.size(); ++index)
      {
        normalized_host_addresses[index] = web::json::value::string(
            utility::s2us(host_addresses[index]));
      }

      settings[utility::s2us("host_addresses")] = normalized_host_addresses;
      if (!host_addresses.empty())
      {
        settings[utility::s2us("host_address")] = web::json::value::string(
            utility::s2us(host_addresses.front()));
      }
    }

    void append_unique_host_address(std::vector<std::string> &host_addresses,
                                    const std::string &host_address)
    {
      if (host_addresses.end() == std::find(host_addresses.begin(),
                                            host_addresses.end(), host_address))
      {
        host_addresses.push_back(host_address);
      }
    }
  }

  web::json::value parse_json_file(const std::string &file_path)
  {
    std::ifstream file(file_path);
    file.exceptions(std::ios_base::failbit);
    auto parsed = web::json::value::parse(file);
    parsed.as_object();
    return parsed;
  }

  void write_json_file(const std::string &file_path, const web::json::value &value)
  {
    if (!value.is_object())
    {
      throw std::runtime_error("node settings payload must be a JSON object");
    }

    std::ofstream file(file_path, std::ios_base::out | std::ios_base::trunc);
    if (!file)
    {
      throw std::runtime_error("failed to open node config file for write: " +
                               file_path);
    }

    file << utility::conversions::to_utf8string(value.serialize());
    if (!file)
    {
      throw std::runtime_error("failed to write node config file: " + file_path);
    }
  }

  void apply_interface_host_addresses(web::json::value &settings)
  {
    const auto host_addresses_field = utility::s2us("host_addresses");
    const auto interfaces_field = utility::s2us("interfaces");
    if (!settings.is_object())
    {
      return;
    }

    std::vector<std::string> resolved_host_addresses;
    if (settings.has_field(host_addresses_field))
    {
      const auto &host_addresses = settings.at(host_addresses_field);
      if (!host_addresses.is_array())
      {
        return;
      }

      for (const auto &host_address : host_addresses.as_array())
      {
        if (!host_address.is_string())
        {
          throw std::runtime_error("host_addresses entries must be IPv4 strings");
        }

        const auto host_address_text = to_utf8_string(host_address.as_string());
        if (!is_ipv4_literal(host_address_text))
        {
          throw std::runtime_error(
              "host_addresses entry '" + host_address_text +
              "' is not a valid IPv4 address; use interfaces for Linux interface names");
        }

        append_unique_host_address(resolved_host_addresses, host_address_text);
      }
    }

    if (settings.has_field(interfaces_field))
    {
      const auto &interface_names = settings.at(interfaces_field);
      if (!interface_names.is_array())
      {
        return;
      }

      for (const auto &interface_name : interface_names.as_array())
      {
        if (!interface_name.is_string())
        {
          throw std::runtime_error("interfaces entries must be Linux interface names");
        }

        const auto interface_name_text = to_utf8_string(interface_name.as_string());
        const auto resolved_address =
            resolve_interface_ipv4_address(interface_name_text);
        if (resolved_address.empty())
        {
          continue;
        }

        append_unique_host_address(resolved_host_addresses, resolved_address);
      }
    }

    if (!resolved_host_addresses.empty())
    {
      set_host_addresses(settings, resolved_host_addresses);
    }
  }

  web::json::value registration_api_to_json(
      const nmos::experimental::resolved_service &service)
  {
    web::json::value object = web::json::value::object();
    object[utility::conversions::to_string_t("uri")] =
        web::json::value::string(service.second.to_string());
    object[utility::conversions::to_string_t("version")] =
        web::json::value::string(nmos::make_api_version(service.first.first));
    object[utility::conversions::to_string_t("priority")] =
        web::json::value::number(service.first.second);
    object[utility::conversions::to_string_t("host")] =
        web::json::value::string(service.second.host());
    object[utility::conversions::to_string_t("port")] =
        web::json::value::number(service.second.port());
    return object;
  }

  NodeSettings::NodeSettings(std::string config_file)
      : config_file_(std::move(config_file))
  {
  }

  const std::string &NodeSettings::config_file() const
  {
    return config_file_;
  }

  bool NodeSettings::has_config_file() const
  {
    return !config_file_.empty();
  }

  web::json::value NodeSettings::load_runtime_settings() const
  {
    std::error_code error;
    auto settings = web::json::value::parse(utility::s2us(config_file_), error);
    if (!error)
    {
      return settings;
    }

    return parse_json_file(config_file_);
  }

  web::json::value NodeSettings::persisted_settings() const
  {
    if (!has_config_file())
    {
      return web::json::value::object();
    }
    return parse_json_file(config_file_);
  }

  void NodeSettings::write_persisted_settings(
      const web::json::value &settings) const
  {
    if (!has_config_file())
    {
      throw std::runtime_error("node config file path is empty");
    }
    write_json_file(config_file_, settings);
  }

  web::json::value NodeSettings::discover_registration_apis(
      const web::json::value &settings, slog::base_gate &gate) const
  {
    mdns::service_discovery discovery(gate);
    auto services = nmos::experimental::resolve_service_(
                        discovery, nmos::service_types::registration, settings)
                        .get();

    web::json::value result = web::json::value::array();
    std::size_t index = 0;
    for (const auto &service : services)
    {
      result[index++] = registration_api_to_json(service);
    }
    return result;
  }
}
