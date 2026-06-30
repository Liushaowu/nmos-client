# Refactor Seam Smoke Tests

This directory contains source-controlled smoke tests for the internal seams added during the Sisyphus refactor, including the Phase 5 connection helper decisions such as transport-params readiness checks.

`refactor_seams_smoke.cpp` is wired into CMake/CTest as `refactor_seams_smoke`. It is intentionally narrow: it exercises extracted internal seams without launching the NMOS daemon or changing package/install behavior.

Recommended verification, from the repository root after dependencies are available:

```bash
cmake --workflow --preset debug
ctest --test-dir build/debug --output-on-failure
```
