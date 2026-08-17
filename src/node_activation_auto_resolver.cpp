#include "node_activation_context.h"
#include "node_connection_transport_params.h"
#include "node_implementation.h"

#include <nmos/connection_api.h>
#include <nmos/json_fields.h>
#include <nmos/type.h>
#include <slog/all_in_one.h>

#include <cpprest/json.h>

namespace seeder::nmos_node::internal
{
  std::string receiver_multicast_ip_or_default(
      const std::string &configured_ip,
      const std::string &fallback_ip)
  {
    return configured_ip.empty() ? fallback_ip : configured_ip;
  }

  int receiver_port_or_default(int configured_port, int fallback_port)
  {
    return 0 < configured_port ? configured_port : fallback_port;
  }

  namespace
  {
    struct RuntimeInterfaceLeg
    {
      web::hosts::experimental::host_interface interface;
      std::string ip;
      bool has_ip = false;
    };

    struct RuntimeInterfaceSelection
    {
      RuntimeInterfaceLeg primary;
      RuntimeInterfaceLeg redundancy;
    };

    RuntimeInterfaceLeg make_runtime_interface_leg(
        const web::hosts::experimental::host_interface &interface,
        const std::string &ip)
    {
      return RuntimeInterfaceLeg{interface, ip, true};
    }

    RuntimeInterfaceLeg select_runtime_interface_leg(
        const std::vector<web::hosts::experimental::host_interface> &interfaces,
        const std::string &configured_ip)
    {
      if (!configured_ip.empty())
      {
        const auto it = impl::find_interface(
            interfaces, utility::conversions::to_string_t(configured_ip));
        if (interfaces.end() != it)
        {
          return make_runtime_interface_leg(*it, configured_ip);
        }
      }
      for (const auto &interface : interfaces)
      {
        if (!interface.addresses.empty())
        {
          return make_runtime_interface_leg(
              interface,
              utility::conversions::to_utf8string(interface.addresses.front()));
        }
      }
      if (!interfaces.empty())
      {
        return make_runtime_interface_leg(interfaces.front(), std::string{});
      }
      return RuntimeInterfaceLeg{};
    }

    RuntimeInterfaceLeg select_redundancy_runtime_interface_leg(
        const std::vector<web::hosts::experimental::host_interface> &interfaces,
        const std::string &configured_ip, const RuntimeInterfaceLeg &primary)
    {
      if (!configured_ip.empty())
      {
        const auto it = impl::find_interface(
            interfaces, utility::conversions::to_string_t(configured_ip));
        if (interfaces.end() != it)
        {
          return make_runtime_interface_leg(*it, configured_ip);
        }
      }
      for (const auto &interface : interfaces)
      {
        if (!interface.addresses.empty() &&
            (!primary.has_ip || interface.name != primary.interface.name))
        {
          return make_runtime_interface_leg(
              interface,
              utility::conversions::to_utf8string(interface.addresses.front()));
        }
      }
      if (primary.has_ip)
      {
        return primary;
      }
      return select_runtime_interface_leg(interfaces, std::string{});
    }

    RuntimeInterfaceSelection select_runtime_interfaces(
        const std::vector<web::hosts::experimental::host_interface> &interfaces,
        const std::string &primary_source_ip,
        const std::string &redundancy_source_ip, bool smpte2022_7)
    {
      RuntimeInterfaceSelection selection;
      selection.primary = select_runtime_interface_leg(interfaces, primary_source_ip);
      if (smpte2022_7)
      {
        selection.redundancy = select_redundancy_runtime_interface_leg(
            interfaces, redundancy_source_ip, selection.primary);
      }
      return selection;
    }
  }

