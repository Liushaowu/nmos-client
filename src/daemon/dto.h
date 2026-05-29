#pragma once

#include "../node.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <cpprest/json.h>

namespace seeder::nmos_sync {

struct PtpClockDto {
  struct Entry {
    int clock_accuracy = 0;
    int clock_class = 0;
    std::string grandmaster_identity;
    int grandmaster_priority1 = 0;
    int grandmaster_priority2 = 0;
    bool is_active = false;
    bool is_connected = false;
    bool is_locked = false;
    bool master_initialized = false;
    std::string master_port_id;
    int master_utc_offset = 0;
    double offset = 0.0;
    int offset_scaled_log_variance = 0;
    int t1_domain_number = 0;

    bool operator==(const Entry &other) const {
      return clock_accuracy == other.clock_accuracy &&
             clock_class == other.clock_class &&
             grandmaster_identity == other.grandmaster_identity &&
             grandmaster_priority1 == other.grandmaster_priority1 &&
             grandmaster_priority2 == other.grandmaster_priority2 &&
             is_active == other.is_active &&
             is_connected == other.is_connected &&
             is_locked == other.is_locked &&
             master_initialized == other.master_initialized &&
             master_port_id == other.master_port_id &&
             master_utc_offset == other.master_utc_offset &&
             offset == other.offset &&
             offset_scaled_log_variance == other.offset_scaled_log_variance &&
             t1_domain_number == other.t1_domain_number;
    }
  };

  std::vector<Entry> entries;

  std::string effective_gmid() const;
  bool effective_locked() const;
  bool empty() const;
};

struct SnapshotDto {
  struct DeviceDto {
    std::string device;
    std::string display_name;
    bool enable = false;
    int id = 0;
    std::string sip;

    bool operator==(const DeviceDto &other) const {
      return device == other.device &&
             display_name == other.display_name &&
             enable == other.enable && id == other.id &&
             sip == other.sip;
    }
  };

  PtpClockDto ptp_clock;
  std::vector<DeviceDto> devices;
  std::vector<nmos_node::VideoSender> video_senders;
  std::vector<nmos_node::AudioSender> audio_senders;
  std::vector<nmos_node::AncillarySender> ancillary_senders;
  std::vector<nmos_node::VideoReceiver> video_receivers;
  std::vector<nmos_node::AudioReceiver> audio_receivers;
  std::vector<nmos_node::AncillaryReceiver> ancillary_receivers;

  bool has_any_streams() const;
};

struct SnapshotChangedMessage {
  std::int64_t revision = 0;
  std::string reason;
};

struct SyncFailedMessage {
  std::int64_t revision = 0;
  std::string message;
};

SnapshotDto snapshot_from_json(const web::json::value &value);
web::json::value snapshot_to_json(const SnapshotDto &snapshot);

std::optional<SnapshotChangedMessage>
snapshot_changed_message_from_json(const web::json::value &value);

web::json::value make_node_lifecycle_message(const std::string &old_state,
                                             const std::string &new_state);
web::json::value make_sync_failed_message(const SyncFailedMessage &message);
web::json::value make_streams_drained_message();
web::json::value make_receiver_video_observed_changed_message(
    const nmos_node::VideoReceiver &receiver);
web::json::value make_receiver_audio_observed_changed_message(
    const nmos_node::AudioReceiver &receiver);
web::json::value make_receiver_ancillary_observed_changed_message(
    const nmos_node::AncillaryReceiver &receiver);

bool equivalent(const nmos_node::Redudancy &lhs,
                const nmos_node::Redudancy &rhs);
bool equivalent(const nmos_node::VideoSender &lhs,
                const nmos_node::VideoSender &rhs);
bool equivalent(const nmos_node::AudioSender &lhs,
                const nmos_node::AudioSender &rhs);
bool equivalent(const nmos_node::AncillarySender &lhs,
                const nmos_node::AncillarySender &rhs);
bool equivalent(const nmos_node::VideoReceiver &lhs,
                const nmos_node::VideoReceiver &rhs);
bool equivalent(const nmos_node::AudioReceiver &lhs,
                const nmos_node::AudioReceiver &rhs);
bool equivalent(const nmos_node::AncillaryReceiver &lhs,
                const nmos_node::AncillaryReceiver &rhs);
bool equivalent(const PtpClockDto &lhs, const PtpClockDto &rhs);
bool equivalent(const SnapshotDto::DeviceDto &lhs,
                const SnapshotDto::DeviceDto &rhs);

}
