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
#include <type_traits>

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
      if (!ctx.gate) return;
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
    void apply_primary_params(Receiver &receiver,
                              const web::json::array &transport_params)
    {
      auto safe_str = [](const web::json::value &tp,
                         const utility::string_t &field) -> std::string
      {
        if (!tp.is_object() || !tp.has_field(field)) return {};
        const auto &val = tp.at(field);
        if (val.is_string()) return utility::us2s(val.as_string());
        return {};
      };
      receiver.source_ip =
          safe_str(transport_params.at(0), nmos::fields::interface_ip);
      receiver.ip =
          safe_str(transport_params.at(0), nmos::fields::multicast_ip);
      receiver.port = transport_params.at(0).is_object() &&
                              transport_params.at(0).has_field(nmos::fields::destination_port) &&
                              transport_params.at(0).at(nmos::fields::destination_port).is_integer()
                          ? transport_params.at(0).at(nmos::fields::destination_port).as_integer()
                          : 0;
    }

    template <typename Receiver>
    void apply_secondary_params(Receiver &receiver,
                                const web::json::array &transport_params,
                                bool redundancy_enable)
    {
      if (!receiver.redundancy.present || transport_params.size() < 2) return;
      auto safe_str = [](const web::json::value &tp,
                         const utility::string_t &field) -> std::string
      {
        if (!tp.is_object() || !tp.has_field(field)) return {};
        const auto &val = tp.at(field);
        if (val.is_string()) return utility::us2s(val.as_string());
        return {};
      };
      receiver.redundancy.enable = redundancy_enable;
      receiver.redundancy.source_ip =
          safe_str(transport_params.at(1), nmos::fields::interface_ip);
      receiver.redundancy.ip =
          safe_str(transport_params.at(1), nmos::fields::multicast_ip);
      receiver.redundancy.port =
          transport_params.at(1).is_object() &&
                  transport_params.at(1).has_field(nmos::fields::destination_port) &&
                  transport_params.at(1).at(nmos::fields::destination_port).is_integer()
              ? transport_params.at(1).at(nmos::fields::destination_port).as_integer()
              : 0;
    }

    // single template for all 3 receiver types; SDP update dispatched via if constexpr
    template <typename Receiver>
    void apply_receiver_params(Receiver &receiver,
                                const web::json::value &endpoint_staged,
                                const web::json::array &transport_params,
                                bool stream_enable,
                                bool redundancy_enable,
                                slog::base_gate &gate)
    {
      receiver.enable = stream_enable;
      apply_primary_params(receiver, transport_params);
      apply_secondary_params(receiver, transport_params, redundancy_enable);
      if constexpr (std::is_same_v<Receiver, VideoReceiver>)
      {
        NodeSdpService::update_video_receiver_from_transport_file(
            receiver, nmos::fields::transport_file(endpoint_staged), gate);
      }
      else if constexpr (std::is_same_v<Receiver, AudioReceiver>)
      {
        NodeSdpService::update_audio_receiver_from_transport_file(
            receiver, nmos::fields::transport_file(endpoint_staged), gate);
      }
    }

    // single template for the 3 repeated find+snapshot blocks
    template <typename Receiver>
    std::optional<ReceiverEvent> try_make_receiver_snapshot(
        StreamStore &store,
        Receiver *(StreamStore::*find_fn)(const nmos::id &),
        const nmos::id &resource_id,
        const web::json::value &endpoint_staged,
        const web::json::array &transport_params,
        bool stream_enable,
        bool redundancy_enable,
        slog::base_gate &gate)
    {
      auto *receiver = (store.*find_fn)(resource_id);
      if (!receiver) return std::nullopt;
      auto snapshot = *receiver;
      apply_receiver_params(snapshot, endpoint_staged, transport_params,
                            stream_enable, redundancy_enable, gate);
      return ReceiverEvent{snapshot};
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

      if (auto event = try_make_receiver_snapshot(
              ctx.stream_store, &StreamStore::find_video_receiver_by_resource_id,
              resource.id, endpoint_staged, transport_params,
              stream_enable, redundancy_enable, gate))
        return event;
      if (auto event = try_make_receiver_snapshot(
              ctx.stream_store, &StreamStore::find_audio_receiver_by_resource_id,
              resource.id, endpoint_staged, transport_params,
              stream_enable, redundancy_enable, gate))
        return event;
      if (auto event = try_make_receiver_snapshot(
              ctx.stream_store, &StreamStore::find_ancillary_receiver_by_resource_id,
              resource.id, endpoint_staged, transport_params,
              stream_enable, redundancy_enable, gate))
        return event;

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

      if (!event) return;

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