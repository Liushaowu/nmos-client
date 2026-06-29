#include "node_resource_factory.h"

#include "node_implementation.h"
#include "node_resource_factory_internal.h"

#include <boost/range/adaptor/transformed.hpp>
#include <boost/range/algorithm_ext/push_back.hpp>
#include <boost/range/irange.hpp>
#include <nmos/activation_mode.h>
#include <nmos/channels.h>
#include <nmos/clock_name.h>
#include <nmos/connection_resources.h>
#include <nmos/format.h>
#include <nmos/interlace_mode.h>
#include <nmos/media_type.h>
#include <nmos/node_resources.h>
#include <nmos/rational.h>
#include <nmos/transfer_characteristic.h>
#include <nmos/transport.h>
#include <nmos/version.h>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using web::json::value;
using web::json::value_of;

namespace seeder::nmos_node::internal
{
  using namespace resource_factory_detail;

  SenderResources NodeResourceFactory::make_video_sender_resources(
      const VideoSender &video) const
  {
    std::string id = video.id;
    std::string name = video.name;
    seeder::core::video_format_desc format_desc =
        seeder::core::video_format_desc::get(video.video_format);
    if (format_desc.is_invalid())
    {
      throw node_implementation_init_exception(
          "invalid video sender video_format: " + video.video_format);
    }
    if (video.pg_format < 0 ||
        video.pg_format >= static_cast<int>(ST20_FMT_MAX))
    {
      throw node_implementation_init_exception(
          "invalid video sender pg_format: " + std::to_string(video.pg_format));
    }

    const auto sampling = st_get_color_sampling((st20_fmt)video.pg_format);
    const auto bit_depth = st_get_component_depth((st20_fmt)video.pg_format);
    const auto source_id = impl::make_id(
        seed_id_, nmos::types::source, impl::ports::video, id);
    const auto flow_id = impl::make_id(
        seed_id_, nmos::types::flow, impl::ports::video, id);
    const auto sender_id = impl::make_id(
        seed_id_, nmos::types::sender, impl::ports::video, id);
    const bool ST_2022_7 = video.redundancy.present;

    nmos::rational frame_rate = nmos::parse_rational(
        web::json::value_of({{nmos::fields::numerator,
                              format_desc.framerate.numerator()},
                             {nmos::fields::denominator,
                              format_desc.framerate.denominator()}}));
    nmos::colorspace colorspace =
        (nmos::colorspace)video.colorspace;
    nmos::transfer_characteristic transfer_characteristic =
        (nmos::transfer_characteristic)video.transfer_characteristics;
    nmos::resource source = nmos::make_video_source(
        source_id, device_id_, nmos::clock_names::clk0, frame_rate, settings_);
    impl::set_label_description(source, impl::ports::video, name);

    nmos::interlace_mode interlace_mode =
        format_desc.field_count == 2 ? nmos::interlace_modes::interlaced_tff
                                     : nmos::interlace_modes::progressive;
    nmos::resource flow = nmos::make_raw_video_flow(
        flow_id, source_id, device_id_, frame_rate, format_desc.width,
        format_desc.height, interlace_mode, colorspace,
        transfer_characteristic, sampling, bit_depth, settings_);
    impl::set_label_description(flow, impl::ports::video, name);

    const auto manifest_href =
        nmos::experimental::make_manifest_api_manifest(sender_id, settings_);
    const auto interface_selection = select_runtime_interfaces(
        runtime_interfaces_, video.source_ip, video.redundancy.source_ip, ST_2022_7);
    const auto interface_names =
        selected_interface_names(interface_selection, ST_2022_7);
    auto sender = nmos::make_sender(
        sender_id, flow_id, nmos::transports::rtp_mcast, device_id_,
        manifest_href.to_string(), interface_names, settings_);
    impl::set_label_description(sender, impl::ports::video, name);
    impl::insert_group_hint(sender, impl::ports::video, id, name);

    auto connection_sender =
        nmos::make_connection_rtp_sender(sender_id, ST_2022_7);
    connection_sender.data[nmos::fields::endpoint_constraints][0]
                          [nmos::fields::source_ip] =
        interface_address_constraint(interface_selection.primary);
    if (ST_2022_7)
    {
      connection_sender.data[nmos::fields::endpoint_constraints][1]
                            [nmos::fields::source_ip] =
          interface_address_constraint(interface_selection.redundancy);
    }
    auto &staged = connection_sender.data[nmos::fields::endpoint_staged];
    auto &active = connection_sender.data[nmos::fields::endpoint_active];
    initialize_sender_endpoint_enable(staged, active, video.enable,
                                      video.redundancy.enable);
    staged[nmos::fields::activation] =
        value_of({{nmos::fields::mode,
                   nmos::activation_modes::activate_scheduled_relative.name},
                  {nmos::fields::requested_time, U("0:0")},
                  {nmos::fields::activation_time, nmos::make_version()}});

    return {std::move(source), std::move(flow), std::move(sender),
            std::move(connection_sender)};
  }

