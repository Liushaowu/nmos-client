#pragma once
#include "cpprest/grant_type.h"
#include "cpprest/token_endpoint_auth_method.h"
#include "nmos/api_utils.h" // for make_api_listener
#include "nmos/authorization_behaviour.h"
#include "nmos/authorization_redirect_api.h"
#include "nmos/authorization_state.h"
#include "nmos/control_protocol_state.h"
#include "nmos/id.h"
#include "nmos/jwks_uri_api.h"
#include "nmos/log_gate.h"
#include "nmos/model.h"
#include "nmos/node_server.h"
#include "nmos/ocsp_behaviour.h"
#include "nmos/ocsp_response_handler.h"
#include "nmos/ocsp_state.h"
#include "nmos/process_utils.h"
#include "nmos/server.h"
#include "nmos/server_utils.h" // for make_http_listener_config
#include "node_implementation.h"
#include "string"
#include <cpprest/details/basic_types.h>
#include <cstddef>
#include <fstream>
#include <functional>
#include <iostream>
#include <mtl/st20_api.h>
#include <nmos/id.h>
#include <nmos/mutex.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
const unsigned int delay_millis{0};
typedef std::unordered_map<nmos::id, std::string> session_map_t;
namespace seeder {
namespace nmos_node {

struct Redudancy {
  bool enable = false;
  std::string ip;
  int port;
};

struct VideoSender {
  std::string id;
  std::string name;
  bool enable = false;
  double fps;
  int width;
  int height;
  std::string source_ip;
  std::string ip;
  int port;
  Redudancy redudancy;
  st20_fmt pg_format;
};

struct AudioSender {
  double fps;
  std::string id;
  std::string name;
  std::string source_ip;
  bool enable = false;
  int channel_count;
  int bit_depth;
  int simple_rate;
  std::string ip;
  int port;
  Redudancy redudancy;
};
struct VideoReceiver {
  std::string id;
  std::string name;
  bool enable = false;
  double fps;
  int width;
  int height;
  std::string source_ip;
  std::string ip;
  int port;
  Redudancy redudancy;
  st20_fmt pg_format;
};
struct AudioReceiver {
  double fps;
  std::string id;
  std::string name;
  bool enable = false;
  int channel_count;
  int bit_depth;
  int simple_rate;
  double packet_time;
  std::string ip;
  std::string source_ip;
  int port;
  Redudancy redudancy;
};
class Node {
private:
  std::thread thread_;
  std::string config_file_;
  nmos::experimental::log_gate *gate_;
  nmos::id node_id_;
  nmos::id device_id_;
  utility::string_t seed_id_;
  std::vector<nmos::id> sender_ids_;
  std::vector<nmos::id> receiver_ids_;
  std::vector<nmos::id> node_ids_;
  std::vector<nmos::id> device_ids_;
  std::vector<nmos::id> source_ids_;
  std::vector<nmos::id> flow_ids_;
  web::hosts::experimental::host_interface primary_interface;
  web::hosts::experimental::host_interface secondary_interface;

  std::unordered_map<nmos::id, VideoSender> sender_video_session_map_;
  std::unordered_map<nmos::id, AudioSender> sender_audio_session_map_;

  std::unordered_map<nmos::id, VideoReceiver> receiver_video_session_map_;
  std::unordered_map<nmos::id, AudioReceiver> receiver_audio_session_map_;

  nmos::connection_sender_transportfile_setter set_transportfile;
  nmos::connection_resource_auto_resolver resolve_auto;
  bool insert_resource_after(unsigned int milliseconds,
                             nmos::resources &resources,
                             nmos::resource &&resource, slog::base_gate &gate,
                             nmos::write_lock &lock);
  bool remove_resource_after(unsigned int milliseconds,
                             nmos::resources &resources, nmos::id id,
                             slog::base_gate &gate, nmos::write_lock &lock);
  nmos::experimental::node_implementation make_node_implementation();
  nmos::system_global_handler make_node_implementation_system_global_handler();
  nmos::registration_handler make_node_implementation_registration_handler();
  nmos::transport_file_parser make_node_implementation_transport_file_parser();
  nmos::connection_activation_handler
  make_node_implementation_connection_activation_handler();
  nmos::connection_resource_auto_resolver
  make_node_implementation_auto_resolver(const nmos::settings &settings);
  nmos::connection_sender_transportfile_setter
  make_node_implementation_transportfile_setter(
      const nmos::resources &node_resources, const nmos::settings &settings);
  void node_implementation_run();
  void init_device();
  void init();
  void thread_run();
  int nmos_node_start();

public:
  nmos::node_model node_model_;

  Node(std::string node_config_path_);
  ~Node();
  std::function<void(VideoSender video)> update_video_sender_func;
  std::function<void(AudioSender audio)> update_audio_sender_func;
  std::function<void(VideoReceiver video)> update_video_receiver_func;
  std::function<void(AudioReceiver audio)> update_audio_receiver_func;

  std::function<void(VideoSender *video)> setting_video_sender_func;
  std::function<void(AudioSender *audio)> setting_audio_sender_func;
  std::function<void(VideoReceiver *video)> setting_video_receiver_func;
  std::function<void(AudioReceiver *audio)> setting_audio_receiver_func;
  void start();
  void add_video_sender(VideoSender video);
  void add_audio_sender(AudioSender audio);

  void add_video_receiver(VideoReceiver video);
  void add_audio_receiver(AudioReceiver audio);

  void remove_video_sender(std::string id);
  void remove_audio_sender(std::string id);

  void remove_video_receiver(std::string id);
  void remove_audio_receiver(std::string id);

  void update_video_sender(VideoSender video);
  void update_audio_sender(AudioSender audio);

  void update_video_receiver(VideoReceiver video);
  void update_audio_receiver(AudioReceiver audio);
};
} // namespace nmos_node
} // namespace seeder
