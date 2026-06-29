#include "node_stream_store.h"

#include <algorithm>
#include <utility>

namespace seeder::nmos_node::internal
{
  namespace
  {
    template <typename Stream>
    bool replace_by_id(std::vector<Stream> &streams, const Stream &replacement)
    {
      for (auto &stream : streams)
      {
        if (stream.id == replacement.id)
        {
          stream = replacement;
          return true;
        }
      }
      return false;
    }

    template <typename Stream, typename Matches>
    Stream *find_matching(std::vector<Stream> &streams, Matches matches)
    {
      const auto found = std::find_if(streams.begin(), streams.end(), matches);
      return streams.end() == found ? nullptr : &*found;
    }

    template <typename Stream, typename Matches>
    void remove_matching(std::vector<Stream> &streams, Matches matches)
    {
      streams.erase(std::remove_if(streams.begin(), streams.end(), matches),
                    streams.end());
    }

  }

  bool StreamStore::has_senders() const
  {
    return !video_senders_.empty() || !audio_senders_.empty() ||
           !ancillary_senders_.empty();
  }

  bool StreamStore::has_receivers() const
  {
    return !video_receivers_.empty() || !audio_receivers_.empty() ||
           !ancillary_receivers_.empty();
  }

  std::vector<VideoSender> StreamStore::video_senders() const
  {
    return video_senders_;
  }

  std::vector<AudioSender> StreamStore::audio_senders() const
  {
    return audio_senders_;
  }

  std::vector<AncillarySender> StreamStore::ancillary_senders() const
  {
    return ancillary_senders_;
  }

  std::vector<VideoReceiver> StreamStore::video_receivers() const
  {
    return video_receivers_;
  }

  std::vector<AudioReceiver> StreamStore::audio_receivers() const
  {
    return audio_receivers_;
  }

  std::vector<AncillaryReceiver> StreamStore::ancillary_receivers() const
  {
    return ancillary_receivers_;
  }

  void StreamStore::clear_senders()
  {
    video_senders_.clear();
    audio_senders_.clear();
    ancillary_senders_.clear();
  }

  void StreamStore::clear_receivers()
  {
    video_receivers_.clear();
    audio_receivers_.clear();
    ancillary_receivers_.clear();
    video_receiver_resource_ids_.clear();
    audio_receiver_resource_ids_.clear();
    ancillary_receiver_resource_ids_.clear();
  }

  void StreamStore::add(VideoSender sender)
  {
    video_senders_.push_back(std::move(sender));
  }

  void StreamStore::add(AudioSender sender)
  {
    audio_senders_.push_back(std::move(sender));
  }

  void StreamStore::add(AncillarySender sender)
  {
    ancillary_senders_.push_back(std::move(sender));
  }

  void StreamStore::add(VideoReceiver receiver)
  {
    video_receivers_.push_back(std::move(receiver));
  }

  void StreamStore::add(VideoReceiver receiver, nmos::id resource_id)
  {
    video_receiver_resource_ids_.push_back({std::move(resource_id), receiver.id});
    add(std::move(receiver));
  }

  void StreamStore::add(AudioReceiver receiver)
  {
    audio_receivers_.push_back(std::move(receiver));
  }

  void StreamStore::add(AudioReceiver receiver, nmos::id resource_id)
  {
    audio_receiver_resource_ids_.push_back({std::move(resource_id), receiver.id});
    add(std::move(receiver));
  }

  void StreamStore::add(AncillaryReceiver receiver)
  {
    ancillary_receivers_.push_back(std::move(receiver));
  }

  void StreamStore::add(AncillaryReceiver receiver, nmos::id resource_id)
  {
    ancillary_receiver_resource_ids_.push_back({std::move(resource_id), receiver.id});
    add(std::move(receiver));
  }

  bool StreamStore::replace(const VideoSender &sender)
  {
    return replace_by_id(video_senders_, sender);
  }

  bool StreamStore::replace(const AudioSender &sender)
  {
    return replace_by_id(audio_senders_, sender);
  }

