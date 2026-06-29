#pragma once

#include "../node_types.h"
#include "dto.h"

#include <mutex>
#include <optional>
#include <string>

namespace seeder::nmos_sync {

struct StatusSnapshot {
  std::string daemon_state{"starting"};
  std::string node_state{"stopped"};
  std::int64_t last_seen_revision{0};
  std::int64_t last_applied_revision{0};
  std::string last_sync_error;
  bool streams_registered{false};
  bool protection_mode{false};
  bool has_received_snapshot_changed{false};
};

class StateStore {
public:
  void set_daemon_state(const std::string &state);
  void set_node_state(const std::string &state);
  void set_last_seen_revision(std::int64_t revision);
  void mark_snapshot_signal_received();
  void mark_sync_failed(const std::string &message);
  void mark_apply_success(std::int64_t applied_revision,
                          const SnapshotDto &snapshot,
                          bool streams_registered);
  void mark_drained_due_to_ws_disconnect();
  void set_last_snapshot(const SnapshotDto &snapshot);
  void clear_last_snapshot();
  void set_registry_status(const nmos_node::RegistrationStatus &status);

  StatusSnapshot status() const;
  nmos_node::RegistrationStatus registry_status() const;
  std::optional<SnapshotDto> last_snapshot() const;
  web::json::value status_json() const;
  web::json::value snapshot_json() const;

  bool consume_pending_drain_notification();

private:
  mutable std::mutex mutex_;
  StatusSnapshot status_;
  nmos_node::RegistrationStatus registry_status_;
  std::optional<SnapshotDto> last_snapshot_;
  bool pending_drain_notification_{false};
};

}
