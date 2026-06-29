#pragma once

#include <cpprest/json.h>
#include <nmos/mdns.h>
#include <slog/all_in_one.h>

#include <string>

namespace seeder::nmos_node::internal
{
  web::json::value parse_json_file(const std::string &file_path);
  void write_json_file(const std::string &file_path,
                       const web::json::value &value);
  void apply_interface_host_addresses(web::json::value &settings);
  web::json::value registration_api_to_json(
      const nmos::experimental::resolved_service &service);

  class NodeSettings
  {
  public:
    NodeSettings() = default;
    explicit NodeSettings(std::string config_file);

    const std::string &config_file() const;
    bool has_config_file() const;
    web::json::value load_runtime_settings() const;
    web::json::value persisted_settings() const;
    void write_persisted_settings(const web::json::value &settings) const;
    web::json::value discover_registration_apis(
        const web::json::value &settings, slog::base_gate &gate) const;

  private:
    std::string config_file_;
  };
}
