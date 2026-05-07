#pragma once

#include "dto.h"

#include <string>

namespace seeder::nmos_sync {

class SnapshotClient {
public:
  SnapshotClient(std::string snapshot_url, int timeout_ms);

  SnapshotDto fetch_snapshot() const;

private:
  std::string snapshot_url_;
  int timeout_ms_;
};

}
