#include "node_activation_context.h"
#include "node_implementation.h"
#include "node_sdp_service.h"

#include <nmos/connection_api.h>
#include <nmos/format.h>
#include <nmos/json_fields.h>
#include <nmos/media_type.h>
#include <nmos/node_resource.h>
#include <nmos/sdp_utils.h>
#include <nmos/transport.h>
#include <sdp/sdp.h>

#include <cpprest/json.h>
#include <cpprest/json_utils.h>

#include <iostream>

namespace seeder::nmos_node::internal
{
  nmos::connection_sender_transportfile_setter make_transportfile_setter(
      const nmos::resources &node_resources,
      const nmos::settings &settings,
      ActivationContext ctx)
  {
    using web::json::value;
    return [ctx, &node_resources, &settings](
               const nmos::resource &sender,
               const nmos::resource &connection_sender,
               value &endpoint_transportfile)
    {
      if (ctx.stream_store.has_sender_id(connection_sender.id))
      {
        auto node =
            nmos::find_resource(node_resources, {ctx.node_id, nmos::types::node});
        if (node_resources.end() == node)
        {
          throw std::logic_error("matching IS-04 node, source or flow not found");
        }
        if (!sender.data.has_field(nmos::fields::flow_id))
        {
          throw std::logic_error("matching IS-04 sender flow_id not found");
        }
        const nmos::id flow_id = nmos::fields::flow_id(sender.data).as_string();
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
        const nmos::id source_id = nmos::fields::source_id(flow->data);
        auto source = nmos::find_resource(node_resources,
                                          {source_id, nmos::types::source});
        if (node_resources.end() == source)
        {
          throw std::logic_error("matching IS-04 source not found");
        }
        utility::string_t session_name = nmos::fields::description(sender.data);
        const auto &transport_params = nmos::fields::transport_params(
            nmos::fields::endpoint_active(connection_sender.data));
        auto transportfile_transport_params = transport_params;
        if (transport_params.is_array())
        {
          transportfile_transport_params = value::array();
          size_t leg = 0;
          for (const auto &transport_param : transport_params.as_array())
          {
            transportfile_transport_params[leg] =
                NodeSdpService::transport_param_with_valid_destination_ip(
                    transport_param);
            ++leg;
          }
        }
        auto media_stream_ids = [&]
        {
          std::vector<utility::string_t> ids;
          if (!transportfile_transport_params.is_array())
          {
            return ids;
          }
          const auto leg_count = transportfile_transport_params.as_array().size();
          if (leg_count < 2)
          {
            return ids;
          }
          ids.reserve(leg_count);
          for (std::size_t leg = 0; leg < leg_count; ++leg)
          {
            if (0 == leg)
            {
              ids.push_back(U("PRIMARY"));
            }
            else if (1 == leg)
            {
              ids.push_back(U("SECONDARY"));
            }
            else
            {
              ids.push_back(utility::conversions::to_string_t(
                  "LEG" + std::to_string(leg + 1)));
            }
          }
          return ids;
        }();
        auto sdp_params = [&]
        {
          const nmos::format format{nmos::fields::format(flow->data)};
          if (nmos::formats::video == format)
          {
            const nmos::media_type video_type{
                nmos::fields::media_type(flow->data)};
            if (nmos::media_types::video_raw == video_type)
            {
              nmos::video_raw_parameters raw_params;
              try
              {
                raw_params = nmos::make_video_raw_parameters(
                    node->data, source->data, flow->data, sender.data,
                    sdp::type_parameters::type_N);
              }
              catch (const std::exception &error)
              {
                const auto flow_json = utility::conversions::to_utf8string(
                    flow->data.serialize());
                const auto sender_json = utility::conversions::to_utf8string(
                    sender.data.serialize());
                throw std::runtime_error(
                    std::string("failed to make video/raw SDP parameters: ") +
                    error.what() + ", flow=" + flow_json +
                    ", sender=" + sender_json);
              }
              const auto ts_refclk = nmos::details::make_ts_refclk(
                  node->data, source->data, sender.data, ctx.ptp_domain_number);
              return nmos::make_video_raw_sdp_parameters(
                  session_name, raw_params,
                  nmos::details::payload_type_video_default, media_stream_ids,
                  ts_refclk);
            }
            throw std::logic_error("unexpected video media type");
          }
          else if (nmos::formats::audio == format)
          {
            double packet_time = 1;
            auto audio_L_params = nmos::make_audio_L_parameters(
                node->data, source->data, flow->data, sender.data, packet_time);
            const auto ts_refclk = nmos::details::make_ts_refclk(
                node->data, source->data, sender.data, ctx.ptp_domain_number);
            return nmos::make_audio_L_sdp_parameters(
                session_name, audio_L_params,
                nmos::details::payload_type_audio_default, media_stream_ids,
                ts_refclk);
          }
          else if (nmos::formats::data == format)
          {
            auto samp291_params = nmos::make_video_smpte291_parameters(
                node->data, source->data, flow->data, sender.data,
                nmos::vpid_codes::vpid_1_5Gbps_1080_line,
                sdp::transmission_models::compatible);
            const auto ts_refclk = nmos::details::make_ts_refclk(
                node->data, source->data, sender.data, ctx.ptp_domain_number);
            return nmos::make_video_smpte291_sdp_parameters(
                session_name, samp291_params,
                nmos::details::payload_type_data_default, media_stream_ids,
                ts_refclk);
          }
          else if (nmos::formats::mux == format)
          {
            auto SMPTE2022_6_params = nmos::make_video_SMPTE2022_6_parameters(
                node->data, source->data, flow->data, sender.data,
                sdp::type_parameters::type_N);
            const auto ts_refclk = nmos::details::make_ts_refclk(
                node->data, source->data, sender.data, ctx.ptp_domain_number);
            return nmos::make_video_SMPTE2022_6_sdp_parameters(
                session_name, SMPTE2022_6_params,
                nmos::details::payload_type_mux_default, media_stream_ids,
                ts_refclk);
          }
          else
          {
            throw std::logic_error("unexpected flow format");
          }
          throw std::logic_error("failed to make SDP parameters");
        }();

        utility::string_t transport_params_json =
            transportfile_transport_params.serialize();
        std::cout << "transport_params_json: "
                  << utility::us2s(transport_params_json) << std::endl;
        auto session_description = nmos::make_session_description(
            sdp_params, transportfile_transport_params);
        auto sdp =
            utility::s2us(sdp::make_session_description(session_description));
        endpoint_transportfile =
            nmos::make_connection_rtp_sender_transportfile(sdp);
      }
    };
  }
}
