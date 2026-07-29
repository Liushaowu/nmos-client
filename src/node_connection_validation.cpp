#include "node_connection_validation.h"

#include "node_connection_transport_params.h"
#include "node_implementation.h"
#include "node_sdp_service.h"

#include <cpprest/details/basic_types.h>
#include <cpprest/json.h>
#include <nmos/activation_mode.h>
#include <nmos/json_fields.h>
#include <nmos/type.h>
#include <slog/all_in_one.h>

#include <optional>
#include <stdexcept>
#include <string>

namespace seeder::nmos_node::internal
{
  namespace
  {
    bool is_immediate_activation(const web::json::value &endpoint_staged)
    {
      const auto &activation = nmos::fields::activation(endpoint_staged);
      const auto &mode = nmos::fields::mode(activation);
      return mode.is_string() &&
             mode.as_string() == nmos::activation_modes::activate_immediate.name;
    }

    void log_validation_no_handler(ActivationContext ctx,
                                   const nmos::resource &resource)
    {
      if (!ctx.gate)
      {
        return;
      }
      slog::log<slog::severities::warning>(*ctx.gate, SLOG_FLF)
          << nmos::stash_category(impl::categories::node_implementation)
          << "receiver connection validation allowed without handler for "
          << resource.id;
    }

    web::json::array require_transport_params(
        const web::json::value &endpoint_staged,
        const nmos::resource &resource)
    {
      const auto &transport_params_value =
          nmos::fields::transport_params(endpoint_staged);
      const auto transport_params_state =
          connection_transport_params::state(transport_params_value);
      if (connection_transport_params::State::not_array == transport_params_state)
      {
        throw web::json::json_exception(
            "invalid receiver activation transport_params for " +
            utility::conversions::to_utf8string(resource.id) + ": not an array");
      }
      if (connection_transport_params::State::empty == transport_params_state)
      {
        throw web::json::json_exception(
            "invalid receiver activation transport_params for " +
            utility::conversions::to_utf8string(resource.id) + ": empty array");
      }
      return transport_params_value.as_array();
    }

    template <typename Receiver>
    void apply_primary_receiver_params(Receiver &receiver,
                                       const web::json::array &transport_params)
    {
      receiver.source_ip = utility::us2s(
          nmos::fields::interface_ip(transport_params.at(0)).as_string());
      receiver.ip = utility::us2s(
          nmos::fields::multicast_ip(transport_params.at(0)).as_string());
      receiver.port =
          nmos::fields::destination_port(transport_params.at(0)).as_integer();
    }

    template <typename Receiver>
    void apply_secondary_receiver_params(Receiver &receiver,
                                         const web::json::array &transport_params,
                                         bool redundancy_enable)
    {
      if (!receiver.redundancy.present || transport_params.size() < 2)
      {
        return;
      }

      receiver.redundancy.enable = redundancy_enable;
      receiver.redundancy.source_ip = utility::us2s(
          nmos::fields::interface_ip(transport_params.at(1)).as_string());
      receiver.redundancy.ip = utility::us2s(
          nmos::fields::multicast_ip(transport_params.at(1)).as_string());
      receiver.redundancy.port =
          nmos::fields::destination_port(transport_params.at(1)).as_integer();
    }

    void apply_receiver_params(VideoReceiver &receiver,
                               const web::json::value &endpoint_staged,
                               const web::json::array &transport_params,
                               bool stream_enable,
                               bool redundancy_enable,
                               slog::base_gate &gate)
    {
      receiver.enable = stream_enable;
      apply_primary_receiver_params(receiver, transport_params);
      apply_secondary_receiver_params(receiver, transport_params,
                                      redundancy_enable);
      NodeSdpService::update_video_receiver_from_transport_file(
          receiver, nmos::fields::transport_file(endpoint_staged), gate);
    }

    void apply_receiver_params(AudioReceiver &receiver,
                               const web::json::value &endpoint_staged,
                               const web::json::array &transport_params,
                               bool stream_enable,
                               bool redundancy_enable,
                               slog::base_gate &gate)
    {
      receiver.enable = stream_enable;
      apply_primary_receiver_params(receiver, transport_params);
      apply_secondary_receiver_params(receiver, transport_params,
                                      redundancy_enable);
      NodeSdpService::update_audio_receiver_from_transport_file(
          receiver, nmos::fields::transport_file(endpoint_staged), gate);
    }

