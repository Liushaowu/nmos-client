#include "node_activation_context.h"
#include "node_implementation.h"

#include <cpprest/json.h>

namespace seeder::nmos_node::internal
{
  RegistrationStatus registration_status_from_uri(const web::uri &uri)
  {
    RegistrationStatus status;
    if (uri.is_empty())
    {
      return status;
    }
    status.connected = true;
    status.uri = utility::conversions::to_utf8string(uri.to_string());
    status.scheme = utility::conversions::to_utf8string(uri.scheme());
    status.host = utility::conversions::to_utf8string(uri.host());
    status.port = uri.port();

    const auto path = utility::conversions::to_utf8string(uri.path());
    if (!path.empty())
    {
      const auto slash = path.find_last_of('/');
      if (std::string::npos != slash && slash + 1 < path.size())
      {
        status.version = path.substr(slash + 1);
      }
    }
    return status;
  }

  nmos::registration_handler make_registration_handler(
      ActivationContext ctx)
  {
    return [ctx](const web::uri &registration_uri)
    {
      auto callback = ctx.callbacks.registration_changed_callback();
      if (!callback)
      {
        return;
      }
      callback(registration_status_from_uri(registration_uri));
    };
  }
}