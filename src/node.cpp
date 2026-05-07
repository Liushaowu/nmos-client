#include "node.h"
#include "daemon/video_format.h"
#include "mtl/st20_api.h"
#include "nmos/api_utils.h" // for make_api_listener
#include "nmos/authorization_behaviour.h"
#include "nmos/authorization_redirect_api.h"
#include "nmos/control_protocol_state.h"
#include "nmos/jwks_uri_api.h"
#include "nmos/log_gate.h"
#include "nmos/model.h"
#include "nmos/node_server.h"
#include "nmos/mdns.h"
#include "nmos/ocsp_behaviour.h"
#include "nmos/ocsp_response_handler.h"
#include "nmos/process_utils.h"
#include "nmos/sdp_utils.h"
#include "nmos/server.h"
#include "nmos/server_utils.h" // for make_http_listener_config
#include "node_implementation.h"
#include <asm-generic/errno.h>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cpprest/details/basic_types.h>
#include <cpprest/json.h>
#include <cpprest/json_ops.h>
#include <cpprest/json_utils.h>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <fstream>
#include <sstream>
#include <thread>
#include <nmos/capabilities.h>
#include <nmos/connection_api.h>
#include <nmos/id.h>
#include <nmos/interlace_mode.h>
#include <nmos/json_fields.h>
#include <nmos/media_type.h>
#include <nmos/mutex.h>
#include <nmos/node_resource.h>
#include <nmos/is04_versions.h>
#include <nmos/rational.h>
#include <nmos/resource.h>
#include <nmos/resources.h>
#include <nmos/tai.h>
#include <nmos/transport.h>
#include <nmos/type.h>
#include <nmos/version.h>
#include <sdp/json.h>
#include <slog/all_in_one.h>
#include <mdns/service_discovery.h>
#include <stdexcept>
#include <string>
#include <vector>

#include <nmos/id.h>
#include <nmos/mutex.h>
#include <algorithm>
using web::json::value;
using web::json::value_from_elements;
using web::json::value_of;

namespace
{
  std::string to_utf8_string(const utility::string_t &value)
  {
    return utility::conversions::to_utf8string(value);
  }

  std::string colorimetry_to_string(const sdp::colorimetry &value)
  {
    return to_utf8_string(value.name);
  }

  std::string transfer_characteristic_to_string(
      const sdp::transfer_characteristic_system &value)
  {
    return to_utf8_string(value.name);
  }

  std::string guess_video_format_name(const nmos::video_raw_parameters &params)
  {
    if (0 == params.width || 0 == params.height || 0 == params.exactframerate.denominator())
    {
      return {};
    }

    const auto fps = static_cast<double>(params.exactframerate.numerator()) /
                     static_cast<double>(params.exactframerate.denominator());
    const auto frame_rate_matches = [&](const seeder::core::video_format_desc &desc)
    {
      return desc.width == static_cast<int>(params.width) &&
             desc.height == static_cast<int>(params.height) &&
             desc.field_count == (params.interlace ? 2 : 1) &&
             std::fabs(desc.fps - fps) < 0.02;
    };

    for (const auto &desc : seeder::core::format_descs)
    {
      if (frame_rate_matches(desc))
      {
        return desc.name;
      }
    }

    return {};
  }

  bool update_video_receiver_from_transport_file(
      seeder::nmos_node::VideoReceiver &receiver,
      const web::json::value &transport_file,
      slog::base_gate &gate)
  {
    if (!transport_file.is_object())
    {
      return false;
    }

    if (!transport_file.has_field(nmos::fields::transportfile_type) ||
        !transport_file.has_field(nmos::fields::transportfile_data))
    {
      return false;
    }

    const auto transport_file_type =
        nmos::fields::transportfile_type(transport_file);
    if (transport_file_type != nmos::media_types::application_sdp.name)
    {
      return false;
    }

    const auto transport_file_data =
        nmos::fields::transportfile_data(transport_file);
    if (!transport_file_data.is_string())
    {
      return false;
    }

    try
    {
      const auto session_description =
          sdp::parse_session_description(to_utf8_string(transport_file_data.as_string()));
      const auto [sdp_params, transport_params] =
          nmos::parse_session_description(session_description);
      static_cast<void>(transport_params);

      const auto media_type = nmos::get_media_type(sdp_params);
      if (nmos::media_types::video_raw == media_type)
      {
        const auto video = nmos::get_video_raw_parameters(sdp_params);
        const auto format_name = guess_video_format_name(video);
        if (!format_name.empty())
        {
          receiver.format = format_name;
        }
        receiver.colorspace = colorimetry_to_string(video.colorimetry);
        receiver.transfer_characteristics =
            transfer_characteristic_to_string(video.tcs);
        return true;
      }

      if (nmos::media_types::video_jxsv == media_type)
      {
        const auto video = nmos::get_video_jxsv_parameters(sdp_params);
        receiver.colorspace = colorimetry_to_string(video.colorimetry);
        receiver.transfer_characteristics =
            transfer_characteristic_to_string(video.tcs);
        return true;
      }
    }
    catch (const std::exception &error)
    {
      slog::log<slog::severities::warning>(gate, SLOG_FLF)
          << nmos::stash_category(impl::categories::node_implementation)
          << "parse transport_file.data SDP failed: " << error.what();
    }

    return false;
  }

  bool update_audio_receiver_from_transport_file(
      seeder::nmos_node::AudioReceiver &receiver,
      const web::json::value &transport_file,
      slog::base_gate &gate)
  {
    if (!transport_file.is_object())
    {
      return false;
    }

    if (!transport_file.has_field(nmos::fields::transportfile_type) ||
        !transport_file.has_field(nmos::fields::transportfile_data))
    {
      return false;
    }

    const auto transport_file_type =
        nmos::fields::transportfile_type(transport_file);
    if (transport_file_type != nmos::media_types::application_sdp.name)
    {
      return false;
    }

    const auto transport_file_data =
        nmos::fields::transportfile_data(transport_file);
    if (!transport_file_data.is_string())
    {
      return false;
    }

    try
    {
      const auto session_description =
          sdp::parse_session_description(to_utf8_string(transport_file_data.as_string()));
      const auto [sdp_params, transport_params] =
          nmos::parse_session_description(session_description);
      static_cast<void>(transport_params);

      if (to_utf8_string(nmos::get_media_type(sdp_params).name).rfind("audio/L", 0) == 0)
      {
        const auto audio = nmos::get_audio_L_parameters(sdp_params);
        receiver.channel_count = static_cast<int>(audio.channel_count);
        receiver.bit_depth = static_cast<int>(audio.bit_depth);
        receiver.simple_rate = static_cast<int>(audio.sample_rate);
        receiver.packet_time = audio.packet_time;
        return true;
      }
    }
    catch (const std::exception &error)
    {
      slog::log<slog::severities::warning>(gate, SLOG_FLF)
          << nmos::stash_category(impl::categories::node_implementation)
          << "parse transport_file.data SDP failed: " << error.what();
    }

    return false;
  }

  std::vector<utility::string_t>
  to_utility_string_vector(const std::vector<std::string> &values)
  {
    std::vector<utility::string_t> result;
    result.reserve(values.size());
    for (const auto &value : values)
    {
      result.push_back(utility::conversions::to_string_t(value));
    }
    return result;
  }

  bool is_valid_ptp_gmid(const std::string &gmid)
  {
    if (23 != gmid.size())
    {
      return false;
    }
    for (std::size_t index = 0; index < gmid.size(); ++index)
    {
      if (2 == index % 3)
      {
        if ('-' != gmid[index])
        {
          return false;
        }
        continue;
      }
      const char value = gmid[index];
      if (!std::isdigit(static_cast<unsigned char>(value)) &&
          !(value >= 'a' && value <= 'f'))
      {
        return false;
      }
    }
    return true;
  }

  std::string normalize_ptp_gmid(std::string gmid)
  {
    gmid.erase(std::remove_if(gmid.begin(), gmid.end(),
                              [](unsigned char value)
                              { return ':' == value || '-' == value || std::isspace(value); }),
               gmid.end());

    std::transform(gmid.begin(), gmid.end(), gmid.begin(),
                   [](unsigned char value)
                   {
                     return static_cast<char>(std::tolower(value));
                   });

    if (16 != gmid.size())
    {
      return {};
    }

    if (!std::all_of(gmid.begin(), gmid.end(), [](unsigned char value)
                     { return std::isxdigit(value); }))
    {
      return {};
    }

    std::string normalized;
    normalized.reserve(23);
    for (std::size_t index = 0; index < gmid.size(); index += 2)
    {
      if (!normalized.empty())
      {
        normalized.push_back('-');
      }
      normalized.append(gmid.substr(index, 2));
    }

    return normalized;
  }

  std::string first_interface_address(
      const web::hosts::experimental::host_interface &interface)
  {
    return interface.addresses.empty()
               ? std::string{}
               : utility::conversions::to_utf8string(interface.addresses.front());
  }

  std::string receiver_interface_ip_or_default(
      const std::string &configured_ip,
      const web::hosts::experimental::host_interface &interface)
  {
    return configured_ip.empty() ? first_interface_address(interface)
                                 : configured_ip;
  }

  std::string receiver_multicast_ip_or_default(const std::string &configured_ip,
                                               const std::string &fallback_ip)
  {
    return configured_ip.empty() ? fallback_ip : configured_ip;
  }

  std::string uri_path_to_version(const web::uri &uri)
  {
    const auto path = utility::conversions::to_utf8string(uri.path());
    if (path.empty())
    {
      return {};
    }

    const auto slash = path.find_last_of('/');
    if (std::string::npos == slash || slash + 1 >= path.size())
    {
      return {};
    }
    return path.substr(slash + 1);
  }

