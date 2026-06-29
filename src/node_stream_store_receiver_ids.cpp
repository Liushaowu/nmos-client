#include "node_stream_store.h"

#include <algorithm>
#include <utility>

namespace seeder::nmos_node::internal
{
  namespace
  {
    template <typename Matches>
    auto find_mapping(std::vector<std::pair<nmos::id, std::string>> &mappings,
                      Matches matches)
    {
      const auto found = std::find_if(mappings.begin(), mappings.end(), matches);
      return mappings.end() == found ? nullptr : &*found;
    }

    template <typename Matches>
    void remove_mapping(std::vector<std::pair<nmos::id, std::string>> &mappings,
                        Matches matches)
    {
      mappings.erase(std::remove_if(mappings.begin(), mappings.end(), matches),
                     mappings.end());
    }
  }

  VideoReceiver *StreamStore::find_video_receiver_by_resource_id(
      const nmos::id &id)
  {
    const auto resource = find_mapping(
        video_receiver_resource_ids_,
        [&](const std::pair<nmos::id, std::string> &entry)
        { return entry.first == id; });
    return resource ? find_video_receiver_by_id(resource->second) : nullptr;
  }

  AudioReceiver *StreamStore::find_audio_receiver_by_resource_id(
      const nmos::id &id)
  {
    const auto resource = find_mapping(
        audio_receiver_resource_ids_,
        [&](const std::pair<nmos::id, std::string> &entry)
        { return entry.first == id; });
    return resource ? find_audio_receiver_by_id(resource->second) : nullptr;
  }

  AncillaryReceiver *StreamStore::find_ancillary_receiver_by_resource_id(
      const nmos::id &id)
  {
    const auto resource = find_mapping(
        ancillary_receiver_resource_ids_,
        [&](const std::pair<nmos::id, std::string> &entry)
        { return entry.first == id; });
    return resource ? find_ancillary_receiver_by_id(resource->second) : nullptr;
  }

  void StreamStore::remove_video_receiver_resource_id(const std::string &id)
  {
    remove_mapping(video_receiver_resource_ids_,
                   [&](const std::pair<nmos::id, std::string> &entry)
                   { return entry.second == id; });
  }

  void StreamStore::remove_audio_receiver_resource_id(const std::string &id)
  {
    remove_mapping(audio_receiver_resource_ids_,
                   [&](const std::pair<nmos::id, std::string> &entry)
                   { return entry.second == id; });
  }

  void StreamStore::remove_ancillary_receiver_resource_id(const std::string &id)
  {
    remove_mapping(ancillary_receiver_resource_ids_,
                   [&](const std::pair<nmos::id, std::string> &entry)
                   { return entry.second == id; });
  }
}
