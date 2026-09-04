#include "app.h"

#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>

namespace {

std::atomic<bool> g_stop_requested{false};

void handle_signal(int) { g_stop_requested = true; }

}

int main(int argc, char **argv) {
  printf("nmos-sync-daemon starting with config: %s\n", argc > 1 ? argv[1] : "daemon_config.json");
  const std::string config_path =
      argc > 1 ? argv[1] : "daemon_config.json";

  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  try {
    seeder::nmos_sync::App app(config_path);
    std::thread runner([&app]() { app.run(); });
    printf("nmos-sync-daemon started, waiting for stop signal...\n");
    while (!g_stop_requested.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    app.request_stop();
    runner.join();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "nmos-sync-daemon failed: " << error.what() << std::endl;
    return 1;
  }
}
