#pragma once

#include "node_callbacks.h"
#include "node_types.h"

#include <mutex>

namespace seeder::nmos_node::internal
{
  class CallbackDispatcher
  {
  public:
    struct SenderCallbacks
    {
      VideoSenderCallback video;
      AudioSenderCallback audio;
      AncillarySenderCallback ancillary;
    };

    struct ReceiverCallbacks
    {
      VideoReceiverCallback video;
      AudioReceiverCallback audio;
      AncillaryReceiverCallback ancillary;
    };

    void set_update_video_sender_callback(VideoSenderCallback func);
    void set_update_audio_sender_callback(AudioSenderCallback func);
    void set_update_ancillary_sender_callback(AncillarySenderCallback func);
    void set_update_video_receiver_callback(VideoReceiverCallback func);
    void set_update_audio_receiver_callback(AudioReceiverCallback func);
    void set_update_ancillary_receiver_callback(AncillaryReceiverCallback func);
    void set_registration_changed_callback(RegistrationChangedCallback func);

    SenderCallbacks sender_callbacks() const;
    ReceiverCallbacks receiver_callbacks() const;
    RegistrationChangedCallback registration_changed_callback() const;

  private:
    mutable std::mutex mutex_;
    VideoSenderCallback update_video_sender_func_;
    AudioSenderCallback update_audio_sender_func_;
    AncillarySenderCallback update_ancillary_sender_func_;
    VideoReceiverCallback update_video_receiver_func_;
    AudioReceiverCallback update_audio_receiver_func_;
    AncillaryReceiverCallback update_ancillary_receiver_func_;
    RegistrationChangedCallback registration_changed_func_;
  };
}
