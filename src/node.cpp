#include "node.h"
#include "nmos/sdp_utils.h"
#include "node_implementation.h"
#include <asm-generic/errno.h>
#include <cpprest/json.h>
#include <cpprest/json_ops.h>
#include <functional>
#include <nmos/capabilities.h>
#include <nmos/id.h>
#include <nmos/json_fields.h>
#include <nmos/media_type.h>
#include <nmos/mutex.h>
#include <nmos/rational.h>
#include <nmos/resource.h>
#include <nmos/resources.h>
#include <nmos/type.h>
#include <sdp/json.h>
#include <slog/all_in_one.h>
#include <stdexcept>
#include <string>
#include <vector>
using web::json::value;
using web::json::value_from_elements;
using web::json::value_of;
sdp::sampling st_get_color_sampling(st20_fmt fmt) {
  switch (fmt) {
  case ST20_FMT_YUV_422_10BIT: /**< 10-bit YUV 4:2:2 */
    return sdp::samplings::YCbCr_4_2_2;
  case ST20_FMT_YUV_422_8BIT: /**< 8-bit YUV 4:2:2 */
    return sdp::samplings::YCbCr_4_2_2;
  case ST20_FMT_YUV_422_12BIT: /**< 12-bit YUV 4:2:2 */
    return sdp::samplings::YCbCr_4_2_2;
  case ST20_FMT_YUV_422_16BIT: /**< 16-bit YUV 4:2:2 */
    return sdp::samplings::YCbCr_4_2_2;
  case ST20_FMT_YUV_420_8BIT: /**< 8-bit YUV 4:2:0 */
    return sdp::samplings::YCbCr_4_2_0;
  case ST20_FMT_YUV_420_10BIT: /**< 10-bit YUV 4:2:0 */
    return sdp::samplings::YCbCr_4_2_0;
  case ST20_FMT_YUV_420_12BIT: /**< 12-bit YUV 4:2:0 */
    return sdp::samplings::YCbCr_4_2_0;
  case ST20_FMT_RGB_8BIT: /**< 8-bit RGB */
    return sdp::samplings::RGB;
  case ST20_FMT_RGB_10BIT: /**< 10-bit RGB */
    return sdp::samplings::RGB;
  case ST20_FMT_RGB_12BIT: /**< 12-bit RGB */
    return sdp::samplings::RGB;
  case ST20_FMT_RGB_16BIT: /**< 16-bit RGB */
    return sdp::samplings::RGB;
  case ST20_FMT_YUV_444_8BIT: /**< 8-bit YUV 4:4:4 */
    return sdp::samplings::YCbCr_4_4_4;
  case ST20_FMT_YUV_444_10BIT: /**< 10-bit YUV 4:4:4 */
    return sdp::samplings::YCbCr_4_4_4;
  case ST20_FMT_YUV_444_12BIT: /**< 12-bit YUV 4:4:4 */
    return sdp::samplings::YCbCr_4_4_4;
  case ST20_FMT_YUV_444_16BIT: /**< 16-bit YUV 4:4:4 */
    return sdp::samplings::YCbCr_4_4_4;
  case ST20_FMT_MAX:
    return sdp::samplings::YCbCr_4_4_4;
  }
  return sdp::samplings::YCbCr_4_2_2;
}

int st_get_component_depth(st20_fmt fmt) {
  switch (fmt) {
  case ST20_FMT_YUV_422_10BIT: /**< 10-bit YUV 4:2:2 */
    return 10;
  case ST20_FMT_YUV_422_8BIT: /**< 8-bit YUV 4:2:2 */
    return 8;
  case ST20_FMT_YUV_422_12BIT: /**< 12-bit YUV 4:2:2 */
    return 12;
  case ST20_FMT_YUV_422_16BIT: /**< 16-bit YUV 4:2:2 */
    return 16;
  case ST20_FMT_YUV_420_8BIT: /**< 8-bit YUV 4:2:0 */
    return 8;
  case ST20_FMT_YUV_420_10BIT: /**< 10-bit YUV 4:2:0 */
    return 10;
  case ST20_FMT_YUV_420_12BIT: /**< 12-bit YUV 4:2:0 */
    return 12;
  case ST20_FMT_RGB_8BIT: /**< 8-bit RGB */
    return 8;
  case ST20_FMT_RGB_10BIT: /**< 10-bit RGB */
    return 10;
  case ST20_FMT_RGB_12BIT: /**< 12-bit RGB */
    return 12;
  case ST20_FMT_RGB_16BIT: /**< 16-bit RGB */
    return 16;
  case ST20_FMT_YUV_444_8BIT: /**< 8-bit YUV 4:4:4 */
    return 8;
  case ST20_FMT_YUV_444_10BIT: /**< 10-bit YUV 4:4:4 */
    return 10;
  case ST20_FMT_YUV_444_12BIT: /**< 12-bit YUV 4:4:4 */
    return 12;
  case ST20_FMT_YUV_444_16BIT: /**< 16-bit YUV 4:4:4 */
    return 16;
  case ST20_FMT_MAX:
    return 8;
  }
  return 8;
}

