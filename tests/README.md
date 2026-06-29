# Refactor Seam Smoke Tests

This directory contains source-controlled smoke tests for the internal seams added during the Sisyphus refactor, including the Phase 5 connection helper decisions such as transport-params readiness checks.

`refactor_seams_smoke.cpp` is intentionally not wired into the default build yet because the current environment may be missing media/NMOS dependencies. It is a phase 7 scaffold that can be compiled once the build environment is complete.

Future fixed command, from the repository root after dependencies are available:

```bash
c++ -std=c++17 -Isrc tests/refactor_seams_smoke.cpp \
  src/node_stream_store.cpp \
  src/node_stream_store_ids.cpp \
  src/node_stream_store_receiver_ids.cpp \
  src/node_callback_dispatcher.cpp \
  src/node_connection_handlers.cpp \
  src/node_sdp_service.cpp \
  src/node_server_runtime.cpp \
  src/daemon/video_format.cpp \
  src/node_implementation.cpp \
  -lnmos-cpp -lcpprest -lboost_system -o /tmp/nmos-refactor-seams-smoke
/tmp/nmos-refactor-seams-smoke
```

When the repository gets a formal test target, move this into CMake/CTest rather than keeping it as a manual command.
