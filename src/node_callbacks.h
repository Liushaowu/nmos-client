#pragma once

#include "node_types.h"

#include <functional>
#include <mutex>
#include <variant>

namespace seeder
{
  namespace nmos_node
  {
    using VideoSenderCallback = std::function<void(const VideoSender &)>;
    using AudioSenderCallback = std::function<void(const AudioSender &)>;
    using AncillarySenderCallback =
        std::function<void(const AncillarySender &)>;

    using VideoReceiverCallback = std::function<void(const VideoReceiver &)>;
    using AudioReceiverCallback = std::function<void(const AudioReceiver &)>;
    using AncillaryReceiverCallback =
        std::function<void(const AncillaryReceiver &)>;

    using RegistrationChangedCallback =
        std::function<void(const RegistrationStatus &)>;

    struct ReceiverEvent
    {
      using Payload = std::variant<VideoReceiver, AudioReceiver,
                                   AncillaryReceiver>;
      Payload payload;
    };

    struct SenderEvent
    {
      using Payload = std::variant<VideoSender, AudioSender,
                                   AncillarySender>;
      Payload payload;
    };

    struct RegistrationEvent
    {
      RegistrationStatus status;
    };

    using ReceiverEventHandler = std::function<void(const ReceiverEvent &)>;
    using SenderEventHandler = std::function<void(const SenderEvent &)>;
    using RegistrationEventHandler =
        std::function<void(const RegistrationEvent &)>;
  }
}