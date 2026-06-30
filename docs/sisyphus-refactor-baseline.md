# Sisyphus Refactor Baseline

This baseline records the first refactor pass started from `docs/sisyphus-refactor-plan.md`.

## Build Baseline

Commands run from the repository root before the user requested no further compile attempts:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cmake --workflow --preset debug
```

Observed result:

* Configure succeeds.
* Build fails before this refactor because `src/node.cpp` includes missing `mtl/st20_api.h`.
* After extracting `node_settings.cpp`, the new settings module compiles before the build reaches the same pre-existing `mtl/st20_api.h` failure.
* CMake emits existing Boost/CMP0167 developer warnings from dependency discovery.

## Behavior Snapshot

The first pass only extracted settings/config/discovery helpers from `src/node.cpp` into `src/node_settings.*`.

Preserved behavior:

* Startup still applies interface-derived host addresses before `nmos::insert_node_default_settings`.
* `persisted_settings()` still returns an empty object when `config_file_` is empty, otherwise parses the same config file path.
* `write_persisted_settings()` still throws `node config file path is empty` when no config path exists, and still writes only JSON objects.
* `discover_registration_apis()` still serializes registration services with `uri`, `version`, `priority`, `host`, and `port` fields.
* `host_addresses`, `host_address`, and `interfaces` handling preserves the previous validation, duplicate removal, and first-address behavior.

Untouched high-risk paths:

* Public `Node` API in `src/node.h`.
* Sender lifecycle: `source -> flow -> sender -> connection_sender`.
* Receiver lifecycle: `receiver -> connection_receiver`.
* Add/remove/update resource symmetry.
* `impl::make_id`, `impl::set_label_description`, and `impl::insert_group_hint`.
* `redundancy` spelling consistency.

## Next Gate

Further refactor passes are proceeding by source review and search only, without compile attempts, because this environment may intentionally miss runtime/media dependencies such as `mtl/st20_api.h`.

## Phase 2 Snapshot

Settings/config responsibilities now live behind `seeder::nmos_node::internal::NodeSettings`:

* `Node::Impl` stores `internal::NodeSettings settings_` instead of a raw `config_file_` string.
* Runtime config loading is delegated to `NodeSettings::load_runtime_settings()`.
* Persisted settings read/write is delegated to `NodeSettings::persisted_settings()` and `NodeSettings::write_persisted_settings()`.
* Registration API discovery JSON serialization is delegated to `NodeSettings::discover_registration_apis()`.
* `Node::Impl` still owns `node_model_`, startup coordination, logging setup, resource lifecycle, connection handling, and callbacks.

## Phase 3 Snapshot

Sender and receiver resource construction now lives behind `seeder::nmos_node::internal::NodeResourceFactory`:

* `NodeResourceFactory` creates video/audio/ancillary sender resource bundles as `source -> flow -> sender -> connection_sender`.
* `NodeResourceFactory` creates video/audio/ancillary receiver resource bundles as `receiver -> connection_receiver`.
* `Node::Impl` keeps thin `make_*_resources()` wrappers so existing add/update/runtime-interface paths still call the same local names.
* `Node::Impl` still owns add/remove/update orchestration, insertion order, rollback cleanup, cache updates, and `node_model_.notify()` timing.
* Receiver resource ID helpers remain reachable through `Node::Impl` wrappers, but their ID generation is delegated to the factory and still uses `impl::make_id`.
* Resource metadata still flows through `impl::set_label_description` and `impl::insert_group_hint`.

## Phase 4A Snapshot

Stream cache ownership now lives behind `seeder::nmos_node::internal::StreamStore`:

* `Node::Impl` stores `internal::StreamStore stream_store_` instead of six direct sender/receiver vectors.
* `StreamStore` owns cached `VideoSender`, `AudioSender`, `AncillarySender`, `VideoReceiver`, `AudioReceiver`, and `AncillaryReceiver` vectors.
* Existing `Node::Impl` sender/receiver mutexes still guard store access, preserving previous lock timing.
* Existing `Node::Impl` helper names such as `find_video_sender_by_id()` remain as thin delegates to `StreamStore`, preserving activation and resolver call sites.
* `Node::Impl` still owns `node_model_`, ID vectors, add/remove/update orchestration, callback dispatch, resource insertion/removal, rollback cleanup, and `node_model_.notify()` timing.
* `ResourceLifecycleService` remains intentionally deferred until a compile-capable validation pass is available.

No compile, build, or CTest command was run for Phase 4A by user instruction.

## Phase 4B Snapshot

Sender/receiver resource ID cache ownership also moved into `StreamStore`:

* `StreamStore` now owns sender IDs, receiver IDs, source IDs, and flow IDs.
* `Node::Impl` still owns node/device IDs because those remain tied to startup model construction.
* Device `senders` and `receivers` fields are still updated in the original add/remove paths, using `StreamStore` ID snapshots.
* Transportfile refresh loops still iterate the same sender ID sequence, now through `stream_store_.sender_ids()`.
* Add/remove/update lifecycle order is still in `Node::Impl`; `ResourceLifecycleService` is still deferred.

No compile, build, or CTest command was run for Phase 4B by user instruction.

## Phase 4C Snapshot

Low-level resource lifecycle helpers now live behind `ResourceLifecycleService`:

* `ResourceLifecycleService` wraps delayed insert, delayed remove, erase-if-present, node resource replacement, and connection resource replacement.
* `Node::Impl` keeps thin helper methods with the old names so add/remove/update call sites remain easy to compare with earlier phases.
* Sender add order remains visible as `source -> flow -> sender -> connection_sender` in `Node::Impl`.
* Receiver add order remains visible as `receiver -> connection_receiver` in `Node::Impl`.
* Full add/remove/update orchestration, cache mutation order, device sender/receiver list updates, callback dispatch, and transportfile updates still remain in `Node::Impl`.
* The broader `ResourceLifecycleService` orchestration target is deferred until the code can be build-validated or covered by phase 7 tests.

No compile, build, or CTest command was run for Phase 4C by user instruction.

## Phase 5A Snapshot

Callback storage now lives behind `CallbackDispatcher`:

* `CallbackDispatcher` owns all user callback functions and its own mutex.
* `Node::Impl` setters delegate to `CallbackDispatcher` and no longer store callback function fields directly.
* Registration and activation paths still call callbacks from the same locations as before, using dispatcher snapshots.
* Receiver and sender activation still update stream state first, release stream locks, then call user callbacks afterward.
* `ConnectionHandlers` and `SdpService` extraction remain deferred; IS-05 activation and transport parameter parsing still live in `Node::Impl` for behavior visibility.

No compile, build, or CTest command was run for Phase 5A by user instruction.

## Phase 5B Snapshot

SDP and transport-file helper logic now lives behind `NodeSdpService`:

* `NodeSdpService` owns destination-IP sanitization for transportfile SDP generation.
* `NodeSdpService` owns RTP enabled fallback parsing for transport parameters.
* `NodeSdpService` owns video/audio receiver updates from SDP transport files.
* `Node::Impl` keeps thin same-name wrapper functions to avoid changing activation and transportfile call sites in this phase.
* IS-05 activation, auto resolver, and sender transportfile setter control flow still remain in `Node::Impl` for behavior visibility.
* `NodeSdpService` does not depend on `node_model_`, `StreamStore`, `CallbackDispatcher`, or lifecycle/resource mutation helpers.

No compile, build, or CTest command was run for Phase 5B by user instruction.

## Phase 6A Snapshot

Thin SDP wrapper functions were removed from `node.cpp`:

* Activation and transportfile code now calls `internal::NodeSdpService` directly for RTP enabled parsing, receiver transport-file updates, and destination-IP sanitization.
* The execution order in activation and sender transportfile generation is unchanged.
* `node.cpp` no longer carries the video-format guessing and SDP receiver-update helper bodies; those remain in `NodeSdpService`.
* `node.cpp` still retains local helpers that are used by non-SDP paths, including PTP and transportfile generation helpers.

No compile, build, or CTest command was run for Phase 6A by user instruction.

## Phase 6B Snapshot

Static review cleanup after phase 8 preparation found and fixed a receiver resource-id lookup issue:

* `StreamStore` now records receiver NMOS resource IDs alongside public receiver IDs when receivers are added.
* `find_*_receiver_by_resource_id()` uses that mapping instead of assuming public receiver structs contain a `receiver_id` field.
* Receiver removal clears both public receiver cache entries and resource-id mappings.
* Sender/receiver/source/flow ID list methods were split into `node_stream_store_ids.cpp` to reduce `node_stream_store.cpp` size.
* Receiver resource-id lookup/removal helpers were split into `node_stream_store_receiver_ids.cpp` to keep `node_stream_store.cpp` under the local size threshold.

No compile, build, CTest, or smoke-test command was run for Phase 6B by user instruction.

## Phase 7A Snapshot

Test scaffolding was added without wiring it into the default build:

* `tests/refactor_seams_smoke.cpp` covers the extracted `StreamStore`, `CallbackDispatcher`, and `NodeSdpService` seams with simple assertions.
* `tests/README.md` documents a fixed future compile/run command for dependency-complete environments.
* No CMake or CTest target was added yet, so normal project build shape remains unchanged.
* This is scaffold source only; it has not been compiled or executed in the current environment by user instruction.

No compile, build, CTest, or smoke-test command was run for Phase 7A by user instruction.

## Phase 8 Static Review Snapshot

Static-only post-refactor review found and fixed two cleanup issues without changing public API or lifecycle ordering:

* The future smoke-test command in `tests/README.md` now lists the split `node_stream_store_ids.cpp` and `node_stream_store_receiver_ids.cpp` files required by the smoke test.
* `tests/refactor_seams_smoke.cpp` now directly includes `<nmos/json_fields.h>` for its `nmos::fields` assertions instead of relying on indirect includes.
* Oversized `node_resource_factory.cpp` was split into small responsibility files: constructor/id generation, sender resource construction, receiver resource construction, runtime interface helpers, transport endpoint helpers, and video-format helpers.
* `src/CMakeLists.txt` was updated only to add the new internal factory source files; no test target, install rule, or build target shape was added.
* `src/node.h` still has no diff, preserving the public API and PImpl boundary.

Static checks performed for Phase 8 included `git diff -- src/node.h`, `git status --short`, grep checks for CMake source wiring, lifecycle insertion order, receiver resource-id lookup paths, and pure LOC measurements for the extracted files.

No compile, build, CTest, LSP diagnostic, or smoke-test command was run for Phase 8 by user instruction and because clangd is unavailable in this environment.

### Background Static Review Results

Four no-build review lanes were run after the Phase 8 cleanup:

* Security static review: PASS. No refactor-introduced security issue was found; generated/build directories were not edited, and the smoke-test command remains documentation-only.
* Code static review: initially FAIL because `node_resource_lifecycle.h` exposed `nmos::write_lock` without directly including `<nmos/mutex.h>`. The header now includes `<nmos/mutex.h>`.
* Context static review: PASS for no generated-dir edits and test scaffold not being wired into default build. It also noted a build-shape mismatch between older repository knowledge and the current `src/CMakeLists.txt`; the uncommitted diff only adds internal source files.
* Goal static review: FAIL for plan-compliance. The current code preserves public API, PImpl, lifecycle ordering, helper usage, `redundancy` semantics, and CMake diff shape, but it does not yet satisfy the plan's full Phase 2, Phase 4, and Phase 5 targets.

Plan-compliance gaps to resolve before calling the refactor complete:

* Phase 2 gap: `NodeServerRuntime` has not been extracted; `Node::Impl` still owns server/thread/lifecycle coordination.
* Phase 4 gap: `ResourceLifecycleService` currently wraps low-level resource helpers, but add/remove/update orchestration remains in `Node::Impl`.
* Phase 5 gap: `CallbackDispatcher` and `NodeSdpService` exist, but `ConnectionHandlers` has not been extracted; activation and connection handling still live in `Node::Impl`.

`docs/sisyphus-refactor-continuation.md` now records the safe no-build continuation boundaries for these gaps. Phase 2, Phase 4, and Phase 5 must be treated as partially satisfied until their deferred runtime/lifecycle/connection orchestration moves can be validated in a dependency-complete environment.

No compile, build, CTest, LSP diagnostic, or smoke-test command was run while collecting or addressing these review results.

## Static Continuation Snapshot

The first no-build continuation pass addressed the review gaps with intentionally narrow seams:

* Phase 2 runtime seam: added `NodeServerRuntime` as an internal shell and moved only stop elapsed logging into it. `Node::Impl` still owns `start`, `stop`, `nmos_node_start`, `thread_run`, server guard lifetime, worker thread ownership, lifecycle state, condition variables, and exception handling.
* Phase 4 lifecycle seam: extended `ResourceLifecycleService` with narrow sender and receiver insertion helpers. The helpers preserve sender insert order (`source -> flow -> sender -> connection_sender`) and receiver insert order (`receiver -> connection_receiver`) plus rollback of already-inserted resources. `Node::Impl` still owns existence checks, store/cache updates, device sender/receiver list updates, notifications, remove orchestration, and update orchestration.
* Phase 5 connection seam: added `NodeConnectionHandlers` and moved only pure JSON helper decisions into it (`active_leg_count_matches`, transport-params array checks, and transport-params empty checks). Activation lambdas, locks, callback dispatch, SDP service calls, auto resolver orchestration, and transportfile setter orchestration remain in `Node::Impl`.
* The smoke-test scaffold was extended to include future checks for `NodeConnectionHandlers` and `NodeServerRuntime` helper linkage. It remains source-only and is not wired into CMake/CTest.
* `tests/README.md` was updated so its future manual command lists the new helper source files. This command was not run.

Static checks performed for this continuation included `git diff -- src/node.h`, `git diff --check`, grep checks for CMake source wiring and helper call sites, lifecycle order checks, and pure LOC measurements for the new/changed seam files.

Remaining deferred work before declaring full plan completion:

* Full `NodeServerRuntime` extraction of server/thread lifecycle ownership.
* Full `ResourceLifecycleService` ownership of add/remove/update orchestration.
* Full `NodeConnectionHandlers` ownership of activation, auto-resolver, and transportfile handler orchestration.

These deferred moves are intentionally not attempted until a dependency-complete environment can compile and run targeted validation.

No compile, build, CTest, LSP diagnostic, or smoke-test command was run for this static continuation by user instruction.

## Build Validation Snapshot

After the `st20_api.h` dependency issue was resolved, the user authorized build validation with the fixed command:

```bash
cmake --workflow --preset debug
```

Build fixes made during validation:

* `node_resource_lifecycle.cpp` now includes `<nmos/slog.h>` so `std::pair<nmos::id, nmos::type>` logging uses the NMOS slog overload after the helper was split out of `node.cpp`.
* `NodeConnectionHandlers::is_transport_params_empty()` now uses `as_array().size()` because cpprest `web::json::array` does not provide `.empty()`.

Validation result:

* Configure completed.
* Build completed: `[100%] Built target nmos-sync-daemon`.
* Package completed: `build/debug/nmos-daemon_1.0.0_amd64.deb` was generated.
* CMake emitted existing developer warnings about policy `CMP0167` / removed `FindBoost`; these did not fail the workflow.

No CTest, smoke-test execution, runtime launch, or LSP diagnostic was run in this validation snapshot.

## Phase 4D Build-Validated Snapshot

The build-validated Phase 4 continuation moved another narrow lifecycle responsibility into `ResourceLifecycleService`:

* Sender add pre-insert cleanup now calls `ResourceLifecycleService::erase_sender_resources_if_present()` for the sender connection resource plus sender, flow, and source node resources.
* Receiver add pre-insert cleanup now calls `ResourceLifecycleService::erase_receiver_resources_if_present()` for the receiver connection resource and receiver node resource.
* Sender and receiver insertion order remains unchanged. Store updates, device sender/receiver list updates, notifications, remove orchestration, update orchestration, locks, and callbacks remain in `Node::Impl`.
* A now-unused `to_utf8_string()` helper was removed from `node.cpp` after the build surfaced it as dead code.

Validation command:

```bash
cmake --workflow --preset debug
```

Validation result:

* Configure completed.
* Build completed: `[100%] Built target nmos-sync-daemon`.
* Package completed: `build/debug/nmos-daemon_1.0.0_amd64.deb` was generated.
* Remaining warnings are CMake developer warnings about `CMP0167` / removed `FindBoost`; no source compiler warnings remained in the final run.

No CTest, smoke-test execution, runtime launch, or LSP diagnostic was run for Phase 4D.

## Phase 4E Build-Validated Snapshot

The next build-validated Phase 4 slice moved sender remove resource sequencing into `ResourceLifecycleService`:

* `ResourceLifecycleService::remove_sender_resources_after()` now owns the sender removal resource order used by video, audio, and ancillary senders: sender node resource, connection sender, source, then flow.
* `Node::Impl` still owns existence checks, sender cache updates, ID-store updates, device sender list updates, notifications, and user-visible logging.
* Public API remained unchanged.

Validation command:

```bash
cmake --workflow --preset debug
```

Validation result:

* Configure completed.
* Build completed: `[100%] Built target nmos-sync-daemon`.
* Package completed: `build/debug/nmos-daemon_1.0.0_amd64.deb` was generated.
* Remaining warnings are CMake developer warnings about `CMP0167` / removed `FindBoost`.

No CTest, smoke-test execution, runtime launch, or LSP diagnostic was run for Phase 4E.

## Phase 4F Build-Validated Snapshot

The next build-validated Phase 4 slice moved receiver remove resource sequencing into `ResourceLifecycleService`:

* `ResourceLifecycleService::remove_receiver_resources_after()` now owns the receiver removal resource order used by video, audio, and ancillary receivers: connection receiver, then receiver node resource.
* `Node::Impl` still owns receiver existence checks, receiver cache updates, receiver ID-store updates, device receiver list updates, notifications, and user-visible logging.
* Public API remained unchanged.

Validation command:

```bash
cmake --workflow --preset debug
```

Validation result:

* Configure completed.
* Build completed: `[100%] Built target nmos-sync-daemon`.
* Package completed: `build/debug/nmos-daemon_1.0.0_amd64.deb` was generated.
* Remaining warnings are CMake developer warnings about `CMP0167` / removed `FindBoost`.

No CTest, smoke-test execution, runtime launch, or LSP diagnostic was run for Phase 4F.

## Phase 4G Build-Validated Snapshot

The next build-validated Phase 4 slice moved update node-resource replacement checks into `ResourceLifecycleService`:

* `ResourceLifecycleService::replace_sender_node_resources()` now owns replacement of sender source, flow, and sender node resources and returns one success flag for the resource-update boundary.
* `ResourceLifecycleService::replace_receiver_node_resource()` now owns receiver node-resource replacement.
* `Node::Impl` still owns connection resource replacement lambdas, active endpoint preservation, sender transportfile refresh, store/cache replacement, notifications, lock boundaries, and update fallback-to-add behavior.
* Public API remained unchanged.

Validation command:

```bash
cmake --workflow --preset debug
```

Validation result:

* Configure completed.
* Build completed: `[100%] Built target nmos-sync-daemon`.
* Package completed: `build/debug/nmos-daemon_1.0.0_amd64.deb` was generated.
* Remaining warnings are CMake developer warnings about `CMP0167` / removed `FindBoost`.

No CTest, smoke-test execution, runtime launch, or LSP diagnostic was run for Phase 4G.

## Phase 5C Build-Validated Snapshot

The next build-validated Phase 5 slice moved one more pure connection decision into `NodeConnectionHandlers`:

* `NodeConnectionHandlers::TransportParamsState` now describes whether a `transport_params` JSON value is ready, not an array, or empty.
* `NodeConnectionHandlers::transport_params_state()` centralizes the duplicated transport-params preflight used by connection activation and `resolve_auto`.
* `Node::Impl` still owns activation orchestration, logging text, early-return timing, locks, stream cache mutation, SDP service calls, auto-resolver details, and callback dispatch.
* The local `Node::Impl::active_leg_count_matches()` forwarding helper was removed; update paths call `NodeConnectionHandlers::active_leg_count_matches()` directly.

Validation command:

```bash
cmake --workflow --preset debug
```

Validation result:

* Configure completed.
* Build completed: `[100%] Built target nmos-sync-daemon`.
* Package completed: `build/debug/nmos-daemon_1.0.0_amd64.deb` was generated.
* Remaining warnings are CMake developer warnings about `CMP0167` / removed `FindBoost`.

No CTest, smoke-test execution, runtime launch, or LSP diagnostic was run for Phase 5C. LSP diagnostics were attempted on the changed C++ files, but the language-server connection is unavailable in this environment.

## Restarted Phase 1 Snapshot - Refactorable `node.h`

This snapshot restarts the plan from Phase 1 after the constraint on `src/node.h` changed. `node.h` is no longer treated as an immutable external library public API; it is now a daemon-facing facade/type surface that may be split and simplified in later phases.

Current build shape:

* Active target is the `nmos-sync-daemon` executable in `src/CMakeLists.txt`.
* `NMOS_CLIENT_CORE_SOURCES` includes the extracted internal modules plus `node.cpp` and `daemon/video_format.cpp`.
* There is no separate `nmos-client` library target in the active source wiring.
* The smoke scaffold under `tests/` remains source-controlled but is not wired into CMake/CTest.

Current `node.h` surface before Phase 2 header work:

* Includes standard function/memory/string/vector headers plus `cpprest/json.h` and `cpprest/host_utils.h`.
* Still defines implementation-adjacent `delay_millis` at header scope.
* Still mixes `Node` facade declaration with `redundancy`, sender/receiver models, `VideoReceiverCaps`, and `RegistrationStatus`.
* Still carries callback signatures directly as `std::function` parameters on `Node` setter methods.
* Still exposes cpprest-facing settings and runtime-interface methods on the facade.

High-risk behavior invariants to preserve while restarting:

* Sender resource lifecycle remains `source -> flow -> sender -> connection_sender`.
* Receiver resource lifecycle remains `receiver -> connection_receiver`.
* `impl::make_id`, `impl::set_label_description`, and `impl::insert_group_hint` remain the metadata/ID helpers.
* `redundancy` spelling is consistent with daemon DTO parsing and current model fields.
* Activation and `resolve_auto` currently use `NodeConnectionHandlers::transport_params_state()` for transport-params readiness, while `Node::Impl` still owns logging, lock boundaries, stream cache mutation, SDP calls, and callback dispatch timing.
* `CallbackDispatcher` owns callback storage/snapshotting; callbacks are invoked after stream cache updates and outside the sender/receiver store locks in the current activation paths.

Recommended next Phase 2 entry point:

* Create a narrow type header for pure models first, then update internal modules and daemon DTO/runtime call sites to include it directly.
* Move `delay_millis` out of `node.h` before or during the facade shrink.
* Do not remove PImpl in the header-splitting slice; keep PImpl removal as a separate future decision with its own validation.

Validation command:

```bash
cmake --workflow --preset debug
```

Validation result:

* Configure completed.
* Build completed: `[100%] Built target nmos-sync-daemon`.
* Package completed: `build/debug/nmos-daemon_1.0.0_amd64.deb` was generated.
* Remaining warnings are CMake developer warnings about `CMP0167` / removed `FindBoost`.

No CTest, smoke-test execution, runtime launch, or LSP diagnostic was run for this restarted Phase 1 snapshot.

## Phase 2A Build-Validated Snapshot

The first restarted Phase 2 slice split pure daemon data models out of `node.h` without changing model fields or facade method signatures:

* Added `src/node_types.h` for `redundancy`, sender/receiver models, `VideoReceiverCaps`, and `RegistrationStatus`.
* `src/node.h` now includes `node_types.h` and only declares the `Node` facade plus its cpprest-facing methods and callback setters.
* `delay_millis` moved out of `node.h` and into `node.cpp`, where the delayed resource lifecycle calls use it.
* Model-only internals now include `node_types.h` directly: `node_callback_dispatcher.h`, `node_stream_store.h`, `node_resource_factory.h`, `node_sdp_service.h`, `daemon/dto.h`, and `daemon/dto.cpp`.
* `daemon/node_runtime.h` now includes `../node.h` directly because it owns the concrete `nmos_node::Node` facade instance.
* `node_types.h` does not include cpprest or NMOS runtime headers; it only depends on standard string/vector types.

Validation command:

```bash
cmake --workflow --preset debug
```

Validation result:

* First run correctly exposed a missing direct facade include in `daemon/node_runtime.h`; this was fixed by including `../node.h` there.
* Re-run configure completed.
* Re-run build completed: `[100%] Built target nmos-sync-daemon`.
* Package completed: `build/debug/nmos-daemon_1.0.0_amd64.deb` was generated.
* Remaining warnings are CMake developer warnings about `CMP0167` / removed `FindBoost`.

No CTest, smoke-test execution, runtime launch, or reliable LSP diagnostic was run for Phase 2A. LSP diagnostic calls were attempted but the language-server connection closed in this environment.

## Phase 2B Build-Validated Snapshot

The next restarted Phase 2 slice split callback function type declarations out of the facade and dispatcher:

* Added `src/node_callbacks.h` for named sender, receiver, and registration callback aliases.
* `src/node.h` now uses `VideoSenderCallback`, `AudioSenderCallback`, `AncillarySenderCallback`, `VideoReceiverCallback`, `AudioReceiverCallback`, `AncillaryReceiverCallback`, and `RegistrationChangedCallback` in the facade setters.
* `CallbackDispatcher` now uses the same aliases for stored callbacks, snapshots, setters, and the registration callback accessor.
* The aliases are exact `std::function<void(const Model &)>` equivalents, so callback invocation behavior and call sites are unchanged.

Validation command:

```bash
cmake --workflow --preset debug
```

Validation result:

* Configure completed.
* Build completed: `[100%] Built target nmos-sync-daemon`.
* Package completed: `build/debug/nmos-daemon_1.0.0_amd64.deb` was generated.
* Remaining warnings are CMake developer warnings about `CMP0167` / removed `FindBoost`.

No CTest, smoke-test execution, runtime launch, or LSP diagnostic was run for Phase 2B.

## Phase 2C Build-Validated Snapshot

The final restarted Phase 2 slice isolated the cpprest-facing runtime type spellings behind named facade aliases:

* Added `src/node_runtime_types.h` for `NodeSettingsJson`, `RuntimeInterface`, and `RuntimeInterfaces`.
* `src/node.h` no longer includes cpprest headers directly; it includes `node_runtime_types.h` and uses aliases in the settings/runtime-interface facade methods.
* `Node::Impl` and the outer `Node` method definitions now use the same runtime aliases for `effective_settings`, `persisted_settings`, `discover_registration_apis`, `write_persisted_settings`, `set_runtime_interfaces`, and the stored runtime interface list.
* The aliases preserve the existing concrete cpprest types: `web::json::value` and `std::vector<web::hosts::experimental::host_interface>`.

Validation command:

```bash
cmake --workflow --preset debug
```

Validation result:

* Configure completed.
* Build completed: `[100%] Built target nmos-sync-daemon`.
* Package completed: `build/debug/nmos-daemon_1.0.0_amd64.deb` was generated.
* Remaining warnings are CMake developer warnings about `CMP0167` / removed `FindBoost`.

No CTest, smoke-test execution, runtime launch, or LSP diagnostic was run for Phase 2C.

## Phase 3 Build-Validated Snapshot

The restarted Phase 3 slice extracted the NMOS node server lifecycle (`nmos_node_start`) from `Node::Impl` into `NodeServerRuntime`:

* Added `LifecycleState` enum to `node_server_runtime.h` (moved from `Node::Impl` private enum).
* Added `NodeServerRuntime::ServerContext` struct holding `node_model`, `settings`, `gate_ptr`, `lifecycle_mutex`, `lifecycle_state`, `lifecycle_cv`, and `stop_requested` references.
* Added `NodeServerRuntime::start(ServerContext&, thread_run_fn, node_impl)` static method that now owns the full `nmos_node_start` body (log model construction, settings loading, node server creation, server guard, lifecycle state transitions, blocking wait, and cleanup).
* `Node::Impl::nmos_node_start()` is now a thin 10-line wrapper that constructs a `ServerContext`, calls `make_node_implementation()`, and delegates to `NodeServerRuntime::start()`.
* `Node::Impl::wait_for_stop_signal()` was removed; the blocking wait is now inline in `NodeServerRuntime::start()`.
* `Node::Impl` now uses `using LifecycleState = internal::LifecycleState;` instead of a private enum.

Validation command:

```bash
cmake --workflow --preset debug
```

Validation result:

* Configure completed.
* Build completed: `[100%] Built target nmos-sync-daemon`.
* Package completed: `build/debug/nmos-daemon_1.0.0_amd64.deb` was generated.
* Remaining warnings are CMake developer warnings about `CMP0167` / removed `FindBoost`.

No CTest, smoke-test execution, runtime launch, or LSP diagnostic was run for Phase 3.

## Phase 7 Build-Validated Snapshot

Phase 7 removed dead commented-out code from `node.cpp`:

* Removed 6 commented-out `try_bind_*` call sites in `add_video_sender`, `add_audio_sender`, `add_ancillary_sender`, `add_video_receiver`, `add_audio_receiver`, and `add_ancillary_receiver`.
* Removed 6 commented-out `try_bind_*` method definitions (~137 lines of dead code).
* `node.cpp` reduced from 2932 to 2790 lines (-142 lines, -5%).
* `receiver_mutex_` is still actively used in live code paths and was preserved.

Validation command:

```bash
cmake --workflow --preset debug
```

Validation result:

* Configure completed.
* Build completed: `[100%] Built target nmos-sync-daemon`.
* Package completed: `build/debug/nmos-daemon_1.0.0_amd64.deb` was generated.
* Remaining warnings are CMake developer warnings about `CMP0167` / removed `FindBoost`.

No CTest, smoke-test execution, runtime launch, or LSP diagnostic was run for Phase 7.

## Merge Refactor: Eliminate NodeRuntime

Net result: **-157 lines** across the codebase. `NodeRuntime` class deleted, event types and handlers moved into `Node`. `StateStore` now uses `nmos_node::RegistrationStatus` directly.

Build: `cmake --workflow --preset debug` → `[100%] Built target nmos-sync-daemon`.

## Build-Validated Continuation: Event, Resource, and Runtime Interface Extraction

The next continuation followed the plan's Phase 5, Phase 4, and Phase 3 boundaries by moving more remaining `Node::Impl` responsibilities into focused internal modules while preserving the `Node` PImpl facade.

Completed slices:

* Added `src/node_event_bridge.h` and `src/node_event_bridge.cpp` for event-handler bridge state and callback wiring. `Node::Impl` no longer owns receiver, sender, registration event handlers, or the event callback mutex directly.
* Added `src/node_resource_controller.h` and `src/node_resource_controller.cpp` for sender/receiver add, remove, update, resource replacement, stream-store lookup, and resource lifecycle orchestration. `Node::Impl` now delegates resource lifecycle methods to `NodeResourceController`.
* Added `src/node_runtime_interface_updater.h` and `src/node_runtime_interface_updater.cpp` for `set_runtime_interfaces()` reconciliation. Runtime interface changes now refresh sender/receiver resources and transport files through this updater.
* `src/CMakeLists.txt` includes the three new implementation files.
* `src/node.cpp` is reduced to startup/lifecycle coordination, settings/PTP/runtime facade wrappers, thin resource/event/updater delegation, and public `Node` PImpl forwarding.

Current line counts after this continuation:

```text
1106 src/node.cpp
 141 src/node_event_bridge.cpp
 926 src/node_resource_controller.cpp
 108 src/node_runtime_interface_updater.cpp
