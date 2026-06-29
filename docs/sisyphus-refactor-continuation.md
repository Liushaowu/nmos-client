# Sisyphus Refactor Static Continuation

This continuation tracks the remaining phase-compliance gaps found by the no-build static review. It is deliberately conservative because the current environment must not run compile, build, CTest, smoke-test, or LSP validation.

## Phase 2 Runtime Seam

Current state:

* `NodeSettings` has been extracted.
* `Node::Impl` still owns server/thread/lifecycle state and the startup chain.
* No `NodeServerRuntime` module owns runtime coordination yet.

Safe static-only next step:

* Add a minimal internal `NodeServerRuntime` seam for tiny state-free helpers only.
* Keep `Node::Impl` owning `start`, `stop`, `nmos_node_start`, `thread_run`, server guard lifetime, worker thread ownership, lifecycle condition variables, and exception handling.

Deferred until compile-capable validation:

* Moving thread/server lifecycle ownership out of `Node::Impl`.
* Moving shutdown waiting or lifecycle-state transitions that affect synchronization.
* Moving log model or NMOS server initialization ownership.

## Header / Type Surface Seam

Current state:

* `src/node.h` is no longer treated as an immutable public API contract.
* `node.h` still mixes the `Node` facade, sender/receiver data models, registration state, cpprest-facing methods, callback signatures, and an implementation constant.

Safe next step:

* Split pure data models into a narrow internal type header first.
* Move callback aliases and runtime-interface/cpprest-facing declarations only after call sites include the new type header directly.
* Keep `Node` as a lightweight facade unless a later validated slice proves PImpl removal is simpler.

Deferred until build validation:

* Renaming model fields beyond mechanical call-site updates.
* Removing PImpl or replacing `Node` with direct services.
* Changing daemon-facing method semantics.

## Phase 4 Lifecycle Seam

Current state:

* `ResourceLifecycleService` wraps delayed insert/remove, erase-if-present, and resource replacement.
* Full add/remove/update orchestration remains in `Node::Impl`.

Safe static-only next step:

* Move only contiguous resource insertion blocks whose order and rollback can be textually compared.
* Candidate sender order: `source -> flow -> sender -> connection_sender`.
* Candidate receiver order: `receiver -> connection_receiver`.
* Keep store mutation, device sender/receiver list updates, existence checks, notifications, and callback timing in `Node::Impl`.

Deferred until compile-capable validation:

* Moving full `add_*`, `remove_*`, or `update_*` methods.
* Changing `node_model_.notify()` placement.
* Changing sender/receiver mutex boundaries.
* Changing update fallback-to-add behavior.

## Phase 5 Connection Handler Seam

Current state:

* `CallbackDispatcher` owns callback storage and snapshotting.
* `NodeSdpService` owns SDP and transport-file helper logic.
* `NodeConnectionHandlers` owns pure JSON connection decisions: active leg-count matching and transport-params readiness checks.
* Activation, auto-resolver, and transportfile setter orchestration still live in `Node::Impl`.

Safe static-only next step:

* Move pure helpers that do not touch callbacks, locks, `node_model_`, `StreamStore`, `NodeSdpService`, or `gate_`.
* Candidate helpers: additional transport-params guards and endpoint active leg-count decisions that remain pure JSON checks.

Deferred until compile-capable validation:

* Moving the full connection activation lambda.
* Moving callback dispatch sequencing.
* Changing stream cache mutation timing.
* Moving `resolve_auto` or `set_transportfile` ownership.

## Static Verification Rules

Allowed now:

* Static diff review.
* Grep checks for call sites, include self-containment, CMake source wiring, lifecycle order, and stale wide-header dependencies.
* Documentation consistency updates.

Not allowed now:

* `cmake`, `cmake --build`, `cmake --workflow`, `ctest`, direct compiler commands, smoke-test execution, or LSP diagnostics.
