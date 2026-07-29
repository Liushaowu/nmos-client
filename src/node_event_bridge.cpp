#include "node_event_bridge.h"

#include <utility>

namespace seeder::nmos_node::internal
{
  NodeEventBridge::NodeEventBridge(NodeEventBridgeContext ctx)
      : callbacks_(ctx.callbacks)
  {
  }

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

  void NodeEventBridge::set_receiver_event_handler(ReceiverEventHandler handler)
  {
    std::lock_guard<std::mutex> lock(event_callback_mutex_);
    receiver_event_handler_ = std::move(handler);
    if (receiver_event_handler_)
    {
      callbacks_.set_update_video_receiver_callback(
          [this](const VideoReceiver &r)
          { publish_event(ReceiverEvent{r}); });
      callbacks_.set_update_audio_receiver_callback(
          [this](const AudioReceiver &r)
          { publish_event(ReceiverEvent{r}); });
      callbacks_.set_update_ancillary_receiver_callback(
          [this](const AncillaryReceiver &r)
          { publish_event(ReceiverEvent{r}); });
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
      callbacks_.set_update_video_sender_callback(
          [this](const VideoSender &s)
          { publish_event(SenderEvent{s}); });
      callbacks_.set_update_audio_sender_callback(
          [this](const AudioSender &s)
          { publish_event(SenderEvent{s}); });
      callbacks_.set_update_ancillary_sender_callback(
          [this](const AncillarySender &s)
          { publish_event(SenderEvent{s}); });
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
          [this](const RegistrationStatus &status)
          { publish_event(RegistrationEvent{status}); });
    }
  }

  void NodeEventBridge::publish_event(const ReceiverEvent &event)
  {
    ReceiverEventHandler handler;
    {
      std::lock_guard<std::mutex> lock(event_callback_mutex_);
      handler = receiver_event_handler_;
    }
    if (handler)
    {
      handler(event);
    }
  }

  void NodeEventBridge::publish_event(const SenderEvent &event)
  {
    SenderEventHandler handler;
    {
      std::lock_guard<std::mutex> lock(event_callback_mutex_);
      handler = sender_event_handler_;
    }
    if (handler)
    {
      handler(event);
    }
  }

  void NodeEventBridge::publish_event(const RegistrationEvent &event)
  {
    RegistrationEventHandler handler;
    {
      std::lock_guard<std::mutex> lock(event_callback_mutex_);
      handler = registration_event_handler_;
    }
    if (handler)
    {
      handler(event);
    }
  }
}
