#pragma once

#include "node_callback_dispatcher.h"
#include "node_callbacks.h"

#include <mutex>

namespace seeder::nmos_node::internal
{
  struct NodeEventBridgeContext
  {
    CallbackDispatcher &callbacks;
  };

  class NodeEventBridge
  {
  public:
    explicit NodeEventBridge(NodeEventBridgeContext ctx);

    void set_update_video_sender_callback(VideoSenderCallback func);
    void set_update_audio_sender_callback(AudioSenderCallback func);
    void set_update_ancillary_sender_callback(AncillarySenderCallback func);
    void set_update_video_receiver_callback(VideoReceiverCallback func);
    void set_update_audio_receiver_callback(AudioReceiverCallback func);
    void set_update_ancillary_receiver_callback(AncillaryReceiverCallback func);
    void set_registration_changed_callback(RegistrationChangedCallback func);

    void set_receiver_event_handler(ReceiverEventHandler handler);
    void set_receiver_connection_validation_handler(
        ReceiverConnectionValidationHandler handler);
    void set_sender_event_handler(SenderEventHandler handler);
    void set_registration_event_handler(RegistrationEventHandler handler);

  private:
    template <typename Event, typename Handler>
    void publish_event_impl(const Event &event, Handler &handler_store);

    CallbackDispatcher &callbacks_;
    ReceiverEventHandler receiver_event_handler_;
    SenderEventHandler sender_event_handler_;
    RegistrationEventHandler registration_event_handler_;
    std::mutex event_callback_mutex_;
  };
}