  nmos::connection_resource_auto_resolver make_auto_resolver(
      const nmos::settings &settings, ActivationContext ctx)
  {
    using web::json::value;
    return [ctx, &settings](const nmos::resource &resource,
                             const nmos::resource &connection_resource,
                             value &transport_params)
    {
      const auto transport_params_state =
          connection_transport_params::state(transport_params);
      if (connection_transport_params::State::not_array == transport_params_state)
      {
        slog::log<slog::severities::error>(*ctx.gate, SLOG_FLF)
            << nmos::stash_category(impl::categories::node_implementation)
            << "resolve_auto ignored: transport_params is not array for "
            << connection_resource.id;
        return;
      }
      auto &transport_params_array = transport_params.as_array();
      if (connection_transport_params::State::empty == transport_params_state)
      {
        slog::log<slog::severities::error>(*ctx.gate, SLOG_FLF)
            << nmos::stash_category(impl::categories::node_implementation)
            << "resolve_auto ignored: transport_params is empty for "
            << connection_resource.id;
        return;
      }

      const std::pair<nmos::id, nmos::type> id_type{connection_resource.id,
                                                     connection_resource.type};
      bool smpte2022_7 = false;
      std::string source_ip = "";
      std::string redundancy_source_ip = "";
      std::string ip = "";
      int port = 0;
      std::string redundancy_ip;
      if (id_type.second == nmos::types::sender)
      {
        {
          std::lock_guard<std::mutex> sender_lock(ctx.sender_mutex);
          const auto sender_id = utility::us2s(connection_resource.id);
          VideoSender *video =
              ctx.stream_store.find_video_sender_by_sender_id(sender_id);
          if (video)
          {
            smpte2022_7 = video->redundancy.present;
            source_ip = video->source_ip;
            redundancy_source_ip = video->redundancy.source_ip;
            ip = video->ip;
            redundancy_ip = video->redundancy.ip;
            port = video->port;
          }
          AudioSender *audio =
              ctx.stream_store.find_audio_sender_by_sender_id(sender_id);
          if (audio)
          {
            smpte2022_7 = audio->redundancy.present;
            source_ip = audio->source_ip;
            redundancy_source_ip = audio->redundancy.source_ip;
            ip = audio->ip;
            redundancy_ip = audio->redundancy.ip;
            port = audio->port;
          }
          AncillarySender *ancillary =
              ctx.stream_store.find_ancillary_sender_by_sender_id(sender_id);
          if (ancillary)
          {
            smpte2022_7 = ancillary->redundancy.present;
            source_ip = ancillary->source_ip;
            redundancy_source_ip = ancillary->redundancy.source_ip;
            ip = ancillary->ip;
            redundancy_ip = ancillary->redundancy.ip;
            port = ancillary->port;
          }
        }
        std::vector<web::hosts::experimental::host_interface> interfaces;
        {
          std::lock_guard<std::mutex> lock(ctx.runtime_interfaces_mutex);
          interfaces = ctx.runtime_interfaces;
        }
        const auto selection = select_runtime_interfaces(
            interfaces, source_ip, redundancy_source_ip, smpte2022_7);
        nmos::details::resolve_auto(transport_params_array[0],
                                    nmos::fields::source_ip,
                                    [&]
                                    { return value::string(selection.primary.ip.empty()
                                        ? U("0.0.0.0")
                                        : selection.primary.ip); });
        if (smpte2022_7 && transport_params_array.size() > 1)
        {
          nmos::details::resolve_auto(transport_params_array[1],
                                      nmos::fields::source_ip,
                                      [&]
                                      { return value::string(selection.redundancy.ip.empty()
                                          ? U("0.0.0.0")
                                          : selection.redundancy.ip); });
        }
        nmos::details::resolve_auto(transport_params_array[0],
                                    nmos::fields::destination_ip,
                                    [&]
                                    { return value::string(utility::s2us(ip)); });
        if (smpte2022_7 && transport_params_array.size() > 1)
        {
          nmos::details::resolve_auto(
              transport_params_array[1], nmos::fields::destination_ip,
              [&]
              { return value::string(utility::s2us(redundancy_ip)); });
        }
        nmos::resolve_rtp_auto(id_type.second, transport_params, port);
      }
      else if (id_type.second == nmos::types::receiver)
      {
        // 断开连接时无需解析 auto 参数，直接返回避免访问异常的 transport_params
        const auto master_enable = nmos::fields::master_enable(
            nmos::fields::endpoint_staged(connection_resource.data));
        if (!master_enable)
        {
          return;
        }

        bool smpte2022_7 = false;
        std::string interface_ip = "";
        std::string secondary_multicast_ip = "";
        std::string secondary_interface_ip = "";
        int secondary_port = 0;
        std::string primary_source_ip;
        std::string redundancy_source_ip;
        {
          std::lock_guard<std::mutex> receiver_lock(ctx.receiver_mutex);
          VideoReceiver *video =
              ctx.stream_store.find_video_receiver_by_resource_id(connection_resource.id);
          if (video)
          {
            source_ip = "";
            port = video->port;
            smpte2022_7 = video->redundancy.present;
            ip = video->ip;
            primary_source_ip = video->source_ip;
            redundancy_source_ip = video->redundancy.source_ip;
            secondary_multicast_ip = receiver_multicast_ip_or_default(
                video->redundancy.ip, video->ip);
            secondary_port = receiver_port_or_default(video->redundancy.port,
                                                      video->port);
          }
          AudioReceiver *audio =
              ctx.stream_store.find_audio_receiver_by_resource_id(connection_resource.id);
          if (audio)
          {
            smpte2022_7 = audio->redundancy.present;
            source_ip = "";
            ip = audio->ip;
            port = audio->port;
            primary_source_ip = audio->source_ip;
            redundancy_source_ip = audio->redundancy.source_ip;
            secondary_multicast_ip = receiver_multicast_ip_or_default(
                audio->redundancy.ip, audio->ip);
            secondary_port = receiver_port_or_default(audio->redundancy.port,
                                                      audio->port);
          }
          AncillaryReceiver *ancillary =
              ctx.stream_store.find_ancillary_receiver_by_resource_id(connection_resource.id);
          if (ancillary)
          {
            smpte2022_7 = ancillary->redundancy.present;
            source_ip = "";
            ip = ancillary->ip;
            port = ancillary->port;
            primary_source_ip = ancillary->source_ip;
            redundancy_source_ip = ancillary->redundancy.source_ip;
            secondary_multicast_ip = receiver_multicast_ip_or_default(
                ancillary->redundancy.ip, ancillary->ip);
            secondary_port = receiver_port_or_default(
                ancillary->redundancy.port, ancillary->port);
          }
        }
        std::vector<web::hosts::experimental::host_interface> interfaces;
        {
          std::lock_guard<std::mutex> lock(ctx.runtime_interfaces_mutex);
          interfaces = ctx.runtime_interfaces;
        }
        const auto selection = select_runtime_interfaces(
            interfaces, primary_source_ip, redundancy_source_ip, smpte2022_7);
        interface_ip = selection.primary.ip;
        secondary_interface_ip = selection.redundancy.ip;
        nmos::details::resolve_auto(transport_params_array[0],
                                    nmos::fields::multicast_ip,
                                    [&]
                                    { return value::string(utility::s2us(ip)); });
        nmos::details::resolve_auto(transport_params_array[0],
                                    nmos::fields::source_ip,
                                    [&]
                                    { return value::string(utility::s2us(source_ip)); });
        nmos::details::resolve_auto(transport_params_array[0],
                                    nmos::fields::destination_port,
                                    [&]
                                    { return port; });
        auto &tp0 = transport_params_array[0];
        if (!tp0.has_field(nmos::fields::interface_ip))
        {
          tp0[nmos::fields::interface_ip] =
              value::string(utility::s2us(interface_ip));
        }
        else
        {
          nmos::details::resolve_auto(
              tp0, nmos::fields::interface_ip,
              [&]
              { return value::string(utility::s2us(interface_ip)); });
        }
        if (smpte2022_7 && transport_params_array.size() > 1)
        {
          nmos::details::resolve_auto(
              transport_params_array[1], nmos::fields::multicast_ip,
              [&]
              { return value::string(utility::s2us(secondary_multicast_ip)); });
          nmos::details::resolve_auto(
              transport_params_array[1], nmos::fields::destination_port,
              [&]
              { return secondary_port; });
          auto &tp1 = transport_params_array[1];
          if (!tp1.has_field(nmos::fields::interface_ip))
          {
            tp1[nmos::fields::interface_ip] =
                value::string(utility::s2us(secondary_interface_ip));
          }
          else
          {
            nmos::details::resolve_auto(
                tp1, nmos::fields::interface_ip,
                [&]
                { return value::string(utility::s2us(secondary_interface_ip)); });
          }
        }
        nmos::resolve_rtp_auto(id_type.second, transport_params);
      }
    };
  }
}
