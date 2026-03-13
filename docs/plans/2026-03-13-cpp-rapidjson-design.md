# Design — C++ variant with RapidJSON + valijson

**Date:** 2026-03-13
**Goal:** Maximize C++ batch throughput by replacing `nlohmann/json` with RapidJSON (in-situ DOM parsing) and `pboettch/json-schema-validator` with valijson.

---

## Problem

The current C++ implementation uses `nlohmann/json` (DOM parser that copies all strings) + `pboettch/json-schema-validator`. In batch mode it scores ~60.8 MB/s, well below Rust (112.6 MB/s) and Node.js (222.8 MB/s). The README identifies this bottleneck: RapidJSON would likely push C++ ahead of Rust.

---

## Approach

New file `cpp/validator_rapidjson.cpp` + second CMake target `validator_rj`. The existing `validator` target (nlohmann) is untouched. Both coexist in the same build.

### Stack

| Component | Library | Notes |
|-----------|---------|-------|
| JSON parser | RapidJSON (Tencent, header-only) | In-situ parsing, zero string copies |
| Schema validator | valijson (header-only) | JSON Schema Draft 7, native RapidJSON adapter |
| CLI | getopt_long (stdlib) | Same flags as existing impl |
| Colors | ANSI codes | Same as existing impl |

### Key technical choices

**In-situ parsing**: Read file into `std::vector<char>` (mutable buffer), call `doc.ParseInsitu(buf.data())`. RapidJSON modifies the buffer in-place — strings are pointers into the buffer, zero heap allocations for string data.

**Schema compilation**: `valijson::SchemaParser` + `valijson::Schema` — compiled once in batch mode, reused per file via `valijson::Validator`.

**RapidJSON adapter**: `valijson::adapters::RapidJsonAdapter` wraps a `rapidjson::Document` without copying. Schema and instance both go through adapters.

**Error collection**: `valijson::ValidationResults` — iterable, each result has `context` (JSON pointer path) and `description` (message). Mapped to the same `ErrorEntry{path, message}` struct as the existing impl.

### Interface (identical to existing)

```
validator_rj -s SCHEMA (-f FILE | -b DIR) [-v] [-j]
```

Output format (text + JSON `-j`) is word-for-word identical to `validator`.

---

## Files to create/modify

| File | Action |
|------|--------|
| `cpp/validator_rapidjson.cpp` | Create — new implementation |
| `cpp/CMakeLists.txt` | Modify — add FetchContent for RapidJSON + valijson, add `validator_rj` target |

---

## Expected outcome

C++ batch throughput should significantly exceed the current ~60.8 MB/s and likely surpass Rust (~112.6 MB/s). The new result will appear as a 6th row in the benchmark comparison if `benchmark.sh` is updated.

---

## Out of scope

- Multithreading (separate concern)
- Modifying the benchmark script (optional follow-up)
- Replacing the existing `validator` target