  bool StreamStore::replace(const AncillarySender &sender)
  {
    return replace_by_id(ancillary_senders_, sender);
  }

  bool StreamStore::replace(const VideoReceiver &receiver)
  {
    return replace_by_id(video_receivers_, receiver);
  }

  bool StreamStore::replace(const AudioReceiver &receiver)
  {
    return replace_by_id(audio_receivers_, receiver);
  }

  bool StreamStore::replace(const AncillaryReceiver &receiver)
  {
    return replace_by_id(ancillary_receivers_, receiver);
  }

  VideoSender *StreamStore::find_video_sender_by_id(const std::string &id)
  {
    return find_matching(video_senders_, [&](const VideoSender &video)
                         { return video.id == id; });
  }

  VideoSender *StreamStore::find_video_sender_by_sender_id(
      const std::string &id)
  {
    return find_matching(video_senders_, [&](const VideoSender &video)
                         { return video.sender_id == id; });
  }

  AudioSender *StreamStore::find_audio_sender_by_id(const std::string &id)
  {
    return find_matching(audio_senders_, [&](const AudioSender &audio)
                         { return audio.id == id; });
  }

  AudioSender *StreamStore::find_audio_sender_by_sender_id(
      const std::string &id)
  {
    return find_matching(audio_senders_, [&](const AudioSender &audio)
                         { return audio.sender_id == id; });
  }

  AncillarySender *StreamStore::find_ancillary_sender_by_id(
      const std::string &id)
  {
    return find_matching(ancillary_senders_,
                         [&](const AncillarySender &ancillary)
                         { return ancillary.id == id; });
  }

  AncillarySender *StreamStore::find_ancillary_sender_by_sender_id(
      const std::string &id)
  {
    return find_matching(ancillary_senders_,
                         [&](const AncillarySender &ancillary)
                         { return ancillary.sender_id == id; });
  }

  VideoReceiver *StreamStore::find_video_receiver_by_id(const std::string &id)
  {
    return find_matching(video_receivers_, [&](const VideoReceiver &video)
                         { return video.id == id; });
  }

  AudioReceiver *StreamStore::find_audio_receiver_by_id(const std::string &id)
  {
    return find_matching(audio_receivers_, [&](const AudioReceiver &audio)
                         { return audio.id == id; });
  }

  AncillaryReceiver *StreamStore::find_ancillary_receiver_by_id(
      const std::string &id)
  {
    return find_matching(ancillary_receivers_,
                         [&](const AncillaryReceiver &ancillary)
                         { return ancillary.id == id; });
  }

  void StreamStore::remove_video_sender_by_sender_id(const std::string &id)
  {
    remove_matching(video_senders_, [&](const VideoSender &video)
                    { return video.sender_id == id; });
  }

  void StreamStore::remove_audio_sender_by_sender_id(const std::string &id)
  {
    remove_matching(audio_senders_, [&](const AudioSender &audio)
                    { return audio.sender_id == id; });
  }

  void StreamStore::remove_ancillary_sender_by_sender_id(
      const std::string &id)
  {
    remove_matching(ancillary_senders_, [&](const AncillarySender &ancillary)
                    { return ancillary.sender_id == id; });
  }

  void StreamStore::remove_video_receiver_by_id(const std::string &id)
  {
    remove_matching(video_receivers_, [&](const VideoReceiver &video)
                    { return video.id == id; });
    remove_video_receiver_resource_id(id);
  }

  void StreamStore::remove_audio_receiver_by_id(const std::string &id)
  {
    remove_matching(audio_receivers_, [&](const AudioReceiver &audio)
                    { return audio.id == id; });
    remove_audio_receiver_resource_id(id);
  }

  void StreamStore::remove_ancillary_receiver_by_id(const std::string &id)
  {
    remove_matching(ancillary_receivers_,
                    [&](const AncillaryReceiver &ancillary)
                    { return ancillary.id == id; });
    remove_ancillary_receiver_resource_id(id);
  }
}
