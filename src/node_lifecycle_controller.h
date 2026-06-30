#pragma once

#include "node_server_runtime.h"

#include <nmos/model.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace seeder::nmos_node::internal
{
  struct NodeLifecycleControllerContext
  {
    std::thread &thread;
    nmos::node_model &node_model;
    nmos::experimental::log_gate *&gate;
    std::mutex &lifecycle_mutex;
    std::mutex &thread_mutex;
    std::condition_variable &lifecycle_cv;
    bool &stop_requested;
    std::atomic<LifecycleState> &lifecycle_state;
    bool &needs_model_reset;
    std::function<void()> reset_model_state;
    std::function<int()> nmos_node_start;
  };

  class NodeLifecycleController
  {
  public:
    explicit NodeLifecycleController(NodeLifecycleControllerContext ctx);

    bool start();
    bool stop();

  private:
    void finish_worker_thread();

    std::thread &thread_;
    nmos::node_model &node_model_;
    nmos::experimental::log_gate *&gate_;
    std::mutex &lifecycle_mutex_;
    std::mutex &thread_mutex_;
    std::condition_variable &lifecycle_cv_;
    bool &stop_requested_;
    std::atomic<LifecycleState> &lifecycle_state_;
    bool &needs_model_reset_;
    std::function<void()> reset_model_state_;
    std::function<int()> nmos_node_start_;
  };
}
