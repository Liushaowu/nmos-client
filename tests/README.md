# Refactor Seam Smoke Tests

This directory contains source-controlled smoke tests for the internal seams added during the Sisyphus refactor, including the Phase 5 connection helper decisions such as transport-params readiness checks.

`refactor_seams_smoke.cpp` is wired into CMake/CTest as `refactor_seams_smoke`. It is intentionally narrow: it exercises extracted internal seams without launching the NMOS daemon or changing package/install behavior.

Recommended verification, from the repository root after dependencies are available:

```bash
cmake --workflow --preset debug
ctest --test-dir build/debug --output-on-failure
```

`registry_client_test` starts a temporary loopback HTTP server to exercise the
registry Sender query client: upstream pagination, filtering before pagination,
UTF-8 keywords, type classification, selected-page SDP retrieval, missing Flow or
manifest, SDP HTTP failures, malformed upstream responses and pagination loops.
It requires permission to listen on a local TCP port.

`sdp_format_test` validates video/raw and JPEG XS format extraction, progressive,
interlaced and PsF naming, exact fractional frame rates, DUP descriptions, PCM
audio bit depth and channels, compressed audio metadata and malformed SDP. It
does not start network services. The registry client test also verifies that
parsed audio/video formats appear in HTTP-backed query results and that malformed
SDP preserves its original text without failing the Sender list.
