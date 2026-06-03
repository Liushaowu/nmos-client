#include "reconcile_engine.h"

#include <unordered_map>

namespace seeder::nmos_sync {

namespace {

template <typename T>
using ResourceMap = std::unordered_map<std::string, T>;

template <typename T>
ResourceMap<T> make_map(const std::vector<T> &items) {
  ResourceMap<T> map;
  for (const auto &item : items) {
    map.emplace(item.id, item);
  }
  return map;
}

}

template <typename T, typename Equivalent, typename Remove, typename Update,
          typename Add>
void ReconcileEngine::reconcile_list(const std::vector<T> &current,
                                      const std::vector<T> &next,
                                      Equivalent equivalent, Remove remove,
                                      Update update, Add add) {
  const auto current_map = make_map(current);
  const auto next_map = make_map(next);

  for (const auto &[id, current_item] : current_map) {
    const auto found = next_map.find(id);
    if (found == next_map.end()) {
      remove(id);
      continue;
    }
    if (!equivalent(current_item, found->second)) {
      update(found->second);
    }
  }

  for (const auto &[id, next_item] : next_map) {
    const auto found = current_map.find(id);
    if (found == current_map.end()) {
      add(next_item);
    }
  }
}

void ReconcileEngine::apply_snapshot(
    const std::optional<SnapshotDto> &current_snapshot,
    const SnapshotDto &new_snapshot, NodeRuntime &runtime) {
  const SnapshotDto empty_snapshot;
  const SnapshotDto &current = current_snapshot.value_or(empty_snapshot);
  if (!equivalent(current.ptp_clock, new_snapshot.ptp_clock)) {
    runtime.set_ptp_clock(new_snapshot.ptp_clock);
  }
  if (current.devices != new_snapshot.devices) {
    runtime.set_runtime_devices(new_snapshot.devices);
  }
  reconcile_list(current.video_receivers, new_snapshot.video_receivers,
                  [](const nmos_node::VideoReceiver &lhs,
                     const nmos_node::VideoReceiver &rhs)
                  { return equivalent(lhs, rhs); },
                  [&](const std::string &id) { runtime.remove_video_receiver(id); },
                  [&](const nmos_node::VideoReceiver &receiver)
                  { runtime.update_video_receiver(receiver); },
                  [&](const nmos_node::VideoReceiver &receiver)
                  { runtime.apply_video_receiver(receiver); });
  reconcile_list(current.audio_receivers, new_snapshot.audio_receivers,
                  [](const nmos_node::AudioReceiver &lhs,
                     const nmos_node::AudioReceiver &rhs)
                  { return equivalent(lhs, rhs); },
                  [&](const std::string &id) { runtime.remove_audio_receiver(id); },
                  [&](const nmos_node::AudioReceiver &receiver)
                  { runtime.update_audio_receiver(receiver); },
                  [&](const nmos_node::AudioReceiver &receiver)
                  { runtime.apply_audio_receiver(receiver); });
  reconcile_list(current.ancillary_receivers,
                 new_snapshot.ancillary_receivers,
                 [](const nmos_node::AncillaryReceiver &lhs,
                    const nmos_node::AncillaryReceiver &rhs)
                  { return equivalent(lhs, rhs); },
                  [&](const std::string &id)
                  { runtime.remove_ancillary_receiver(id); },
                  [&](const nmos_node::AncillaryReceiver &receiver)
                  { runtime.update_ancillary_receiver(receiver); },
                  [&](const nmos_node::AncillaryReceiver &receiver)
                  { runtime.apply_ancillary_receiver(receiver); });

  reconcile_list(current.video_senders, new_snapshot.video_senders,
                  [](const nmos_node::VideoSender &lhs,
                     const nmos_node::VideoSender &rhs)
                  { return equivalent(lhs, rhs); },
                  [&](const std::string &id) { runtime.remove_video_sender(id); },
                  [&](const nmos_node::VideoSender &sender)
                  { runtime.update_video_sender(sender); },
                  [&](const nmos_node::VideoSender &sender)
                  { runtime.apply_video_sender(sender); });
  reconcile_list(current.audio_senders, new_snapshot.audio_senders,
                  [](const nmos_node::AudioSender &lhs,
                     const nmos_node::AudioSender &rhs)
                  { return equivalent(lhs, rhs); },
                  [&](const std::string &id) { runtime.remove_audio_sender(id); },
                  [&](const nmos_node::AudioSender &sender)
                  { runtime.update_audio_sender(sender); },
                  [&](const nmos_node::AudioSender &sender)
                  { runtime.apply_audio_sender(sender); });
  reconcile_list(current.ancillary_senders, new_snapshot.ancillary_senders,
                 [](const nmos_node::AncillarySender &lhs,
                    const nmos_node::AncillarySender &rhs)
                  { return equivalent(lhs, rhs); },
                  [&](const std::string &id)
                  { runtime.remove_ancillary_sender(id); },
                  [&](const nmos_node::AncillarySender &sender)
                  { runtime.update_ancillary_sender(sender); },
                  [&](const nmos_node::AncillarySender &sender)
                  { runtime.apply_ancillary_sender(sender); });


}

void ReconcileEngine::drain_all(const std::optional<SnapshotDto> &current_snapshot,
                                NodeRuntime &runtime) {
  if (!current_snapshot.has_value()) {
    return;
  }

  for (const auto &receiver : current_snapshot->video_receivers) {
    runtime.remove_video_receiver(receiver.id);
  }
  for (const auto &receiver : current_snapshot->audio_receivers) {
    runtime.remove_audio_receiver(receiver.id);
  }
  for (const auto &receiver : current_snapshot->ancillary_receivers) {
    runtime.remove_ancillary_receiver(receiver.id);
  }
  for (const auto &sender : current_snapshot->video_senders) {
    runtime.remove_video_sender(sender.id);
  }
  for (const auto &sender : current_snapshot->audio_senders) {
    runtime.remove_audio_sender(sender.id);
  }
  for (const auto &sender : current_snapshot->ancillary_senders) {
    runtime.remove_ancillary_sender(sender.id);
  }
}

}
