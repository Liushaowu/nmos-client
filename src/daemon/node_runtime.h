#pragma once

#include "dto.h"

#include <functional>
#include <mutex>
#include <variant>

#include <cpprest/json.h>
#include <cpprest/host_utils.h>

namespace seeder::nmos_sync {

struct ReceiverEvent {
  using Payload = std::variant<nmos_node::VideoReceiver, nmos_node::AudioReceiver,
                               nmos_node::AncillaryReceiver>;
  Payload payload;
};

struct RegistrationEvent {
  nmos_node::RegistrationStatus status;
};

class NodeRuntime {
public:
  explicit NodeRuntime(const std::string &node_config_path);

  bool start();
  void stop();

  void set_receiver_event_handler(
      std::function<void(const ReceiverEvent &event)> handler);
  void set_registration_event_handler(
      std::function<void(const RegistrationEvent &event)> handler);

  void set_ptp_clock(const PtpClockDto &ptp_clock);
  void set_runtime_devices(const std::vector<SnapshotDto::DeviceDto> &devices);
  web::json::value effective_settings() const;
  web::json::value persisted_settings() const;
  web::json::value discover_registration_apis() const;
  void write_persisted_settings(const web::json::value &settings);

  void apply_video_sender(const nmos_node::VideoSender &sender);
  void apply_audio_sender(const nmos_node::AudioSender &sender);
  void apply_ancillary_sender(const nmos_node::AncillarySender &sender);
  void apply_video_receiver(const nmos_node::VideoReceiver &receiver);
  void apply_audio_receiver(const nmos_node::AudioReceiver &receiver);
  void apply_ancillary_receiver(const nmos_node::AncillaryReceiver &receiver);

  void update_video_sender(const nmos_node::VideoSender &sender);
  void update_audio_sender(const nmos_node::AudioSender &sender);
  void update_ancillary_sender(const nmos_node::AncillarySender &sender);
  void update_video_receiver(const nmos_node::VideoReceiver &receiver);
  void update_audio_receiver(const nmos_node::AudioReceiver &receiver);
  void update_ancillary_receiver(const nmos_node::AncillaryReceiver &receiver);

  void remove_video_sender(const std::string &id);
  void remove_audio_sender(const std::string &id);
  void remove_ancillary_sender(const std::string &id);
  void remove_video_receiver(const std::string &id);
  void remove_audio_receiver(const std::string &id);
  void remove_ancillary_receiver(const std::string &id);

private:
  void publish_event(const ReceiverEvent &event);
  void publish_event(const RegistrationEvent &event);

  nmos_node::Node node_;
  std::function<void(const ReceiverEvent &event)> receiver_event_handler_;
  std::function<void(const RegistrationEvent &event)> registration_event_handler_;
  std::mutex callback_mutex_;
};

}
