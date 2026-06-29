#include "node_sdp_service.h"

#include "daemon/video_format.h"
#include "node_implementation.h"

#include <arpa/inet.h>
#include <cmath>
#include <cpprest/basic_utils.h>
#include <nmos/sdp_utils.h>
#include <sdp/json.h>

namespace seeder::nmos_node::internal
{
  namespace
  {
    std::string to_utf8_string(const utility::string_t &value)
    {
      return utility::conversions::to_utf8string(value);
    }

    bool is_valid_ip_literal(const std::string &value)
    {
      in_addr ipv4{};
      if (1 == inet_pton(AF_INET, value.c_str(), &ipv4))
      {
        return true;
      }

      in6_addr ipv6{};
      return 1 == inet_pton(AF_INET6, value.c_str(), &ipv6);
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
      if (0 == params.width || 0 == params.height ||
          0 == params.exactframerate.denominator())
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
  }

  web::json::value NodeSdpService::transport_param_with_valid_destination_ip(
      const web::json::value &transport_param)
  {
    auto sanitized = transport_param;
    if (!sanitized.is_object())
    {
      return sanitized;
    }

    if (!sanitized.has_field(nmos::fields::destination_ip))
    {
      sanitized[nmos::fields::destination_ip] =
          web::json::value::string(U("0.0.0.0"));
      return sanitized;
    }

    const auto &destination_ip = sanitized.at(nmos::fields::destination_ip);
    if (!destination_ip.is_string() ||
        !is_valid_ip_literal(to_utf8_string(destination_ip.as_string())))
    {
      sanitized[nmos::fields::destination_ip] =
          web::json::value::string(U("0.0.0.0"));
    }
    return sanitized;
  }

  bool NodeSdpService::transport_param_rtp_enabled(
      const web::json::value &transport_param,
      const bool fallback)
  {
    if (!transport_param.is_object() ||
        !transport_param.has_field(nmos::fields::rtp_enabled))
    {
      return fallback;
    }

    const auto &rtp_enabled = transport_param.at(nmos::fields::rtp_enabled);
    return rtp_enabled.is_boolean() ? rtp_enabled.as_bool() : fallback;
  }

  bool NodeSdpService::update_video_receiver_from_transport_file(
      VideoReceiver &receiver,
      const web::json::value &transport_file,
      slog::base_gate &gate)
  {
    if (!transport_file.is_object() ||
        !transport_file.has_field(nmos::fields::transportfile_type) ||
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

  bool NodeSdpService::update_audio_receiver_from_transport_file(
      AudioReceiver &receiver,
      const web::json::value &transport_file,
      slog::base_gate &gate)
  {
    if (!transport_file.is_object() ||
        !transport_file.has_field(nmos::fields::transportfile_type) ||
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
        receiver.sample_rate = static_cast<int>(audio.sample_rate);
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
}
