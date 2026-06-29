#include "node_resource_factory.h"

#include "node_implementation.h"
#include "node_resource_factory_internal.h"

#include <nmos/connection_resources.h>
#include <nmos/format.h>
#include <nmos/interlace_mode.h>
#include <nmos/media_type.h>
#include <nmos/node_resources.h>
#include <nmos/rational.h>
#include <nmos/transport.h>
#include <nmos/version.h>

#include <string>
#include <utility>
#include <vector>

using web::json::value;
using web::json::value_from_elements;
using web::json::value_of;

namespace seeder::nmos_node::internal
{
  using namespace resource_factory_detail;

  ReceiverResources NodeResourceFactory::make_video_receiver_resources(
      const VideoReceiver &video) const
  {
    std::string id = video.id;
    std::string name = video.name;
    const bool ST_2022_7 = video.redundancy.present;
    const auto receiver_id = make_video_receiver_resource_id(id);
    const auto interface_selection = select_runtime_interfaces(
        runtime_interfaces_, video.source_ip, video.redundancy.source_ip, ST_2022_7);
    const auto primary_interface_ip = interface_selection.primary.ip;
    const auto secondary_interface_ip = interface_selection.redundancy.ip;
    const auto secondary_multicast_ip =
        receiver_multicast_ip_or_default(video.redundancy.ip, video.ip);
    const auto secondary_port =
        receiver_port_or_default(video.redundancy.port, video.port);
    const auto interface_names =
        selected_interface_names(interface_selection, ST_2022_7);

    nmos::resource receiver = nmos::make_receiver(
        receiver_id, device_id_, nmos::transports::rtp_mcast,
        interface_names, nmos::formats::video, {nmos::media_types::video_raw},
        settings_);
    std::vector<web::json::value> constraint_sets;
    const auto colorspaces = to_utility_string_vector(video.caps.colorspaces);
    const auto transfer_characteristics =
        to_utility_string_vector(video.caps.transfer_characteristics);
    for (const auto &format : video.caps.formats)
    {
      seeder::core::video_format_desc format_desc =
          seeder::core::video_format_desc::get(format);
      const auto interlace_modes =
          format_desc.field_count == 2
              ? std::vector<utility::string_t>{
                    nmos::interlace_modes::interlaced_bff.name,
                    nmos::interlace_modes::interlaced_tff.name,
                    nmos::interlace_modes::interlaced_psf.name}
              : std::vector<utility::string_t>{
                    nmos::interlace_modes::progressive.name};

      web::json::value constraint_set = value_of(
          {{nmos::caps::format::grain_rate,
            nmos::make_caps_rational_constraint({nmos::parse_rational(
                web::json::value_of({{nmos::fields::numerator,
                                      format_desc.framerate.numerator()},
                                     {nmos::fields::denominator,
                                      format_desc.framerate.denominator()}}))})},
           {nmos::caps::format::frame_width,
            nmos::make_caps_integer_constraint({format_desc.width})},
           {nmos::caps::format::frame_height,
            nmos::make_caps_integer_constraint({format_desc.height})},
           {nmos::caps::format::interlace_mode,
            nmos::make_caps_string_constraint(interlace_modes)}});

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
        interface_address_constraint(interface_selection.primary);
    auto &staged = connection_receiver.data[nmos::fields::endpoint_staged];
    auto &active = connection_receiver.data[nmos::fields::endpoint_active];
    staged[nmos::fields::master_enable] = value::boolean(true);
    active[nmos::fields::master_enable] = value::boolean(true);
    if (ST_2022_7)
    {
      initialize_receiver_transport_params(
          staged, active, video.ip, primary_interface_ip, video.port,
          secondary_multicast_ip, secondary_interface_ip, secondary_port);
      connection_receiver.data[nmos::fields::endpoint_constraints][1]
                              [nmos::fields::interface_ip] =
          interface_address_constraint(interface_selection.redundancy);
    }
    else
    {
      initialize_receiver_transport_params(staged, active, video.ip,
                                           primary_interface_ip, video.port);
    }

    return {std::move(receiver), std::move(connection_receiver)};
  }

  ReceiverResources NodeResourceFactory::make_audio_receiver_resources(
      const AudioReceiver &audio) const
  {
    std::string id = audio.id;
    std::string name = audio.name;
    const bool ST_2022_7 = audio.redundancy.present;
    const auto interface_selection = select_runtime_interfaces(
        runtime_interfaces_, audio.source_ip, audio.redundancy.source_ip, ST_2022_7);
    const auto primary_interface_ip = interface_selection.primary.ip;
    const auto secondary_interface_ip = interface_selection.redundancy.ip;
    const auto secondary_multicast_ip =
        receiver_multicast_ip_or_default(audio.redundancy.ip, audio.ip);
    const auto secondary_port =
        receiver_port_or_default(audio.redundancy.port, audio.port);
    const auto receiver_id = make_audio_receiver_resource_id(id);
    const auto interface_names =
        selected_interface_names(interface_selection, ST_2022_7);

    nmos::resource receiver = nmos::make_audio_receiver(
        receiver_id, device_id_, nmos::transports::rtp_mcast,
        interface_names, audio.bit_depth, settings_);
    impl::set_label_description(receiver, impl::ports::audio, name);
    impl::insert_group_hint(receiver, impl::ports::audio, id, name);

    auto connection_receiver =
        nmos::make_connection_rtp_receiver(receiver_id, ST_2022_7);
    connection_receiver.data[nmos::fields::endpoint_constraints][0]
                            [nmos::fields::interface_ip] =
        interface_address_constraint(interface_selection.primary);
    auto &staged = connection_receiver.data[nmos::fields::endpoint_staged];
    auto &active = connection_receiver.data[nmos::fields::endpoint_active];
    staged[nmos::fields::master_enable] = value::boolean(true);
    active[nmos::fields::master_enable] = value::boolean(true);
    if (ST_2022_7)
    {
      initialize_receiver_transport_params(
          staged, active, audio.ip, primary_interface_ip, audio.port,
          secondary_multicast_ip, secondary_interface_ip, secondary_port);
      connection_receiver.data[nmos::fields::endpoint_constraints][1]
                              [nmos::fields::interface_ip] =
          interface_address_constraint(interface_selection.redundancy);
    }
    else
    {
      initialize_receiver_transport_params(staged, active, audio.ip,
                                           primary_interface_ip, audio.port);
    }

    return {std::move(receiver), std::move(connection_receiver)};
  }

  ReceiverResources NodeResourceFactory::make_ancillary_receiver_resources(
      const AncillaryReceiver &ancillary) const
  {
    std::string id = ancillary.id;
    std::string name = ancillary.name;
    const bool ST_2022_7 = ancillary.redundancy.present;
    const auto interface_selection = select_runtime_interfaces(
        runtime_interfaces_, ancillary.source_ip, ancillary.redundancy.source_ip,
        ST_2022_7);
    const auto primary_interface_ip = interface_selection.primary.ip;
    const auto secondary_interface_ip = interface_selection.redundancy.ip;
    const auto secondary_multicast_ip = receiver_multicast_ip_or_default(
        ancillary.redundancy.ip, ancillary.ip);
    const auto secondary_port =
        receiver_port_or_default(ancillary.redundancy.port, ancillary.port);
    const auto receiver_id = make_ancillary_receiver_resource_id(id);
    const auto interface_names =
        selected_interface_names(interface_selection, ST_2022_7);

    nmos::resource receiver = nmos::make_sdianc_data_receiver(
        receiver_id, device_id_, nmos::transports::rtp_mcast,
        interface_names, settings_);
    receiver.data[nmos::fields::version] =
        receiver.data[nmos::fields::caps][nmos::fields::version] =
            value(nmos::make_version());
    impl::set_label_description(receiver, impl::ports::data, name);
    impl::insert_group_hint(receiver, impl::ports::data, id, name);

    auto connection_receiver =
        nmos::make_connection_rtp_receiver(receiver_id, ST_2022_7);
    connection_receiver.data[nmos::fields::endpoint_constraints][0]
                            [nmos::fields::interface_ip] =
        interface_address_constraint(interface_selection.primary);
    auto &staged = connection_receiver.data[nmos::fields::endpoint_staged];
    auto &active = connection_receiver.data[nmos::fields::endpoint_active];
    staged[nmos::fields::master_enable] = value::boolean(true);
    active[nmos::fields::master_enable] = value::boolean(true);
    if (ST_2022_7)
    {
      initialize_receiver_transport_params(
          staged, active, ancillary.ip, primary_interface_ip,
          ancillary.port, secondary_multicast_ip, secondary_interface_ip,
          secondary_port);
      connection_receiver.data[nmos::fields::endpoint_constraints][1]
                              [nmos::fields::interface_ip] =
          interface_address_constraint(interface_selection.redundancy);
    }
    else
    {
      initialize_receiver_transport_params(staged, active, ancillary.ip,
                                           primary_interface_ip,
                                           ancillary.port);
    }

    return {std::move(receiver), std::move(connection_receiver)};
  }
}
