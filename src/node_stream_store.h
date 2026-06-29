#pragma once

#include "node_types.h"

#include <nmos/id.h>

#include <utility>
#include <string>
#include <vector>

namespace seeder::nmos_node::internal
{
  class StreamStore
  {
  public:
    bool has_senders() const;
    bool has_receivers() const;
    bool has_sender_id(const nmos::id &id) const;

    std::vector<nmos::id> sender_ids() const;
    std::vector<nmos::id> receiver_ids() const;
    std::vector<nmos::id> source_ids() const;
    std::vector<nmos::id> flow_ids() const;
    std::vector<VideoSender> video_senders() const;
    std::vector<AudioSender> audio_senders() const;
    std::vector<AncillarySender> ancillary_senders() const;
    std::vector<VideoReceiver> video_receivers() const;
    std::vector<AudioReceiver> audio_receivers() const;
    std::vector<AncillaryReceiver> ancillary_receivers() const;

    void clear_senders();
    void clear_receivers();
    void clear_resource_ids();

    void add_sender_resource_ids(const nmos::id &sender_id,
                                 const nmos::id &source_id,
                                 const nmos::id &flow_id);
    void add_receiver_id(const nmos::id &receiver_id);
    void remove_sender_resource_ids(const nmos::id &sender_id,
                                    const nmos::id &source_id,
                                    const nmos::id &flow_id);
    void remove_receiver_id(const nmos::id &receiver_id);

    void add(VideoSender sender);
    void add(AudioSender sender);
    void add(AncillarySender sender);
    void add(VideoReceiver receiver);
    void add(VideoReceiver receiver, nmos::id resource_id);
    void add(AudioReceiver receiver);
    void add(AudioReceiver receiver, nmos::id resource_id);
    void add(AncillaryReceiver receiver);
    void add(AncillaryReceiver receiver, nmos::id resource_id);

    bool replace(const VideoSender &sender);
    bool replace(const AudioSender &sender);
    bool replace(const AncillarySender &sender);
    bool replace(const VideoReceiver &receiver);
    bool replace(const AudioReceiver &receiver);
    bool replace(const AncillaryReceiver &receiver);

    VideoSender *find_video_sender_by_id(const std::string &id);
    VideoSender *find_video_sender_by_sender_id(const std::string &id);
    AudioSender *find_audio_sender_by_id(const std::string &id);
    AudioSender *find_audio_sender_by_sender_id(const std::string &id);
    AncillarySender *find_ancillary_sender_by_id(const std::string &id);
    AncillarySender *find_ancillary_sender_by_sender_id(const std::string &id);

    VideoReceiver *find_video_receiver_by_id(const std::string &id);
    VideoReceiver *find_video_receiver_by_resource_id(const nmos::id &id);
    AudioReceiver *find_audio_receiver_by_id(const std::string &id);
    AudioReceiver *find_audio_receiver_by_resource_id(const nmos::id &id);
    AncillaryReceiver *find_ancillary_receiver_by_id(const std::string &id);
    AncillaryReceiver *find_ancillary_receiver_by_resource_id(const nmos::id &id);

    void remove_video_sender_by_sender_id(const std::string &id);
    void remove_audio_sender_by_sender_id(const std::string &id);
    void remove_ancillary_sender_by_sender_id(const std::string &id);
    void remove_video_receiver_by_id(const std::string &id);
    void remove_audio_receiver_by_id(const std::string &id);
    void remove_ancillary_receiver_by_id(const std::string &id);

  private:
    void remove_video_receiver_resource_id(const std::string &id);
    void remove_audio_receiver_resource_id(const std::string &id);
    void remove_ancillary_receiver_resource_id(const std::string &id);

    std::vector<nmos::id> sender_ids_;
    std::vector<nmos::id> receiver_ids_;
    std::vector<nmos::id> source_ids_;
    std::vector<nmos::id> flow_ids_;
    std::vector<VideoSender> video_senders_;
    std::vector<AudioSender> audio_senders_;
    std::vector<AncillarySender> ancillary_senders_;
    std::vector<VideoReceiver> video_receivers_;
    std::vector<AudioReceiver> audio_receivers_;
    std::vector<AncillaryReceiver> ancillary_receivers_;
    std::vector<std::pair<nmos::id, std::string>> video_receiver_resource_ids_;
    std::vector<std::pair<nmos::id, std::string>> audio_receiver_resource_ids_;
    std::vector<std::pair<nmos::id, std::string>> ancillary_receiver_resource_ids_;
  };
}
