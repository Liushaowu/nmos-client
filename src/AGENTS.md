# SOURCE SUBTREE GUIDE

Scope: applies to `src/**` only. In conflicts, this file overrides root `AGENTS.md` for this subtree.

## OVERVIEW
`src/` contains all hand-written C++ implementation for NMOS node runtime and public API.

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Core lifecycle | `node.cpp` | Main orchestration, threading, NMOS callbacks |
| Public API/contracts | `node.h` | Data models and `Node` method surface |
| Shared helpers | `node_implementation.h/.cpp` | ID generation, labels, group hints, port enums |
| Build target wiring | `CMakeLists.txt` | Static library target and install/export rules |
| Manual runtime example | `main.cpp` | Sample object wiring and callback registration |

## CONVENTIONS (LOCAL)
- Preserve PImpl boundary: public declarations in `node.h`, heavy logic in `node.cpp`.
- Keep sender/receiver operations symmetric across add/remove paths.
- Reuse `impl::make_id`, `impl::set_label_description`, `impl::insert_group_hint` for consistency.
- Keep naming and schema compatibility for `redudancy` fields (spelling is intentional in this codebase).

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
