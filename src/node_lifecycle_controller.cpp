#include "node_lifecycle_controller.h"
#include "node_implementation.h"

#include <nmos/log_gate.h>
#include <nmos/mutex.h>
#include <nmos/slog.h>
#include <slog/all_in_one.h>

#include <chrono>

namespace seeder::nmos_node::internal
{
  NodeLifecycleController::NodeLifecycleController(
      NodeLifecycleControllerContext ctx)
      : thread_(ctx.thread), node_model_(ctx.node_model), gate_(ctx.gate),
        lifecycle_mutex_(ctx.lifecycle_mutex), thread_mutex_(ctx.thread_mutex),
        lifecycle_cv_(ctx.lifecycle_cv), stop_requested_(ctx.stop_requested),
        lifecycle_state_(ctx.lifecycle_state),
        needs_model_reset_(ctx.needs_model_reset),
        reset_model_state_(std::move(ctx.reset_model_state)),
        nmos_node_start_(std::move(ctx.nmos_node_start))
  {
  }

  bool NodeLifecycleController::stop()
  {
    const auto stop_started = std::chrono::steady_clock::now();
    const auto log_elapsed = [&stop_started](const char *step)
    {
      NodeServerRuntime::log_stop_elapsed(stop_started, step);
    };

    log_elapsed("begin");
    bool stop_completed = false;
    bool should_join_thread = false;
    std::thread::id worker_thread_id;

    {
      std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
      if (LifecycleState::stopped != lifecycle_state_)
      {
        lifecycle_state_ = LifecycleState::stopping;
        stop_requested_ = true;
        lifecycle_cv_.notify_all();
      }
      else
      {
        stop_completed = true;
      }
    }

    {
      auto lock = node_model_.write_lock();
      node_model_.shutdown = true;
    }
    node_model_.notify();
    node_model_.shutdown_condition.notify_all();

    {
      std::lock_guard<std::mutex> thread_lock(thread_mutex_);
      if (thread_.joinable())
      {
        worker_thread_id = thread_.get_id();
        should_join_thread = std::this_thread::get_id() != worker_thread_id;
        if (should_join_thread)
        {
          log_elapsed("joining worker thread");
          thread_.join();
          log_elapsed("worker thread joined");
          stop_completed = true;
        }
      }
      else
      {
        stop_completed = true;
      }
    }

    if (should_join_thread || std::thread::id{} == worker_thread_id)
    {
      std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
      lifecycle_state_ = LifecycleState::stopped;
      stop_requested_ = false;
      needs_model_reset_ = true;
      stop_completed = true;
    }

    log_elapsed("complete");
    return stop_completed;
  }

  bool NodeLifecycleController::start()
  {
    {
      std::scoped_lock<std::mutex, std::mutex> lock(lifecycle_mutex_,
                                                    thread_mutex_);
      if (LifecycleState::running == lifecycle_state_)
      {
        return true;
      }

      if (LifecycleState::starting == lifecycle_state_ ||
          LifecycleState::stopping == lifecycle_state_ || thread_.joinable())
      {
        if (gate_)
        {
          slog::log<slog::severities::warning>(*gate_, SLOG_FLF)
              << nmos::stash_category(impl::categories::node_implementation)
              << "start ignored: node thread already active";
        }
        return false;
      }

      stop_requested_ = false;
      if (needs_model_reset_)
      {
        reset_model_state_();
        needs_model_reset_ = false;
      }
      lifecycle_state_ = LifecycleState::starting;

      try
      {
        thread_ = std::thread([this]
                              {
          nmos_node_start_();
          finish_worker_thread(); });
      }
      catch (...)
      {
        lifecycle_state_ = LifecycleState::stopped;
        stop_requested_ = false;
        lifecycle_cv_.notify_all();
        return false;
      }
    }

    std::unique_lock<std::mutex> lifecycle_lock(lifecycle_mutex_);
    lifecycle_cv_.wait(lifecycle_lock, [&]
                       { return LifecycleState::starting != lifecycle_state_; });

    return LifecycleState::running == lifecycle_state_;
  }

  void NodeLifecycleController::finish_worker_thread()
  {
    {
      std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
      lifecycle_state_ = LifecycleState::stopped;
      stop_requested_ = false;
    }
    lifecycle_cv_.notify_all();
  }
}
