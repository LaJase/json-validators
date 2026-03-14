# Go Validator — Stdlib Optimization Design

**Date:** 2026-03-14
**Scope:** `go/validator.go` — batch mode only
**Constraint:** No library changes, single-threaded (to stay comparable with Rust/C++)

## Goal

Reduce unnecessary overhead in batch mode without changing `encoding/json` or the
schema validator library. A second executable using a faster JSON parser will be
explored separately afterward.

## Changes

All changes are localized to `runBatch` in `go/validator.go`.

### 1. Remove redundant `sort.Strings`

`os.ReadDir` already returns entries in lexicographic order. The explicit
`sort.Strings(files)` call is dead code and can be removed.

### 2. Pre-allocate the `files` slice

```go
// Before
var files []string

// After
files := make([]string, 0, len(entries))
```

Avoids repeated reallocations as files are appended.

### 3. Eliminate double syscall per file (`os.Stat` + `os.ReadFile`)

The batch loop currently calls `os.Stat` (to get file size) then `readJSON`
(which calls `os.ReadFile`). That's two syscalls per file. Replace both with a
single `os.ReadFile` and derive the size from `len(data)`.

```go
// Before
info, _ := os.Stat(path)
if info != nil { totalBytes += info.Size() }
instance, err := readJSON(path)

// After
data, err := os.ReadFile(path)
if err != nil { invalidCount++; continue }
totalBytes += int64(len(data))
var instance interface{}
if err := json.Unmarshal(data, &instance); err != nil { invalidCount++; continue }
```

`readJSON` is kept unchanged — it is still used by `runSingle`.

## Expected Impact

Modest gains in batch mode: eliminates N extra syscalls (one per file, ~100 files
in the benchmark). The primary bottleneck remains `encoding/json` reflection-based
parsing, which will be addressed in a follow-up with an alternative JSON library.

## Out of Scope

- Goroutines / parallelism (would be unfair vs single-threaded Rust/C++)
- Single-file mode (dominated by startup + schema compilation, not JSON parsing)
- Library changes (deferred to a separate executable)
