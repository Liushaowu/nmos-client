#include "node_server_runtime.h"
#include "node_implementation.h"
#include "node_settings.h"

#include <nmos/json_fields.h>
#include <nmos/log_gate.h>
#include <nmos/model.h>
#include <nmos/node_resources.h>
#include <nmos/node_server.h>
#include <nmos/process_utils.h>
#include <nmos/server.h>
#include <nmos/slog.h>
#include <slog/all_in_one.h>

#include <cpprest/http_msg.h>
#include <cpprest/json.h>
#include <cpprest/ws_client.h>

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <system_error>

namespace seeder::nmos_node::internal
{
  void NodeServerRuntime::log_stop_elapsed(
      const std::chrono::steady_clock::time_point started_at,
      const char *step)
  {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at);
    std::cerr << "nmos node stop: " << step
              << ", elapsed_ms=" << elapsed.count() << std::endl;
  }

  int NodeServerRuntime::start(ServerContext &ctx,
                               std::function<void()> thread_run_fn,
                               nmos::experimental::node_implementation &node_impl)
  {
    int i = 0;

    // Construct our data models including mutexes to protect them
    nmos::experimental::log_model log_model;
    {
      auto lock = ctx.node_model.write_lock();
      ctx.node_model.shutdown = false;
    }

    // Streams for logging, initially configured to write errors to stderr and
    // to discard the access log
    std::filebuf error_log_buf;
    std::ostream error_log(std::cerr.rdbuf());
    std::filebuf access_log_buf;
    std::ostream access_log(&access_log_buf);

    // Logging should all go through this logging gateway
    nmos::experimental::log_gate gate(error_log, access_log, log_model);
    ctx.gate_ptr = &gate;
    try
    {
      slog::log<slog::severities::info>(gate, SLOG_FLF)
          << "Starting nmos-cpp node";

      slog::log<slog::severities::info>(gate, SLOG_FLF)
          << "node_config_file_path: " << ctx.settings.config_file();
      if (!ctx.settings.has_config_file())
      {
        return -1;
      }
      ctx.node_model.settings = ctx.settings.load_runtime_settings();

      // Prepare run-time default settings (different than header defaults)
      apply_interface_host_addresses(ctx.node_model.settings);
      nmos::insert_node_default_settings(ctx.node_model.settings);

      // copy to the logging settings
      log_model.settings = ctx.node_model.settings;

      // the logging level is a special case
      log_model.level = nmos::fields::logging_level(log_model.settings);

      // Reconfigure the logging streams according to settings
      if (!nmos::fields::error_log(ctx.node_model.settings).empty())
      {
        error_log_buf.open(nmos::fields::error_log(ctx.node_model.settings),
                           std::ios_base::out | std::ios_base::app);
        auto lock = log_model.write_lock();
        error_log.rdbuf(&error_log_buf);
      }

      if (!nmos::fields::access_log(ctx.node_model.settings).empty())
      {
        access_log_buf.open(nmos::fields::access_log(ctx.node_model.settings),
                            std::ios_base::out | std::ios_base::app);
        auto lock = log_model.write_lock();
        access_log.rdbuf(&access_log_buf);
      }

      // Log the process ID and initial settings
      slog::log<slog::severities::info>(gate, SLOG_FLF)
          << "Process ID: " << nmos::details::get_process_id();
      slog::log<slog::severities::info>(gate, SLOG_FLF)
          << "Build settings: " << nmos::get_build_settings_info();
      slog::log<slog::severities::info>(gate, SLOG_FLF)
          << "Initial settings: " << ctx.node_model.settings.serialize();

      // Set up the node server
      auto node_server = nmos::experimental::make_node_server(
          ctx.node_model, node_impl, log_model, gate);

      // Add the underlying implementation thread function
      node_server.thread_functions.push_back(
          [thread_run_fn] { thread_run_fn(); });

      if (!nmos::experimental::fields::http_trace(ctx.node_model.settings))
      {
        // Disable TRACE method
        for (auto &http_listener : node_server.http_listeners)
        {
          http_listener.support(
              web::http::methods::TRCE, [](web::http::http_request req)
              { req.reply(web::http::status_codes::MethodNotAllowed); });
        }
      }

      // Open the API ports and start up node operation
      slog::log<slog::severities::info>(gate, SLOG_FLF)
          << "Preparing for connections";

      nmos::server_guard node_server_guard(node_server);

      slog::log<slog::severities::info>(gate, SLOG_FLF)
          << "Ready for connections";

      {
        std::lock_guard<std::mutex> lifecycle_lock(ctx.lifecycle_mutex);
        if (LifecycleState::starting == ctx.lifecycle_state)
        {
          ctx.lifecycle_state = LifecycleState::running;
        }
      }
      ctx.lifecycle_cv.notify_all();

      // Wait for stop signal
      {
        std::unique_lock<std::mutex> lifecycle_lock(ctx.lifecycle_mutex);
        ctx.lifecycle_cv.wait(lifecycle_lock, [&]
                              { return ctx.stop_requested; });
      }

      slog::log<slog::severities::info>(gate, SLOG_FLF)
          << "Closing connections";
    }
    catch (const web::json::json_exception &e)
    {
      slog::log<slog::severities::error>(gate, SLOG_FLF)
          << "JSON error: " << e.what();
      i = 1;
    }
    catch (const web::http::http_exception &e)
    {
      slog::log<slog::severities::error>(gate, SLOG_FLF)
          << "HTTP error: " << e.what() << " [" << e.error_code() << "]";
      i = 1;
    }
    catch (const web::websockets::websocket_exception &e)
    {
      slog::log<slog::severities::error>(gate, SLOG_FLF)
          << "WebSocket error: " << e.what() << " [" << e.error_code() << "]";
      i = 1;
    }
    catch (const std::ios_base::failure &e)
    {
      slog::log<slog::severities::error>(gate, SLOG_FLF)
          << "File error: " << e.what();
      i = 1;
    }
    catch (const std::system_error &e)
    {
      slog::log<slog::severities::error>(gate, SLOG_FLF)
          << "System error: " << e.what() << " [" << e.code() << "]";
      i = 1;
    }
    catch (const std::runtime_error &e)
    {
      slog::log<slog::severities::error>(gate, SLOG_FLF)
          << "Implementation error: " << e.what();
      i = 1;
    }
    catch (const std::exception &e)
    {
      slog::log<slog::severities::error>(gate, SLOG_FLF)
          << "Unexpected exception: " << e.what();
      i = 1;
    }
    catch (...)
    {
      slog::log<slog::severities::severe>(gate, SLOG_FLF)
          << "Unexpected unknown exception";
      i = 1;
    }

    slog::log<slog::severities::info>(gate, SLOG_FLF)
        << "Stopping nmos-cpp node";

    {
      std::lock_guard<std::mutex> lifecycle_lock(ctx.lifecycle_mutex);
      if (LifecycleState::starting == ctx.lifecycle_state)
      {
        ctx.lifecycle_state = LifecycleState::stopped;
      }
    }
    ctx.lifecycle_cv.notify_all();

    return i;
  }
} // namespace seeder::nmos_node::internal