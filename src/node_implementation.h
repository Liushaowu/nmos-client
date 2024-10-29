#pragma once
#ifndef NMOS_CPP_NODE_NODE_IMPLEMENTATION_H
#define NMOS_CPP_NODE_NODE_IMPLEMENTATION_H

#include <nmos/id.h>

#include "cpprest/host_utils.h"
#include "pplx/pplx_utils.h" // for pplx::complete_after, etc.
#include <boost/range/adaptor/filtered.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <boost/range/algorithm/find.hpp>
#include <boost/range/algorithm/find_first_of.hpp>
#include <boost/range/algorithm/find_if.hpp>
#include <boost/range/algorithm_ext/push_back.hpp>
#include <boost/range/irange.hpp>
#include <boost/range/join.hpp>
#include <cpprest/details/basic_types.h>
#include <cpprest/json.h>
#include <cstdio>
// #include <json/value.h>
#include <nmos/connection_api.h>
#include <nmos/json_fields.h>
#include <nmos/type.h>
#include <slog/all_in_one.h>
#include <string>
#ifdef HAVE_LLDP
#include "lldp/lldp_manager.h"
#endif
#include "nmos/activation_mode.h"
#include "nmos/capabilities.h"
#include "nmos/channelmapping_resources.h"
#include "nmos/channels.h"
#include "nmos/clock_name.h"
#include "nmos/colorspace.h"
#include "nmos/connection_events_activation.h"
#include "nmos/connection_resources.h"
#include "nmos/control_protocol_resource.h"
#include "nmos/control_protocol_resources.h"
#include "nmos/control_protocol_state.h"
#include "nmos/control_protocol_utils.h"
#include "nmos/events_resources.h"
#include "nmos/format.h"
#include "nmos/group_hint.h"
#include "nmos/interlace_mode.h"
#include "nmos/is12_versions.h" // for IS-12 gain control
#ifdef HAVE_LLDP
#include "nmos/lldp_manager.h"
#endif
#include "nmos/media_type.h"
#include "nmos/model.h"
#include "nmos/node_interfaces.h"
#include "nmos/node_resource.h"
#include "nmos/node_resources.h"
#include "nmos/node_server.h"
#include "nmos/random.h"
#include "nmos/sdp_utils.h"
#include "nmos/slog.h"
#include "nmos/st2110_21_sender_type.h"
#include "nmos/system_resources.h"
#include "nmos/transfer_characteristic.h"
#include "nmos/transport.h"
#include "nmos/video_jxsv.h"
#include "sdp/sdp.h"


// example node implementation details
namespace impl {
// custom logging category for the example node implementation thread
namespace categories {
const nmos::category node_implementation{"node_implementation"};
}

// custom settings for the example node implementation
namespace fields {
// node_tags, device_tags: used in resource tags fields
// "Each tag has a single key, but MAY have multiple values."
// See
// https://specs.amwa.tv/is-04/releases/v1.3.2/docs/APIs_-_Common_Keys.html#tags
// {
//     "tag_1": [ "tag_1_value_1", "tag_1_value_2" ],
//     "tag_2": [ "tag_2_value_1" ]
// }
const web::json::field_as_value_or node_tags{U("node_tags"),
                                             web::json::value::object()};
const web::json::field_as_value_or device_tags{U("device_tags"),
                                               web::json::value::object()};

// colorspace: controls the colorspace of video flows, see nmos::colorspace
const web::json::field_as_string_or colorspace{U("colorspace"), U("BT709")};

// transfer_characteristic: controls the transfer characteristic system of video
// flows, see nmos::transfer_characteristic
const web::json::field_as_string_or transfer_characteristic{
    U("transfer_characteristic"), U("SDR")};
// video_type: media type of video flows, e.g. "video/raw" or "video/jxsv", see
// nmos::media_types
// const web::json::field_as_string_or video_type{U("video_type"),
// U("video/raw")};
} // namespace fields

// nmos::interlace_mode get_interlace_mode(enum video_format video_format);

// the different kinds of 'port' (standing for the format/media type/event type)
// implemented by the example node each 'port' of the example node has a source,
// flow, sender and/or compatible receiver
DEFINE_STRING_ENUM(port)
namespace ports {
// video/raw, video/jxsv, etc.
const port video{U("VIDEO")};
// audio/L24
const port audio{U("AUDIO")};
// video/smpte291
const port data{U("DATA")};
// video/SMPTE2022-6
const port mux{U("MUX")};

// example measurement event
const port temperature{U("t")};
// example boolean event
const port burn{U("b")};
// example string event
const port nonsense{U("s")};
// example number/enum event
const port catcall{U("c")};

const std::vector<port> rtp{video, audio, data, mux};
const std::vector<port> ws{temperature, burn, nonsense, catcall};
const std::vector<port> all{
    boost::copy_range<std::vector<port>>(boost::range::join(rtp, ws))};
} // namespace ports

bool is_rtp_port(const port &port);
bool is_ws_port(const port &port);
std::vector<port> parse_ports(const web::json::value &value);

const std::vector<nmos::channel> channels_repeat{
    {U("Left Channel"), nmos::channel_symbols::L},
    {U("Right Channel"), nmos::channel_symbols::R},
    {U("Center Channel"), nmos::channel_symbols::C},
    {U("Low Frequency Effects Channel"), nmos::channel_symbols::LFE}};

// find interface with the specified address
std::vector<web::hosts::experimental::host_interface>::const_iterator
find_interface(
    const std::vector<web::hosts::experimental::host_interface> &interfaces,
    const utility::string_t &address);

// generate repeatable ids for the example node's resources
nmos::id make_id(const nmos::id &seed_id, const nmos::type &type,
                 const port &port = {}, const std::string &id = "");
std::vector<nmos::id> make_ids(const nmos::id &seed_id, const nmos::type &type,
                               const port &port, int how_many = 1);
std::vector<nmos::id> make_ids(const nmos::id &seed_id, const nmos::type &type,
                               const std::vector<port> &ports,
                               int how_many = 1);
std::vector<nmos::id> make_ids(const nmos::id &seed_id,
                               const std::vector<nmos::type> &types,
                               const std::vector<port> &ports,
                               int how_many = 1);

// generate a repeatable source-specific multicast address for each leg of a
// sender
utility::string_t make_source_specific_multicast_address_v4(const nmos::id &id,
                                                            int leg = 0);

// add a selection of parents to a source or flow
// void insert_parents(nmos::resource &resource, const nmos::id &seed_id,
//                     const port &port, const std::string &id);

// add a helpful suffix to the label of a sub-resource for the example node
void set_label_description(nmos::resource &resource, const port &port,
                           std::string &id);

// add an example "natural grouping" hint to a sender or receiver
void insert_group_hint(nmos::resource &resource, const port &port,
                       std::string &id, std::string &name);
} // namespace impl

struct node_implementation_init_exception : public std::runtime_error {
    using std::runtime_error::runtime_error;
};
// #include "st_config_utils.h"
namespace slog {
class base_gate;
}

namespace nmos {
struct node_model;

namespace experimental {
struct node_implementation;
struct control_protocol_state;
} // namespace experimental
} // namespace nmos

#endif
