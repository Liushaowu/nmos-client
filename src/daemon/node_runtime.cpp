#include "node_runtime.h"

namespace seeder::nmos_sync {

namespace {

web::hosts::experimental::host_interface device_to_interface(
    const SnapshotDto::DeviceDto &device) {
  std::vector<utility::string_t> addresses;
  if (!device.sip.empty()) {
    addresses.push_back(utility::conversions::to_string_t(device.sip));
  }

  return web::hosts::experimental::host_interface(
      static_cast<std::uint32_t>(device.id),
      utility::conversions::to_string_t(device.display_name),
      utility::conversions::to_string_t(device.device), addresses, U("."));
}

}

NodeRuntime::NodeRuntime(const std::string &node_config_path)
    : node_(node_config_path) {
  node_.set_update_video_receiver_callback(
      [&](const nmos_node::VideoReceiver &receiver)
      { publish_event(ReceiverEvent{receiver}); });
  node_.set_update_audio_receiver_callback(
      [&](const nmos_node::AudioReceiver &receiver)
      { publish_event(ReceiverEvent{receiver}); });
  node_.set_update_ancillary_receiver_callback(
      [&](const nmos_node::AncillaryReceiver &receiver)
      { publish_event(ReceiverEvent{receiver}); });
  node_.set_registration_changed_callback(
      [&](const nmos_node::RegistrationStatus &status)
      { publish_event(RegistrationEvent{status}); });
}

bool NodeRuntime::start() { return node_.start(); }

void NodeRuntime::stop() { node_.stop(); }

void NodeRuntime::set_receiver_event_handler(
    std::function<void(const ReceiverEvent &event)> handler) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  receiver_event_handler_ = std::move(handler);
}

void NodeRuntime::set_registration_event_handler(
    std::function<void(const RegistrationEvent &event)> handler) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  registration_event_handler_ = std::move(handler);
}

void NodeRuntime::set_ptp_clock(const PtpClockDto &ptp_clock) {
  node_.set_ptp_clock(ptp_clock.effective_gmid(),
                      ptp_clock.effective_locked(),
                      ptp_clock.effective_ptp_domain());
}

void NodeRuntime::set_runtime_devices(
    const std::vector<SnapshotDto::DeviceDto> &devices) {
  if (devices.empty()) {
    return;
  }

  const auto primary = device_to_interface(devices.front());
  const auto secondary =
      1 < devices.size() ? device_to_interface(devices[1]) : primary;
  node_.set_runtime_interfaces(primary, secondary);
}

web::json::value NodeRuntime::effective_settings() const {
  return node_.effective_settings();
}

web::json::value NodeRuntime::persisted_settings() const {
  return node_.persisted_settings();
}

web::json::value NodeRuntime::discover_registration_apis() const {
  return node_.discover_registration_apis();
}

void NodeRuntime::write_persisted_settings(const web::json::value &settings) {
  node_.write_persisted_settings(settings);
}

void NodeRuntime::apply_video_sender(const nmos_node::VideoSender &sender) {
  node_.add_video_sender(sender);
}

void NodeRuntime::apply_audio_sender(const nmos_node::AudioSender &sender) {
  node_.add_audio_sender(sender);
}

void NodeRuntime::apply_ancillary_sender(
    const nmos_node::AncillarySender &sender) {
  node_.add_ancillary_sender(sender);
}

void NodeRuntime::apply_video_receiver(
    const nmos_node::VideoReceiver &receiver) {
  node_.add_video_receiver(receiver);
}

void NodeRuntime::apply_audio_receiver(
    const nmos_node::AudioReceiver &receiver) {
  node_.add_audio_receiver(receiver);
}

void NodeRuntime::apply_ancillary_receiver(
    const nmos_node::AncillaryReceiver &receiver) {
  node_.add_ancillary_receiver(receiver);
}

void NodeRuntime::update_video_sender(const nmos_node::VideoSender &sender) {
  node_.update_video_sender(sender);
}

void NodeRuntime::update_audio_sender(const nmos_node::AudioSender &sender) {
  node_.update_audio_sender(sender);
}

void NodeRuntime::update_ancillary_sender(
    const nmos_node::AncillarySender &sender) {
  node_.update_ancillary_sender(sender);
}

void NodeRuntime::update_video_receiver(
    const nmos_node::VideoReceiver &receiver) {
  node_.update_video_receiver(receiver);
}

void NodeRuntime::update_audio_receiver(
    const nmos_node::AudioReceiver &receiver) {
  node_.update_audio_receiver(receiver);
}

void NodeRuntime::update_ancillary_receiver(
    const nmos_node::AncillaryReceiver &receiver) {
  node_.update_ancillary_receiver(receiver);
}

void NodeRuntime::remove_video_sender(const std::string &id) {
  node_.remove_video_sender(id);
}

void NodeRuntime::remove_audio_sender(const std::string &id) {
  node_.remove_audio_sender(id);
}

void NodeRuntime::remove_ancillary_sender(const std::string &id) {
  node_.remove_ancillary_sender(id);
}

void NodeRuntime::remove_video_receiver(const std::string &id) {
  node_.remove_video_receiver(id);
}

void NodeRuntime::remove_audio_receiver(const std::string &id) {
  node_.remove_audio_receiver(id);
}

void NodeRuntime::remove_ancillary_receiver(const std::string &id) {
  node_.remove_ancillary_receiver(id);
}

void NodeRuntime::publish_event(const ReceiverEvent &event) {
  std::function<void(const ReceiverEvent &event)> handler;
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    handler = receiver_event_handler_;
  }
  if (handler) {
    handler(event);
  }
}

void NodeRuntime::publish_event(const RegistrationEvent &event) {
  std::function<void(const RegistrationEvent &event)> handler;
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    handler = registration_event_handler_;
  }
  if (handler) {
    handler(event);
  }
}

}
