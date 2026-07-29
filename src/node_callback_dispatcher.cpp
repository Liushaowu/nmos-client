#include "node_callback_dispatcher.h"

#include <utility>

namespace seeder::nmos_node::internal
{
  void CallbackDispatcher::set_update_video_sender_callback(
      VideoSenderCallback func)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    update_video_sender_func_ = std::move(func);
  }

  void CallbackDispatcher::set_update_audio_sender_callback(
      AudioSenderCallback func)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    update_audio_sender_func_ = std::move(func);
  }

  void CallbackDispatcher::set_update_ancillary_sender_callback(
      AncillarySenderCallback func)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    update_ancillary_sender_func_ = std::move(func);
  }

  void CallbackDispatcher::set_update_video_receiver_callback(
      VideoReceiverCallback func)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    update_video_receiver_func_ = std::move(func);
  }

  void CallbackDispatcher::set_update_audio_receiver_callback(
      AudioReceiverCallback func)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    update_audio_receiver_func_ = std::move(func);
  }

  void CallbackDispatcher::set_update_ancillary_receiver_callback(
      AncillaryReceiverCallback func)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    update_ancillary_receiver_func_ = std::move(func);
  }

  void CallbackDispatcher::set_receiver_connection_validation_handler(
      ReceiverConnectionValidationHandler handler)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    receiver_connection_validation_handler_ = std::move(handler);
  }

  void CallbackDispatcher::set_registration_changed_callback(
      RegistrationChangedCallback func)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    registration_changed_func_ = std::move(func);
  }

  CallbackDispatcher::SenderCallbacks CallbackDispatcher::sender_callbacks() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return {update_video_sender_func_, update_audio_sender_func_,
            update_ancillary_sender_func_};
  }

  CallbackDispatcher::ReceiverCallbacks CallbackDispatcher::receiver_callbacks() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return {update_video_receiver_func_, update_audio_receiver_func_,
            update_ancillary_receiver_func_};
  }

  ReceiverConnectionValidationHandler
  CallbackDispatcher::receiver_connection_validation_handler() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return receiver_connection_validation_handler_;
  }

  RegistrationChangedCallback CallbackDispatcher::registration_changed_callback() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return registration_changed_func_;
  }
}
