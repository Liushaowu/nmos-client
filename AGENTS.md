# PROJECT KNOWLEDGE BASE

**Generated:** 2026-03-04 11:17:10 CST  
**Commit:** 7e079b0  
**Branch:** main

## OVERVIEW
This repository is a CMake-based C++ NMOS client library. Core logic is concentrated in `src/node.cpp`; root mostly contains build wiring and runtime config.

## SESSION QUICK READ
Use this section as the first 1-2 minute onboarding pass in a new session.

- **Product role**: This project is a control-plane NMOS node wrapper library on top of `nmos-cpp`, not a media data-plane engine.
- **Build artifact**: Default target is static library `nmos-client`; executable wiring in `src/CMakeLists.txt` is currently commented out.
- **Public API entry**: `src/node.h` exposes `Node` plus `VideoSender`/`AudioSender`/`VideoReceiver`/`AudioReceiver` models.
- **Runtime startup chain**: `Node::start` -> `Node::Impl::start` -> `nmos_node_start` in `src/node.cpp`.
- **Core resource pattern**:
  - Sender path: `source -> flow -> sender -> connection_sender`
  - Receiver path: `receiver -> connection_receiver`
  - Shared helpers: `impl::make_id`, `impl::set_label_description`, `impl::insert_group_hint`
- **Configuration source**: runtime settings come from the path passed into `Node(...)`; sample keys live in `node_config_dev.json`.
- **Scope boundary**: This repo manages NMOS resource/connection lifecycle (IS-04/IS-05 style behavior), including transport activation callbacks, not stream payload processing.
- **Fast read order**:
  1. `src/node.h` (what can be called)
  2. `src/node.cpp` (`nmos_node_start`, `add/remove_*`)
  3. `src/node_implementation.*` (ID/label/grouping conventions)
  4. `node_config_dev.json` (runtime knobs)

### Common Misreads
- Do not assume a runnable `nmos-client` executable exists by default.
- Do not treat `build/` outputs as source-of-truth.
- Do not break sender/receiver add/remove symmetry when changing resource lifecycle logic.
- Keep the `redudancy` field spelling unchanged for compatibility with existing code paths.

## STRUCTURE
```text
./
├── CMakeLists.txt          # Root build orchestration
├── node_config_dev.json    # Runtime NMOS node settings sample
├── src/                    # Hand-written source of nmos-client library
├── build/                  # Generated artifacts (CMake cache, .a, .deb)
└── .vscode/                # Local debug configuration
```

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Build entry | `CMakeLists.txt` | Delegates all targets to `src/` |
| Library target | `src/CMakeLists.txt` | `nmos-client` static library definition |
| Runtime startup chain | `src/node.cpp` | `Node::start` → `Impl::start` → `nmos_node_start` |
| Public API surface | `src/node.h` | `Node` + sender/receiver data models |
| Runtime sample wiring | `src/main.cpp` | Example object setup and API calls |
| Runtime settings keys | `node_config_dev.json` | Host/IP/version and NMOS behavior knobs |

## CODE MAP
Implementation-level details are maintained in `src/AGENTS.md`; this map stays repository-oriented.

| Symbol | Type | Location | Refs | Role |
|--------|------|----------|------|------|
| `Node::Impl` | Class | `src/node.cpp` | 20 | Internal runtime state + orchestration |
| `nmos_node_start` | Method | `src/node.cpp` | 2 | Main NMOS server startup/teardown flow |
| `impl::make_id` | Function | `src/node_implementation.cpp` | 19 | Stable resource ID generation |
| `impl::set_label_description` | Function | `src/node_implementation.cpp` | 10 | Consistent resource metadata labeling |
| `impl::insert_group_hint` | Function | `src/node_implementation.cpp` | 6 | Group hints for sender/receiver resources |
| `main` | Function | `src/main.cpp` | 1 | Sample run path and API usage |

## CONVENTIONS
- Root `CMakeLists.txt` hard-sets `CMAKE_BUILD_TYPE Debug` instead of relying on caller flags.
- Default active target is static library (`add_library`); executable block in `src/CMakeLists.txt` is commented out.
- Runtime sample currently uses an absolute config path in `src/main.cpp`; repository config file lives at root (`node_config_dev.json`).
- `build/` is ignored by git and treated as generated-only; do not encode source-of-truth rules there.

## ANTI-PATTERNS (THIS PROJECT)
- Do not treat `build/`, `build_initdeep/`, or `.cache/` content as editable source-of-truth.
- Do not rely on stale CMake cache generated from another source path; configure a fresh build directory.
- Do not assume executable `nmos-client` exists by default; current active target is the static library.

## UNIQUE STYLES
- PImpl style: public `Node` in header, heavy logic in `Node::Impl` inside `src/node.cpp`.
- NMOS resource creation path follows repeated pattern: make IDs → make resource(s) → insert resources → set connection resources.
- Helper namespace `impl` centralizes repeatable IDs, labels, and grouping hints instead of duplicating inline logic.

## COMMANDS
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --workflow --preset debug
cmake --install build
```

## NOTES
- No repository-defined test target found (`enable_testing`/`add_test` absent).
- `.vscode/launch.json` expects `build/src/nmos-client`, but executable target is currently commented in `src/CMakeLists.txt`.
- Deep directory complexity is mostly generated CMake layers under `build/`, not source-domain complexity.
- Source-implementation constraints (resource symmetry, field compatibility, audio sender hotspot) are defined in `src/AGENTS.md`.
