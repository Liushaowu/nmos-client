#pragma once

#include "node_callbacks.h"
#include "node_runtime_types.h"
#include "node_types.h"

#include <memory>
#include <string>

namespace seeder
{
  namespace nmos_node
  {
    class Node
    {
    private:
      class Impl;
      std::unique_ptr<Impl> p_impl;

    public:
      explicit Node(std::string node_config_path_);
      ~Node();

      bool start();
      bool stop();
      void add_video_sender(VideoSender video);
      void add_audio_sender(AudioSender audio);
      void add_ancillary_sender(AncillarySender ancillary);

      void add_video_receiver(VideoReceiver video);
      void add_audio_receiver(AudioReceiver audio);
      void add_ancillary_receiver(AncillaryReceiver ancillary);

      void update_video_sender(VideoSender video);
      void update_audio_sender(AudioSender audio);
      void update_ancillary_sender(AncillarySender ancillary);

      void update_video_receiver(VideoReceiver video);
      void update_audio_receiver(AudioReceiver audio);
      void update_ancillary_receiver(AncillaryReceiver ancillary);

      void remove_video_sender(std::string id);
      void remove_audio_sender(std::string id);
      void remove_ancillary_sender(std::string id);

      void remove_video_receiver(std::string id);
      void remove_audio_receiver(std::string id);
      void remove_ancillary_receiver(std::string id);

      void set_update_video_sender_callback(VideoSenderCallback func);
      void set_update_audio_sender_callback(AudioSenderCallback func);
      void set_update_ancillary_sender_callback(AncillarySenderCallback func);
      void set_update_video_receiver_callback(VideoReceiverCallback func);
      void set_update_audio_receiver_callback(AudioReceiverCallback func);
      void set_update_ancillary_receiver_callback(AncillaryReceiverCallback func);
      void set_registration_changed_callback(RegistrationChangedCallback func);
      void set_receiver_event_handler(ReceiverEventHandler handler);
      void set_sender_event_handler(SenderEventHandler handler);
      void set_registration_event_handler(RegistrationEventHandler handler);
      NodeSettingsJson effective_settings() const;
      NodeSettingsJson persisted_settings() const;
      NodeSettingsJson discover_registration_apis() const;
      NodeSettingsJson network_interfaces_json() const;
      void write_persisted_settings(const NodeSettingsJson &settings);
      void set_runtime_interfaces(RuntimeInterfaces interfaces);

      void set_ptp_clock(std::string gmtid, bool locked, int ptp_domain = 127);
    };
  } // namespace nmos_node
} // namespace seeder
