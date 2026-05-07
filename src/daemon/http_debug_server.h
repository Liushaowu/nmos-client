#pragma once

#include "app.h"
#include "state_store.h"

#include <cpprest/http_msg.h>

#include <memory>
#include <string>

namespace web::http::experimental::listener {
class http_listener;
}

namespace seeder::nmos_sync {

class HttpDebugServer {
public:
  HttpDebugServer(std::string url, App &app, const StateStore &state_store);
  ~HttpDebugServer();

  void start();
  void stop();

private:
  void handle_get(web::http::http_request request);
  void handle_put(web::http::http_request request);
  void handle_patch(web::http::http_request request);

  std::string url_;
  App &app_;
  const StateStore &state_store_;
  std::unique_ptr<web::http::experimental::listener::http_listener> listener_;
};

}
