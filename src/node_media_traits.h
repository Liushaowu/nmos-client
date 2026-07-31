#pragma once

#include "node_implementation.h"
#include "node_types.h"

namespace seeder::nmos_node::internal {

// Tag types for compile-time dispatch on media type
struct VideoTag {};
struct AudioTag {};
struct AncillaryTag {};

template <typename Tag>
struct MediaTraits;

template <>
struct MediaTraits<VideoTag>
{
  using Sender   = VideoSender;
  using Receiver = VideoReceiver;
  static const impl::port &port() { return impl::ports::video; }
  static const char       *name() { return "video"; }
};

template <>
struct MediaTraits<AudioTag>
{
  using Sender   = AudioSender;
  using Receiver = AudioReceiver;
  static const impl::port &port() { return impl::ports::audio; }
  static const char       *name() { return "audio"; }
};

template <>
struct MediaTraits<AncillaryTag>
{
  using Sender   = AncillarySender;
  using Receiver = AncillaryReceiver;
  static const impl::port &port() { return impl::ports::data; }
  static const char       *name() { return "ancillary"; }
};

}  // namespace seeder::nmos_node::internal