    void apply_receiver_params(AncillaryReceiver &receiver,
                               const web::json::value &,
                               const web::json::array &transport_params,
                               bool stream_enable,
                               bool redundancy_enable,
                               slog::base_gate &)
    {
      receiver.enable = stream_enable;
      apply_primary_receiver_params(receiver, transport_params);
      apply_secondary_receiver_params(receiver, transport_params,
                                      redundancy_enable);
    }

    std::optional<ReceiverEvent> make_receiver_event_snapshot(
        ActivationContext ctx,
        const nmos::resource &resource,
        const web::json::value &endpoint_staged,
        const web::json::array &transport_params,
        slog::base_gate &gate)
    {
      const bool master_enable = nmos::fields::master_enable(endpoint_staged);
      const bool stream_enable = master_enable &&
                                 NodeSdpService::transport_param_rtp_enabled(
                                     transport_params.at(0), false);
      const bool redundancy_enable =
          transport_params.size() > 1 &&
          NodeSdpService::transport_param_rtp_enabled(transport_params.at(1),
                                                      false);

      std::lock_guard<std::mutex> receiver_lock(ctx.receiver_mutex);
      if (auto *video = ctx.stream_store.find_video_receiver_by_resource_id(
              resource.id))
      {
        auto snapshot = *video;
        apply_receiver_params(snapshot, endpoint_staged, transport_params,
                              stream_enable, redundancy_enable, gate);
        return ReceiverEvent{snapshot};
      }
      if (auto *audio = ctx.stream_store.find_audio_receiver_by_resource_id(
              resource.id))
      {
        auto snapshot = *audio;
        apply_receiver_params(snapshot, endpoint_staged, transport_params,
                              stream_enable, redundancy_enable, gate);
        return ReceiverEvent{snapshot};
      }
      if (auto *ancillary =
              ctx.stream_store.find_ancillary_receiver_by_resource_id(resource.id))
      {
        auto snapshot = *ancillary;
        apply_receiver_params(snapshot, endpoint_staged, transport_params,
                              stream_enable, redundancy_enable, gate);
        return ReceiverEvent{snapshot};
      }

      return std::nullopt;
    }
  }

  nmos::details::connection_resource_patch_validator
  make_connection_resource_patch_validator(const nmos::settings &settings,
                                           ActivationContext ctx)
  {
    return [ctx, &settings](const nmos::resource &resource,
                            const nmos::resource &connection_resource,
                            const web::json::value &endpoint_staged,
                            slog::base_gate &gate)
    {
      // slog::log<slog::severities::debug>(gate, SLOG_FLF)
      //     << nmos::stash_category(impl::categories::node_implementation)
      //     << "receiver connection validation for " << resource.id;
            slog::log<slog::severities::info>(gate, SLOG_FLF)
          << nmos::stash_category(impl::categories::node_implementation)
          << "receiver connection validation  "
          << resource.id;
      if (resource.type != nmos::types::receiver)
      {
        return;
      }
      if (!is_immediate_activation(endpoint_staged) ||
          !nmos::fields::master_enable(endpoint_staged))
      {
        return;
      }

      std::optional<ReceiverEvent> event;
      try
      {
        auto resolved_endpoint_staged = endpoint_staged;
        auto &transport_params =
            nmos::fields::transport_params(resolved_endpoint_staged);
        require_transport_params(resolved_endpoint_staged, resource);
        auto resolve_auto = make_auto_resolver(settings, ctx);
        resolve_auto(resource, connection_resource, transport_params);
        const auto resolved_transport_params =
            require_transport_params(resolved_endpoint_staged, resource);

        event = make_receiver_event_snapshot(ctx, resource,
                                             resolved_endpoint_staged,
                                             resolved_transport_params, gate);
      }
      catch (const web::json::json_exception &error)
      {
        throw web::json::json_exception(
            std::string("invalid receiver activation transport_params for ") +
            utility::conversions::to_utf8string(resource.id) + ": " +
            error.what());
      }
      catch (const std::exception &error)
      {
        throw web::json::json_exception(
            std::string("invalid receiver activation transport_params for ") +
            utility::conversions::to_utf8string(resource.id) + ": " +
            error.what());
      }

      if (!event)
      {
        return;
      }

      auto handler = ctx.callbacks.receiver_connection_validation_handler();
      if (!handler)
      {
        log_validation_no_handler(ctx, resource);
        return;
      }

      try
      {
        handler(*event);
      }
      catch (const std::exception &error)
      {
        throw web::json::json_exception(error.what());
      }
    };
  }
}