nmos::interlace_mode get_interlace_mode(double fps, int height) {
  const auto frame_rate = nmos::parse_rational(web::json::value_of(
      {{nmos::fields::numerator, fps}, {nmos::fields::denominator, 1}}));
  const auto frame_height = height;
  return (nmos::rates::rate25 == frame_rate ||
          nmos::rates::rate29_97 == frame_rate) &&
                 1080 == frame_height
             ? nmos::interlace_modes::interlaced_tff
             : nmos::interlace_modes::progressive;
}
namespace seeder {
namespace nmos_node {

void Node::start() {
  thread_ = std::thread([this] { nmos_node_start(); });
}
Node::Node(std::string node_config_path_) { config_file_ = node_config_path_; }
Node::~Node() {}
int Node::nmos_node_start() {

  // Construct our data models including mutexes to protect them
  int i = 0;

  nmos::experimental::log_model log_model;

  // Streams for logging, initially configured to write errors to stderr and to
  // discard the access log
  std::filebuf error_log_buf;
  std::ostream error_log(std::cerr.rdbuf());
  std::filebuf access_log_buf;
  std::ostream access_log(&access_log_buf);

  // Logging should all go through this logging gateway
  nmos::experimental::log_gate gate(error_log, access_log, log_model);
  gate_ = &gate;
  try {
    slog::log<slog::severities::info>(gate, SLOG_FLF)
        << "Starting nmos-cpp node";

    // Settings can be passed on the command-line, directly or in a
    // configuration file, and a few may be changed dynamically by PATCH to
    // /settings/all on the Settings API
    //
    // * "logging_level": integer value, between 40 (least verbose, only fatal
    // messages) and -40 (most verbose)
    // * "registry_address": used to construct request URLs for registry APIs
    // (if not discovered via DNS-SD)
    //
    // E.g.
    //
    // # ./nmos-cpp-node "{\"logging_level\":-40}"
    // # ./nmos-cpp-node config.json
    // # curl -X PATCH -H "Content-Type: application/json"
    // http://localhost:3209/settings/all -d
    // "{\"logging_level\":-40}" # curl -X PATCH -H "Content-Type:
    // application/json" http://localhost:3209/settings/all -T config.json

    slog::log<slog::severities::info>(gate, SLOG_FLF)
        << "node_config_file_path: " << config_file_;
    if (config_file_.empty()) {
      return -1;
      ;
    }
    std::error_code error;
    node_model_.settings =
        web::json::value::parse(utility::s2us(config_file_), error);
    if (error) {
      std::ifstream file(config_file_);
      // check the file can be opened, and is parsed to an object
      file.exceptions(std::ios_base::failbit);
      node_model_.settings = web::json::value::parse(file);
      node_model_.settings.as_object();
    }

    // Prepare run-time default settings (different than header defaults)

    nmos::insert_node_default_settings(node_model_.settings);

    // copy to the logging settings
    // hmm, this is a bit icky, but simplest for now
    log_model.settings = node_model_.settings;

    // the logging level is a special case because we want to turn it into an
    // atomic value that can be read by logging statements without locking the
    // mutex protecting the settings
    log_model.level = nmos::fields::logging_level(log_model.settings);

    // Reconfigure the logging streams according to settings
    // (obviously, until this point, the logging gateway has its default
    // behaviour...)

    if (!nmos::fields::error_log(node_model_.settings).empty()) {
      error_log_buf.open(nmos::fields::error_log(node_model_.settings),
                         std::ios_base::out | std::ios_base::app);
      auto lock = log_model.write_lock();
      error_log.rdbuf(&error_log_buf);
    }

    if (!nmos::fields::access_log(node_model_.settings).empty()) {
      access_log_buf.open(nmos::fields::access_log(node_model_.settings),
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
        << "Initial settings: " << node_model_.settings.serialize();

    // Set up the callbacks between the node server and the underlying
    // implementation
    auto node_implementation = make_node_implementation();

    // Set up the node server

    auto node_server = nmos::experimental::make_node_server(
        node_model_, node_implementation, log_model, gate);

    // Add the underlying implementation, which will set up the node resources,
    // etc.

    node_server.thread_functions.push_back([&] { thread_run(); });

    if (!nmos::experimental::fields::http_trace(node_model_.settings)) {
      // Disable TRACE method

      for (auto &http_listener : node_server.http_listeners) {
        http_listener.support(
            web::http::methods::TRCE, [](web::http::http_request req) {
              req.reply(web::http::status_codes::MethodNotAllowed);
            });
      }
    }

    // Open the API ports and start up node operation (including the DNS-SD
    // advertisements)

    slog::log<slog::severities::info>(gate, SLOG_FLF)
        << "Preparing for connections";

    nmos::server_guard node_server_guard(node_server);

    slog::log<slog::severities::info>(gate, SLOG_FLF)
        << "Ready for connections";

    // Wait for a process termination signal
    nmos::details::wait_term_signal();

    slog::log<slog::severities::info>(gate, SLOG_FLF) << "Closing connections";
  } catch (const web::json::json_exception &e) {
    // most likely from incorrect syntax or incorrect value types in the command
    // line settings
    slog::log<slog::severities::error>(gate, SLOG_FLF)
        << "JSON error: " << e.what();
    i = 1;
  } catch (const web::http::http_exception &e) {
    slog::log<slog::severities::error>(gate, SLOG_FLF)
        << "HTTP error: " << e.what() << " [" << e.error_code() << "]";
    i = 1;
  } catch (const web::websockets::websocket_exception &e) {
    slog::log<slog::severities::error>(gate, SLOG_FLF)
        << "WebSocket error: " << e.what() << " [" << e.error_code() << "]";
    i = 1;
  } catch (const std::ios_base::failure &e) {
    // most likely from failing to open the command line settings file
    slog::log<slog::severities::error>(gate, SLOG_FLF)
        << "File error: " << e.what();
    i = 1;
  } catch (const std::system_error &e) {
    slog::log<slog::severities::error>(gate, SLOG_FLF)
        << "System error: " << e.what() << " [" << e.code() << "]";
    i = 1;
  } catch (const std::runtime_error &e) {
    slog::log<slog::severities::error>(gate, SLOG_FLF)
        << "Implementation error: " << e.what();
    i = 1;
  } catch (const std::exception &e) {
    slog::log<slog::severities::error>(gate, SLOG_FLF)
        << "Unexpected exception: " << e.what();
    i = 1;
  } catch (...) {
    slog::log<slog::severities::severe>(gate, SLOG_FLF)
        << "Unexpected unknown exception";
    i = 1;
  }

  slog::log<slog::severities::info>(gate, SLOG_FLF) << "Stopping nmos-cpp node";
  return i;
}
bool Node::insert_resource_after(unsigned int milliseconds,
                                 nmos::resources &resources,
                                 nmos::resource &&resource,
                                 slog::base_gate &gate,
                                 nmos::write_lock &lock) {

  if (nmos::details::wait_for(node_model_.shutdown_condition, lock,
                              std::chrono::milliseconds(milliseconds),
                              [&] { return node_model_.shutdown; })) {
    return false;
  }

  const std::pair<nmos::id, nmos::type> id_type{resource.id, resource.type};
  const bool success = insert_resource(resources, std::move(resource)).second;

  if (success)
    slog::log<slog::severities::info>(gate, SLOG_FLF)
        << "Updated node_model_ with " << id_type;
  else
    slog::log<slog::severities::severe>(gate, SLOG_FLF)
        << "Model update error: " << id_type;

  slog::log<slog::severities::too_much_info>(gate, SLOG_FLF)
      << "Notifying node behaviour thread"; // and anyone else who cares...
  node_model_.notify();
  return success;
}

bool Node::remove_resource_after(unsigned int milliseconds,
                                 nmos::resources &resources, nmos::id id,
                                 slog::base_gate &gate,
                                 nmos::write_lock &lock) {
  if (nmos::details::wait_for(node_model_.shutdown_condition, lock,
                              std::chrono::milliseconds(milliseconds),
                              [&] { return node_model_.shutdown; }))
    return false;

  const bool success = erase_resource(resources, id);
  if (success)
    slog::log<slog::severities::info>(gate, SLOG_FLF)
        << "Remove node_model_ with " << id;
  else
    slog::log<slog::severities::severe>(gate, SLOG_FLF)
        << "Model Remove error: " << id;

  slog::log<slog::severities::too_much_info>(gate, SLOG_FLF)
      << "Notifying node behaviour thread"; // and anyone else who cares...
  node_model_.notify();
  return success;
}

void Node::thread_run() {
  nmos::details::omanip_gate gate{
      *gate_, nmos::stash_category(impl::categories::node_implementation)};
  try {
    init();
    node_implementation_run();
  } catch (const node_implementation_init_exception &) {
    // node_implementation_init writes the log message
  } catch (const web::json::json_exception &e) {
    // most likely from incorrect value types in the command line settings
    slog::log<slog::severities::error>(gate, SLOG_FLF)
        << "JSON error: " << e.what();
  } catch (const std::system_error &e) {
    slog::log<slog::severities::error>(gate, SLOG_FLF)
        << "System error: " << e.what() << " [" << e.code() << "]";
  } catch (const std::runtime_error &e) {
    slog::log<slog::severities::error>(gate, SLOG_FLF)
        << "Implementation error: " << e.what();
  } catch (const std::exception &e) {
    slog::log<slog::severities::error>(gate, SLOG_FLF)
        << "Unexpected exception: " << e.what();
  } catch (...) {
    slog::log<slog::severities::severe>(gate, SLOG_FLF)
        << "Unexpected unknown exception";
  }
}

void Node::node_implementation_run() {
  auto lock = node_model_.read_lock();
  // wait for the thread to be interrupted because the server is being shut down
  node_model_.shutdown_condition.wait(lock,
                                      [&] { return node_model_.shutdown; });
  nmos::details::reverse_lock_guard<nmos::read_lock> unlock{lock};
}

// This constructs all the callbacks used to integrate the example
// device-specific underlying implementation into the server instance for the
// NMOS Node.
nmos::experimental::node_implementation Node::make_node_implementation() {

  return nmos::experimental::node_implementation()
      .on_parse_transport_file(make_node_implementation_transport_file_parser())
      .on_resolve_auto(
          make_node_implementation_auto_resolver(node_model_.settings))
      .on_set_transportfile(make_node_implementation_transportfile_setter(
          node_model_.node_resources, node_model_.settings))
      .on_connection_activated(
          make_node_implementation_connection_activation_handler());
}

// Example Connection API activation callback to perform application-specific
// operations to complete activation
nmos::connection_activation_handler
Node::make_node_implementation_connection_activation_handler() {
  return [&](const nmos::resource &resource,
             const nmos::resource &connection_resource) {
    const std::pair<nmos::id, nmos::type> id_type{resource.id, resource.type};

    const std::pair<nmos::id, nmos::type> id__type{connection_resource.id,
                                                   connection_resource.type};
    // std::string connection_resource_json =
    // connection_resource.data.serialize();
    std::string resource_json = resource.data.serialize();
    const web::json::array transport_params =
        nmos::fields::transport_params(
            nmos::fields::endpoint_active(connection_resource.data))
            .as_array();
    bool master_enable = nmos::fields::master_enable(
        nmos::fields::endpoint_active(connection_resource.data));
    std::string connection_resource_json = connection_resource.data.serialize();
    std::error_code ec{};
    std::string json;
    if (resource.type == nmos::types::receiver) {
      int dest_port =
          nmos::fields::destination_port(transport_params.at(0)).as_integer();
      std::string interface_ip =
          nmos::fields::interface_ip(transport_params.at(0)).as_string();
      std::string multicast_ip =
          nmos::fields::multicast_ip(transport_params.at(0)).as_string();
      std::string source_ip =
          nmos::fields::source_ip(transport_params.at(0)).as_string();

      int dest_port_07 = 5004;
      std::string interface_ip_07 = "";
      std::string multicast_ip_07 = "";
      std::string source_ip_07 = "";
      if (transport_params.size() > 1) {
        dest_port_07 =
            nmos::fields::destination_port(transport_params.at(1)).as_integer();
        interface_ip_07 =
            nmos::fields::interface_ip(transport_params.at(1)).as_string();

        multicast_ip_07 =
            nmos::fields::multicast_ip(transport_params.at(1)).as_string();
        source_ip_07 =
            nmos::fields::source_ip(transport_params.at(1)).as_string();
      }
      auto video_pair = receiver_video_session_map_.find(resource.id);
      auto audio_pair = receiver_audio_session_map_.find(resource.id);

      if (video_pair != receiver_video_session_map_.end()) {
        VideoReceiver video = video_pair->second;
        video.enable = master_enable;
        video.ip = multicast_ip;
        video.port = dest_port;
        video.redudancy.ip = multicast_ip_07;
        video.redudancy.port = dest_port_07;
        if (update_video_receiver_func) {
          update_video_receiver_func(video);
          slog::log<slog::severities::error>(*gate_, SLOG_FLF)
              << nmos::stash_category(impl::categories::node_implementation)
              << "update video receiver callback success";
        } else {
          // not found function
        }
      }

      if (audio_pair != receiver_audio_session_map_.end()) {
        AudioReceiver audio = audio_pair->second;
        audio.enable = master_enable;
        audio.ip = multicast_ip;
        audio.port = dest_port;
        audio.redudancy.ip = multicast_ip_07;
        audio.redudancy.port = dest_port_07;
        if (update_audio_receiver_func) {
          update_audio_receiver_func(audio);
        } else {
          // not found function
        }
      }

      // update_connect

    } else if (resource.type == nmos::types::sender) {
      auto video_pair = sender_video_session_map_.find(resource.id);
      auto audio_pair = sender_audio_session_map_.find(resource.id);

      if (video_pair != sender_video_session_map_.end()) {
        VideoSender video = video_pair->second;
        video.enable = master_enable;
        if (update_video_sender_func) {
          update_video_sender_func(video);
        } else {
          // not found function
        }
      }
      if (audio_pair != sender_audio_session_map_.end()) {
        AudioSender audio = audio_pair->second;
        audio.enable = master_enable;
        if (update_audio_sender_func) {
          update_audio_sender_func(audio);
        } else {
          // not found function
        }
      }
    }
    if (ec) {
      slog::log<slog::severities::error>(*gate_, SLOG_FLF)
          << nmos::stash_category(impl::categories::node_implementation)
          << "Activating  " << id_type << "   Error: " << ec.message();
    }

    slog::log<slog::severities::info>(*gate_, SLOG_FLF)
        << nmos::stash_category(impl::categories::node_implementation)
        << "Activating " << id_type;
  };
}

// Example Connection API callback to parse "transport_file" during a PATCH
// /staged request
nmos::transport_file_parser
Node::make_node_implementation_transport_file_parser() {
  // this example uses a custom transport file parser to handle video/jxsv in
  // addition to the core media types otherwise, it could simply return
  // &nmos::parse_rtp_transport_file (if this callback is specified, an 'empty'
  // std::function is not allowed)
  return
      [](const nmos::resource &receiver,
         const nmos::resource &connection_receiver,
         const utility::string_t &transport_file_type,
         const utility::string_t &transport_file_data, slog::base_gate &gate) {
        const auto validate_sdp_parameters =
            [](const web::json::value &receiver,
               const nmos::sdp_parameters &sdp_params) {
              if (nmos::media_types::video_jxsv ==
                  nmos::get_media_type(sdp_params)) {
                nmos::validate_video_jxsv_sdp_parameters(receiver, sdp_params);
              } else {
                // validate core media types, i.e., "video/raw", "audio/L",
                // "video/smpte291" and "video/SMPTE2022-6"
                try {
                  nmos::validate_sdp_parameters(receiver, sdp_params);
                } catch (std::runtime_error &e) {
                  throw std::runtime_error("视频/音频 格式错误");
                }
              }
            };
        return nmos::details::parse_rtp_transport_file(
            validate_sdp_parameters, receiver, connection_receiver,
            transport_file_type, transport_file_data, gate);
      };
}

// Example Connection API activation callback to resolve "auto" values when
// /staged is transitioned to /active
nmos::connection_resource_auto_resolver
Node::make_node_implementation_auto_resolver(const nmos::settings &settings) {
  using web::json::value;
  // although which properties may need to be defaulted depends on the resource
  // type, the default value will almost always be different for each resource
  return [&](const nmos::resource &resource,
             const nmos::resource &connection_resource,
             value &transport_params) {
    const std::pair<nmos::id, nmos::type> id_type{connection_resource.id,
                                                  connection_resource.type};

    // "In some cases the behaviour is more complex, and may be determined
    // by the vendor." See
    // https://specs.amwa.tv/is-05/releases/v1.0.0/docs/2.2._APIs_-_Server_Side_Implementation.html#use-of-auto
    bool smpte2022_7 = false;
    std::string source_ip = "";
    std::string ip = "";
    int port = 0;
    std::string redudancy_ip;
    if (id_type.second == nmos::types::sender) {

      // st_json_session_base_t st_json_sender;
      auto video_session =
          sender_video_session_map_.find(connection_resource.id);

      if (video_session != sender_video_session_map_.end()) {
        VideoSender video = video_session->second;
        if (setting_video_sender_func) {
          setting_video_sender_func(&video);
          smpte2022_7 = video.redudancy.enable;
          source_ip = video.source_ip;
          ip = video.ip;
          redudancy_ip = video.redudancy.ip;
          port = video.port;
        } else {
          slog::log<slog::severities::error>(*gate_, SLOG_FLF)
              << nmos::stash_category(impl::categories::node_implementation)
              << "Not Found Setting Video Sender Function";
        }
      }
      auto audio_session =
          sender_audio_session_map_.find(connection_resource.id);
      if (audio_session != sender_audio_session_map_.end()) {
        AudioSender audio = audio_session->second;
        if (setting_audio_sender_func) {
          setting_audio_sender_func(&audio);
          smpte2022_7 = audio.redudancy.enable;
          source_ip = audio.source_ip;
          ip = audio.ip;
          redudancy_ip = audio.redudancy.ip;
          port = audio.port;
        } else {
          slog::log<slog::severities::error>(*gate_, SLOG_FLF)
              << nmos::stash_category(impl::categories::node_implementation)
              << "Not Found Setting Audio Sender Function";
        }
      }
      nmos::details::resolve_auto(transport_params[0], nmos::fields::source_ip,
                                  [&] { return value::string(source_ip); });
      if (smpte2022_7) {
        nmos::details::resolve_auto(transport_params[1],
                                    nmos::fields::source_ip,
                                    [&] { return value::string(source_ip); });
      }
      nmos::details::resolve_auto(transport_params[0],
                                  nmos::fields::destination_ip,
                                  [&] { return value::string(ip); });
      if (smpte2022_7) {
        nmos::details::resolve_auto(
            transport_params[1], nmos::fields::destination_ip,
            [&] { return value::string(redudancy_ip); });
      }
      // lastly, apply the specification defaults for any properties not
      // handled above
      nmos::resolve_rtp_auto(id_type.second, transport_params, port);

    } else if (id_type.second == nmos::types::receiver) {
      bool smpte2022_7 = false;
      std::string ip = "";
      std::string redudancy_ip = "";
      auto video_session =
          receiver_video_session_map_.find(connection_resource.id);
      if (video_session != receiver_video_session_map_.end()) {
        VideoReceiver video = video_session->second;
        if (setting_video_receiver_func) {
          setting_video_receiver_func(&video);
          smpte2022_7 = video.enable;
          ip = video.ip;
          redudancy_ip = video.redudancy.ip;
        } else {
          slog::log<slog::severities::error>(*gate_, SLOG_FLF)
              << nmos::stash_category(impl::categories::node_implementation)
              << "Not Found Setting Video Revicver Function";
          // not found function
        }
      }
      auto audio_session =
          receiver_audio_session_map_.find(connection_resource.id);
      if (audio_session != receiver_audio_session_map_.end()) {
        AudioReceiver audio = audio_session->second;
        if (setting_audio_receiver_func) {
          setting_audio_receiver_func(&audio);
          smpte2022_7 = audio.enable;
          ip = audio.ip;
          redudancy_ip = audio.redudancy.ip;
        } else {
          slog::log<slog::severities::error>(*gate_, SLOG_FLF)
              << nmos::stash_category(impl::categories::node_implementation)
              << "Not Found Setting Audio Revicver Function";
          // not found function
        }
      }
      std::string json = transport_params.serialize();
      nmos::details::resolve_auto(transport_params[0],
                                  nmos::fields::interface_ip,
                                  [&] { return value::string(ip); });
      if (smpte2022_7) {
        nmos::details::resolve_auto(
            transport_params[1], nmos::fields::interface_ip,
            [&] { return value::string(redudancy_ip); });
      }

      json = transport_params.serialize();

      // lastly, apply the specification defaults for any properties not
      // handled above
      nmos::resolve_rtp_auto(id_type.second, transport_params);
    }
  };
}

nmos::connection_sender_transportfile_setter
Node::make_node_implementation_transportfile_setter(
    const nmos::resources &node_resources, const nmos::settings &settings) {
  using web::json::value;
  // as part of activation, the example sender /transportfile should be
  // updated based on the active transport parameters
  return [&](const nmos::resource &sender,
             const nmos::resource &connection_sender,
             value &endpoint_transportfile) {
    const auto found = boost::range::find(sender_ids_, connection_sender.id);
    if (sender_ids_.end() != found) {
      //  note, node_model_ mutex is already locked by the calling thread, so
      //  access to node_resources is OK...
      auto node =
          nmos::find_resource(node_resources, {node_id_, nmos::types::node});

      if (node_resources.end() == node) {
        throw std::logic_error("matching IS-04 node, source or flow not found");
      }
      std::string flow_id = sender.data.at("flow_id").as_string();
      auto flow =
          nmos::find_resource(node_resources, {flow_id, nmos::types::flow});
      std::string source_id = flow->data.at("source_id").as_string();
      auto source =
          nmos::find_resource(node_resources, {source_id, nmos::types::source});
      // the nmos::make_sdp_parameters overload from the IS-04 resources
      // provides a high-level interface for common "video/raw", "audio/L",
      // "video/smpte291" and "video/SMPTE2022-6" use cases
      // auto sdp_params = nmos::make_sdp_parameters(node->data, source->data,
      // flow->data, sender.data, { U("PRIMARY"), U("SECONDARY") });

      // nmos::make_{video,audio,data,mux}_sdp_parameters provide a little
      // more flexibility for those four media types and the combination of
      // nmos::make_{video_raw,audio_L,video_smpte291,video_SMPTE2022_6}_parameters
      // with the related make_sdp_parameters overloads provides the most
      // flexible and extensible approach
      auto sdp_params = [&] {
        const std::vector<utility::string_t> mids{U("PRIMARY"), U("SECONDARY")};
        const nmos::format format{nmos::fields::format(flow->data)};
        if (nmos::formats::video == format) {
          const nmos::media_type video_type{
              nmos::fields::media_type(flow->data)};
          if (nmos::media_types::video_raw == video_type) {
            return nmos::make_video_sdp_parameters(
                node->data, source->data, flow->data, sender.data,
                nmos::details::payload_type_video_default, mids, {},
                sdp::type_parameters::type_N);
          } else if (nmos::media_types::video_jxsv == video_type) {
            const auto params = nmos::make_video_jxsv_parameters(
                node->data, source->data, flow->data, sender.data);
            const auto ts_refclk = nmos::details::make_ts_refclk(
                node->data, source->data, sender.data, {});
            return nmos::make_sdp_parameters(
                nmos::fields::label(sender.data), params,
                nmos::details::payload_type_video_default, mids, ts_refclk);
          } else {
            throw std::logic_error("unexpected flow media_type");
          }
        } else if (nmos::formats::audio == format) {
          // this example application doesn't actually stream, so just
          // indicate a sensible value for packet time
          const double packet_time =
              nmos::fields::channels(source->data).size() > 8 ? 0.125 : 1;
          return nmos::make_audio_sdp_parameters(
              node->data, source->data, flow->data, sender.data,
              nmos::details::payload_type_audio_default, mids, {}, packet_time);
        } else if (nmos::formats::data == format) {
          return nmos::make_data_sdp_parameters(
              node->data, source->data, flow->data, sender.data,
              nmos::details::payload_type_data_default, mids, {}, {});
        } else if (nmos::formats::mux == format) {
          return nmos::make_mux_sdp_parameters(
              node->data, source->data, flow->data, sender.data,
              nmos::details::payload_type_mux_default, mids, {},
              sdp::type_parameters::type_N);
        } else {
          throw std::logic_error("unexpected flow format");
        }
      }();

      auto &transport_params = nmos::fields::transport_params(
          nmos::fields::endpoint_active(connection_sender.data));

      std::string transport_params_json = transport_params.serialize();

      auto session_description =
          nmos::make_session_description(sdp_params, transport_params);
      auto sdp =
          utility::s2us(sdp::make_session_description(session_description));
      endpoint_transportfile =
          nmos::make_connection_rtp_sender_transportfile(sdp);
    }
  };
}

void Node::init() {
  init_device();
  resolve_auto = make_node_implementation_auto_resolver(node_model_.settings);
  set_transportfile = make_node_implementation_transportfile_setter(
      node_model_.node_resources, node_model_.settings);
}
void Node::add_video_sender(VideoSender video) {
  nmos::write_lock lock = node_model_.write_lock();
  std::string id = video.id;
  std::string name = video.name;

  const auto transfer_characteristic = nmos::transfer_characteristic{
      impl::fields::transfer_characteristic(node_model_.settings)};

  const auto sampling = st_get_color_sampling(video.pg_format);
  const auto bit_depth = st_get_component_depth(video.pg_format);

  auto colorspace =
      nmos::colorspace{impl::fields::colorspace(node_model_.settings)};

  const auto source_id =
      impl::make_id(seed_id_, nmos::types::source, impl::ports::video, id);
  const auto flow_id =
      impl::make_id(seed_id_, nmos::types::flow, impl::ports::video, id);
  const auto sender_id =
      impl::make_id(seed_id_, nmos::types::sender, impl::ports::video, id);
  bool ST_2022_7 = video.redudancy.enable;
  nmos::rational frame_rate = nmos::parse_rational(web::json::value_of(
      {{nmos::fields::numerator, video.fps}, {nmos::fields::denominator, 1}}));
  const auto frame_width = video.width;
  const auto frame_height = video.height;
  if (frame_width == 3840 &&
      frame_height == 2160) { // 4K 的色彩空间 设置为 BT2020
    colorspace = nmos::colorspaces::BT2020;
  }
  nmos::resource source;

  source = nmos::make_video_source(
      source_id, device_id_, nmos::clock_names::clk0, frame_rate,
      node_model_.settings); // 底层使用了 label 和 description 字段

  impl::set_label_description(source, impl::ports::video, name);
  nmos::interlace_mode interlace_mode =
      get_interlace_mode(video.fps, video.height);

  nmos::resource flow;
  flow = nmos::make_raw_video_flow(flow_id, source_id, device_id_, frame_rate,
                                   frame_width, frame_height, interlace_mode,
                                   colorspace, transfer_characteristic,
                                   sampling, bit_depth, node_model_.settings);

  impl::set_label_description(flow, impl::ports::video, name);

  // set_transportfile needs to find the matching source and flow for the
  // sender, so insert these first
  if (!insert_resource_after(delay_millis, node_model_.node_resources,
                             std::move(source), *gate_, lock))
    throw node_implementation_init_exception("");
  if (!insert_resource_after(delay_millis, node_model_.node_resources,
                             std::move(flow), *gate_, lock))
    throw node_implementation_init_exception("");

  const auto manifest_href = nmos::experimental::make_manifest_api_manifest(
      sender_id, node_model_.settings);
  const auto interface_names =
      ST_2022_7 ? std::vector<utility::string_t>{primary_interface.name,
                                                 secondary_interface.name}
                : std::vector<utility::string_t>{primary_interface.name};
  auto sender = nmos::make_sender(sender_id, flow_id, nmos::transports::rtp,
                                  device_id_, manifest_href.to_string(),
                                  interface_names, node_model_.settings);
  impl::set_label_description(sender, impl::ports::video, name);
  impl::insert_group_hint(sender, impl::ports::video, id, name);

  auto connection_sender =
      nmos::make_connection_rtp_sender(sender_id, ST_2022_7);
  // add constraints;
  connection_sender
      .data[nmos::fields::endpoint_constraints][0][nmos::fields::source_ip] =
      value_of({{nmos::fields::constraint_enum,
                 value_from_elements(primary_interface.addresses)}});
  if (ST_2022_7)
    connection_sender
        .data[nmos::fields::endpoint_constraints][1][nmos::fields::source_ip] =
        value_of({{nmos::fields::constraint_enum,
                   value_from_elements(secondary_interface.addresses)}});

  if (video.enable) {
    // initialize this sender with a scheduled activation, e.g. to
    auto &staged = connection_sender.data[nmos::fields::endpoint_staged];
    staged[nmos::fields::master_enable] = value::boolean(true);
    staged[nmos::fields::activation] =
        value_of({{nmos::fields::mode,
                   nmos::activation_modes::activate_scheduled_relative.name},
                  {nmos::fields::requested_time, U("0:0")},
                  {nmos::fields::activation_time, nmos::make_version()}});
  }
  sender_video_session_map_.emplace(connection_sender.id, video);
  sender_ids_.push_back(sender_id);
  source_ids_.push_back(source_id);
  flow_ids_.push_back(flow_id);
  resolve_auto(sender, connection_sender,
               connection_sender.data[nmos::fields::endpoint_active]
                                     [nmos::fields::transport_params]);
  set_transportfile(sender, connection_sender,
                    connection_sender.data[nmos::fields::transport_file]);
  if (!insert_resource_after(delay_millis, node_model_.node_resources,
                             std::move(sender), *gate_, lock))
    throw node_implementation_init_exception("");
  if (!insert_resource_after(delay_millis, node_model_.connection_resources,
                             std::move(connection_sender), *gate_, lock))
    throw node_implementation_init_exception("");
}

void Node::add_audio_sender(AudioSender audio) {
  nmos::write_lock lock = node_model_.write_lock();

  nmos::rational frame_rate = nmos::parse_rational(web::json::value_of(
      {{nmos::fields::numerator, audio.fps}, {nmos::fields::denominator, 1}}));
  ;
  std::string id = audio.id;
  std::string name = audio.name;
  // TODO: 实现音频发送端
  // audio
  impl::port port = impl::ports::audio;

  const auto source_id = impl::make_id(seed_id_, nmos::types::source, port, id);
  const auto flow_id = impl::make_id(seed_id_, nmos::types::flow, port, id);
  const auto sender_id = impl::make_id(seed_id_, nmos::types::sender, port, id);

  const auto channels = boost::copy_range<std::vector<nmos::channel>>(
      boost::irange(0, audio.channel_count) |
      boost::adaptors::transformed([&](const int &index) {
        return impl::channels_repeat[index % (int)impl::channels_repeat.size()];
      }));

  int bit_depth = audio.bit_depth;
  int simple_rate = audio.simple_rate;
  nmos::resource source =
      nmos::make_audio_source(source_id, device_id_, nmos::clock_names::clk0,
                              frame_rate, channels, node_model_.settings);
  // impl::insert_parents(source, seed_id_, port, index);
  impl::set_label_description(source, port, name);
  nmos::resource flow =
      nmos::make_raw_audio_flow(flow_id, source_id, device_id_, simple_rate,
                                bit_depth, node_model_.settings);

  // add optional grain_rate
  flow.data[nmos::fields::grain_rate] = nmos::make_rational(frame_rate);

  // impl::insert_parents(flow, seed_id_, port, index);
  impl::set_label_description(flow, port, name);

  // set_transportfile needs to find the matching source and flow for the
  // sender, so insert these first
  if (!insert_resource_after(delay_millis, node_model_.node_resources,
                             std::move(source), *gate_, lock))
    throw node_implementation_init_exception("");
  if (!insert_resource_after(delay_millis, node_model_.node_resources,
                             std::move(flow), *gate_, lock))
    throw node_implementation_init_exception("");

  const auto manifest_href = nmos::experimental::make_manifest_api_manifest(
      sender_id, node_model_.settings);
  bool ST_2022_7 = audio.redudancy.enable;

  const auto interface_names =
      ST_2022_7 ? std::vector<utility::string_t>{primary_interface.name,
                                                 secondary_interface.name}
                : std::vector<utility::string_t>{primary_interface.name};
  auto sender = nmos::make_sender(sender_id, flow_id, nmos::transports::rtp,
                                  device_id_, manifest_href.to_string(),
                                  interface_names, node_model_.settings);
  impl::set_label_description(sender, port, name);
  impl::insert_group_hint(sender, port, id, name);

  auto connection_sender =
      nmos::make_connection_rtp_sender(sender_id, ST_2022_7);
  // add constraints;
  connection_sender
      .data[nmos::fields::endpoint_constraints][0][nmos::fields::source_ip] =
      value_of({{nmos::fields::constraint_enum,
                 value_from_elements(primary_interface.addresses)}});
  if (ST_2022_7)
    connection_sender
        .data[nmos::fields::endpoint_constraints][1][nmos::fields::source_ip] =
        value_of({{nmos::fields::constraint_enum,
                   value_from_elements(secondary_interface.addresses)}});

  if (audio.enable) {
    // initialize this sender with a scheduled activation, e.g. to enable the
    auto &staged = connection_sender.data[nmos::fields::endpoint_staged];
    staged[nmos::fields::master_enable] = value::boolean(true);
    staged[nmos::fields::activation] =
        value_of({{nmos::fields::mode,
                   nmos::activation_modes::activate_scheduled_relative.name},
                  {nmos::fields::requested_time, U("0:0")},
                  {nmos::fields::activation_time, nmos::make_version()}});
  }

  sender_audio_session_map_.emplace(connection_sender.id, audio);
  sender_ids_.push_back(sender_id);
  source_ids_.push_back(source_id);
  flow_ids_.push_back(flow_id);
  resolve_auto(sender, connection_sender,
               connection_sender.data[nmos::fields::endpoint_active]
                                     [nmos::fields::transport_params]);
  set_transportfile(sender, connection_sender,
                    connection_sender.data[nmos::fields::transport_file]);
  if (!insert_resource_after(delay_millis, node_model_.node_resources,
                             std::move(sender), *gate_, lock))
    throw node_implementation_init_exception("");
  if (!insert_resource_after(delay_millis, node_model_.connection_resources,
                             std::move(connection_sender), *gate_, lock))
    throw node_implementation_init_exception("");
}

/**
  初始化设备
*/
void Node::init_device() {
  nmos::write_lock lock = node_model_.write_lock();

  const auto seed_id =
      nmos::experimental::fields::seed_id(node_model_.settings);
  const auto node_id = impl::make_id(seed_id, nmos::types::node);
  const auto device_id = impl::make_id(seed_id, nmos::types::device);
  node_id_ = node_id;
  device_id_ = device_id;
  seed_id_ = seed_id;

  const auto clocks =
      web::json::value_of({nmos::make_internal_clock(nmos::clock_names::clk0)});
  // filter network interfaces to those that correspond to the specified
  // host_addresses
  const auto host_interfaces = nmos::get_host_interfaces(node_model_.settings);
  const auto interfaces = nmos::experimental::node_interfaces(host_interfaces);

  // example node
  {
    auto node =
        nmos::make_node(node_id, clocks, nmos::make_node_interfaces(interfaces),
                        node_model_.settings);
    node.data[nmos::fields::tags] =
        impl::fields::node_tags(node_model_.settings);
    if (!insert_resource_after(delay_millis, node_model_.node_resources,
                               std::move(node), *gate_, lock))
      throw node_implementation_init_exception("");
  }

#ifdef HAVE_LLDP
  // LLDP manager for advertising server identity, capabilities, and
  // discovering neighbours on a local area network
  slog::log<slog::severities::info>(gate, SLOG_FLF)
      << "Attempting to configure LLDP";
  auto lldp_manager = nmos::experimental::make_lldp_manager(
      node_model_, interfaces, true, gate);
  // hm, open may potentially throw?
  lldp::lldp_manager_guard lldp_manager_guard(lldp_manager);
#endif

  // prepare interface bindings for all senders and receivers
  const auto &host_address = nmos::fields::host_address(node_model_.settings);
  // the interface corresponding to the host address is used for the example
  // node's WebSocket senders and receivers
  const auto host_interface_ =
      impl::find_interface(host_interfaces, host_address);
  if (host_interfaces.end() == host_interface_) {
    slog::log<slog::severities::severe>(*gate_, SLOG_FLF)
        << "No network interface corresponding to host_address?";
    throw node_implementation_init_exception("");
  }
  // hmm, should probably add a custom setting to control the primary and
  // secondary interfaces for the example node's RTP senders and receivers
  // rather than just picking the one(s) corresponding to the first and last
  // of the specified host addresses
  const auto &primary_address =
      node_model_.settings.has_field(nmos::fields::host_addresses)
          ? web::json::front(nmos::fields::host_addresses(node_model_.settings))
                .as_string()
          : host_address;
  const auto &secondary_address =
      node_model_.settings.has_field(nmos::fields::host_addresses)
          ? web::json::back(nmos::fields::host_addresses(node_model_.settings))
                .as_string()
          : host_address;
  const auto primary_interfaces =
      impl::find_interface(host_interfaces, primary_address);
  const auto secondary_interfaces =
      impl::find_interface(host_interfaces, secondary_address);
  if (host_interfaces.end() == primary_interfaces ||
      host_interfaces.end() == secondary_interfaces) {
    slog::log<slog::severities::severe>(*gate_, SLOG_FLF)
        << "No network interface corresponding to one of the host_addresses?";
    throw node_implementation_init_exception("");
  }
  primary_interface = *primary_interfaces;
  secondary_interface = *secondary_interfaces;

  // example device
  {
    std::vector<nmos::id> empty;
    auto device = nmos::make_device(device_id, node_id, empty, empty,
                                    node_model_.settings);
    device.data[nmos::fields::tags] =
        impl::fields::device_tags(node_model_.settings);
    if (!insert_resource_after(delay_millis, node_model_.node_resources,
                               std::move(device), *gate_, lock))
      throw node_implementation_init_exception("");
  }
}

void Node::add_video_receiver(VideoReceiver video) {
  nmos::write_lock lock = node_model_.write_lock();
  std::string id = video.id;
  std::string name = video.name;
  bool ST_2022_7 = video.redudancy.enable;
  nmos::rational frame_rate = nmos::parse_rational(web::json::value_of(
      {{nmos::fields::numerator, video.fps}, {nmos::fields::denominator, 1}}));
  const auto frame_width = video.width;
  const auto frame_height = video.height;
  const auto sampling = st_get_color_sampling(video.pg_format);
  const auto receiver_id =
      impl::make_id(seed_id_, nmos::types::receiver, impl::ports::video, id);
  const auto interface_names =
      ST_2022_7 ? std::vector<utility::string_t>{primary_interface.name,
                                                 secondary_interface.name}
                : std::vector<utility::string_t>{primary_interface.name};
  nmos::resource receiver;
  receiver =
      nmos::make_receiver(receiver_id, device_id_, nmos::transports::rtp,
                          interface_names, nmos::formats::video,
                          {nmos::media_types::video_raw}, node_model_.settings);
  nmos::interlace_mode interlace_mode =
      get_interlace_mode(video.fps, video.height);
  // add an example constraint set; these should be completed fully!
  const auto interlace_modes =
      nmos::interlace_modes::progressive != interlace_mode
          ? std::vector<
                utility::string_t>{nmos::interlace_modes::interlaced_bff.name,
                                   nmos::interlace_modes::interlaced_tff.name,
                                   nmos::interlace_modes::interlaced_psf.name}
          : std::vector<utility::string_t>{
                nmos::interlace_modes::progressive.name};
  receiver.data[nmos::fields::caps][nmos::fields::constraint_sets] = value_of(
      {value_of({{nmos::caps::format::grain_rate,
                  nmos::make_caps_rational_constraint({frame_rate})},
                 {nmos::caps::format::frame_width,
                  nmos::make_caps_integer_constraint({frame_width})},
                 {nmos::caps::format::frame_height,
                  nmos::make_caps_integer_constraint({frame_height})},
                 {nmos::caps::format::interlace_mode,
                  nmos::make_caps_string_constraint(interlace_modes)},
                 {nmos::caps::format::color_sampling,
                  nmos::make_caps_string_constraint({sampling.name})}})});

  receiver.data[nmos::fields::version] =
      receiver.data[nmos::fields::caps][nmos::fields::version] =
          value(nmos::make_version());
  impl::set_label_description(receiver, impl::ports::video, name);
  impl::insert_group_hint(receiver, impl::ports::video, id, name);

  auto connection_receiver =
      nmos::make_connection_rtp_receiver(receiver_id, ST_2022_7);
  // add some example constraints; these should be completed fully!
  connection_receiver
      .data[nmos::fields::endpoint_constraints][0][nmos::fields::interface_ip] =
      value_of({{nmos::fields::constraint_enum,
                 value_from_elements(primary_interface.addresses)}});

  if (ST_2022_7)
    connection_receiver.data[nmos::fields::endpoint_constraints][1]
                            [nmos::fields::interface_ip] =
        value_of({{nmos::fields::constraint_enum,
                   value_from_elements(secondary_interface.addresses)}});
  receiver_video_session_map_.emplace(connection_receiver.id, video);
  resolve_auto(receiver, connection_receiver,
               connection_receiver.data[nmos::fields::endpoint_active]
                                       [nmos::fields::transport_params]);

  if (!insert_resource_after(delay_millis, node_model_.node_resources,
                             std::move(receiver), *gate_, lock))
    throw node_implementation_init_exception("");
  if (!insert_resource_after(delay_millis, node_model_.connection_resources,
                             std::move(connection_receiver), *gate_, lock))
    throw node_implementation_init_exception("");
}

void Node::add_audio_receiver(AudioReceiver audio) {
  nmos::write_lock lock = node_model_.write_lock();

  std::string id = audio.id;
  std::string name = audio.name;
  bool ST_2022_7 = audio.redudancy.enable;
  int bit_depth = audio.bit_depth;
  const auto receiver_id =
      impl::make_id(seed_id_, nmos::types::receiver, impl::ports::audio, id);
  const auto interface_names =
      ST_2022_7 ? std::vector<utility::string_t>{primary_interface.name,
                                                 secondary_interface.name}
                : std::vector<utility::string_t>{primary_interface.name};
  nmos::resource receiver = nmos::make_audio_receiver(
      receiver_id, device_id_, nmos::transports::rtp, interface_names,
      bit_depth, node_model_.settings);

  int sample_rate =
      audio.simple_rate; // st30_get_sample_rate(session->info.audio_sampling);
  double packet_time = audio.packet_time;
  // (double)st30_get_packet_time(session->info.audio_ptime) / 1000000;

  // add some example constraint sets; these should be completed fully!
  receiver.data[nmos::fields::caps][nmos::fields::constraint_sets] = value_of(
      {value_of(
           {{nmos::caps::format::channel_count,
             nmos::make_caps_integer_constraint({}, 1, audio.channel_count)},
            {nmos::caps::format::sample_rate,
             nmos::make_caps_rational_constraint({{sample_rate, 1}})},
            {nmos::caps::format::sample_depth,
             nmos::make_caps_integer_constraint({bit_depth})},
            {nmos::caps::transport::packet_time,
             nmos::make_caps_number_constraint({packet_time})}}),
       value_of(
           {{nmos::caps::meta::preference, -1},
            {nmos::caps::format::channel_count,
             nmos::make_caps_integer_constraint({}, 1, audio.channel_count)},
            {nmos::caps::format::sample_rate,
             nmos::make_caps_rational_constraint({{sample_rate, 1}})},
            {nmos::caps::format::sample_depth,
             nmos::make_caps_integer_constraint({bit_depth})},
            {nmos::caps::transport::packet_time,
             nmos::make_caps_number_constraint({packet_time})}})});
  receiver.data[nmos::fields::version] =
      receiver.data[nmos::fields::caps][nmos::fields::version] =
          value(nmos::make_version());

  impl::set_label_description(receiver, impl::ports::audio, name);
  impl::insert_group_hint(receiver, impl::ports::audio, id, name);

  auto connection_receiver =
      nmos::make_connection_rtp_receiver(receiver_id, ST_2022_7);
  // add some example constraints; these should be completed fully!
  connection_receiver
      .data[nmos::fields::endpoint_constraints][0][nmos::fields::interface_ip] =
      value_of({{nmos::fields::constraint_enum,
                 value_from_elements(primary_interface.addresses)}});
  if (ST_2022_7) {
    connection_receiver.data[nmos::fields::endpoint_constraints][1]
                            [nmos::fields::interface_ip] =
        value_of({{nmos::fields::constraint_enum,
                   value_from_elements(secondary_interface.addresses)}});
  }

  receiver_audio_session_map_.emplace(connection_receiver.id, audio);
  // resolve_auto(receiver, connection_receiver,
  //              connection_receiver.data[nmos::fields::endpoint_active]
  //                                      [nmos::fields::transport_params]);

  if (!insert_resource_after(delay_millis, node_model_.node_resources,
                             std::move(receiver), *gate_, lock)) {
    throw node_implementation_init_exception("");
  }
  if (!insert_resource_after(delay_millis, node_model_.connection_resources,
                             std::move(connection_receiver), *gate_, lock)) {
    throw node_implementation_init_exception("");
  }
}

void Node::remove_audio_sender(std::string id) {
  if (sender_audio_session_map_.find(id) == nullptr) {
    return;
  }
  nmos::write_lock lock = node_model_.write_lock();

  const auto source_a_id =
      impl::make_id(seed_id_, nmos::types::source, impl::ports::audio, id);
  const auto flow_a_id =
      impl::make_id(seed_id_, nmos::types::flow, impl::ports::audio, id);
  const auto sender_a_id =
      impl::make_id(seed_id_, nmos::types::sender, impl::ports::audio, id);

  remove_resource_after(delay_millis, node_model_.node_resources, sender_a_id,
                        *gate_, lock);

  remove_resource_after(delay_millis, node_model_.connection_resources,
                        sender_a_id, *gate_, lock);
  remove_resource_after(delay_millis, node_model_.node_resources, source_a_id,
                        *gate_, lock);

  remove_resource_after(delay_millis, node_model_.node_resources, flow_a_id,
                        *gate_, lock);
  sender_audio_session_map_.erase(sender_a_id);

  const auto found_a_sender = boost::range::find(sender_ids_, sender_a_id);
  const auto idx_a_sender = int(found_a_sender - sender_ids_.begin());
  sender_ids_.erase(sender_ids_.begin() + idx_a_sender);

  const auto found_a_source = boost::range::find(source_ids_, source_a_id);
  const auto idx_a_source = int(found_a_source - source_ids_.begin());
  source_ids_.erase(source_ids_.begin() + idx_a_source);

  const auto found_a_flow = boost::range::find(flow_ids_, flow_a_id);
  const auto idx_a_flow = int(found_a_flow - flow_ids_.begin());
  flow_ids_.erase(flow_ids_.begin() + idx_a_flow);
  slog::log<slog::severities::warning>(*gate_, SLOG_FLF)
      << nmos::stash_category(impl::categories::node_implementation)
      << "Remove id:" << id << "  sender_id:" << sender_a_id;
}

void Node::remove_video_sender(std::string id) {
  if (sender_video_session_map_.find(id) == nullptr) {
    return;
  }
  nmos::write_lock lock = node_model_.write_lock();

  const auto source_v_id =
      impl::make_id(seed_id_, nmos::types::source, impl::ports::video, id);
  const auto flow_v_id =
      impl::make_id(seed_id_, nmos::types::flow, impl::ports::video, id);
  const auto sender_v_id =
      impl::make_id(seed_id_, nmos::types::sender, impl::ports::video, id);

  remove_resource_after(delay_millis, node_model_.node_resources, sender_v_id,
                        *gate_, lock);

  remove_resource_after(delay_millis, node_model_.connection_resources,
                        sender_v_id, *gate_, lock);
  remove_resource_after(delay_millis, node_model_.node_resources, source_v_id,
                        *gate_, lock);

  remove_resource_after(delay_millis, node_model_.node_resources, flow_v_id,
                        *gate_, lock);

  sender_video_session_map_.erase(sender_v_id);

  const auto found_v_sender = boost::range::find(sender_ids_, sender_v_id);
  const auto idx_v_sender = int(found_v_sender - sender_ids_.begin());
  sender_ids_.erase(sender_ids_.begin() + idx_v_sender);

  const auto found_v_source = boost::range::find(source_ids_, source_v_id);
  const auto idx_v_source = int(found_v_source - source_ids_.begin());
  source_ids_.erase(source_ids_.begin() + idx_v_source);

  const auto found_v_flow = boost::range::find(flow_ids_, flow_v_id);
  const auto idx_v_flow = int(found_v_flow - flow_ids_.begin());
  flow_ids_.erase(flow_ids_.begin() + idx_v_flow);
  slog::log<slog::severities::warning>(*gate_, SLOG_FLF)
      << nmos::stash_category(impl::categories::node_implementation)
      << "Remove id:" << id << "  sender_id:" << sender_v_id;
}

void Node::remove_video_receiver(std::string id) {
  nmos::write_lock lock = node_model_.write_lock();
  nmos::id receiver_id;
  for (auto pair : receiver_video_session_map_) {
    if (pair.second.id == id) {
      receiver_id = pair.first;
      slog::log<slog::severities::warning>(*gate_, SLOG_FLF)
          << nmos::stash_category(impl::categories::node_implementation)
          << "Removeing id:" << id << "  receiver_id:" << receiver_id;
    }
  }
  if (receiver_id.empty()) {
    return;
  }
  remove_resource_after(delay_millis, node_model_.node_resources, receiver_id,
                        *gate_, lock);
  remove_resource_after(delay_millis, node_model_.connection_resources,
                        receiver_id, *gate_, lock);
  receiver_video_session_map_.erase(receiver_id);
}

void Node::remove_audio_receiver(std::string id) {
  nmos::write_lock lock = node_model_.write_lock();
  nmos::id receiver_id;
  for (auto pair : receiver_audio_session_map_) {
    if (pair.second.id == id) {
      receiver_id = pair.first;
    }
  }
  if (receiver_id.empty()) {
    return;
  }
  remove_resource_after(delay_millis, node_model_.node_resources, receiver_id,
                        *gate_, lock);
  remove_resource_after(delay_millis, node_model_.connection_resources,
                        receiver_id, *gate_, lock);
  receiver_audio_session_map_.erase(receiver_id);
}
void Node::update_video_sender(VideoSender video) {
  remove_video_sender(video.id);
  add_video_sender(video);
}
void Node::update_audio_sender(AudioSender audio) {
  remove_audio_sender(audio.id);
  add_audio_sender(audio);
}

void Node::update_video_receiver(VideoReceiver video) {
  remove_video_receiver(video.id);
  add_video_receiver(video);
}
void Node::update_audio_receiver(AudioReceiver audio) {
  remove_audio_receiver(audio.id);
  add_audio_receiver(audio);
}

} // namespace nmos_node
} // namespace seeder