```

Validation command:

```bash
cmake --workflow --preset debug
```

Validation result:

* Configure completed.
* Build completed: `[100%] Built target nmos-sync-daemon`.
* Package completed: `build/debug/nmos-daemon_1.0.0_amd64.deb` was generated.
* Remaining warnings are CMake developer warnings about `CMP0167` / removed `FindBoost`.

No CTest, smoke-test execution, runtime launch, or LSP diagnostic was run for this continuation.

## Breaking Cleanup: Redundancy Type Spelling

The redundancy model type now uses the correct C++ type spelling while preserving the JSON schema key and field spelling:

* `src/node_types.h` defines `nmos_node::Redundancy`.
* Sender and receiver models use `Redundancy redundancy`.
* Daemon DTO parsing and serialization continue to read and write the JSON key `redundancy`.
* Project guidance and refactor docs no longer describe the old misspelling as intentional.

Consistency checks performed:

```bash
rg -n "<old misspellings and lowercase type spelling>" src docs
git diff --check
cmake --workflow --preset debug
```

Validation result:

* No old misspelling or old lowercase type spelling remained in `src` or `docs`.
* Whitespace diff check passed.
* Build completed: `[100%] Built target nmos-sync-daemon`.
* Package completed: `build/debug/nmos-daemon_1.0.0_amd64.deb` was generated.

## Build-Validated Continuation: PTP Clock Updater Extraction

The next Phase 7 cleanup slice moved PTP clock update details out of `Node::Impl` without changing the public facade method:

* Added `src/node_ptp_clock_updater.h` and `src/node_ptp_clock_updater.cpp`.
* Moved PTP GMID normalization and validation from `src/node.cpp` into the new updater implementation.
* Moved `set_ptp_clock()` node clock mutation and sender transportfile refresh into `NodePtpClockUpdater`.
* `Node::Impl::set_ptp_clock()` is now a thin delegate to `ptp_clock_updater_`.
* `src/CMakeLists.txt` includes `node_ptp_clock_updater.cpp` in `NMOS_CLIENT_CORE_SOURCES`.

Current line counts after this slice:

 ```text
 1010 src/node.cpp
  41 src/node_ptp_clock_updater.h
 142 src/node_ptp_clock_updater.cpp
 ```

Validation command:

```bash
cmake --workflow --preset debug
```

Validation result:

* Configure completed.
* Build completed: `[100%] Built target nmos-sync-daemon`.
* Package completed: `build/debug/nmos-daemon_1.0.0_amd64.deb` was generated.
* Remaining warnings are CMake developer warnings about `CMP0167` / removed `FindBoost`.

LSP diagnostics were attempted for `src/node.cpp`, `src/node_ptp_clock_updater.h`, and `src/node_ptp_clock_updater.cpp`, but the language-server connection was unavailable (`MCP error -32000: Connection closed` / `Not connected`).

## Build-Validated Continuation: Lifecycle Controller Extraction

The next Phase 3 cleanup slice moved `Node::Impl` start/stop lifecycle glue into a focused controller while preserving the public `Node` facade and the existing `NodeServerRuntime` server-start path:

* Added `src/node_lifecycle_controller.h` and `src/node_lifecycle_controller.cpp`.
* Moved `Node::Impl::start()` and `Node::Impl::stop()` orchestration into `NodeLifecycleController`.
* `Node::Impl::start()` and `Node::Impl::stop()` are now thin delegates to `lifecycle_controller_`.
* `NodeLifecycleController` receives callbacks for `reset_model_state()` and `nmos_node_start()` so the new module does not need to know `Node::Impl`.
* `thread_run()`, `node_implementation_run()`, `init()`, and `nmos_node_start()` remain in `Node::Impl` for this slice.
* `src/CMakeLists.txt` includes `node_lifecycle_controller.cpp` in `NMOS_CLIENT_CORE_SOURCES`.

Current line counts after this slice:

```text
 902 src/node.cpp
  53 src/node_lifecycle_controller.h
 154 src/node_lifecycle_controller.cpp
