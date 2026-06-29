#include "node_stream_store.h"

#include <algorithm>

namespace seeder::nmos_node::internal
{
  namespace
  {
    void remove_id(std::vector<nmos::id> &ids, const nmos::id &id)
    {
      ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
    }
  }

  bool StreamStore::has_sender_id(const nmos::id &id) const
  {
    return sender_ids_.end() !=
           std::find(sender_ids_.begin(), sender_ids_.end(), id);
  }

  std::vector<nmos::id> StreamStore::sender_ids() const
  {
    return sender_ids_;
  }

  std::vector<nmos::id> StreamStore::receiver_ids() const
  {
    return receiver_ids_;
  }

  std::vector<nmos::id> StreamStore::source_ids() const
  {
    return source_ids_;
  }

  std::vector<nmos::id> StreamStore::flow_ids() const
  {
    return flow_ids_;
  }

  void StreamStore::clear_resource_ids()
  {
    sender_ids_.clear();
    receiver_ids_.clear();
    source_ids_.clear();
    flow_ids_.clear();
  }

  void StreamStore::add_sender_resource_ids(const nmos::id &sender_id,
                                            const nmos::id &source_id,
                                            const nmos::id &flow_id)
  {
    sender_ids_.push_back(sender_id);
    source_ids_.push_back(source_id);
    flow_ids_.push_back(flow_id);
  }

  void StreamStore::add_receiver_id(const nmos::id &receiver_id)
  {
    receiver_ids_.push_back(receiver_id);
  }

  void StreamStore::remove_sender_resource_ids(const nmos::id &sender_id,
                                               const nmos::id &source_id,
                                               const nmos::id &flow_id)
  {
    remove_id(sender_ids_, sender_id);
    remove_id(source_ids_, source_id);
    remove_id(flow_ids_, flow_id);
  }

  void StreamStore::remove_receiver_id(const nmos::id &receiver_id)
  {
    remove_id(receiver_ids_, receiver_id);
  }
}
