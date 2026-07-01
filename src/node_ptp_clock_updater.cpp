#include "node_ptp_clock_updater.h"

#include <cpprest/details/basic_types.h>
#include <cpprest/json.h>
#include <nmos/clock_name.h>
#include <nmos/json_fields.h>
#include <nmos/node_resource.h>
#include <nmos/resource.h>
#include <nmos/resources.h>
#include <nmos/slog.h>
#include <nmos/type.h>
#include <slog/all_in_one.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace
{
  bool is_valid_ptp_gmid(const std::string &gmid)
  {
    if (23 != gmid.size())
    {
      return false;
    }
    for (std::size_t index = 0; index < gmid.size(); ++index)
    {
      if (2 == index % 3)
      {
        if ('-' != gmid[index])
        {
          return false;
        }
        continue;
      }
      const char value = gmid[index];
      if (!std::isdigit(static_cast<unsigned char>(value)) &&
          !(value >= 'a' && value <= 'f'))
      {
        return false;
      }
    }
    return true;
  }

  std::string normalize_ptp_gmid(std::string gmid)
  {
    gmid.erase(std::remove_if(gmid.begin(), gmid.end(),
                              [](unsigned char value)
                              { return ':' == value || '-' == value ||
                                       std::isspace(value); }),
               gmid.end());

    std::transform(gmid.begin(), gmid.end(), gmid.begin(),
                   [](unsigned char value)
                   { return static_cast<char>(std::tolower(value)); });

    if (16 != gmid.size())
    {
      return {};
    }

    if (!std::all_of(gmid.begin(), gmid.end(), [](unsigned char value)
                     { return std::isxdigit(value); }))
    {
      return {};
    }

    std::string formatted;
    formatted.reserve(23);
    for (std::size_t index = 0; index < gmid.size(); ++index)
    {
      if (0 != index && 0 == index % 2)
      {
        formatted.push_back('-');
      }
      formatted.push_back(gmid[index]);
    }
    return formatted;
  }
}

namespace seeder::nmos_node::internal
{
  NodePtpClockUpdater::NodePtpClockUpdater(NodePtpClockUpdaterContext ctx)
      : node_model_(ctx.node_model), stream_store_(ctx.stream_store),
        node_id_(ctx.node_id), ptp_domain_number_(ctx.ptp_domain_number),
        set_transportfile_(ctx.set_transportfile), gate_(ctx.gate)
  {
  }

  void NodePtpClockUpdater::set_ptp_clock(std::string gmid, bool locked,
                                          int ptp_domain)
  {
    const auto normalized_gmid = normalize_ptp_gmid(std::move(gmid));
    auto lock = node_model_.write_lock();
    ptp_domain_number_ = 0 <= ptp_domain && ptp_domain <= 127 ? ptp_domain : 127;
    if (is_valid_ptp_gmid(normalized_gmid))
    {
      slog::log<slog::severities::info>(*gate_, SLOG_FLF)
          << "Setting PTP clock GMID to " << normalized_gmid
          << " and locked to " << locked
          << " with domain number " << ptp_domain_number_;
      nmos::modify_resource(
          node_model_.node_resources, node_id_, [&](nmos::resource &node)
          {
            node.data[nmos::fields::clocks] = web::json::value_of(
                {nmos::make_ptp_clock(nmos::clock_names::clk0, false,
                                      utility::s2us(normalized_gmid), locked)});
          });
    }

    refresh_sender_transportfiles();
  }

  void NodePtpClockUpdater::refresh_sender_transportfiles()
  {
    for (const auto &sender_id : stream_store_.sender_ids())
    {
      auto sender = nmos::find_resource(node_model_.node_resources,
                                        {sender_id, nmos::types::sender});
      if (node_model_.node_resources.end() == sender)
      {
        continue;
      }

      nmos::modify_resource(node_model_.connection_resources, sender_id,
                            [&](nmos::resource &connection_sender)
                            {
                              auto &endpoint_transportfile =
                                  connection_sender.data[nmos::fields::endpoint_transportfile];
                              set_transportfile_(*sender, connection_sender,
                                                 endpoint_transportfile);
                              std::string transportfile_json =
                                  utility::conversions::to_utf8string(
                                      endpoint_transportfile.serialize());
                              slog::log<slog::severities::info>(*gate_, SLOG_FLF)
                                  << "Updated transportfile for sender "
                                  << sender_id << ": " << transportfile_json;
                            });
    }
  }
}