```

Validation command:

```bash
cmake --workflow --preset debug
```

Validation result:

* Configure completed.
* Build completed: `[100%] Built target nmos-sync-daemon`.
* Package completed: `build/debug/nmos-daemon_1.0.0_amd64.deb` was generated.
* Remaining warnings are CMake developer warnings about `CMP0167` / removed `FindBoost`.

## Phase 8 Closeout Preparation: CTest Smoke Wiring

The Phase 8 scaffold is no longer documentation-only:

* Root CMake now includes `CTest` and adds the `tests/` subtree when `BUILD_TESTING` is enabled.
* Added `tests/CMakeLists.txt` with a narrow `refactor_seams_smoke` executable and matching `add_test()` entry.
* The smoke target reuses the same `nmos-cpp::compile-settings` and `nmos-cpp::nmos-cpp` link targets as `nmos-sync-daemon`.
* Production target wiring, install rules, and CPack package settings remain unchanged.
* `tests/refactor_seams_smoke.cpp` now uses the current `connection_transport_params` seam instead of the retired `NodeConnectionHandlers` spelling.
* `tests/README.md` now documents the CMake/CTest verification path:

```bash
cmake --workflow --preset debug
ctest --test-dir build/debug --output-on-failure
```

Closeout validation commands:

```bash
cmake --workflow --preset debug
ctest --test-dir build/debug --output-on-failure
```

Validation result:

* Configure completed.
* Build completed: `[100%] Built target refactor_seams_smoke` and `nmos-sync-daemon` remained built.
* Package completed: `build/debug/nmos-daemon_1.0.0_amd64.deb` was generated.
* CTest ran `refactor_seams_smoke` successfully: `100% tests passed, 0 tests failed out of 1`.
* Existing CMake developer warnings about `CMP0167` / removed `FindBoost` remained non-blocking.
* LSP diagnostics remain unavailable in this environment (`MCP error -32000: Connection closed`) and are not treated as authoritative validation.

## Phase 9 Final Review

Final `review-work` was run across the completed refactor closeout:

* Goal and constraint verification: PASS.
* Hands-on QA execution: PASS.
* Code quality review: PASS.
* Security review: PASS, with a non-blocking note that the `gate_` null-dereference risk in the PTP logging path is pre-existing and not introduced by this refactor.
* Context mining: initially FAIL because root `AGENTS.md` still contained stale static-library/no-test-target guidance.

The context-mining blocker was fixed by updating root `AGENTS.md` to describe the current `nmos-sync-daemon` executable target, the `refactor_seams_smoke` CTest target, and the current build/test commands. The context-mining lane was then re-run and returned PASS.

Final validation after the review fix:

```bash
git diff --check
cmake --workflow --preset debug
ctest --test-dir build/debug --output-on-failure
```

Final validation result:

* Whitespace diff check passed.
* Build completed: `[100%] Built target refactor_seams_smoke` and `[100%] Built target nmos-sync-daemon`.
* Package completed: `build/debug/nmos-daemon_1.0.0_amd64.deb` was generated.
* CTest ran `refactor_seams_smoke` successfully: `100% tests passed, 0 tests failed out of 1`.
* Existing CMake developer warnings about `CMP0167` / removed `FindBoost` remained non-blocking.
