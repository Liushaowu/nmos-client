# SOURCE SUBTREE GUIDE

Scope: applies to `src/**` only. In conflicts, this file overrides root `AGENTS.md` for this subtree.

## OVERVIEW
`src/` contains all hand-written C++ implementation for the NMOS node runtime and daemon-facing API surface.

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Core lifecycle | `node.cpp` | Main orchestration, threading, NMOS callbacks |
| Facade/type surface | `node.h` and narrow `node_*` headers | `Node` entry plus internal data models |
| Shared helpers | `node_implementation.h/.cpp` | ID generation, labels, group hints, port enums |
| Build target wiring | `CMakeLists.txt` | Static library target and install/export rules |
| Manual runtime example | `main.cpp` | Sample object wiring and callback registration |

## CONVENTIONS (LOCAL)
- `node.h` is no longer protected as an immutable library public API; it may be split and simplified when all daemon call sites are updated.
- Preserve the PImpl boundary by default: `Node` remains a lightweight facade and heavy logic lives in `node.cpp` or focused internal modules.
- Keep sender/receiver operations symmetric across add/remove paths.
- Reuse `impl::make_id`, `impl::set_label_description`, `impl::insert_group_hint` for consistency.
- Use the correct `redundancy` spelling for redundancy fields and schema keys.

## ANTI-PATTERNS
- Do not edit generated code assumptions into source paths (e.g., values copied from `build/`).
- Do not introduce one-sided resource insertion/removal (causes model inconsistency).
- Do not bypass helper utilities by duplicating NMOS metadata-building logic inline.
- Do not skip reviewing `node.cpp` audio sender TODO block (`add_audio_sender`) when modifying audio flow.

## LOCAL VALIDATION
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --workflow --preset debug
```
