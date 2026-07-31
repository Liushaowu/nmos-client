#include "node_activation_context.h"
#include "node_connection_transport_params.h"
#include "node_implementation.h"
#include "node_sdp_service.h"

#include <cpprest/details/basic_types.h>
#include <nmos/json_fields.h>
#include <nmos/type.h>
#include <slog/all_in_one.h>

#include <optional>

namespace seeder::nmos_node::internal
{
  nmos::connection_activation_handler make_activation_handler(
      ActivationContext ctx)
  {
    return [ctx](const nmos::resource &resource,
                  const nmos::resource &connection_resource)
    {
      const std::pair<nmos::id, nmos::type> id_type{resource.id, resource.type};
      const std::pair<nmos::id, nmos::type> id__type{connection_resource.id,
                                                       connection_resource.type};
      const auto &endpoint_active =
          nmos::fields::endpoint_active(connection_resource.data);
      const auto &transport_params_value =
          nmos::fields::transport_params(endpoint_active);
      const auto transport_params_state =
          connection_transport_params::state(transport_params_value);
      if (connection_transport_params::State::not_array == transport_params_state)
      {
        slog::log<slog::severities::error>(*ctx.gate, SLOG_FLF)
            << nmos::stash_category(impl::categories::node_implementation)
            << "connection activation ignored: transport_params is not array for "
            << id_type;
        return;
      }
      const web::json::array transport_params = transport_params_value.as_array();
      if (connection_transport_params::State::empty == transport_params_state)
      {
        slog::log<slog::severities::error>(*ctx.gate, SLOG_FLF)
            << nmos::stash_category(impl::categories::node_implementation)
            << "connection activation ignored: transport_params is empty for "
            << id_type;
        return;
      }
      bool master_enable = nmos::fields::master_enable(endpoint_active);
      const bool stream_enable = master_enable &&
                                 NodeSdpService::transport_param_rtp_enabled(
                                     transport_params.at(0), false);
      std::string connection_resource_json =
          utility::conversions::to_utf8string(connection_resource.data.serialize());
      std::error_code ec{};
      const bool has_secondary_transport_param = transport_params.size() > 1;

      // ---- generic lambda: fill common transport fields into any item ----
      auto fill_snapshot = [&](auto *item, const std::string &primary_ip,
                                const std::string &primary_dest_ip, int primary_port)
      {
        if (!item) return;
        item->enable = stream_enable;
        item->source_ip = primary_ip;
        item->ip = primary_dest_ip;
        item->port = primary_port;
        if (item->redundancy.present && has_secondary_transport_param)
        {
          item->redundancy.enable = false;
          item->redundancy.source_ip = "";
          item->redundancy.ip = "";
          item->redundancy.port = 5004;
        }
      };

      // ---- generic lambda: dispatch a callback with snapshot ----
      auto dispatch_callback = [&](const auto &snapshot, auto callback,
                                    const char *name)
      {
        if (!snapshot) return;
        if (callback)
        {
          callback(*snapshot);
        }
        else
        {
          slog::log<slog::severities::error>(*ctx.gate, SLOG_FLF)
              << nmos::stash_category(impl::categories::node_implementation)
              << "update " << name << " callback failed: function not set";
        }
      };

      if (resource.type == nmos::types::receiver)
      {
        const auto &transport_file = nmos::fields::transport_file(endpoint_active);
        int dest_port =
            nmos::fields::destination_port(transport_params.at(0)).as_integer();
        std::string interface_ip = utility::us2s(
            nmos::fields::interface_ip(transport_params.at(0)).as_string());
        std::string multicast_ip = utility::us2s(
            nmos::fields::multicast_ip(transport_params.at(0)).as_string());
        std::string source_ip = utility::us2s(
            nmos::fields::source_ip(transport_params.at(0)).as_string());

        int dest_port_07 = 5004;
        std::string interface_ip_07 = "";
        std::string multicast_ip_07 = "";
        std::string source_ip_07 = "";
        bool redundancy_enable = false;
        if (has_secondary_transport_param)
        {
          dest_port_07 = nmos::fields::destination_port(transport_params.at(1))
                             .as_integer();
          interface_ip_07 = utility::us2s(
              nmos::fields::interface_ip(transport_params.at(1)).as_string());
          multicast_ip_07 = utility::us2s(
              nmos::fields::multicast_ip(transport_params.at(1)).as_string());
          source_ip_07 = utility::us2s(
              nmos::fields::source_ip(transport_params.at(1)).as_string());
          redundancy_enable = NodeSdpService::transport_param_rtp_enabled(
              transport_params.at(1), false);
        }

        std::optional<VideoReceiver> video_snapshot;
        std::optional<AudioReceiver> audio_snapshot;
        std::optional<AncillaryReceiver> ancillary_snapshot;
        CallbackDispatcher::ReceiverCallbacks callbacks;

        {
          std::lock_guard<std::mutex> receiver_lock(ctx.receiver_mutex);
          VideoReceiver *video =
              ctx.stream_store.find_video_receiver_by_resource_id(resource.id);
          AudioReceiver *audio =
              ctx.stream_store.find_audio_receiver_by_resource_id(resource.id);
          AncillaryReceiver *ancillary =
              ctx.stream_store.find_ancillary_receiver_by_resource_id(resource.id);

          fill_snapshot(video, interface_ip, multicast_ip, dest_port);
          if (video && video->redundancy.present && has_secondary_transport_param)
          {
            video->redundancy.enable = redundancy_enable;
            video->redundancy.source_ip = interface_ip_07;
            video->redundancy.ip = multicast_ip_07;
            video->redundancy.port = dest_port_07;
          }
          if (video)
          {
            NodeSdpService::update_video_receiver_from_transport_file(
                *video, transport_file, *ctx.gate);
            video_snapshot = *video;
          }

          fill_snapshot(audio, interface_ip, multicast_ip, dest_port);
          if (audio && audio->redundancy.present && has_secondary_transport_param)
          {
            audio->redundancy.enable = redundancy_enable;
            audio->redundancy.source_ip = interface_ip_07;
            audio->redundancy.ip = multicast_ip_07;
            audio->redundancy.port = dest_port_07;
          }
          if (audio)
          {
            NodeSdpService::update_audio_receiver_from_transport_file(
                *audio, transport_file, *ctx.gate);
            audio_snapshot = *audio;
          }

          fill_snapshot(ancillary, interface_ip, multicast_ip, dest_port);
          if (ancillary && ancillary->redundancy.present && has_secondary_transport_param)
          {
            ancillary->redundancy.enable = redundancy_enable;
            ancillary->redundancy.source_ip = interface_ip_07;
            ancillary->redundancy.ip = multicast_ip_07;
            ancillary->redundancy.port = dest_port_07;
          }
          ancillary_snapshot = ancillary ? std::optional(*ancillary) : std::nullopt;
        }

        callbacks = ctx.callbacks.receiver_callbacks();

        dispatch_callback(video_snapshot, callbacks.video, "video receiver");
        dispatch_callback(audio_snapshot, callbacks.audio, "audio receiver");
        dispatch_callback(ancillary_snapshot, callbacks.ancillary, "ancillary receiver");
      }
      else if (resource.type == nmos::types::sender)
      {
        int dest_port = 0;
        std::string destination_ip;
        std::string source_ip;

        int dest_port_07 = 5004;
        std::string destination_ip_07 = "";
        std::string source_ip_07 = "";
        bool redundancy_enable = false;
        try
        {
          dest_port =
              nmos::fields::destination_port(transport_params.at(0)).as_integer();
          destination_ip = utility::us2s(
              nmos::fields::destination_ip(transport_params.at(0)).as_string());
          source_ip = utility::us2s(
              nmos::fields::source_ip(transport_params.at(0)).as_string());
          if (dest_port < 0 || 65535 < dest_port)
          {
            throw std::runtime_error("destination_port out of range");
          }

          if (has_secondary_transport_param)
          {
            dest_port_07 = nmos::fields::destination_port(transport_params.at(1))
                               .as_integer();
            destination_ip_07 = utility::us2s(
                nmos::fields::destination_ip(transport_params.at(1)).as_string());
            source_ip_07 = utility::us2s(
                nmos::fields::source_ip(transport_params.at(1)).as_string());
            redundancy_enable = NodeSdpService::transport_param_rtp_enabled(
                transport_params.at(1), false);
            if (dest_port_07 < 0 || 65535 < dest_port_07)
            {
              throw std::runtime_error("secondary destination_port out of range");
            }
          }
        }
        catch (const std::exception &error)
        {
          slog::log<slog::severities::error>(*ctx.gate, SLOG_FLF)
              << nmos::stash_category(impl::categories::node_implementation)
              << "connection activation ignored: invalid sender transport_params for "
              << id_type << ": " << error.what();
          return;
        }

        std::optional<VideoSender> video_snapshot;
        std::optional<AudioSender> audio_snapshot;
        std::optional<AncillarySender> ancillary_snapshot;
        CallbackDispatcher::SenderCallbacks callbacks;

        {
          std::lock_guard<std::mutex> sender_lock(ctx.sender_mutex);
          const auto sender_id = utility::us2s(resource.id);
          VideoSender *video =
              ctx.stream_store.find_video_sender_by_sender_id(sender_id);
          AudioSender *audio =
              ctx.stream_store.find_audio_sender_by_sender_id(sender_id);
          AncillarySender *ancillary =
              ctx.stream_store.find_ancillary_sender_by_sender_id(sender_id);

          fill_snapshot(video, source_ip, destination_ip, dest_port);
          if (video && video->redundancy.present && has_secondary_transport_param)
          {
            video->redundancy.enable = redundancy_enable;
            video->redundancy.source_ip = source_ip_07;
            video->redundancy.ip = destination_ip_07;
            video->redundancy.port = dest_port_07;
          }
          video_snapshot = video ? std::optional(*video) : std::nullopt;

          fill_snapshot(audio, source_ip, destination_ip, dest_port);
          if (audio && audio->redundancy.present && has_secondary_transport_param)
          {
            audio->redundancy.enable = redundancy_enable;
            audio->redundancy.source_ip = source_ip_07;
            audio->redundancy.ip = destination_ip_07;
            audio->redundancy.port = dest_port_07;
          }
          audio_snapshot = audio ? std::optional(*audio) : std::nullopt;

          fill_snapshot(ancillary, source_ip, destination_ip, dest_port);
          if (ancillary && ancillary->redundancy.present && has_secondary_transport_param)
          {
            ancillary->redundancy.enable = redundancy_enable;
            ancillary->redundancy.source_ip = source_ip_07;
            ancillary->redundancy.ip = destination_ip_07;
            ancillary->redundancy.port = dest_port_07;
          }
          ancillary_snapshot = ancillary ? std::optional(*ancillary) : std::nullopt;
        }

        callbacks = ctx.callbacks.sender_callbacks();

        dispatch_callback(video_snapshot, callbacks.video, "video sender");
        dispatch_callback(audio_snapshot, callbacks.audio, "audio sender");
        dispatch_callback(ancillary_snapshot, callbacks.ancillary, "ancillary sender");
      }

      if (ec)
      {
        slog::log<slog::severities::error>(*ctx.gate, SLOG_FLF)
            << nmos::stash_category(impl::categories::node_implementation)
            << "Activating  " << id_type << "   Error: " << ec.message();
      }

      slog::log<slog::severities::info>(*ctx.gate, SLOG_FLF)
          << nmos::stash_category(impl::categories::node_implementation)
          << "Activating " << id_type;
    };
  }
}