  web::json::value parse_json_file(const std::string &file_path)
  {
    std::ifstream file(file_path);
    file.exceptions(std::ios_base::failbit);
    auto parsed = web::json::value::parse(file);
    parsed.as_object();
    return parsed;
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

  seeder::nmos_node::RegistrationStatus
  registration_status_from_uri(const web::uri &uri)
  {
    seeder::nmos_node::RegistrationStatus status;
    if (uri.is_empty())
    {
      return status;
    }

    status.connected = true;
    status.uri = utility::conversions::to_utf8string(uri.to_string());
    status.scheme = utility::conversions::to_utf8string(uri.scheme());
    status.host = utility::conversions::to_utf8string(uri.host());
    status.port = uri.port();
    status.version = uri_path_to_version(uri);
    return status;
  }

  int receiver_port_or_default(const int configured_port, const int fallback_port)
  {
    return 0 < configured_port ? configured_port : fallback_port;
  }

  void set_receiver_transport_params_leg(web::json::value &endpoint,
                                         const std::size_t leg_index,
                                         const std::string &multicast_ip,
                                         const std::string &interface_ip,
                                         const int destination_port)
  {
    endpoint[nmos::fields::transport_params][leg_index]
            [nmos::fields::multicast_ip] = value(multicast_ip);
    endpoint[nmos::fields::transport_params][leg_index]
            [nmos::fields::interface_ip] = value(interface_ip);
    endpoint[nmos::fields::transport_params][leg_index]
            [nmos::fields::destination_port] = destination_port;
  }

  void initialize_receiver_transport_params(
      web::json::value &staged, web::json::value &active,
      const std::string &multicast_ip, const std::string &interface_ip,
      const int destination_port)
  {
    set_receiver_transport_params_leg(staged, 0, multicast_ip, interface_ip,
                                      destination_port);
    set_receiver_transport_params_leg(active, 0, multicast_ip, interface_ip,
                                      destination_port);
  }

  void initialize_receiver_transport_params(
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
}

sdp::sampling st_get_color_sampling(st20_fmt fmt)
{
  switch (fmt)
  {
  case ST20_FMT_YUV_422_10BIT: /**< 10-bit YUV 4:2:2 */
    return sdp::samplings::YCbCr_4_2_2;
  case ST20_FMT_YUV_422_8BIT: /**< 8-bit YUV 4:2:2 */
    return sdp::samplings::YCbCr_4_2_2;
  case ST20_FMT_YUV_422_12BIT: /**< 12-bit YUV 4:2:2 */
    return sdp::samplings::YCbCr_4_2_2;
  case ST20_FMT_YUV_422_16BIT: /**< 16-bit YUV 4:2:2 */
    return sdp::samplings::YCbCr_4_2_2;
  case ST20_FMT_YUV_420_8BIT: /**< 8-bit YUV 4:2:0 */
    return sdp::samplings::YCbCr_4_2_0;
  case ST20_FMT_YUV_420_10BIT: /**< 10-bit YUV 4:2:0 */
    return sdp::samplings::YCbCr_4_2_0;
  case ST20_FMT_YUV_420_12BIT: /**< 12-bit YUV 4:2:0 */
    return sdp::samplings::YCbCr_4_2_0;
  case ST20_FMT_RGB_8BIT: /**< 8-bit RGB */
    return sdp::samplings::RGB;
  case ST20_FMT_RGB_10BIT: /**< 10-bit RGB */
    return sdp::samplings::RGB;
  case ST20_FMT_RGB_12BIT: /**< 12-bit RGB */
    return sdp::samplings::RGB;
  case ST20_FMT_RGB_16BIT: /**< 16-bit RGB */
    return sdp::samplings::RGB;
  case ST20_FMT_YUV_444_8BIT: /**< 8-bit YUV 4:4:4 */
    return sdp::samplings::YCbCr_4_4_4;
  case ST20_FMT_YUV_444_10BIT: /**< 10-bit YUV 4:4:4 */
    return sdp::samplings::YCbCr_4_4_4;
  case ST20_FMT_YUV_444_12BIT: /**< 12-bit YUV 4:4:4 */
    return sdp::samplings::YCbCr_4_4_4;
  case ST20_FMT_YUV_444_16BIT: /**< 16-bit YUV 4:4:4 */
    return sdp::samplings::YCbCr_4_4_4;
  case ST20_FMT_MAX:
    return sdp::samplings::YCbCr_4_4_4;
  }
  return sdp::samplings::YCbCr_4_2_2;
}

int st_get_component_depth(st20_fmt fmt)
{
  switch (fmt)
  {
  case ST20_FMT_YUV_422_10BIT: /**< 10-bit YUV 4:2:2 */
    return 10;
  case ST20_FMT_YUV_422_8BIT: /**< 8-bit YUV 4:2:2 */
    return 8;
  case ST20_FMT_YUV_422_12BIT: /**< 12-bit YUV 4:2:2 */
    return 12;
  case ST20_FMT_YUV_422_16BIT: /**< 16-bit YUV 4:2:2 */
    return 16;
  case ST20_FMT_YUV_420_8BIT: /**< 8-bit YUV 4:2:0 */
    return 8;
  case ST20_FMT_YUV_420_10BIT: /**< 10-bit YUV 4:2:0 */
    return 10;
  case ST20_FMT_YUV_420_12BIT: /**< 12-bit YUV 4:2:0 */
    return 12;
  case ST20_FMT_RGB_8BIT: /**< 8-bit RGB */
    return 8;
  case ST20_FMT_RGB_10BIT: /**< 10-bit RGB */
    return 10;
  case ST20_FMT_RGB_12BIT: /**< 12-bit RGB */
    return 12;
  case ST20_FMT_RGB_16BIT: /**< 16-bit RGB */
    return 16;
  case ST20_FMT_YUV_444_8BIT: /**< 8-bit YUV 4:4:4 */
    return 8;
  case ST20_FMT_YUV_444_10BIT: /**< 10-bit YUV 4:4:4 */
    return 10;
  case ST20_FMT_YUV_444_12BIT: /**< 12-bit YUV 4:4:4 */
    return 12;
  case ST20_FMT_YUV_444_16BIT: /**< 16-bit YUV 4:4:4 */
    return 16;
  case ST20_FMT_MAX:
    return 8;
  }
  return 8;
}

nmos::interlace_mode get_interlace_mode(int fps_numerator, int fps_denominator,
                                        int height)
{
  const auto frame_rate = nmos::parse_rational(
      web::json::value_of({{nmos::fields::numerator, fps_numerator},
                           {nmos::fields::denominator, fps_denominator}}));
  const auto frame_height = height;
  return (nmos::rates::rate25 == frame_rate ||
          nmos::rates::rate29_97 == frame_rate) &&
                 1080 == frame_height
             ? nmos::interlace_modes::interlaced_tff
             : nmos::interlace_modes::progressive;
}

namespace seeder
{
  namespace nmos_node
  {
    class Node::Impl
    {
    public:
      enum class LifecycleState
      {
        stopped,
        starting,
        running,
        stopping
      };

      std::thread thread_;
      std::string config_file_;
      nmos::experimental::log_gate *gate_ = nullptr;
      nmos::id node_id_;
      nmos::id device_id_;
      utility::string_t seed_id_;
      std::vector<nmos::id> sender_ids_;
      std::vector<nmos::id> receiver_ids_;
      std::vector<nmos::id> node_ids_;
      std::vector<nmos::id> device_ids_;
      std::vector<nmos::id> source_ids_;
      std::vector<nmos::id> flow_ids_;
      web::hosts::experimental::host_interface primary_interface;
      web::hosts::experimental::host_interface secondary_interface;

      std::vector<VideoSender> video_senders;
      std::vector<AudioSender> audio_senders;
      std::vector<AncillarySender> ancillary_senders;
      std::vector<VideoReceiver> video_receivers;
      std::vector<AudioReceiver> audio_receivers;
      std::vector<AncillaryReceiver> ancillary_receivers;

      nmos::connection_sender_transportfile_setter set_transportfile;
      nmos::connection_resource_auto_resolver resolve_auto;
      nmos::node_model node_model_;

      std::mutex receiver_mutex_;
      std::mutex callback_mutex_;
      std::mutex lifecycle_mutex_;
      std::mutex thread_mutex_;
      std::condition_variable lifecycle_cv_;
      bool stop_requested_{false};
      std::atomic<LifecycleState> lifecycle_state_{LifecycleState::stopped};
      bool needs_model_reset_{false};

      std::function<void(const VideoReceiver &video)> update_video_receiver_func;
      std::function<void(const AudioReceiver &audio)> update_audio_receiver_func;
      std::function<void(const AncillaryReceiver &ancillary)>
          update_ancillary_receiver_func;
      std::function<void(const RegistrationStatus &status)>
          registration_changed_func;

      Impl() {}
      ~Impl() { stop(); }

      bool stop()
      {
        bool stop_completed = false;
        bool should_join_thread = false;
        std::thread::id worker_thread_id;

        {
          std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
          if (LifecycleState::stopped != lifecycle_state_)
          {
            lifecycle_state_ = LifecycleState::stopping;
            stop_requested_ = true;
            lifecycle_cv_.notify_all();
          }
          else
          {
            stop_completed = true;
          }
        }

        {
          auto lock = node_model_.write_lock();
          node_model_.shutdown = true;
        }
        node_model_.notify();
        node_model_.shutdown_condition.notify_all();

        {
          std::lock_guard<std::mutex> thread_lock(thread_mutex_);
          if (thread_.joinable())
          {
            worker_thread_id = thread_.get_id();
            should_join_thread = std::this_thread::get_id() != worker_thread_id;
            if (should_join_thread)
            {
              thread_.join();
              stop_completed = true;
            }
          }
          else
          {
            stop_completed = true;
          }
        }

        if (should_join_thread || std::thread::id{} == worker_thread_id)
        {
          std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
          lifecycle_state_ = LifecycleState::stopped;
          stop_requested_ = false;
          needs_model_reset_ = true;
          stop_completed = true;
        }

        return stop_completed;
      }

      void wait_for_stop_signal()
      {
        std::unique_lock<std::mutex> lifecycle_lock(lifecycle_mutex_);
        lifecycle_cv_.wait(lifecycle_lock, [&]
                           { return stop_requested_; });
      }

      void node_implementation_run()
      {
        auto lock = node_model_.read_lock();
        // wait for the thread to be interrupted because the server is being shut
        // down
        node_model_.shutdown_condition.wait(lock,
                                            [&]
                                            { return node_model_.shutdown; });
        nmos::details::reverse_lock_guard<nmos::read_lock> unlock{lock};
      }

      // This constructs all the callbacks used to integrate the example
      // device-specific underlying implementation into the server instance for the
      // NMOS Node.
      nmos::experimental::node_implementation make_node_implementation()
      {

        return nmos::experimental::node_implementation()
            .on_parse_transport_file(
                make_node_implementation_transport_file_parser())
            .on_resolve_auto(
                make_node_implementation_auto_resolver(node_model_.settings))
            .on_set_transportfile(make_node_implementation_transportfile_setter(
                node_model_.node_resources, node_model_.settings))
            .on_registration_changed(
                make_node_implementation_registration_handler())
            .on_connection_activated(
                make_node_implementation_connection_activation_handler());
      }

      nmos::registration_handler make_node_implementation_registration_handler()
      {
        return [&](const web::uri &registration_uri)
        {
          std::function<void(const RegistrationStatus &status)> callback;
          {
            std::lock_guard<std::mutex> callback_lock(callback_mutex_);
            callback = registration_changed_func;
          }

          if (!callback)
          {
            return;
          }

          callback(registration_status_from_uri(registration_uri));
        };
      }

      nmos::id make_video_receiver_resource_id(const std::string &id) const
      {
        return impl::make_id(seed_id_, nmos::types::receiver, impl::ports::video, id);
      }

      nmos::id make_audio_receiver_resource_id(const std::string &id) const
      {
        return impl::make_id(seed_id_, nmos::types::receiver, impl::ports::audio, id);
      }

      nmos::id make_ancillary_receiver_resource_id(const std::string &id) const
      {
        return impl::make_id(seed_id_, nmos::types::receiver, impl::ports::data, id);
      }

      // Example Connection API activation callback to perform application-specific
      // operations to complete activation
      nmos::connection_activation_handler
      make_node_implementation_connection_activation_handler()
      {
        return [&](const nmos::resource &resource,
                   const nmos::resource &connection_resource)
        {
          const std::pair<nmos::id, nmos::type> id_type{resource.id, resource.type};

          const std::pair<nmos::id, nmos::type> id__type{connection_resource.id,
                                                         connection_resource.type};
          const auto &endpoint_active =
              nmos::fields::endpoint_active(connection_resource.data);
          const auto &transport_params_value =
              nmos::fields::transport_params(endpoint_active);
          if (!transport_params_value.is_array())
          {
            slog::log<slog::severities::error>(*gate_, SLOG_FLF)
                << nmos::stash_category(impl::categories::node_implementation)
                << "connection activation ignored: transport_params is not array for "
                << id_type;
            return;
          }
          const web::json::array transport_params = transport_params_value.as_array();
          if (0 == transport_params.size())
          {
            slog::log<slog::severities::error>(*gate_, SLOG_FLF)
                << nmos::stash_category(impl::categories::node_implementation)
                << "connection activation ignored: transport_params is empty for "
                << id_type;
            return;
          }
          bool master_enable = nmos::fields::master_enable(
              endpoint_active);
          std::string connection_resource_json =
              connection_resource.data.serialize();
          std::error_code ec{};
          std::string json;
          if (resource.type == nmos::types::receiver)
          {
            const auto &transport_file = nmos::fields::transport_file(endpoint_active);
            int dest_port =
                nmos::fields::destination_port(transport_params.at(0)).as_integer();
            std::string interface_ip =
                nmos::fields::interface_ip(transport_params.at(0)).as_string();
            std::string multicast_ip =
                nmos::fields::multicast_ip(transport_params.at(0)).as_string();
            std::string source_ip =
                nmos::fields::source_ip(transport_params.at(0)).as_string();

            int dest_port_07 = 5004;
            std::string interface_ip_07 = "";
            std::string multicast_ip_07 = "";
            std::string source_ip_07 = "";
            if (transport_params.size() > 1)
            {
              dest_port_07 = nmos::fields::destination_port(transport_params.at(1))
                                 .as_integer();
              interface_ip_07 =
                  nmos::fields::interface_ip(transport_params.at(1)).as_string();

              multicast_ip_07 =
                  nmos::fields::multicast_ip(transport_params.at(1)).as_string();
              source_ip_07 =
                  nmos::fields::source_ip(transport_params.at(1)).as_string();
            }
            std::optional<VideoReceiver> video_snapshot;
            std::optional<AudioReceiver> audio_snapshot;
            std::optional<AncillaryReceiver> ancillary_snapshot;
            std::function<void(const VideoReceiver &)> video_callback;
            std::function<void(const AudioReceiver &)> audio_callback;
            std::function<void(const AncillaryReceiver &)> ancillary_callback;
            {
              std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
              VideoReceiver *video = find_video_receiver_by_resource_id(resource.id);
              AudioReceiver *audio = find_audio_receiver_by_resource_id(resource.id);
              AncillaryReceiver *ancillary =
                  find_ancillary_receiver_by_resource_id(resource.id);
              if (video)
              {
                video->enable = master_enable;
                video->ip = multicast_ip;
                video->port = dest_port;
                video->redudancy.ip = multicast_ip_07;
                video->redudancy.port = dest_port_07;
                update_video_receiver_from_transport_file(*video, transport_file, *gate_);
                video_snapshot = *video;
              }
              if (audio)
              {
                audio->enable = master_enable;
                audio->ip = multicast_ip;
                audio->port = dest_port;
                audio->redudancy.ip = multicast_ip_07;
                audio->redudancy.port = dest_port_07;
                update_audio_receiver_from_transport_file(*audio, transport_file, *gate_);
                audio_snapshot = *audio;
              }
              if (ancillary)
              {
                ancillary->enable = master_enable;
                ancillary->ip = multicast_ip;
                ancillary->port = dest_port;
                ancillary->redudancy.ip = multicast_ip_07;
                ancillary->redudancy.port = dest_port_07;
                ancillary_snapshot = *ancillary;
              }
            }

            {
              std::lock_guard<std::mutex> callback_lock(callback_mutex_);
              video_callback = update_video_receiver_func;
              audio_callback = update_audio_receiver_func;
              ancillary_callback = update_ancillary_receiver_func;
            }

            if (video_snapshot)
            {
              if (video_callback)
              {
                video_callback(*video_snapshot);
              }
              else
              {
                slog::log<slog::severities::error>(*gate_, SLOG_FLF)
                    << nmos::stash_category(impl::categories::node_implementation)
                    << "update video receiver callback failed not found "
                       "update_video_receiver_func function";
              }
            }
            if (audio_snapshot)
            {
              if (audio_callback)
              {
                audio_callback(*audio_snapshot);
                slog::log<slog::severities::info>(*gate_, SLOG_FLF)
                    << nmos::stash_category(impl::categories::node_implementation)
                    << "update audio receiver callback success " << audio_snapshot->ip
                    << ":" << audio_snapshot->port;
              }
              else
              {
                slog::log<slog::severities::info>(*gate_, SLOG_FLF)
                    << nmos::stash_category(impl::categories::node_implementation)
                    << "update video receiver callback failed not found "
                       "update_audio_receiver_func function";
              }
            }
            if (ancillary_snapshot)
            {
              if (ancillary_callback)
              {
                ancillary_callback(*ancillary_snapshot);
                slog::log<slog::severities::info>(*gate_, SLOG_FLF)
                    << nmos::stash_category(impl::categories::node_implementation)
                    << "update ancillary receiver callback success "
                    << ancillary_snapshot->ip << ":" << ancillary_snapshot->port;
              }
              else
              {
                slog::log<slog::severities::info>(*gate_, SLOG_FLF)
                    << nmos::stash_category(impl::categories::node_implementation)
                    << "update ancillary receiver callback failed not found "
                       "update_ancillary_receiver_func function";
              }
            }
          }

          if (ec)
          {
            slog::log<slog::severities::error>(*gate_, SLOG_FLF)
                << nmos::stash_category(impl::categories::node_implementation)
                << "Activating  " << id_type << "   Error: " << ec.message();
          }

          slog::log<slog::severities::info>(*gate_, SLOG_FLF)
              << nmos::stash_category(impl::categories::node_implementation)
              << "Activating " << id_type;
        };
      }

      nmos::transport_file_parser make_node_implementation_transport_file_parser()
      {
        return [](const nmos::resource &receiver,
                  const nmos::resource &connection_receiver,
                  const utility::string_t &transport_file_type,
                  const utility::string_t &transport_file_data,
                  slog::base_gate &gate)
        {
          const auto validate_sdp_parameters =
              [](const web::json::value &receiver,
                 const nmos::sdp_parameters &sdp_params)
          {
            if (nmos::media_types::video_jxsv ==
                nmos::get_media_type(sdp_params))
            {
              nmos::validate_video_jxsv_sdp_parameters(receiver, sdp_params);
            }
            else
            {
              // validate core media types, i.e., "video/raw", "audio/L",
              // "video/smpte291" and "video/SMPTE2022-6"
              try {
                nmos::validate_sdp_parameters(receiver, sdp_params);
              } catch (std::runtime_error &e) {
                throw std::runtime_error("The destination does not support this sdp paramters.");
              }
            }
          };
          return nmos::details::parse_rtp_transport_file(
              validate_sdp_parameters, receiver, connection_receiver,
              transport_file_type, transport_file_data, gate);
        };
      }

      // Example Connection API activation callback to resolve "auto" values when
      // /staged is transitioned to /active
      nmos::connection_resource_auto_resolver
      make_node_implementation_auto_resolver(const nmos::settings &settings)
      {
        using web::json::value;
        // although which properties may need to be defaulted depends on the
        // resource type, the default value will almost always be different for each
        // resource
        return [&](const nmos::resource &resource,
                   const nmos::resource &connection_resource,
                   value &transport_params)
        {
          if (!transport_params.is_array())
          {
            slog::log<slog::severities::error>(*gate_, SLOG_FLF)
                << nmos::stash_category(impl::categories::node_implementation)
                << "resolve_auto ignored: transport_params is not array for "
                << connection_resource.id;
            return;
          }
          auto &transport_params_array = transport_params.as_array();
          if (0 == transport_params_array.size())
          {
            slog::log<slog::severities::error>(*gate_, SLOG_FLF)
                << nmos::stash_category(impl::categories::node_implementation)
                << "resolve_auto ignored: transport_params is empty for "
                << connection_resource.id;
            return;
          }

          const std::pair<nmos::id, nmos::type> id_type{connection_resource.id,
                                                        connection_resource.type};

          // "In some cases the behaviour is more complex, and may be determined
          // by the vendor." See
          // https://specs.amwa.tv/is-05/releases/v1.0.0/docs/2.2._APIs_-_Server_Side_Implementation.html#use-of-auto
          bool smpte2022_7 = false;
          std::string source_ip = "";
          std::string ip = "";
          int port = 0;
          std::string redudancy_ip;
          if (id_type.second == nmos::types::sender)
          {
            VideoSender *video =
                find_video_sender_by_sender_id(connection_resource.id);
            if (video)
            {
              smpte2022_7 = video->redudancy.enable;
              source_ip = video->source_ip;
              ip = video->ip;
              redudancy_ip = video->redudancy.ip;
              port = video->port;
            }
            AudioSender *audio =
                find_audio_sender_by_sender_id(connection_resource.id);
            if (audio)
            {
              smpte2022_7 = audio->redudancy.enable;
              source_ip = audio->source_ip;
              ip = audio->ip;
              redudancy_ip = audio->redudancy.ip;
              port = audio->port;
            }
            AncillarySender *ancillary =
                find_ancillary_sender_by_sender_id(connection_resource.id);
            if (ancillary)
            {
              smpte2022_7 = ancillary->redudancy.enable;
              source_ip = ancillary->source_ip;
              ip = ancillary->ip;
              redudancy_ip = ancillary->redudancy.ip;
              port = ancillary->port;
            }
            nmos::details::resolve_auto(transport_params_array[0],
                                        nmos::fields::source_ip,
                                        [&]
                                        { return value::string(source_ip); });
            if (smpte2022_7 && transport_params_array.size() > 1)
            {
              nmos::details::resolve_auto(transport_params_array[1],
                                          nmos::fields::source_ip,
                                          [&]
                                          { return value::string(source_ip); });
            }
            nmos::details::resolve_auto(transport_params_array[0],
                                        nmos::fields::destination_ip,
                                        [&]
                                        { return value::string(ip); });
            if (smpte2022_7 && transport_params_array.size() > 1)
            {
              nmos::details::resolve_auto(
                  transport_params_array[1], nmos::fields::destination_ip,
                  [&]
                  { return value::string(redudancy_ip); });
            }
            // lastly, apply the specification defaults for any properties not
            // handled above
            nmos::resolve_rtp_auto(id_type.second, transport_params, port);
          }
          else if (id_type.second == nmos::types::receiver)
          {
            bool smpte2022_7 = false;
            std::string interface_ip = "";
            std::string secondary_multicast_ip = "";
            std::string secondary_interface_ip = "";
            int secondary_port = 0;
            {
              std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
              VideoReceiver *video =
                  find_video_receiver_by_resource_id(connection_resource.id);
              if (video)
              {
                source_ip = "";
                port = video->port;
                smpte2022_7 = video->redudancy.enable;
                ip = video->ip;
                interface_ip = receiver_interface_ip_or_default(video->source_ip,
                                                                primary_interface);
                secondary_multicast_ip = receiver_multicast_ip_or_default(
                    video->redudancy.ip, video->ip);
                secondary_interface_ip = first_interface_address(secondary_interface);
                secondary_port = receiver_port_or_default(video->redudancy.port,
                                                          video->port);
              }
              AudioReceiver *audio =
                  find_audio_receiver_by_resource_id(connection_resource.id);
              if (audio)
              {
                smpte2022_7 = audio->redudancy.enable;
                source_ip = "";
                ip = audio->ip;
                port = audio->port;
                interface_ip = receiver_interface_ip_or_default(audio->source_ip,
                                                                primary_interface);
                secondary_multicast_ip = receiver_multicast_ip_or_default(
                    audio->redudancy.ip, audio->ip);
                secondary_interface_ip = first_interface_address(secondary_interface);
                secondary_port = receiver_port_or_default(audio->redudancy.port,
                                                          audio->port);
              }
              AncillaryReceiver *ancillary =
                  find_ancillary_receiver_by_resource_id(connection_resource.id);
              if (ancillary)
              {
                smpte2022_7 = ancillary->redudancy.enable;
                source_ip = "";
                ip = ancillary->ip;
                port = ancillary->port;
                interface_ip = receiver_interface_ip_or_default(
                    ancillary->source_ip, primary_interface);
                secondary_multicast_ip = receiver_multicast_ip_or_default(
                    ancillary->redudancy.ip, ancillary->ip);
                secondary_interface_ip = first_interface_address(secondary_interface);
                secondary_port = receiver_port_or_default(
                    ancillary->redudancy.port, ancillary->port);
              }
            }
            std::string json = transport_params.serialize();
            nmos::details::resolve_auto(transport_params_array[0],
                                        nmos::fields::multicast_ip,
                                        [&]
                                        { return value::string(ip); });
            nmos::details::resolve_auto(transport_params_array[0],
                                        nmos::fields::source_ip,
                                        [&]
                                        { return value::string(source_ip); });
            nmos::details::resolve_auto(transport_params_array[0],
                                        nmos::fields::destination_port,
                                        [&]
                                        { return port; });
            nmos::details::resolve_auto(
                transport_params_array[0], nmos::fields::interface_ip,
                [&]
                { return value::string(interface_ip); });
            if (smpte2022_7 && transport_params_array.size() > 1)
            {
              nmos::details::resolve_auto(
                  transport_params_array[1], nmos::fields::multicast_ip,
                  [&]
                  { return value::string(secondary_multicast_ip); });
              nmos::details::resolve_auto(
                  transport_params_array[1], nmos::fields::destination_port,
                  [&]
                  { return secondary_port; });
              nmos::details::resolve_auto(
                  transport_params_array[1], nmos::fields::interface_ip,
                  [&]
                  { return value::string(secondary_interface_ip); });
            }

            json = transport_params.serialize();

            // lastly, apply the specification defaults for any properties not
            // handled above
            nmos::resolve_rtp_auto(id_type.second, transport_params);
          }
        };
      }

      nmos::connection_sender_transportfile_setter
      make_node_implementation_transportfile_setter(
          const nmos::resources &node_resources, const nmos::settings &settings)
      {
        using web::json::value;
        // as part of activation, the example sender /transportfile should be
        // updated based on the active transport parameters
        return [&](const nmos::resource &sender,
                   const nmos::resource &connection_sender,
                   value &endpoint_transportfile)
        {
          const auto found = boost::range::find(sender_ids_, connection_sender.id);
          if (sender_ids_.end() != found)
          {
            //  note, node_model_ mutex is already locked by the calling thread, so
            //  access to node_resources is OK...
            auto node =
                nmos::find_resource(node_resources, {node_id_, nmos::types::node});

            if (node_resources.end() == node)
            {
              throw std::logic_error(
                  "matching IS-04 node, source or flow not found");
            }
            if (!sender.data.has_field(nmos::fields::flow_id))
            {
              throw std::logic_error("matching IS-04 sender flow_id not found");
            }
            std::string flow_id = nmos::fields::flow_id(sender.data).as_string();
            auto flow =
                nmos::find_resource(node_resources, {flow_id, nmos::types::flow});
            if (node_resources.end() == flow)
            {
              throw std::logic_error("matching IS-04 flow not found");
            }
            if (!flow->data.has_field(nmos::fields::source_id))
            {
              throw std::logic_error("matching IS-04 flow source_id not found");
            }
            std::string source_id = nmos::fields::source_id(flow->data);
            auto source = nmos::find_resource(node_resources,
                                              {source_id, nmos::types::source});
            if (node_resources.end() == source)
            {
              throw std::logic_error("matching IS-04 source not found");
            }
            // the nmos::make_sdp_parameters overload from the IS-04 resources
            // provides a high-level interface for common "video/raw", "audio/L",
            // "video/smpte291" and "video/SMPTE2022-6" use cases
            // auto sdp_params = nmos::make_sdp_parameters(node->data, source->data,
            // flow->data, sender.data, { U("PRIMARY"), U("SECONDARY") });

            // nmos::make_{video,audio,data,mux}_sdp_parameters provide a little
            // more flexibility for those four media types and the combination of
            // nmos::make_{video_raw,audio_L,video_smpte291,video_SMPTE2022_6}_parameters
            // with the related make_sdp_parameters overloads provides the most
            // flexible and extensible approach
            std::string con = sender.data.serialize();
            std::string session_name = nmos::fields::description(sender.data);
            auto sdp_params = [&]
            {
              const std::vector<utility::string_t> mids{U("PRIMARY"),
                                                        U("SECONDARY")};
              const nmos::format format{nmos::fields::format(flow->data)};
              if (nmos::formats::video == format)
              {
                const nmos::media_type video_type{
                    nmos::fields::media_type(flow->data)};
                if (nmos::media_types::video_raw == video_type)
                {
                  auto raw_params = nmos::make_video_raw_parameters(node->data, source->data, flow->data, sender.data, sdp::type_parameters::type_N);
                  const auto ts_refclk = nmos::details::make_ts_refclk(node->data, source->data, sender.data, 127);
                  return nmos::make_video_raw_sdp_parameters(
                      session_name, raw_params, nmos::details::payload_type_video_default, {}, ts_refclk);
                }
              }
              else if (nmos::formats::audio == format)
              {
                double packet_time = 1;

                auto audio_L_params = nmos::make_audio_L_parameters(node->data, source->data, flow->data, sender.data, packet_time);
                return nmos::make_audio_L_sdp_parameters(session_name, audio_L_params, nmos::details::payload_type_audio_default, {}, {});
              }
              else if (nmos::formats::data == format)
              {
                auto samp291_params = nmos::make_video_smpte291_parameters(node->data, source->data, flow->data, sender.data, nmos::vpid_codes::vpid_1_5Gbps_1080_line, sdp::transmission_models::compatible);
                return nmos::make_video_smpte291_sdp_parameters(
                    session_name, samp291_params,
                    nmos::details::payload_type_data_default, mids, {});
              }
              else if (nmos::formats::mux == format)
              {
                auto SMPTE2022_6_params = nmos::make_video_SMPTE2022_6_parameters(node->data, source->data, flow->data, sender.data, sdp::type_parameters::type_N);
                return nmos::make_video_SMPTE2022_6_sdp_parameters(
                    session_name, SMPTE2022_6_params,
                    nmos::details::payload_type_mux_default, mids, {});
              }
              else
              {
                throw std::logic_error("unexpected flow format");
              }
            }();

            auto &transport_params = nmos::fields::transport_params(
                nmos::fields::endpoint_active(connection_sender.data));

            // std::string transport_params_json = transport_params.serialize();
            // std::cout << "transport_params_json: "
            //           << utility::us2s(transport_params_json) << std::endl;
            auto session_description =
                nmos::make_session_description(sdp_params, transport_params);
            auto sdp =
                utility::s2us(sdp::make_session_description(session_description));
            endpoint_transportfile =
                nmos::make_connection_rtp_sender_transportfile(sdp);
          }
        };
      }

      bool insert_resource_after(unsigned int milliseconds,
                                 nmos::resources &resources,
                                 nmos::resource &&resource, slog::base_gate &gate,
                                 nmos::write_lock &lock)
      {

        if (nmos::details::wait_for(node_model_.shutdown_condition, lock,
                                    bst::chrono::milliseconds(milliseconds),
                                    [&]
                                    { return node_model_.shutdown; }))
          return false;

        const std::pair<nmos::id, nmos::type> id_type{resource.id, resource.type};
        const bool success = insert_resource(resources, std::move(resource)).second;

        if (success)
          slog::log<slog::severities::info>(gate, SLOG_FLF)
              << "Updated node_model_ with " << id_type;
        else
          slog::log<slog::severities::severe>(gate, SLOG_FLF)
              << "Model update error: " << id_type;

        slog::log<slog::severities::too_much_info>(gate, SLOG_FLF)
            << "Notifying node behaviour thread"; // and anyone else who cares...
        node_model_.notify();
        return success;
      }

      bool remove_resource_after(unsigned int milliseconds,
                                 nmos::resources &resources, nmos::id id,
                                 slog::base_gate &gate, nmos::write_lock &lock)
      {

        if (nmos::details::wait_for(node_model_.shutdown_condition, lock,
                                    bst::chrono::milliseconds(milliseconds),
                                    [&]
                                    { return node_model_.shutdown; }))
          return false;

        const bool success = erase_resource(resources, id);
        if (success)
          slog::log<slog::severities::info>(gate, SLOG_FLF)
              << "Remove node_model_ with " << id;
        else
          slog::log<slog::severities::severe>(gate, SLOG_FLF)
              << "Model Remove error: " << id;

        slog::log<slog::severities::too_much_info>(gate, SLOG_FLF)
            << "Notifying node behaviour thread"; // and anyone else who cares...
        node_model_.notify();
        return success;
      }

      void erase_resource_if_present(nmos::resources &resources, const nmos::id &id)
      {
        nmos::erase_resource(resources, id);
      }
      int nmos_node_start()
      {

        // Construct our data models including mutexes to protect them
        int i = 0;

        nmos::experimental::log_model log_model;
        {
          auto lock = node_model_.write_lock();
          node_model_.shutdown = false;
        }

        // Streams for logging, initially configured to write errors to stderr and
        // to discard the access log
        std::filebuf error_log_buf;
        std::ostream error_log(std::cerr.rdbuf());
        std::filebuf access_log_buf;
        std::ostream access_log(&access_log_buf);

        // Logging should all go through this logging gateway
        nmos::experimental::log_gate gate(error_log, access_log, log_model);
        gate_ = &gate;
        try
        {
          slog::log<slog::severities::info>(gate, SLOG_FLF)
              << "Starting nmos-cpp node";

          // Settings can be passed on the command-line, directly or in a
          // configuration file, and a few may be changed dynamically by PATCH to
          // /settings/all on the Settings API
          //
          // * "logging_level": integer value, between 40 (least verbose, only fatal
          // messages) and -40 (most verbose)
          // * "registry_address": used to construct request URLs for registry APIs
          // (if not discovered via DNS-SD)
          //
          // E.g.
          //
          // # ./nmos-cpp-node "{\"logging_level\":-40}"
          // # ./nmos-cpp-node config.json
          // # curl -X PATCH -H "Content-Type: application/json"
          // http://localhost:3209/settings/all -d
          // "{\"logging_level\":-40}" # curl -X PATCH -H "Content-Type:
          // application/json" http://localhost:3209/settings/all -T config.json

          slog::log<slog::severities::info>(gate, SLOG_FLF)
              << "node_config_file_path: " << config_file_;
          if (config_file_.empty())
          {
            return -1;
            ;
          }
          std::error_code error;
          node_model_.settings =
              web::json::value::parse(utility::s2us(config_file_), error);
          if (error)
          {
            std::ifstream file(config_file_);
            // check the file can be opened, and is parsed to an object
            file.exceptions(std::ios_base::failbit);
            node_model_.settings = web::json::value::parse(file);
            node_model_.settings.as_object();
          }

          // Prepare run-time default settings (different than header defaults)

          nmos::insert_node_default_settings(node_model_.settings);

          // copy to the logging settings
          // hmm, this is a bit icky, but simplest for now
          log_model.settings = node_model_.settings;

          // the logging level is a special case because we want to turn it into an
          // atomic value that can be read by logging statements without locking the
          // mutex protecting the settings
          log_model.level = nmos::fields::logging_level(log_model.settings);

          // Reconfigure the logging streams according to settings
          // (obviously, until this point, the logging gateway has its default
          // behaviour...)

          if (!nmos::fields::error_log(node_model_.settings).empty())
          {
            error_log_buf.open(nmos::fields::error_log(node_model_.settings),
                               std::ios_base::out | std::ios_base::app);
            auto lock = log_model.write_lock();
            error_log.rdbuf(&error_log_buf);
          }

          if (!nmos::fields::access_log(node_model_.settings).empty())
          {
            access_log_buf.open(nmos::fields::access_log(node_model_.settings),
                                std::ios_base::out | std::ios_base::app);
            auto lock = log_model.write_lock();
            access_log.rdbuf(&access_log_buf);
          }

          // Log the process ID and initial settings
          slog::log<slog::severities::info>(gate, SLOG_FLF)
              << "Process ID: " << nmos::details::get_process_id();
          slog::log<slog::severities::info>(gate, SLOG_FLF)
              << "Build settings: " << nmos::get_build_settings_info();
          slog::log<slog::severities::info>(gate, SLOG_FLF)
              << "Initial settings: " << node_model_.settings.serialize();

          // Set up the callbacks between the node server and the underlying
          // implementation
          auto node_implementation = make_node_implementation();

          // Set up the node server

          auto node_server = nmos::experimental::make_node_server(
              node_model_, node_implementation, log_model, gate);

          // Add the underlying implementation, which will set up the node
          // resources, etc.

          node_server.thread_functions.push_back([&]
                                                 { thread_run(); });

          if (!nmos::experimental::fields::http_trace(node_model_.settings))
          {
            // Disable TRACE method

            for (auto &http_listener : node_server.http_listeners)
            {
              http_listener.support(
                  web::http::methods::TRCE, [](web::http::http_request req)
                  { req.reply(web::http::status_codes::MethodNotAllowed); });
            }
          }

          // Open the API ports and start up node operation (including the DNS-SD
          // advertisements)

          slog::log<slog::severities::info>(gate, SLOG_FLF)
              << "Preparing for connections";

          nmos::server_guard node_server_guard(node_server);

          slog::log<slog::severities::info>(gate, SLOG_FLF)
              << "Ready for connections";

          {
            std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
            if (LifecycleState::starting == lifecycle_state_)
            {
              lifecycle_state_ = LifecycleState::running;
            }
          }
          lifecycle_cv_.notify_all();

          wait_for_stop_signal();

          slog::log<slog::severities::info>(gate, SLOG_FLF)
              << "Closing connections";
        }
        catch (const web::json::json_exception &e)
        {
          // most likely from incorrect syntax or incorrect value types in the
          // command line settings
          slog::log<slog::severities::error>(gate, SLOG_FLF)
              << "JSON error: " << e.what();
          i = 1;
        }
        catch (const web::http::http_exception &e)
        {
          slog::log<slog::severities::error>(gate, SLOG_FLF)
              << "HTTP error: " << e.what() << " [" << e.error_code() << "]";
          i = 1;
        }
        catch (const web::websockets::websocket_exception &e)
        {
          slog::log<slog::severities::error>(gate, SLOG_FLF)
              << "WebSocket error: " << e.what() << " [" << e.error_code() << "]";
          i = 1;
        }
        catch (const std::ios_base::failure &e)
        {
          // most likely from failing to open the command line settings file
          slog::log<slog::severities::error>(gate, SLOG_FLF)
              << "File error: " << e.what();
          i = 1;
        }
        catch (const std::system_error &e)
        {
          slog::log<slog::severities::error>(gate, SLOG_FLF)
              << "System error: " << e.what() << " [" << e.code() << "]";
          i = 1;
        }
        catch (const std::runtime_error &e)
        {
          slog::log<slog::severities::error>(gate, SLOG_FLF)
              << "Implementation error: " << e.what();
          i = 1;
        }
        catch (const std::exception &e)
        {
          slog::log<slog::severities::error>(gate, SLOG_FLF)
              << "Unexpected exception: " << e.what();
          i = 1;
        }
        catch (...)
        {
          slog::log<slog::severities::severe>(gate, SLOG_FLF)
              << "Unexpected unknown exception";
          i = 1;
        }

        slog::log<slog::severities::info>(gate, SLOG_FLF)
            << "Stopping nmos-cpp node";

        {
          std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
          if (LifecycleState::starting == lifecycle_state_)
          {
            lifecycle_state_ = LifecycleState::stopped;
          }
        }
        lifecycle_cv_.notify_all();

        return i;
      }
      bool start()
      {
        {
          std::scoped_lock<std::mutex, std::mutex> lock(lifecycle_mutex_,
                                                        thread_mutex_);
          if (LifecycleState::running == lifecycle_state_)
          {
            return true;
          }

          if (LifecycleState::starting == lifecycle_state_ ||
              LifecycleState::stopping == lifecycle_state_ || thread_.joinable())
          {
            if (gate_)
            {
              slog::log<slog::severities::warning>(*gate_, SLOG_FLF)
                  << nmos::stash_category(impl::categories::node_implementation)
                  << "start ignored: node thread already active";
            }
            return false;
          }

          stop_requested_ = false;
          if (needs_model_reset_)
          {
            reset_model_state();
            needs_model_reset_ = false;
          }
          lifecycle_state_ = LifecycleState::starting;

          try
          {
            thread_ = std::thread([this]
                                  {
          nmos_node_start();
          {
            std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
            lifecycle_state_ = LifecycleState::stopped;
            stop_requested_ = false;
          }
          lifecycle_cv_.notify_all(); });
          }
          catch (...)
          {
            lifecycle_state_ = LifecycleState::stopped;
            stop_requested_ = false;
            lifecycle_cv_.notify_all();
            return false;
          }
        }

        std::unique_lock<std::mutex> lifecycle_lock(lifecycle_mutex_);
        lifecycle_cv_.wait(lifecycle_lock, [&]
                           { return LifecycleState::starting != lifecycle_state_; });

        return LifecycleState::running == lifecycle_state_;
      }

      void thread_run()
      {
        nmos::details::omanip_gate gate{
            *gate_, nmos::stash_category(impl::categories::node_implementation)};
        try
        {
          init();
          node_implementation_run();
        }
        catch (const node_implementation_init_exception &)
        {
          // node_implementation_init writes the log message
        }
        catch (const web::json::json_exception &e)
        {
          // most likely from incorrect value types in the command line settings
          slog::log<slog::severities::error>(gate, SLOG_FLF)
              << "JSON error: " << e.what();
        }
        catch (const std::system_error &e)
        {
          slog::log<slog::severities::error>(gate, SLOG_FLF)
              << "System error: " << e.what() << " [" << e.code() << "]";
        }
        catch (const std::runtime_error &e)
        {
          slog::log<slog::severities::error>(gate, SLOG_FLF)
              << "Implementation error: " << e.what();
        }
        catch (const std::exception &e)
        {
          slog::log<slog::severities::error>(gate, SLOG_FLF)
              << "Unexpected exception: " << e.what();
        }
        catch (...)
        {
          slog::log<slog::severities::severe>(gate, SLOG_FLF)
              << "Unexpected unknown exception";
        }
      }

      void init()
      {
        init_device();
        reinsert_cached_resources();
        resolve_auto = make_node_implementation_auto_resolver(node_model_.settings);
        set_transportfile = make_node_implementation_transportfile_setter(
            node_model_.node_resources, node_model_.settings);
      }

      void init_device()
      {
        nmos::write_lock lock = node_model_.write_lock();

        const auto seed_id =
            nmos::experimental::fields::seed_id(node_model_.settings);
        const auto node_id = impl::make_id(seed_id, nmos::types::node);
        const auto device_id = impl::make_id(seed_id, nmos::types::device);
        node_id_ = node_id;
        device_id_ = device_id;
        seed_id_ = seed_id;
        const auto clocks = web::json::value_of({nmos::make_internal_clock(nmos::clock_names::clk0)});

        // filter network interfaces to those that correspond to the specified
        // host_addresses
        const auto host_interfaces =
            nmos::get_host_interfaces(node_model_.settings);
        const auto interfaces =
            nmos::experimental::node_interfaces(host_interfaces);
        // example node
        {
          auto node = nmos::make_node(node_id, clocks,
                                      nmos::make_node_interfaces(interfaces),
                                      node_model_.settings);
          utility::string_t clocks = node.data[nmos::fields::clocks].serialize();
          node.data[nmos::fields::tags] =
              impl::fields::node_tags(node_model_.settings);
          if (!insert_resource_after(delay_millis, node_model_.node_resources,
                                     std::move(node), *gate_, lock))
            throw node_implementation_init_exception("insert node failed!");
        }

        // // prepare interface bindings for all senders and receivers
        // const auto &host_address = nmos::fields::host_address(node_model_.settings);
        // // the interface corresponding to the host address is used for the example
        // // node's WebSocket senders and receivers
        // const auto host_interface_ =
        //     impl::find_interface(host_interfaces, host_address);
        // if (host_interfaces.end() == host_interface_)
        // {
        //   slog::log<slog::severities::severe>(*gate_, SLOG_FLF)
        //       << "No network interface corresponding to host_address?";
        //   throw node_implementation_init_exception("find host interface failed!");
        // }
        // // hmm, should probably add a custom setting to control the primary and
        // // secondary interfaces for the example node's RTP senders and receivers
        // // rather than just picking the one(s) corresponding to the first and last
        // // of the specified host addresses
        // const auto &primary_address =
        //     node_model_.settings.has_field(nmos::fields::host_addresses)
        //         ? web::json::front(
        //               nmos::fields::host_addresses(node_model_.settings))
        //               .as_string()
        //         : host_address;
        // const auto &secondary_address =
        //     node_model_.settings.has_field(nmos::fields::host_addresses)
        //         ? web::json::back(
        //               nmos::fields::host_addresses(node_model_.settings))
        //               .as_string()
        //         : host_address;
        // const auto primary_interfaces =
        //     impl::find_interface(host_interfaces, primary_address);
        // const auto secondary_interfaces =
        //     impl::find_interface(host_interfaces, secondary_address);
        // if (host_interfaces.end() == primary_interfaces ||
        //     host_interfaces.end() == secondary_interfaces)
        // {
        //   slog::log<slog::severities::severe>(*gate_, SLOG_FLF)
        //       << "No network interface corresponding to one of the host_addresses?";
        //   throw node_implementation_init_exception("insert interface failed!");
        // }
        // primary_interface = *primary_interfaces;
        // secondary_interface = *secondary_interfaces;

        // example device
        {
          std::vector<nmos::id> empty;
          auto device = nmos::make_device(device_id, node_id, empty, empty,
                                          node_model_.settings);
          device.data[nmos::fields::tags] =
              impl::fields::device_tags(node_model_.settings);
          if (!insert_resource_after(delay_millis, node_model_.node_resources,
                                     std::move(device), *gate_, lock))
            throw node_implementation_init_exception("insert device failed!");
        }
      }

      void set_ptp_clock(std::string gmid_, bool locked_)
      {
        const auto normalized_gmid = normalize_ptp_gmid(gmid_);
        auto lock = node_model_.write_lock();
        nmos::modify_resource(node_model_.node_resources, node_id_, ([&](nmos::resource &node)
                                                                     { node.data[nmos::fields::clocks] = web::json::value_of(
                                                                           {is_valid_ptp_gmid(normalized_gmid)
                                                                                ? nmos::make_ptp_clock(nmos::clock_names::clk0, false,
                                                                                                       utility::s2us(normalized_gmid), locked_)
                                                                                : nmos::make_internal_clock(nmos::clock_names::clk0)}); }));

        for (const auto &sender_id : sender_ids_)
        {
          auto sender = nmos::find_resource(node_model_.node_resources,
                                            {sender_id, nmos::types::sender});
          if (node_model_.node_resources.end() == sender)
          {
            continue;
          }

          nmos::modify_resource(node_model_.connection_resources, sender_id,
                                [&](nmos::resource &connection_sender)
                                {
                                  auto &endpoint_transportfile =
                                      connection_sender.data[nmos::fields::endpoint_transportfile];
                                  set_transportfile(*sender, connection_sender,
                                                    endpoint_transportfile);
                                });
        }
      }

      void reset_model_state()
      {
        auto lock = node_model_.write_lock();
        node_model_.shutdown = false;
        node_model_.node_resources.clear();
        node_model_.connection_resources.clear();
        node_model_.events_resources.clear();
        node_model_.channelmapping_resources.clear();
        node_model_.control_protocol_resources.clear();
        node_model_.settings = web::json::value::object();

        sender_ids_.clear();
        receiver_ids_.clear();
        node_ids_.clear();
        device_ids_.clear();
        source_ids_.clear();
        flow_ids_.clear();
      }

      void reinsert_cached_resources()
      {
        if (video_senders.empty() && audio_senders.empty() &&
            ancillary_senders.empty() && video_receivers.empty() &&
            audio_receivers.empty() && ancillary_receivers.empty())
        {
          return;
        }

        const auto cached_video_senders = video_senders;
        const auto cached_audio_senders = audio_senders;
        const auto cached_ancillary_senders = ancillary_senders;

        std::vector<VideoReceiver> cached_video_receivers;
        std::vector<AudioReceiver> cached_audio_receivers;
        std::vector<AncillaryReceiver> cached_ancillary_receivers;
        {
          std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
          cached_video_receivers = video_receivers;
          cached_audio_receivers = audio_receivers;
          cached_ancillary_receivers = ancillary_receivers;
          video_receivers.clear();
          audio_receivers.clear();
          ancillary_receivers.clear();
        }

        video_senders.clear();
        audio_senders.clear();
        ancillary_senders.clear();

        for (const auto &video : cached_video_senders)
        {
          add_video_sender(video);
        }
        for (const auto &audio : cached_audio_senders)
        {
          add_audio_sender(audio);
        }
        for (const auto &ancillary : cached_ancillary_senders)
        {
          add_ancillary_sender(ancillary);
        }
        for (const auto &video : cached_video_receivers)
        {
          add_video_receiver(video);
        }
        for (const auto &audio : cached_audio_receivers)
        {
          add_audio_receiver(audio);
        }
        for (const auto &ancillary : cached_ancillary_receivers)
        {
          add_ancillary_receiver(ancillary);
        }
      }

      void add_video_sender(VideoSender video)
      {
        if (video.enable == false)
        {
          return;
        }
        nmos::write_lock lock = node_model_.write_lock();
        std::string id = video.id;
        std::string name = video.name;
        seeder::core::video_format_desc format_desc = seeder::core::video_format_desc::get(video.video_format);

        const auto sampling = st_get_color_sampling((st20_fmt)video.pg_format);
        const auto bit_depth = st_get_component_depth((st20_fmt)video.pg_format);

        const auto source_id =
            impl::make_id(seed_id_, nmos::types::source, impl::ports::video, id);
        const auto flow_id =
            impl::make_id(seed_id_, nmos::types::flow, impl::ports::video, id);
        const auto sender_id =
            impl::make_id(seed_id_, nmos::types::sender, impl::ports::video, id);
        bool ST_2022_7 = video.redudancy.enable;

        erase_resource_if_present(node_model_.connection_resources, sender_id);
        erase_resource_if_present(node_model_.node_resources, sender_id);
        erase_resource_if_present(node_model_.node_resources, flow_id);
        erase_resource_if_present(node_model_.node_resources, source_id);

        nmos::rational frame_rate = nmos::parse_rational(
            web::json::value_of({{nmos::fields::numerator, format_desc.framerate.numerator()},
                                 {nmos::fields::denominator, format_desc.framerate.denominator()}}));
        nmos::colorspace colorspace = (nmos::colorspace)video.colorspace;
        nmos::transfer_characteristic transfer_characteristic = (nmos::transfer_characteristic)video.transfer_characteristics;
        nmos::resource source;

        source = nmos::make_video_source(
            source_id, device_id_, nmos::clock_names::clk0, frame_rate,
            node_model_.settings); // 底层使用了 label 和 description 字段

        impl::set_label_description(source, impl::ports::video, name);

        nmos::interlace_mode interlace_mode =
            format_desc.field_count == 2 ? nmos::interlace_modes::interlaced_tff
                                         : nmos::interlace_modes::progressive;

        nmos::resource flow;
        flow = nmos::make_raw_video_flow(flow_id, source_id, device_id_, frame_rate,
                                         format_desc.width, format_desc.height, interlace_mode,
                                         colorspace, transfer_characteristic,
                                         sampling, bit_depth, node_model_.settings);

        impl::set_label_description(flow, impl::ports::video, name);

        if (!insert_resource_after(delay_millis, node_model_.node_resources,
                                   std::move(source), *gate_, lock))
          throw node_implementation_init_exception("add video sender source failed!");
        if (!insert_resource_after(delay_millis, node_model_.node_resources,
                                   std::move(flow), *gate_, lock))
        {
          erase_resource_if_present(node_model_.node_resources, source_id);
          throw node_implementation_init_exception("add video sender flow failed!");
        }

        const auto manifest_href = nmos::experimental::make_manifest_api_manifest(
            sender_id, node_model_.settings);
        const auto interface_names =
            ST_2022_7 ? std::vector<utility::string_t>{primary_interface.name,
                                                       secondary_interface.name}
                      : std::vector<utility::string_t>{primary_interface.name};
        auto sender = nmos::make_sender(sender_id, flow_id, nmos::transports::rtp_mcast,
                                        device_id_, manifest_href.to_string(),
                                        interface_names, node_model_.settings);
        impl::set_label_description(sender, impl::ports::video, name);
        impl::insert_group_hint(sender, impl::ports::video, id, name);

        auto connection_sender =
            nmos::make_connection_rtp_sender(sender_id, ST_2022_7);
        // add constraints;
        connection_sender
            .data[nmos::fields::endpoint_constraints][0][nmos::fields::source_ip] =
            value_of({{nmos::fields::constraint_enum,
                       value_from_elements(primary_interface.addresses)}});
        if (ST_2022_7)
          connection_sender.data[nmos::fields::endpoint_constraints][1]
                                [nmos::fields::source_ip] =
              value_of({{nmos::fields::constraint_enum,
                         value_from_elements(secondary_interface.addresses)}});
        auto &staged = connection_sender.data[nmos::fields::endpoint_staged];
        staged[nmos::fields::master_enable] = value::boolean(true);
        staged[nmos::fields::activation] =
            value_of({{nmos::fields::mode,
                       nmos::activation_modes::activate_scheduled_relative.name},
                      {nmos::fields::requested_time, U("0:0")},
                      {nmos::fields::activation_time, nmos::make_version()}});
        if (!insert_resource_after(delay_millis, node_model_.node_resources,
                                   std::move(sender), *gate_, lock))
        {
          erase_resource_if_present(node_model_.node_resources, flow_id);
          erase_resource_if_present(node_model_.node_resources, source_id);
          throw node_implementation_init_exception("add video sender failed!");
        }
        if (!insert_resource_after(delay_millis, node_model_.connection_resources,
                                   std::move(connection_sender), *gate_, lock))
        {
          erase_resource_if_present(node_model_.node_resources, sender_id);
          erase_resource_if_present(node_model_.node_resources, flow_id);
          erase_resource_if_present(node_model_.node_resources, source_id);
          throw node_implementation_init_exception("add video sender connection failed!");
        }
        video.sender_id = sender_id;
        video_senders.push_back(video);
        sender_ids_.push_back(sender_id);
        source_ids_.push_back(source_id);
        flow_ids_.push_back(flow_id);
        // try_bind_video_sender_receiver(video);
        nmos::modify_resource(node_model_.node_resources, device_id_, ([&](nmos::resource &device)
                                                                       {
                                                                         device.data[nmos::fields::senders] = value_from_elements(sender_ids_);
                                                                         device.data[nmos::fields::version] = value(nmos::make_version()); }));
      }

      void add_audio_sender(AudioSender audio)
      {
        if (audio.enable == false)
        {
          return;
        }
        nmos::write_lock lock = node_model_.write_lock();

        nmos::rational frame_rate = nmos::parse_rational(web::json::value_of(
            {{nmos::fields::numerator, audio.simple_rate},
             {nmos::fields::denominator, 1}}));
        ;
        std::string id = audio.id;
        std::string name = audio.name;
        // audio
        impl::port port = impl::ports::audio;

        const auto source_id =
            impl::make_id(seed_id_, nmos::types::source, port, id);
        const auto flow_id = impl::make_id(seed_id_, nmos::types::flow, port, id);
        const auto sender_id =
            impl::make_id(seed_id_, nmos::types::sender, port, id);

        erase_resource_if_present(node_model_.connection_resources, sender_id);
        erase_resource_if_present(node_model_.node_resources, sender_id);
        erase_resource_if_present(node_model_.node_resources, flow_id);
        erase_resource_if_present(node_model_.node_resources, source_id);

        erase_resource_if_present(node_model_.connection_resources, sender_id);
        erase_resource_if_present(node_model_.node_resources, sender_id);
        erase_resource_if_present(node_model_.node_resources, flow_id);
        erase_resource_if_present(node_model_.node_resources, source_id);

        const auto channels = boost::copy_range<std::vector<nmos::channel>>(
            boost::irange(0, audio.channel_count) |
            boost::adaptors::transformed([&](const int &index)
                                         { return impl::channels_repeat[index %
                                                                        (int)impl::channels_repeat.size()]; }));

        int bit_depth = audio.bit_depth;
        int simple_rate = audio.simple_rate;
        nmos::resource source =
            nmos::make_audio_source(source_id, device_id_, nmos::clock_names::clk0,
                                    frame_rate, channels, node_model_.settings);
        // impl::insert_parents(source, seed_id_, port, index);
        impl::set_label_description(source, port, name);
        nmos::resource flow =
            nmos::make_raw_audio_flow(flow_id, source_id, device_id_, simple_rate,
                                      bit_depth, node_model_.settings);

        // impl::insert_parents(flow, seed_id_, port, index);
        impl::set_label_description(flow, port, name);

        // set_transportfile needs to find the matching source and flow for the
        // sender, so insert these first
        if (!insert_resource_after(delay_millis, node_model_.node_resources,
                                   std::move(source), *gate_, lock))
          throw node_implementation_init_exception("add audio sender source failed!");
        if (!insert_resource_after(delay_millis, node_model_.node_resources,
                                   std::move(flow), *gate_, lock))
        {
          erase_resource_if_present(node_model_.node_resources, source_id);
          throw node_implementation_init_exception("add audio sender flow failed!");
        }

        const auto manifest_href = nmos::experimental::make_manifest_api_manifest(
            sender_id, node_model_.settings);
        bool ST_2022_7 = audio.redudancy.enable;

        const auto interface_names =
            ST_2022_7 ? std::vector<utility::string_t>{primary_interface.name,
                                                       secondary_interface.name}
                      : std::vector<utility::string_t>{primary_interface.name};
        auto sender = nmos::make_sender(sender_id, flow_id, nmos::transports::rtp_mcast,
                                        device_id_, manifest_href.to_string(),
                                        interface_names, node_model_.settings);
        impl::set_label_description(sender, port, name);
        impl::insert_group_hint(sender, port, id, name);

        auto connection_sender =
            nmos::make_connection_rtp_sender(sender_id, ST_2022_7);
        // add constraints;
        connection_sender
            .data[nmos::fields::endpoint_constraints][0][nmos::fields::source_ip] =
            value_of({{nmos::fields::constraint_enum,
                       value_from_elements(primary_interface.addresses)}});
        if (ST_2022_7)
          connection_sender.data[nmos::fields::endpoint_constraints][1]
                                [nmos::fields::source_ip] =
              value_of({{nmos::fields::constraint_enum,
                         value_from_elements(secondary_interface.addresses)}});

        // initialize this sender with a scheduled activation, e.g. to enable the
        auto &staged = connection_sender.data[nmos::fields::endpoint_staged];
        staged[nmos::fields::master_enable] = value::boolean(true);
        staged[nmos::fields::activation] =
            value_of({{nmos::fields::mode,
                       nmos::activation_modes::activate_scheduled_relative.name},
                      {nmos::fields::requested_time, U("0:0")},
                      {nmos::fields::activation_time, nmos::make_version()}});
        if (!insert_resource_after(delay_millis, node_model_.node_resources,
                                   std::move(sender), *gate_, lock))
        {
          erase_resource_if_present(node_model_.node_resources, flow_id);
          erase_resource_if_present(node_model_.node_resources, source_id);
          throw node_implementation_init_exception("insert audio sender failed!");
        }
        if (!insert_resource_after(delay_millis, node_model_.connection_resources,
                                   std::move(connection_sender), *gate_, lock))
        {
          erase_resource_if_present(node_model_.node_resources, sender_id);
          erase_resource_if_present(node_model_.node_resources, flow_id);
          erase_resource_if_present(node_model_.node_resources, source_id);
          throw node_implementation_init_exception("insert audio connection sender failed!");
        }
        audio.sender_id = sender_id;
        audio_senders.push_back(audio);
        sender_ids_.push_back(sender_id);
        source_ids_.push_back(source_id);
        flow_ids_.push_back(flow_id);
        // try_bind_audio_sender_receiver(audio);
        nmos::modify_resource(node_model_.node_resources, device_id_, ([&](nmos::resource &device)
                                                                       {
                                                                         device.data[nmos::fields::senders] = value_from_elements(sender_ids_);
                                                                         device.data[nmos::fields::version] = value(nmos::make_version()); }));
      }

      void add_ancillary_sender(AncillarySender ancillary)
      {
        if (ancillary.enable == false)
        {
          return;
        }
        nmos::write_lock lock = node_model_.write_lock();
        std::string id = ancillary.id;
        std::string name = ancillary.name;
        impl::port port = impl::ports::data;
        nmos::rational grain_rate = nmos::parse_rational(web::json::value_of(
            {{nmos::fields::numerator, 50},
             {nmos::fields::denominator, 1}}));
        if (!ancillary.format.empty())
        {
          try
          {
            const auto format_desc = seeder::core::video_format_desc::get(ancillary.format);
            grain_rate = nmos::parse_rational(
                web::json::value_of({{nmos::fields::numerator,
                                      format_desc.framerate.numerator()},
                                     {nmos::fields::denominator,
                                      format_desc.framerate.denominator()}}));
          }
          catch (const std::exception &)
          {
          }
        }

        const auto source_id =
            impl::make_id(seed_id_, nmos::types::source, port, id);
        const auto flow_id = impl::make_id(seed_id_, nmos::types::flow, port, id);
        const auto sender_id =
            impl::make_id(seed_id_, nmos::types::sender, port, id);

        nmos::resource source = nmos::make_data_source(
            source_id, device_id_, nmos::clock_names::clk0, grain_rate,
            node_model_.settings);
        impl::set_label_description(source, port, name);

        nmos::resource flow =
            nmos::make_sdianc_data_flow(flow_id, source_id, device_id_,
                                        node_model_.settings);
        flow.data[nmos::fields::grain_rate] = nmos::make_rational(grain_rate);
        impl::set_label_description(flow, port, name);

        if (!insert_resource_after(delay_millis, node_model_.node_resources,
                                   std::move(source), *gate_, lock))
          throw node_implementation_init_exception(" add ancillary sender source failed!");
        if (!insert_resource_after(delay_millis, node_model_.node_resources,
                                   std::move(flow), *gate_, lock))
        {
          erase_resource_if_present(node_model_.node_resources, source_id);
          throw node_implementation_init_exception("add ancillary sender flow failed!");
        }

        const auto manifest_href = nmos::experimental::make_manifest_api_manifest(
            sender_id, node_model_.settings);
        const bool ST_2022_7 = ancillary.redudancy.enable;
        const auto interface_names =
            ST_2022_7 ? std::vector<utility::string_t>{primary_interface.name,
                                                       secondary_interface.name}
                      : std::vector<utility::string_t>{primary_interface.name};
        auto sender = nmos::make_sender(sender_id, flow_id, nmos::transports::rtp_mcast,
                                        device_id_, manifest_href.to_string(),
                                        interface_names, node_model_.settings);
        impl::set_label_description(sender, port, name);
        impl::insert_group_hint(sender, port, id, name);

        auto connection_sender =
            nmos::make_connection_rtp_sender(sender_id, ST_2022_7);
        connection_sender
            .data[nmos::fields::endpoint_constraints][0][nmos::fields::source_ip] =
            value_of({{nmos::fields::constraint_enum,
                       value_from_elements(primary_interface.addresses)}});
        if (ST_2022_7)
        {
          connection_sender.data[nmos::fields::endpoint_constraints][1]
                                [nmos::fields::source_ip] =
              value_of({{nmos::fields::constraint_enum,
                         value_from_elements(secondary_interface.addresses)}});
        }
        auto &staged = connection_sender.data[nmos::fields::endpoint_staged];
        staged[nmos::fields::master_enable] = value::boolean(true);
        staged[nmos::fields::activation] =
            value_of({{nmos::fields::mode,
                       nmos::activation_modes::activate_scheduled_relative.name},
                      {nmos::fields::requested_time, U("0:0")},
                      {nmos::fields::activation_time, nmos::make_version()}});

        if (!insert_resource_after(delay_millis, node_model_.node_resources,
                                   std::move(sender), *gate_, lock))
        {
          erase_resource_if_present(node_model_.node_resources, flow_id);
          erase_resource_if_present(node_model_.node_resources, source_id);
          throw node_implementation_init_exception("add ancillary sender failed!");
        }
        if (!insert_resource_after(delay_millis, node_model_.connection_resources,
                                   std::move(connection_sender), *gate_, lock))
        {
          erase_resource_if_present(node_model_.node_resources, sender_id);
          erase_resource_if_present(node_model_.node_resources, flow_id);
          erase_resource_if_present(node_model_.node_resources, source_id);
          throw node_implementation_init_exception("add ancillary sender connection failed!");
        }
        ancillary.sender_id = sender_id;
        ancillary_senders.push_back(ancillary);
        sender_ids_.push_back(sender_id);
        source_ids_.push_back(source_id);
        flow_ids_.push_back(flow_id);
        // try_bind_ancillary_sender_receiver(ancillary);
        nmos::modify_resource(node_model_.node_resources, device_id_, ([&](nmos::resource &device)
                                                                       {
                                                                         device.data[nmos::fields::senders] = value_from_elements(sender_ids_);
                                                                         device.data[nmos::fields::version] = value(nmos::make_version()); }));
      }

      /**
        初始化设备
      */

      void add_video_receiver(VideoReceiver video)
      {
        if (video.enable == false)
        {
          return;
        }
        nmos::write_lock lock = node_model_.write_lock();
        std::string id = video.id;
        std::string name = video.name;
        bool ST_2022_7 = video.redudancy.enable;
        const auto receiver_id =
            impl::make_id(seed_id_, nmos::types::receiver, impl::ports::video, id);
        const auto primary_interface_ip =
            receiver_interface_ip_or_default(video.source_ip, primary_interface);
        const auto secondary_interface_ip = first_interface_address(secondary_interface);
        const auto secondary_multicast_ip =
            receiver_multicast_ip_or_default(video.redudancy.ip, video.ip);
        const auto secondary_port =
            receiver_port_or_default(video.redudancy.port, video.port);
        const auto interface_names =
            ST_2022_7 ? std::vector<utility::string_t>{primary_interface.name,
                                                       secondary_interface.name}
                      : std::vector<utility::string_t>{primary_interface.name};

        erase_resource_if_present(node_model_.connection_resources, receiver_id);
        erase_resource_if_present(node_model_.node_resources, receiver_id);

        nmos::resource receiver;
        receiver = nmos::make_receiver(
            receiver_id, device_id_, nmos::transports::rtp_mcast, interface_names,
            nmos::formats::video, {nmos::media_types::video_raw},
            node_model_.settings);
        std::vector<web::json::value> constraint_sets;
        const auto colorspaces = to_utility_string_vector(video.caps.colorspaces);
        const auto transfer_characteristics =
            to_utility_string_vector(video.caps.transfer_characteristics);
        for (const auto &format : video.caps.formats)
        {
          seeder::core::video_format_desc format_desc = seeder::core::video_format_desc::get(format);
          const auto interlace_modes =
              format_desc.field_count == 2 ? std::vector<
                                                 utility::string_t>{nmos::interlace_modes::interlaced_bff.name,
                                                                    nmos::interlace_modes::interlaced_tff.name,
                                                                    nmos::interlace_modes::interlaced_psf.name}
                                           : std::vector<utility::string_t>{
                                                 nmos::interlace_modes::progressive.name};

          web::json::value constraint_set = value_of(
              {{nmos::caps::format::grain_rate, nmos::make_caps_rational_constraint({nmos::parse_rational(web::json::value_of({{nmos::fields::numerator, format_desc.framerate.numerator()},
                                                                                                                               {nmos::fields::denominator, format_desc.framerate.denominator()}}))})},
               {nmos::caps::format::frame_width, nmos::make_caps_integer_constraint({format_desc.width})},
               {nmos::caps::format::frame_height, nmos::make_caps_integer_constraint({format_desc.height})},
               {nmos::caps::format::interlace_mode, nmos::make_caps_string_constraint(interlace_modes)}});

          if (!colorspaces.empty())
          {
            constraint_set[nmos::caps::format::colorspace] =
                nmos::make_caps_string_constraint(colorspaces);
          }

          if (!transfer_characteristics.empty())
          {
            constraint_set[nmos::caps::format::transfer_characteristic] =
                nmos::make_caps_string_constraint(transfer_characteristics);
          }

          constraint_sets.push_back(std::move(constraint_set));
        }

        if (constraint_sets.empty() &&
            (!colorspaces.empty() || !transfer_characteristics.empty()))
        {
          web::json::value constraint_set = value::object();

          if (!colorspaces.empty())
          {
            constraint_set[nmos::caps::format::colorspace] =
                nmos::make_caps_string_constraint(colorspaces);
          }

          if (!transfer_characteristics.empty())
          {
            constraint_set[nmos::caps::format::transfer_characteristic] =
                nmos::make_caps_string_constraint(transfer_characteristics);
          }

          constraint_sets.push_back(std::move(constraint_set));
        }

        if (!constraint_sets.empty())
        {

          receiver.data[nmos::fields::caps][nmos::fields::constraint_sets] =
              value_from_elements(constraint_sets);
          std::string caps_json = receiver.data[nmos::fields::caps][nmos::fields::constraint_sets].serialize();
          // printf("receiver caps: %s\n", caps_json.c_str());
        }

        receiver.data[nmos::fields::version] =
            receiver.data[nmos::fields::caps][nmos::fields::version] =
                value(nmos::make_version());
        impl::set_label_description(receiver, impl::ports::video, name);
        impl::insert_group_hint(receiver, impl::ports::video, id, name);
        auto connection_receiver =
            nmos::make_connection_rtp_receiver(receiver_id, ST_2022_7);
        connection_receiver.data[nmos::fields::endpoint_constraints][0]
                                [nmos::fields::interface_ip] =
            value_of({{nmos::fields::constraint_enum,
                       value_from_elements(primary_interface.addresses)}});

        // connection_receiver
        //     .data[nmos::fields::endpoint_active][nmos::fields::transport_params]
        //          [nmos::fields::source_ip] = value(video.source_ip);
        auto &staged = connection_receiver.data[nmos::fields::endpoint_staged];
        auto &active = connection_receiver.data[nmos::fields::endpoint_active];
        staged[nmos::fields::master_enable] = value::boolean(true);
        active[nmos::fields::master_enable] = value::boolean(true);
        if (ST_2022_7)
        {
          initialize_receiver_transport_params(
              staged, active, video.ip, primary_interface_ip, video.port,
              secondary_multicast_ip, secondary_interface_ip, secondary_port);
        }
        else
        {
          initialize_receiver_transport_params(staged, active, video.ip,
                                               primary_interface_ip,
                                               video.port);
        }

        if (ST_2022_7)
          connection_receiver.data[nmos::fields::endpoint_constraints][1]
                                  [nmos::fields::interface_ip] =
              value_of({{nmos::fields::constraint_enum,
                         value_from_elements(secondary_interface.addresses)}});
        // resolve_auto(receiver, connection_receiver,
        //              connection_receiver.data[nmos::fields::endpoint_active]
        //                                      [nmos::fields::transport_params]);
        if (!insert_resource_after(delay_millis, node_model_.node_resources,
                                   std::move(receiver), *gate_, lock))
          throw node_implementation_init_exception("insert video receiver failed!");
        if (!insert_resource_after(delay_millis, node_model_.connection_resources,
                                   std::move(connection_receiver), *gate_, lock))
        {
          erase_resource_if_present(node_model_.node_resources, receiver_id);
          throw node_implementation_init_exception("insert video connection receiver failed!");
        }
        {
          std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
          video_receivers.push_back(video);
        }
        receiver_ids_.push_back(receiver_id);
        // try_bind_video_receiver_sender(video);
        nmos::modify_resource(node_model_.node_resources, device_id_, ([&](nmos::resource &device)
                                                                       {
                                                                         device.data[nmos::fields::receivers] = value_from_elements(receiver_ids_);
                                                                         device.data[nmos::fields::version] = value(nmos::make_version()); }));
      }

      void add_audio_receiver(AudioReceiver audio)
      {
        if (audio.enable == false)
        {
          return;
        }
        nmos::write_lock lock = node_model_.write_lock();
        std::string id = audio.id;
        std::string name = audio.name;
        bool ST_2022_7 = audio.redudancy.enable;
        int bit_depth = audio.bit_depth;
        const auto primary_interface_ip =
            receiver_interface_ip_or_default(audio.source_ip, primary_interface);
        const auto secondary_interface_ip = first_interface_address(secondary_interface);
        const auto secondary_multicast_ip =
            receiver_multicast_ip_or_default(audio.redudancy.ip, audio.ip);
        const auto secondary_port =
            receiver_port_or_default(audio.redudancy.port, audio.port);
        const auto receiver_id =
            impl::make_id(seed_id_, nmos::types::receiver, impl::ports::audio, id);
        const auto interface_names =
            ST_2022_7 ? std::vector<utility::string_t>{primary_interface.name,
                                                       secondary_interface.name}
                      : std::vector<utility::string_t>{primary_interface.name};

        erase_resource_if_present(node_model_.connection_resources, receiver_id);
        erase_resource_if_present(node_model_.node_resources, receiver_id);

        nmos::resource receiver = nmos::make_audio_receiver(
            receiver_id, device_id_, nmos::transports::rtp_mcast, interface_names,
            bit_depth, node_model_.settings);

        int sample_rate =
            audio
                .simple_rate; // st30_get_sample_rate(session->info.audio_sampling);
        double packet_time = audio.packet_time;
        // (double)st30_get_packet_time(session->info.audio_ptime) / 1000000;

        web::json::value audio_constraint_set = value::object();
        bool has_audio_caps = false;

        if (audio.channel_count > 0)
        {
          audio_constraint_set[nmos::caps::format::channel_count] =
              nmos::make_caps_integer_constraint({}, 1, audio.channel_count);
          has_audio_caps = true;
        }

        if (sample_rate > 0)
        {
          audio_constraint_set[nmos::caps::format::sample_rate] =
              nmos::make_caps_rational_constraint({nmos::rational{sample_rate, 1}});
          has_audio_caps = true;
        }

        if (bit_depth > 0)
        {
          audio_constraint_set[nmos::caps::format::sample_depth] =
              nmos::make_caps_integer_constraint({bit_depth});
          has_audio_caps = true;
        }

        if (packet_time > 0.0)
        {
          audio_constraint_set[nmos::caps::transport::packet_time] =
              nmos::make_caps_number_constraint({packet_time});
          has_audio_caps = true;
        }

        if (has_audio_caps)
        {
          web::json::value audio_constraint_sets = value::array();
          audio_constraint_sets[0] = std::move(audio_constraint_set);
          receiver.data[nmos::fields::caps][nmos::fields::constraint_sets] =
              std::move(audio_constraint_sets);
        }

        receiver.data[nmos::fields::version] =
            receiver.data[nmos::fields::caps][nmos::fields::version] =
                value(nmos::make_version());

        impl::set_label_description(receiver, impl::ports::audio, name);
        impl::insert_group_hint(receiver, impl::ports::audio, id, name);

        auto connection_receiver =
            nmos::make_connection_rtp_receiver(receiver_id, ST_2022_7);
        // add some example constraints; these should be completed fully!
        // connection_receiver
        //     .data[nmos::fields::endpoint_constraints][0][nmos::fields::interface_ip]
        //     = value_of({{nmos::fields::constraint_enum,
        //                value_from_elements(primary_interface.addresses)}});
        // if (ST_2022_7) {
        //   connection_receiver.data[nmos::fields::endpoint_constraints][1]
        //                           [nmos::fields::interface_ip] =
        //       value_of({{nmos::fields::constraint_enum,
        //                  value_from_elements(secondary_interface.addresses)}});
        // }
        connection_receiver.data[nmos::fields::endpoint_constraints][0]
                                [nmos::fields::interface_ip] =
            value_of({{nmos::fields::constraint_enum,
                       value_from_elements(primary_interface.addresses)}});
        auto &staged = connection_receiver.data[nmos::fields::endpoint_staged];
        auto &active = connection_receiver.data[nmos::fields::endpoint_active];
        staged[nmos::fields::master_enable] = value::boolean(true);
        active[nmos::fields::master_enable] = value::boolean(true);
        if (ST_2022_7)
        {
          initialize_receiver_transport_params(
              staged, active, audio.ip, primary_interface_ip, audio.port,
              secondary_multicast_ip, secondary_interface_ip, secondary_port);
        }
        else
        {
          initialize_receiver_transport_params(staged, active, audio.ip,
                                               primary_interface_ip,
                                               audio.port);
        }
        if (ST_2022_7)
        {
          connection_receiver.data[nmos::fields::endpoint_constraints][1]
                                  [nmos::fields::interface_ip] =
              value_of({{nmos::fields::constraint_enum,
                         value_from_elements(secondary_interface.addresses)}});
        }
        // receiver_audio_session_map_.emplace(connection_receiver.id, audio);
        // resolve_auto(receiver, connection_receiver,
        //              connection_receiver.data[nmos::fields::endpoint_active]
        //                                      [nmos::fields::transport_params]);
        if (!insert_resource_after(delay_millis, node_model_.node_resources,
                                   std::move(receiver), *gate_, lock))
        {
          throw node_implementation_init_exception("add audio receiver failed!");
        }
        if (!insert_resource_after(delay_millis, node_model_.connection_resources,
                                   std::move(connection_receiver), *gate_, lock))
        {
          erase_resource_if_present(node_model_.node_resources, receiver_id);
          throw node_implementation_init_exception("add audio receiver connection failed!");
        }
        {
          std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
          audio_receivers.push_back(audio);
        }
        receiver_ids_.push_back(receiver_id);
        // try_bind_audio_receiver_sender(audio);

        nmos::modify_resource(node_model_.node_resources, device_id_, ([&](nmos::resource &device)
                                                                       {
                                                                         device.data[nmos::fields::receivers] = value_from_elements(receiver_ids_);
                                                                         device.data[nmos::fields::version] = value(nmos::make_version()); }));
      }

      void add_ancillary_receiver(AncillaryReceiver ancillary)
      {
        if (ancillary.enable == false)
        {
          return;
        }
        nmos::write_lock lock = node_model_.write_lock();
        std::string id = ancillary.id;
        std::string name = ancillary.name;
        const bool ST_2022_7 = ancillary.redudancy.enable;
        const auto primary_interface_ip = receiver_interface_ip_or_default(
            ancillary.source_ip, primary_interface);
        const auto secondary_interface_ip = first_interface_address(secondary_interface);
        const auto secondary_multicast_ip = receiver_multicast_ip_or_default(
            ancillary.redudancy.ip, ancillary.ip);
        const auto secondary_port = receiver_port_or_default(
            ancillary.redudancy.port, ancillary.port);
        const auto receiver_id =
            impl::make_id(seed_id_, nmos::types::receiver, impl::ports::data, id);
        const auto interface_names =
            ST_2022_7 ? std::vector<utility::string_t>{primary_interface.name,
                                                       secondary_interface.name}
                      : std::vector<utility::string_t>{primary_interface.name};

        erase_resource_if_present(node_model_.connection_resources, receiver_id);
        erase_resource_if_present(node_model_.node_resources, receiver_id);

        nmos::resource receiver = nmos::make_sdianc_data_receiver(
            receiver_id, device_id_, nmos::transports::rtp_mcast, interface_names,
            node_model_.settings);
        receiver.data[nmos::fields::version] =
            receiver.data[nmos::fields::caps][nmos::fields::version] =
                value(nmos::make_version());
        impl::set_label_description(receiver, impl::ports::data, name);
        impl::insert_group_hint(receiver, impl::ports::data, id, name);

        auto connection_receiver =
            nmos::make_connection_rtp_receiver(receiver_id, ST_2022_7);
        connection_receiver.data[nmos::fields::endpoint_constraints][0]
                                [nmos::fields::interface_ip] =
            value_of({{nmos::fields::constraint_enum,
                       value_from_elements(primary_interface.addresses)}});
        auto &staged = connection_receiver.data[nmos::fields::endpoint_staged];
        auto &active = connection_receiver.data[nmos::fields::endpoint_active];
        staged[nmos::fields::master_enable] = value::boolean(true);
        active[nmos::fields::master_enable] = value::boolean(true);
        if (ST_2022_7)
        {
          initialize_receiver_transport_params(
              staged, active, ancillary.ip, primary_interface_ip,
              ancillary.port, secondary_multicast_ip,
              secondary_interface_ip, secondary_port);
        }
        else
        {
          initialize_receiver_transport_params(staged, active, ancillary.ip,
                                               primary_interface_ip,
                                               ancillary.port);
        }
        if (ST_2022_7)
        {
          connection_receiver.data[nmos::fields::endpoint_constraints][1]
                                  [nmos::fields::interface_ip] =
              value_of({{nmos::fields::constraint_enum,
                         value_from_elements(secondary_interface.addresses)}});
        }

        if (!insert_resource_after(delay_millis, node_model_.node_resources,
                                   std::move(receiver), *gate_, lock))
        {
          throw node_implementation_init_exception("add ancillary receiver failed!");
        }
        if (!insert_resource_after(delay_millis, node_model_.connection_resources,
                                   std::move(connection_receiver), *gate_, lock))
        {
          erase_resource_if_present(node_model_.node_resources, receiver_id);
          throw node_implementation_init_exception("add ancillary receiver connection failed!");
        }
        {
          std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
          ancillary_receivers.push_back(ancillary);
        }
        receiver_ids_.push_back(receiver_id);
        // try_bind_ancillary_receiver_sender(ancillary);
        nmos::modify_resource(node_model_.node_resources, device_id_, ([&](nmos::resource &device)
                                                                       {
                                                                         device.data[nmos::fields::receivers] = value_from_elements(receiver_ids_);
                                                                         device.data[nmos::fields::version] = value(nmos::make_version()); }));
      }

      void remove_audio_sender(std::string id)
      {
        AudioSender *audio_sender = find_audio_sender_by_id(id);
        if (!audio_sender)
        {
          slog::log<slog::severities::error>(*gate_, SLOG_FLF)
              << nmos::stash_category(impl::categories::node_implementation)
              << "remove audio sender not found audio sender id:" << id;
          return;
        }
        nmos::write_lock lock = node_model_.write_lock();

        const auto source_a_id =
            impl::make_id(seed_id_, nmos::types::source, impl::ports::audio, id);
        const auto flow_a_id =
            impl::make_id(seed_id_, nmos::types::flow, impl::ports::audio, id);
        const auto sender_a_id =
            impl::make_id(seed_id_, nmos::types::sender, impl::ports::audio, id);

        remove_resource_after(delay_millis, node_model_.node_resources, sender_a_id,
                              *gate_, lock);

        remove_resource_after(delay_millis, node_model_.connection_resources,
                              sender_a_id, *gate_, lock);
        remove_resource_after(delay_millis, node_model_.node_resources, source_a_id,
                              *gate_, lock);

        remove_resource_after(delay_millis, node_model_.node_resources, flow_a_id,
                              *gate_, lock);

        remove_audio_sender_by_sender_id(sender_a_id);

        const auto found_a_sender = boost::range::find(sender_ids_, sender_a_id);
        if (sender_ids_.end() != found_a_sender)
        {
          sender_ids_.erase(found_a_sender);
        }

        const auto found_a_source = boost::range::find(source_ids_, source_a_id);
        if (source_ids_.end() != found_a_source)
        {
          source_ids_.erase(found_a_source);
        }

        const auto found_a_flow = boost::range::find(flow_ids_, flow_a_id);
        if (flow_ids_.end() != found_a_flow)
        {
          flow_ids_.erase(found_a_flow);
        }
        nmos::modify_resource(node_model_.node_resources, device_id_, ([&](nmos::resource &device)
                                                                       {
                                                                         device.data[nmos::fields::senders] = value_from_elements(sender_ids_);
                                                                         device.data[nmos::fields::version] = value(nmos::make_version()); }));
        slog::log<slog::severities::warning>(*gate_, SLOG_FLF)
            << nmos::stash_category(impl::categories::node_implementation)
            << "Remove id:" << id << "  sender_id:" << sender_a_id;
      }

      void remove_video_sender(std::string id)
      {
        VideoSender *video = find_video_sender_by_id(id);
        if (!video)
        {
          slog::log<slog::severities::error>(*gate_, SLOG_FLF)
              << nmos::stash_category(impl::categories::node_implementation)
              << "remove video sender not found video sender id:" << id;
          return;
        }
        nmos::write_lock lock = node_model_.write_lock();

        const auto source_v_id =
            impl::make_id(seed_id_, nmos::types::source, impl::ports::video, id);
        const auto flow_v_id =
            impl::make_id(seed_id_, nmos::types::flow, impl::ports::video, id);
        const auto sender_v_id =
            impl::make_id(seed_id_, nmos::types::sender, impl::ports::video, id);

        remove_resource_after(delay_millis, node_model_.node_resources, sender_v_id,
                              *gate_, lock);

        remove_resource_after(delay_millis, node_model_.connection_resources,
                              sender_v_id, *gate_, lock);
        remove_resource_after(delay_millis, node_model_.node_resources, source_v_id,
                              *gate_, lock);

        remove_resource_after(delay_millis, node_model_.node_resources, flow_v_id,
                              *gate_, lock);

        remove_video_sender_by_sender_id(sender_v_id);

        const auto found_v_sender = boost::range::find(sender_ids_, sender_v_id);
        if (sender_ids_.end() != found_v_sender)
        {
          sender_ids_.erase(found_v_sender);
        }

        const auto found_v_source = boost::range::find(source_ids_, source_v_id);
        if (source_ids_.end() != found_v_source)
        {
          source_ids_.erase(found_v_source);
        }

        const auto found_v_flow = boost::range::find(flow_ids_, flow_v_id);
        if (flow_ids_.end() != found_v_flow)
        {
          flow_ids_.erase(found_v_flow);
        }
        slog::log<slog::severities::warning>(*gate_, SLOG_FLF)
            << nmos::stash_category(impl::categories::node_implementation)
            << "Remove id:" << id << "  sender_id:" << sender_v_id;
        nmos::modify_resource(node_model_.node_resources, device_id_, ([&](nmos::resource &device)
                                                                       {
                                                                         device.data[nmos::fields::senders] = value_from_elements(sender_ids_);
                                                                         device.data[nmos::fields::version] = value(nmos::make_version()); }));
      }

      void remove_ancillary_sender(std::string id)
      {
        AncillarySender *ancillary = find_ancillary_sender_by_id(id);
        if (!ancillary)
        {
          slog::log<slog::severities::error>(*gate_, SLOG_FLF)
              << nmos::stash_category(impl::categories::node_implementation)
              << "remove ancillary sender not found ancillary sender id:" << id;
          return;
        }
        nmos::write_lock lock = node_model_.write_lock();

        const auto source_id =
            impl::make_id(seed_id_, nmos::types::source, impl::ports::data, id);
        const auto flow_id =
            impl::make_id(seed_id_, nmos::types::flow, impl::ports::data, id);
        const auto sender_id =
            impl::make_id(seed_id_, nmos::types::sender, impl::ports::data, id);

        remove_resource_after(delay_millis, node_model_.node_resources, sender_id,
                              *gate_, lock);
        remove_resource_after(delay_millis, node_model_.connection_resources,
                              sender_id, *gate_, lock);
        remove_resource_after(delay_millis, node_model_.node_resources, source_id,
                              *gate_, lock);
        remove_resource_after(delay_millis, node_model_.node_resources, flow_id,
                              *gate_, lock);

        remove_ancillary_sender_by_sender_id(sender_id);

        const auto found_sender = boost::range::find(sender_ids_, sender_id);
        if (sender_ids_.end() != found_sender)
        {
          sender_ids_.erase(found_sender);
        }
        const auto found_source = boost::range::find(source_ids_, source_id);
        if (source_ids_.end() != found_source)
        {
          source_ids_.erase(found_source);
        }
        const auto found_flow = boost::range::find(flow_ids_, flow_id);
        if (flow_ids_.end() != found_flow)
        {
          flow_ids_.erase(found_flow);
        }

        slog::log<slog::severities::warning>(*gate_, SLOG_FLF)
            << nmos::stash_category(impl::categories::node_implementation)
            << "Remove id:" << id << "  sender_id:" << sender_id;

        nmos::modify_resource(node_model_.node_resources, device_id_, ([&](nmos::resource &device)
                                                                       {
                                                                         device.data[nmos::fields::senders] = value_from_elements(sender_ids_);
                                                                         device.data[nmos::fields::version] = value(nmos::make_version()); }));
      }

      void remove_video_receiver(std::string id)
      {
        nmos::write_lock lock = node_model_.write_lock();
        std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
        VideoReceiver *video = find_video_receiver_by_id(id);
        if (!video)
        {
          slog::log<slog::severities::error>(*gate_, SLOG_FLF)
              << nmos::stash_category(impl::categories::node_implementation)
              << "remove video receiver not found video receiver id:" << id;
          return;
        }
        const auto receiver_id = make_video_receiver_resource_id(id);
        const auto found_receiver = boost::range::find(receiver_ids_, receiver_id);
        if (receiver_ids_.end() != found_receiver)
        {
          receiver_ids_.erase(found_receiver);
        }

        remove_resource_after(delay_millis, node_model_.node_resources, receiver_id,
                              *gate_, lock);
        remove_resource_after(delay_millis, node_model_.connection_resources,
                              receiver_id, *gate_, lock);
        remove_video_receiver_by_id(id);
        nmos::modify_resource(node_model_.node_resources, device_id_, ([&](nmos::resource &device)
                                                                       {
                                                                         device.data[nmos::fields::receivers] = value_from_elements(receiver_ids_);
                                                                         device.data[nmos::fields::version] = value(nmos::make_version()); }));
      }

      void remove_audio_receiver(std::string id)
      {
        nmos::write_lock lock = node_model_.write_lock();
        std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
        AudioReceiver *audio = find_audio_receiver_by_id(id);
        if (!audio)
        {
          slog::log<slog::severities::error>(*gate_, SLOG_FLF)
              << nmos::stash_category(impl::categories::node_implementation)
              << "remove audio receiver not found audio receiver id:" << id;
          return;
        }
        const auto receiver_id = make_audio_receiver_resource_id(id);
        const auto found_receiver = boost::range::find(receiver_ids_, receiver_id);
        if (receiver_ids_.end() != found_receiver)
        {
          receiver_ids_.erase(found_receiver);
        }
        remove_resource_after(delay_millis, node_model_.node_resources, receiver_id,
                              *gate_, lock);
        remove_resource_after(delay_millis, node_model_.connection_resources,
                              receiver_id, *gate_, lock);
        remove_audio_receiver_by_id(id);
        nmos::modify_resource(node_model_.node_resources, device_id_, ([&](nmos::resource &device)
                                                                       {
                                                                         device.data[nmos::fields::receivers] = value_from_elements(receiver_ids_);
                                                                         device.data[nmos::fields::version] = value(nmos::make_version()); }));
      }
      void remove_ancillary_receiver(std::string id)
      {
        nmos::write_lock lock = node_model_.write_lock();
        std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
        AncillaryReceiver *ancillary = find_ancillary_receiver_by_id(id);
        if (!ancillary)
        {
          slog::log<slog::severities::error>(*gate_, SLOG_FLF)
              << nmos::stash_category(impl::categories::node_implementation)
              << "remove ancillary receiver not found ancillary receiver id:" << id;
          return;
        }
        const auto receiver_id = make_ancillary_receiver_resource_id(id);
        const auto found_receiver = boost::range::find(receiver_ids_, receiver_id);
        if (receiver_ids_.end() != found_receiver)
        {
          receiver_ids_.erase(found_receiver);
        }
        remove_resource_after(delay_millis, node_model_.node_resources, receiver_id,
                              *gate_, lock);
        remove_resource_after(delay_millis, node_model_.connection_resources,
                              receiver_id, *gate_, lock);
        remove_ancillary_receiver_by_id(id);
        nmos::modify_resource(node_model_.node_resources, device_id_, ([&](nmos::resource &device)
                                                                       {
                                                                         device.data[nmos::fields::receivers] = value_from_elements(receiver_ids_);
                                                                         device.data[nmos::fields::version] = value(nmos::make_version()); }));
      }
      void update_video_sender(VideoSender video)
      {
        remove_video_sender(video.id);
        add_video_sender(video);
      }
      void update_audio_sender(AudioSender audio)
      {
        remove_audio_sender(audio.id);
        add_audio_sender(audio);
      }

      void update_video_receiver(VideoReceiver video)
      {
        remove_video_receiver(video.id);
        add_video_receiver(video);
      }
      void update_audio_receiver(AudioReceiver audio)
      {
        remove_audio_receiver(audio.id);
        add_audio_receiver(audio);
      }

      void update_ancillary_sender(AncillarySender ancillary)
      {
        remove_ancillary_sender(ancillary.id);
        add_ancillary_sender(ancillary);
      }

      void update_ancillary_receiver(AncillaryReceiver ancillary)
      {
        remove_ancillary_receiver(ancillary.id);
        add_ancillary_receiver(ancillary);
      }

      void set_update_video_receiver_callback(
          std::function<void(const VideoReceiver &video)> func)
      {
        std::lock_guard<std::mutex> callback_lock(callback_mutex_);
        update_video_receiver_func = std::move(func);
      }

      void set_update_audio_receiver_callback(
          std::function<void(const AudioReceiver &audio)> func)
      {
        std::lock_guard<std::mutex> callback_lock(callback_mutex_);
        update_audio_receiver_func = std::move(func);
      }

      void set_update_ancillary_receiver_callback(
          std::function<void(const AncillaryReceiver &ancillary)> func)
      {
        std::lock_guard<std::mutex> callback_lock(callback_mutex_);
        update_ancillary_receiver_func = std::move(func);
      }

      void set_registration_changed_callback(
          std::function<void(const RegistrationStatus &status)> func)
      {
        std::lock_guard<std::mutex> callback_lock(callback_mutex_);
        registration_changed_func = std::move(func);
      }

      void set_runtime_interfaces(
          const web::hosts::experimental::host_interface &primary,
          const web::hosts::experimental::host_interface &secondary)
      {
        nmos::write_lock lock = node_model_.write_lock();
        primary_interface = primary;
        secondary_interface = secondary;

        for (const auto &sender_id : sender_ids_)
        {
          auto sender = nmos::find_resource(node_model_.node_resources,
                                            {sender_id, nmos::types::sender});
          if (node_model_.node_resources.end() == sender)
          {
            continue;
          }

          nmos::modify_resource(node_model_.connection_resources, sender_id,
                                [&](nmos::resource &connection_sender)
                                {
                                  auto &endpoint_transportfile =
                                      connection_sender.data[nmos::fields::endpoint_transportfile];
                                  set_transportfile(*sender, connection_sender,
                                                    endpoint_transportfile);
                                });
        }
      }

      web::json::value effective_settings() const
      {
        auto lock = node_model_.read_lock();
        return node_model_.settings;
      }

      web::json::value persisted_settings() const
      {
        if (config_file_.empty())
        {
          return web::json::value::object();
        }
        return parse_json_file(config_file_);
      }

      web::json::value discover_registration_apis() const
      {
        const auto settings = effective_settings();
        mdns::service_discovery discovery(*gate_);
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

      void write_persisted_settings(const web::json::value &settings)
      {
        if (config_file_.empty())
        {
          throw std::runtime_error("node config file path is empty");
        }
        write_json_file(config_file_, settings);
      }

      AudioSender *find_audio_sender_by_id(std::string id)
      {
        if (audio_senders.empty())
        {
          return nullptr;
        }
        for (auto &audio : audio_senders)
        {
          if (audio.id == id)
          {
            return &audio;
          }
        }
        return nullptr;
      }

      AudioSender *find_audio_sender_by_sender_id(std::string id)
      {
        if (audio_senders.empty())
        {
          return nullptr;
        }
        for (auto &audio : audio_senders)
        {
          if (audio.sender_id == id)
          {
            return &audio;
          }
        }
        return nullptr;
      }

      void remove_audio_sender_by_sender_id(std::string id)
      {
        if (audio_senders.empty())
        {
          return;
        }
        audio_senders.erase(
            std::remove_if(audio_senders.begin(), audio_senders.end(),
                           [&](const AudioSender &a)
                           { return a.sender_id == id; }),
            audio_senders.end());
      }

      AncillarySender *find_ancillary_sender_by_id(std::string id)
      {
        if (ancillary_senders.empty())
        {
          return nullptr;
        }
        for (auto &ancillary : ancillary_senders)
        {
          if (ancillary.id == id)
          {
            return &ancillary;
          }
        }
        return nullptr;
      }

      AncillarySender *find_ancillary_sender_by_sender_id(std::string id)
      {
        if (ancillary_senders.empty())
        {
          return nullptr;
        }
        for (auto &ancillary : ancillary_senders)
        {
          if (ancillary.sender_id == id)
          {
            return &ancillary;
          }
        }
        return nullptr;
      }

      void remove_ancillary_sender_by_sender_id(std::string id)
      {
        if (ancillary_senders.empty())
        {
          return;
        }
        ancillary_senders.erase(
            std::remove_if(ancillary_senders.begin(), ancillary_senders.end(),
                           [&](const AncillarySender &a)
                           {
                             return a.sender_id == id;
                           }),
            ancillary_senders.end());
      }

      VideoSender *find_video_sender_by_id(std::string id)
      {
        if (video_senders.empty())
        {
          return nullptr;
        }
        for (auto &video : video_senders)
        {
          if (video.id == id)
          {
            return &video;
          }
        }
        return nullptr;
      }

      VideoSender *find_video_sender_by_sender_id(std::string id)
      {
        if (video_senders.empty())
        {
          return nullptr;
        }
        for (auto &video : video_senders)
        {
          if (video.sender_id == id)
          {
            return &video;
          }
        }
        return nullptr;
      }

      void remove_video_sender_by_sender_id(std::string id)
      {
        if (video_senders.empty())
        {
          return;
        }
        video_senders.erase(
            std::remove_if(video_senders.begin(), video_senders.end(),
                           [&](const VideoSender &v)
                           { return v.sender_id == id; }),
            video_senders.end());
      }

      AudioReceiver *find_audio_receiver_by_id(std::string id)
      {
        if (audio_receivers.empty())
        {
          return nullptr;
        }
        for (auto &audio : audio_receivers)
        {
          if (audio.id == id)
          {
            return &audio;
          }
        }
        return nullptr;
      }

      AudioReceiver *find_audio_receiver_by_resource_id(const nmos::id &id)
      {
        if (audio_receivers.empty())
        {
          return nullptr;
        }
        for (auto &audio : audio_receivers)
        {
          if (make_audio_receiver_resource_id(audio.id) == id)
          {
            return &audio;
          }
        }
        return nullptr;
      }
      void remove_audio_receiver_by_id(std::string id)
      {
        if (audio_receivers.empty())
        {
          return;
        }
        audio_receivers.erase(std::remove_if(audio_receivers.begin(),
                                             audio_receivers.end(),
                                             [&](const AudioReceiver &a)
                                             {
                                               return a.id == id;
                                             }),
                              audio_receivers.end());
      }

      AncillaryReceiver *find_ancillary_receiver_by_id(std::string id)
      {
        if (ancillary_receivers.empty())
        {
          return nullptr;
        }
        for (auto &ancillary : ancillary_receivers)
        {
          if (ancillary.id == id)
          {
            return &ancillary;
          }
        }
        return nullptr;
      }

      AncillaryReceiver *find_ancillary_receiver_by_resource_id(const nmos::id &id)
      {
        if (ancillary_receivers.empty())
        {
          return nullptr;
        }
        for (auto &ancillary : ancillary_receivers)
        {
          if (make_ancillary_receiver_resource_id(ancillary.id) == id)
          {
            return &ancillary;
          }
        }
        return nullptr;
      }

      void remove_ancillary_receiver_by_id(std::string id)
      {
        if (ancillary_receivers.empty())
        {
          return;
        }
        ancillary_receivers.erase(
            std::remove_if(ancillary_receivers.begin(), ancillary_receivers.end(),
                           [&](const AncillaryReceiver &a)
                           {
                             return a.id == id;
                           }),
            ancillary_receivers.end());
      }

      VideoReceiver *find_video_receiver_by_id(std::string id)
      {
        if (video_receivers.empty())
        {
          return nullptr;
        }
        for (auto &video : video_receivers)
        {
          if (video.id == id)
          {
            return &video;
          }
        }
        return nullptr;
      }

      VideoReceiver *find_video_receiver_by_resource_id(const nmos::id &id)
      {
        if (video_receivers.empty())
        {
          return nullptr;
        }
        for (auto &video : video_receivers)
        {
          if (make_video_receiver_resource_id(video.id) == id)
          {
            return &video;
          }
        }
        return nullptr;
      }
      void remove_video_receiver_by_id(std::string id)
      {
        if (video_receivers.empty())
        {
          return;
        }
        video_receivers.erase(std::remove_if(video_receivers.begin(),
                                             video_receivers.end(),
                                             [&](const VideoReceiver &v)
                                             {
                                               return v.id == id;
                                             }),
                              video_receivers.end());
      }

      void update_subscription_for_pair(const nmos::id &receiver_resource_id,
                                        const nmos::id &sender_id)
      {
        if (receiver_resource_id.empty() || sender_id.empty())
        {
          return;
        }
        const auto activation_time = nmos::tai_now();
        bool updated = nmos::modify_resource(
            node_model_.node_resources, receiver_resource_id,
            [&](nmos::resource &resource)
            {
              nmos::set_resource_subscription(resource, true, sender_id,
                                              activation_time);
            });
        updated = nmos::modify_resource(
                      node_model_.node_resources, sender_id,
                      [&](nmos::resource &resource)
                      {
                        nmos::set_resource_subscription(resource, true, receiver_resource_id,
                                                        activation_time);
                      }) ||
                  updated;
        if (updated)
        {
          node_model_.notify();
        }
      }

      // void try_bind_video_receiver_sender(const VideoReceiver &video)
      // {
      //   {
      //     return;
      //   }
      //   auto sender = std::find_if(
      //       video_senders.begin(), video_senders.end(), [&](const VideoSender &s)
      //       { return s.ip == video.ip && s.port == video.port &&
      //                s.source_ip == video.source_ip && !s.sender_id.empty(); });
      //   if (video_senders.end() == sender)
      //   {
      //     return;
      //   }
      // }

      // void try_bind_audio_receiver_sender(const AudioReceiver &audio)
      // {
      //   {
      //     return;
      //   }
      //   auto sender = std::find_if(
      //       audio_senders.begin(), audio_senders.end(), [&](const AudioSender &s)
      //       { return s.ip == audio.ip && s.port == audio.port &&
      //                s.source_ip == audio.source_ip && !s.sender_id.empty(); });
      //   if (audio_senders.end() == sender)
      //   {
      //     return;
      //   }
      // }

      // void try_bind_ancillary_receiver_sender(const AncillaryReceiver &ancillary)
      // {
      //   {
      //     return;
      //   }
      //   auto sender = std::find_if(
      //       ancillary_senders.begin(), ancillary_senders.end(),
      //       [&](const AncillarySender &s)
      //       {
      //         return s.ip == ancillary.ip && s.port == ancillary.port &&
      //                s.source_ip == ancillary.source_ip && !s.sender_id.empty();
      //       });
      //   if (ancillary_senders.end() == sender)
      //   {
      //     return;
      //   }
      // }

      // void try_bind_video_sender_receiver(const VideoSender &video)
      // {
      //   if (video.ip.empty() || video.port <= 0 || video.sender_id.empty())
      //   {
      //     return;
      //   }
      //   std::optional<VideoReceiver> receiver_snapshot;
      //   {
      //     std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
      //     auto receiver = std::find_if(video_receivers.begin(),
      //                                  video_receivers.end(),
      //                                  [&](const VideoReceiver &r)
      //                                  {
      //                                    return r.ip == video.ip &&
      //                                           r.port == video.port &&
      //                                           r.source_ip == video.source_ip &&
      //                                  });
      //     if (video_receivers.end() != receiver)
      //     {
      //       receiver_snapshot = *receiver;
      //     }
      //   }
      //   if (!receiver_snapshot)
      //   {
      //     return;
      //   }
      //   update_subscription_for_pair(make_video_receiver_resource_id(receiver_snapshot->id),
      //                                utility::s2us(video.sender_id));
      // }

      // void try_bind_audio_sender_receiver(const AudioSender &audio)
      // {
      //   if (audio.ip.empty() || audio.port <= 0 || audio.sender_id.empty())
      //   {
      //     return;
      //   }
      //   std::optional<AudioReceiver> receiver_snapshot;
      //   {
      //     std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
      //     auto receiver = std::find_if(audio_receivers.begin(),
      //                                  audio_receivers.end(),
      //                                  [&](const AudioReceiver &r)
      //                                  {
      //                                    return r.ip == audio.ip &&
      //                                           r.port == audio.port &&
      //                                           r.source_ip == audio.source_ip &&
      //                                  });
      //     if (audio_receivers.end() != receiver)
      //     {
      //       receiver_snapshot = *receiver;
      //     }
      //   }
      //   if (!receiver_snapshot)
      //   {
      //     return;
      //   }
      //   update_subscription_for_pair(make_audio_receiver_resource_id(receiver_snapshot->id),
      //                                utility::s2us(audio.sender_id));
      // }

      // void try_bind_ancillary_sender_receiver(const AncillarySender &ancillary)
      // {
      //   if (ancillary.ip.empty() || ancillary.port <= 0 ||
      //       ancillary.sender_id.empty())
      //   {
      //     return;
      //   }
      //   std::optional<AncillaryReceiver> receiver_snapshot;
      //   {
      //     std::lock_guard<std::mutex> receiver_lock(receiver_mutex_);
      //     auto receiver = std::find_if(ancillary_receivers.begin(),
      //                                  ancillary_receivers.end(),
      //                                  [&](const AncillaryReceiver &r)
      //                                  {
      //                                    return r.ip == ancillary.ip &&
      //                                           r.port == ancillary.port &&
      //                                           r.source_ip == ancillary.source_ip &&
      //                                  });
      //     if (ancillary_receivers.end() != receiver)
      //     {
      //       receiver_snapshot = *receiver;
      //     }
      //   }
      //   if (!receiver_snapshot)
      //   {
      //     return;
      //   }
      //   update_subscription_for_pair(make_ancillary_receiver_resource_id(receiver_snapshot->id),
      //                                utility::s2us(ancillary.sender_id));
      // }
    };

    Node::Node(std::string node_config_path_) : p_impl(new Impl)
    {
      p_impl->config_file_ = node_config_path_;
    }
    Node::~Node() {}
    bool Node::start() { return p_impl->start(); }
    bool Node::stop() { return p_impl->stop(); }

    void Node::add_video_sender(VideoSender video)
    {
      p_impl->add_video_sender(video);
    }
    void Node::add_audio_sender(AudioSender audio)
    {
      p_impl->add_audio_sender(audio);
    }
    void Node::add_ancillary_sender(AncillarySender ancillary)
    {
      p_impl->add_ancillary_sender(ancillary);
    }

    void Node::remove_video_sender(std::string id)
    {
      p_impl->remove_video_sender(id);
    }
    void Node::remove_audio_sender(std::string id)
    {
      p_impl->remove_audio_sender(id);
    }
    void Node::remove_ancillary_sender(std::string id)
    {
      p_impl->remove_ancillary_sender(id);
    }

    void Node::add_video_receiver(VideoReceiver video)
    {
      p_impl->add_video_receiver(video);
    }
    void Node::add_audio_receiver(AudioReceiver audio)
    {
      p_impl->add_audio_receiver(audio);
    }
    void Node::add_ancillary_receiver(AncillaryReceiver ancillary)
    {
      p_impl->add_ancillary_receiver(ancillary);
    }

    void Node::remove_video_receiver(std::string id)
    {
      p_impl->remove_video_receiver(id);
    }
    void Node::remove_audio_receiver(std::string id)
    {
      p_impl->remove_audio_receiver(id);
    }
    void Node::remove_ancillary_receiver(std::string id)
    {
      p_impl->remove_ancillary_receiver(id);
    }

    void Node::set_update_video_receiver_callback(
        std::function<void(const VideoReceiver &video)> func)
    {
      p_impl->set_update_video_receiver_callback(std::move(func));
    }

    void Node::set_update_audio_receiver_callback(
        std::function<void(const AudioReceiver &audio)> func)
    {
      p_impl->set_update_audio_receiver_callback(std::move(func));
    }
    void Node::set_update_ancillary_receiver_callback(
        std::function<void(const AncillaryReceiver &ancillary)> func)
    {
      p_impl->set_update_ancillary_receiver_callback(std::move(func));
    }
    void Node::set_registration_changed_callback(
        std::function<void(const RegistrationStatus &status)> func)
    {
      p_impl->set_registration_changed_callback(std::move(func));
    }
    web::json::value Node::effective_settings() const
    {
      return p_impl->effective_settings();
    }
    web::json::value Node::persisted_settings() const
    {
      return p_impl->persisted_settings();
    }
    web::json::value Node::discover_registration_apis() const
    {
      return p_impl->discover_registration_apis();
    }
    void Node::write_persisted_settings(const web::json::value &settings)
    {
      p_impl->write_persisted_settings(settings);
    }
    void Node::set_runtime_interfaces(
        const web::hosts::experimental::host_interface &primary,
        const web::hosts::experimental::host_interface &secondary)
    {
      p_impl->set_runtime_interfaces(primary, secondary);
    }
    void Node::set_ptp_clock(std::string gmtid, bool locked)
    {
      p_impl->set_ptp_clock(gmtid, locked);
    }

  } // namespace nmos_node
} // namespace seeder