  SenderResources NodeResourceFactory::make_audio_sender_resources(
      const AudioSender &audio) const
  {
    nmos::rational frame_rate = nmos::parse_rational(
        web::json::value_of({{nmos::fields::numerator, audio.sample_rate},
                             {nmos::fields::denominator, 1}}));
    std::string id = audio.id;
    std::string name = audio.name;
    const impl::port port = impl::ports::audio;
    const auto source_id = impl::make_id(seed_id_, nmos::types::source, port, id);
    const auto flow_id = impl::make_id(seed_id_, nmos::types::flow, port, id);
    const auto sender_id = impl::make_id(seed_id_, nmos::types::sender, port, id);

    const auto channels = boost::copy_range<std::vector<nmos::channel>>(
        boost::irange(0, audio.channel_count) |
        boost::adaptors::transformed(
            [&](const int &index)
            {
              return impl::channels_repeat[index %
                                           (int)impl::channels_repeat.size()];
            }));

    nmos::resource source = nmos::make_audio_source(
        source_id, device_id_, nmos::clock_names::clk0, frame_rate, channels,
        settings_);
    impl::set_label_description(source, port, name);
    nmos::resource flow = nmos::make_raw_audio_flow(
        flow_id, source_id, device_id_, frame_rate, audio.bit_depth, settings_);
    impl::set_label_description(flow, port, name);

    const auto manifest_href =
        nmos::experimental::make_manifest_api_manifest(sender_id, settings_);
    const bool ST_2022_7 = audio.redundancy.present;
    const auto interface_selection = select_runtime_interfaces(
        runtime_interfaces_, audio.source_ip, audio.redundancy.source_ip, ST_2022_7);
    const auto interface_names =
        selected_interface_names(interface_selection, ST_2022_7);
    auto sender = nmos::make_sender(
        sender_id, flow_id, nmos::transports::rtp_mcast, device_id_,
        manifest_href.to_string(), interface_names, settings_);
    impl::set_label_description(sender, port, name);
    impl::insert_group_hint(sender, port, id, name);

    auto connection_sender =
        nmos::make_connection_rtp_sender(sender_id, ST_2022_7);
    connection_sender.data[nmos::fields::endpoint_constraints][0]
                          [nmos::fields::source_ip] =
        interface_address_constraint(interface_selection.primary);
    if (ST_2022_7)
    {
      connection_sender.data[nmos::fields::endpoint_constraints][1]
                            [nmos::fields::source_ip] =
          interface_address_constraint(interface_selection.redundancy);
    }
    auto &staged = connection_sender.data[nmos::fields::endpoint_staged];
    auto &active = connection_sender.data[nmos::fields::endpoint_active];
    initialize_sender_endpoint_enable(staged, active, audio.enable,
                                      audio.redundancy.enable);
    staged[nmos::fields::activation] =
        value_of({{nmos::fields::mode,
                   nmos::activation_modes::activate_scheduled_relative.name},
                  {nmos::fields::requested_time, U("0:0")},
                  {nmos::fields::activation_time, nmos::make_version()}});

    return {std::move(source), std::move(flow), std::move(sender),
            std::move(connection_sender)};
  }

  SenderResources NodeResourceFactory::make_ancillary_sender_resources(
      const AncillarySender &ancillary) const
  {
    std::string id = ancillary.id;
    std::string name = ancillary.name;
    const impl::port port = impl::ports::data;
    nmos::rational grain_rate = nmos::parse_rational(
        web::json::value_of({{nmos::fields::numerator, 50},
                             {nmos::fields::denominator, 1}}));
    if (!ancillary.format.empty())
    {
      const auto format_desc =
          seeder::core::video_format_desc::get(ancillary.format);
      if (!format_desc.is_invalid())
      {
        grain_rate = nmos::parse_rational(
            web::json::value_of({{nmos::fields::numerator,
                                  format_desc.framerate.numerator()},
                                 {nmos::fields::denominator,
                                  format_desc.framerate.denominator()}}));
      }
    }

    const auto source_id = impl::make_id(seed_id_, nmos::types::source, port, id);
    const auto flow_id = impl::make_id(seed_id_, nmos::types::flow, port, id);
    const auto sender_id = impl::make_id(seed_id_, nmos::types::sender, port, id);

    nmos::resource source = nmos::make_data_source(
        source_id, device_id_, nmos::clock_names::clk0, grain_rate, settings_);
    impl::set_label_description(source, port, name);
    nmos::resource flow = nmos::make_sdianc_data_flow(
        flow_id, source_id, device_id_, settings_);
    flow.data[nmos::fields::grain_rate] = nmos::make_rational(grain_rate);
    impl::set_label_description(flow, port, name);

    const auto manifest_href =
        nmos::experimental::make_manifest_api_manifest(sender_id, settings_);
    const bool ST_2022_7 = ancillary.redundancy.present;
    const auto interface_selection = select_runtime_interfaces(
        runtime_interfaces_, ancillary.source_ip, ancillary.redundancy.source_ip,
        ST_2022_7);
    const auto interface_names =
        selected_interface_names(interface_selection, ST_2022_7);
    auto sender = nmos::make_sender(
        sender_id, flow_id, nmos::transports::rtp_mcast, device_id_,
        manifest_href.to_string(), interface_names, settings_);
    impl::set_label_description(sender, port, name);
    impl::insert_group_hint(sender, port, id, name);

    auto connection_sender =
        nmos::make_connection_rtp_sender(sender_id, ST_2022_7);
    connection_sender.data[nmos::fields::endpoint_constraints][0]
                          [nmos::fields::source_ip] =
        interface_address_constraint(interface_selection.primary);
    if (ST_2022_7)
    {
      connection_sender.data[nmos::fields::endpoint_constraints][1]
                            [nmos::fields::source_ip] =
          interface_address_constraint(interface_selection.redundancy);
    }
    auto &staged = connection_sender.data[nmos::fields::endpoint_staged];
    auto &active = connection_sender.data[nmos::fields::endpoint_active];
    initialize_sender_endpoint_enable(staged, active, ancillary.enable,
                                      ancillary.redundancy.enable);
    staged[nmos::fields::activation] =
        value_of({{nmos::fields::mode,
                   nmos::activation_modes::activate_scheduled_relative.name},
                  {nmos::fields::requested_time, U("0:0")},
                  {nmos::fields::activation_time, nmos::make_version()}});

    return {std::move(source), std::move(flow), std::move(sender),
            std::move(connection_sender)};
  }
}
