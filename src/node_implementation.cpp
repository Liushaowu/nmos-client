#include "node_implementation.h"

namespace impl {
// nmos::interlace_mode get_interlace_mode(enum video_format video_format) {
//   return st_app_get_interlaced(video_format)
//              ? nmos::interlace_modes::interlaced_tff
//              : nmos::interlace_modes::progressive;
// }

bool is_rtp_port(const impl::port &port) {
  return impl::ports::rtp.end() != boost::range::find(impl::ports::rtp, port);
}

bool is_ws_port(const impl::port &port) {
  return impl::ports::ws.end() != boost::range::find(impl::ports::ws, port);
}

std::vector<port> parse_ports(const web::json::value &value) {
  if (value.is_null())
    return impl::ports::all;
  return boost::copy_range<std::vector<port>>(
      value.as_array() |
      boost::adaptors::transformed([&](const web::json::value &value) {
        return port{value.as_string()};
      }));
}

// find interface with the specified address
std::vector<web::hosts::experimental::host_interface>::const_iterator
find_interface(
    const std::vector<web::hosts::experimental::host_interface> &interfaces,
    const utility::string_t &address) {
  return boost::range::find_if(
      interfaces,
      [&](const web::hosts::experimental::host_interface &interface) {
        return interface.addresses.end() !=
               boost::range::find(interface.addresses, address);
      });
}

// generate repeatable ids for the example node's resources
nmos::id make_id(const nmos::id &seed_id, const nmos::type &type,
                 const impl::port &port, const std::string &id) {
  return nmos::make_repeatable_id(seed_id, U("/x-nmos/node/") + type.name +
                                               U('/') + port.name + id);
}

// generate a repeatable source-specific multicast address for each leg of a
// sender
utility::string_t make_source_specific_multicast_address_v4(const nmos::id &id,
                                                            int leg) {
  // hash the pseudo-random id and leg to generate the address
  const auto s = id + U('/') + utility::conversions::details::to_string_t(leg);
  const auto h = std::hash<utility::string_t>{}(s);
  auto a = boost::asio::ip::address_v4(uint32_t(h)).to_bytes();
  // ensure the address is in the source-specific multicast block reserved for
  // local host allocation, 232.0.1.0-232.255.255.255 see
  // https://www.iana.org/assignments/multicast-addresses/multicast-addresses.xhtml#multicast-addresses-10
  a[0] = 232;
  a[2] |= 1;
  return utility::s2us(boost::asio::ip::address_v4(a).to_string());
}

// add a selection of parents to a source or flow
// void insert_parents(nmos::resource &resource, const nmos::id &seed_id,
//                     const port &port, int index) {
//   // algorithm to produce signal ancestry with a range of depths and breadths
//   // see https://github.com/sony/nmos-cpp/issues/312#issuecomment-1335641637
//   int b = 0;
//   while (index & (1 << b))
//     ++b;
//   if (!b)
//     return;
//   index &= ~(1 << (b - 1));
//   do {
//     index &= ~(1 << b);
//     web::json::push_back(resource.data[nmos::fields::parents],
//                          impl::make_id(seed_id, resource.type, port, index));
//     ++b;
//   } while (index & (1 << b));
// }

// add a helpful suffix to the label of a sub-resource for the example node
void set_label_description(nmos::resource &resource, const impl::port &port,
                           std::string &name) {
  using web::json::value;

  auto label = nmos::fields::label(resource.data);
  if (!label.empty())
    label += U('/');
  label += resource.type.name + U('/') + port.name + "_" + name;
  resource.data[nmos::fields::label] = value::string(label);

  auto description = nmos::fields::description(resource.data);
  if (!description.empty())
    description += U('/');
  description += resource.type.name + U('/') + port.name + "_" + name;
  resource.data[nmos::fields::description] = value::string(description);
}

// add an example "natural grouping" hint to a sender or receiver
void insert_group_hint(nmos::resource &resource, const impl::port &port,
                       std::string &id, std::string& name) {
  web::json::push_back(
      resource.data[nmos::fields::tags][nmos::fields::group_hint],
      nmos::make_group_hint(
          {U(name), resource.type.name + U(' ') + port.name + id}));
}
} // namespace impl
