#include "node_implementation.h"

namespace impl {

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
                                               U('/') + port.name + utility::s2us(id));
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

// add a helpful suffix to the label of a sub-resource for the example node
void set_label_description(nmos::resource &resource, const impl::port &port,
                           std::string &name) {
  using web::json::value;

  // 将 resource.type.name 映射为简短标识
  auto type_label = resource.type.name;
  if (type_label == U("sender"))
    type_label = U("tx");
  else if (type_label == U("receiver"))
    type_label = U("rx");

  auto label = nmos::fields::label(resource.data);
  if (!label.empty())
    label += U('/');
  label += type_label + U('/') + port.name + U('/') + utility::s2us(name);
  resource.data[nmos::fields::label] = value::string(label);

  auto description = nmos::fields::description(resource.data);
  if (!description.empty())
    description += U('/');
  description += type_label + U('/') + port.name + U('/') + utility::s2us(name);
  resource.data[nmos::fields::description] = value::string(description);
}

void insert_group_hint(nmos::resource &resource, const impl::port &port,
                       const std::string &group_name, int index /*= 1*/) {
  // Natural grouping per BCP-002-01
  // Format: "<group-name>:<role-in-group>"
  // group_name  = user-defined (e.g. struct.name, "Playout Master")
  // role_in_group = TitleCase(port.name) + " " + index (e.g. "Video 1")
  auto role = port.name;
  role[0] = utility::char_t(toupper(role[0]));
  for (size_t i = 1; i < role.size(); ++i)
    role[i] = utility::char_t(tolower(role[i]));
  role += U(' ') + utility::s2us(std::to_string(index));

  web::json::push_back(
      resource.data[nmos::fields::tags][nmos::fields::group_hint],
      nmos::make_group_hint(
          {utility::s2us(group_name), role, nmos::group_scopes::device}));
}
} // namespace impl
