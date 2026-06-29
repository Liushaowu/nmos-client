#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>

namespace nmos
{
  struct node_model;
  namespace experimental
  {
    struct node_implementation;
    struct log_gate;
  } // namespace experimental
} // namespace nmos

namespace seeder::nmos_node::internal
{
  class NodeSettings;

  enum class LifecycleState
  {
    stopped,
    starting,
    running,
    stopping
  };

  class NodeServerRuntime
  {
  public:
    struct ServerContext
    {
      nmos::node_model &node_model;
      NodeSettings &settings;
      nmos::experimental::log_gate *&gate_ptr;
      std::mutex &lifecycle_mutex;
      std::atomic<LifecycleState> &lifecycle_state;
      std::condition_variable &lifecycle_cv;
      bool &stop_requested;
    };

    static int start(ServerContext &ctx,
                     std::function<void()> thread_run_fn,
                     nmos::experimental::node_implementation &node_impl);

    static void log_stop_elapsed(
        std::chrono::steady_clock::time_point started_at,
        const char *step);
  };
} // namespace seeder::nmos_node::internal