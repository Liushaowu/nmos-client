#pragma once

#include "node_callback_dispatcher.h"
#include "node_runtime_types.h"
#include "node_stream_store.h"

#include <nmos/id.h>
#include <nmos/log_gate.h>
#include <nmos/model.h>
#include <nmos/node_server.h>

#include <mutex>

namespace seeder::nmos_node::internal
{
  struct ActivationContext
  {
    StreamStore &stream_store;
    CallbackDispatcher &callbacks;
    nmos::experimental::log_gate *&gate;
    std::mutex &receiver_mutex;
    std::mutex &sender_mutex;
    nmos::id &node_id;
    int &ptp_domain_number;
    RuntimeInterfaces &runtime_interfaces;
    std::mutex &runtime_interfaces_mutex;
  };

  // Activation handler factories
  nmos::registration_handler make_registration_handler(ActivationContext ctx);
  nmos::connection_activation_handler make_activation_handler(ActivationContext ctx);
  nmos::transport_file_parser make_transport_file_parser();
  nmos::connection_resource_auto_resolver make_auto_resolver(
      const nmos::settings &settings, ActivationContext ctx);
  nmos::connection_sender_transportfile_setter make_transportfile_setter(
      const nmos::resources &node_resources,
      const nmos::settings &settings,
      ActivationContext ctx);
}