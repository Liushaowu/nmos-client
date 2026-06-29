#pragma once

#include "../node.h"
#include "dto.h"

#include <optional>
#include <string>

namespace seeder::nmos_sync {

class ReconcileEngine {
public:
  void apply_snapshot(const std::optional<SnapshotDto> &current_snapshot,
                      const SnapshotDto &new_snapshot, nmos_node::Node &node);
  void drain_all(const std::optional<SnapshotDto> &current_snapshot,
                 nmos_node::Node &node);

private:
  template <typename T, typename Equivalent, typename Remove, typename Update,
            typename Add>
  void reconcile_list(const char *resource_type, const std::vector<T> &current,
                      const std::vector<T> &next, Equivalent equivalent,
                      Remove remove, Update update, Add add);
};

}
