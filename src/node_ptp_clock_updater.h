#pragma once

#include "node_stream_store.h"

#include <nmos/connection_activation.h>
#include <nmos/id.h>
#include <nmos/log_gate.h>
#include <nmos/model.h>

#include <string>

namespace seeder::nmos_node::internal
{
  struct NodePtpClockUpdaterContext
  {
    nmos::node_model &node_model;
    StreamStore &stream_store;
    nmos::id &node_id;
    int &ptp_domain_number;
    nmos::connection_sender_transportfile_setter &set_transportfile;
    nmos::experimental::log_gate *&gate;
  };

  class NodePtpClockUpdater
  {
  public:
    explicit NodePtpClockUpdater(NodePtpClockUpdaterContext ctx);

    void set_ptp_clock(std::string gmid, bool locked, int ptp_domain);

  private:
    void refresh_sender_transportfiles();

    nmos::node_model &node_model_;
    StreamStore &stream_store_;
    nmos::id &node_id_;
    int &ptp_domain_number_;
    nmos::connection_sender_transportfile_setter &set_transportfile_;
    nmos::experimental::log_gate *&gate_;
  };
}
