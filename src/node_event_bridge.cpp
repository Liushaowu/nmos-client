#include "node_event_bridge.h"

#include <utility>

namespace seeder::nmos_node::internal
{
  NodeEventBridge::NodeEventBridge(NodeEventBridgeContext ctx)
      : callbacks_(ctx.callbacks)
  {
  }

  // ---- template publish_event_impl ----
  template <typename Event, typename Handler>
  void NodeEventBridge::publish_event_impl(const Event &event,
                                            Handler &handler_store)
  {
    Handler handler;
    {
      std::lock_guard<std::mutex> lock(event_callback_mutex_);
      handler = handler_store;
    }
    if (handler)
    {
      handler(event);
    }
  }

  // explicit instantiations
  template void NodeEventBridge::publish_event_impl(
      const ReceiverEvent &, ReceiverEventHandler &);
  template void NodeEventBridge::publish_event_impl(
      const SenderEvent &, SenderEventHandler &);
  template void NodeEventBridge::publish_event_impl(
      const RegistrationEvent &, RegistrationEventHandler &);

  // ---- set_update callbacks ----
  void NodeEventBridge::set_update_video_sender_callback(
      VideoSenderCallback func)
  {
    callbacks_.set_update_video_sender_callback(std::move(func));
  }

  void NodeEventBridge::set_update_audio_sender_callback(
      AudioSenderCallback func)
  {
    callbacks_.set_update_audio_sender_callback(std::move(func));
  }

  void NodeEventBridge::set_update_ancillary_sender_callback(
      AncillarySenderCallback func)
  {
    callbacks_.set_update_ancillary_sender_callback(std::move(func));
  }

  void NodeEventBridge::set_update_video_receiver_callback(
      VideoReceiverCallback func)
  {
    callbacks_.set_update_video_receiver_callback(std::move(func));
  }

  void NodeEventBridge::set_update_audio_receiver_callback(
      AudioReceiverCallback func)
  {
    callbacks_.set_update_audio_receiver_callback(std::move(func));
  }

  void NodeEventBridge::set_update_ancillary_receiver_callback(
      AncillaryReceiverCallback func)
  {
    callbacks_.set_update_ancillary_receiver_callback(std::move(func));
  }

  void NodeEventBridge::set_registration_changed_callback(
      RegistrationChangedCallback func)
  {
    callbacks_.set_registration_changed_callback(std::move(func));
  }

  // ---- set_*_event_handler (with wire-up helper) ----
  void NodeEventBridge::set_receiver_event_handler(ReceiverEventHandler handler)
  {
    std::lock_guard<std::mutex> lock(event_callback_mutex_);
    receiver_event_handler_ = std::move(handler);
    if (receiver_event_handler_)
    {
      auto wire = [this](auto item) {
        publish_event_impl(ReceiverEvent{item}, receiver_event_handler_);
      };
      callbacks_.set_update_video_receiver_callback(
          [wire](const VideoReceiver &r) { wire(r); });
      callbacks_.set_update_audio_receiver_callback(
          [wire](const AudioReceiver &r) { wire(r); });
      callbacks_.set_update_ancillary_receiver_callback(
          [wire](const AncillaryReceiver &r) { wire(r); });
    }
  }

  void NodeEventBridge::set_receiver_connection_validation_handler(
      ReceiverConnectionValidationHandler handler)
  {
    callbacks_.set_receiver_connection_validation_handler(std::move(handler));
  }

  void NodeEventBridge::set_sender_event_handler(SenderEventHandler handler)
  {
    std::lock_guard<std::mutex> lock(event_callback_mutex_);
    sender_event_handler_ = std::move(handler);
    if (sender_event_handler_)
    {
      auto wire = [this](auto item) {
        publish_event_impl(SenderEvent{item}, sender_event_handler_);
      };
      callbacks_.set_update_video_sender_callback(
          [wire](const VideoSender &s) { wire(s); });
      callbacks_.set_update_audio_sender_callback(
          [wire](const AudioSender &s) { wire(s); });
      callbacks_.set_update_ancillary_sender_callback(
          [wire](const AncillarySender &s) { wire(s); });
    }
  }

  void NodeEventBridge::set_registration_event_handler(
      RegistrationEventHandler handler)
  {
    std::lock_guard<std::mutex> lock(event_callback_mutex_);
    registration_event_handler_ = std::move(handler);
    if (registration_event_handler_)
    {
      callbacks_.set_registration_changed_callback(
          [this](const RegistrationStatus &status) {
            publish_event_impl(RegistrationEvent{status},
                               registration_event_handler_);
          });
    }
  }

} // namespace seeder::nmos_node::internal