#include <nmos/json_fields.h>
#include <nmos/media_type.h>
#include <nmos/node_server.h>
#include <nmos/resource.h>
#include <nmos/sdp_utils.h>
#include <nmos/video_jxsv.h>
#include <slog/all_in_one.h>

#include <cpprest/json.h>

namespace seeder::nmos_node::internal
{
  nmos::transport_file_parser make_transport_file_parser()
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
          try
          {
            nmos::validate_sdp_parameters(receiver, sdp_params);
          }
          catch (std::runtime_error &e)
          {
            throw std::runtime_error(
                "The destination does not support this sdp paramters.");
          }
        }
      };
      return nmos::details::parse_rtp_transport_file(
          validate_sdp_parameters, receiver, connection_receiver,
          transport_file_type, transport_file_data, gate);
    };
  }
}