#pragma once

#include "dto.h"
#include "node_runtime.h"

#include <optional>
#include <string>

namespace seeder::nmos_sync {

class ReconcileEngine {
public:
  void apply_snapshot(const std::optional<SnapshotDto> &current_snapshot,
                      const SnapshotDto &new_snapshot, NodeRuntime &runtime);
  void drain_all(const std::optional<SnapshotDto> &current_snapshot,
                 NodeRuntime &runtime);

private:
  template <typename T, typename Equivalent, typename Remove, typename Add>
  void reconcile_list(const std::vector<T> &current, const std::vector<T> &next,
                      Equivalent equivalent, Remove remove, Add add);
};

}
