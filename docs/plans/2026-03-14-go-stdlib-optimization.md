# Go Stdlib Optimization Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Reduce unnecessary overhead in Go batch mode by eliminating a redundant sort, pre-allocating a slice, and replacing a double syscall (Stat + ReadFile) with a single ReadFile.

**Architecture:** All changes are localized to `runBatch` in `go/validator.go`. `readJSON` stays untouched (still used by single-file mode). No library changes, no goroutines.

**Tech Stack:** Go 1.21, `encoding/json` (stdlib), `santhosh-tekuri/jsonschema/v5`

---

### Task 1: Remove redundant `sort.Strings`

**Files:**
- Modify: `go/validator.go:119`

**Step 1: Locate and delete the sort call**

In `runBatch`, find and remove:
```go
sort.Strings(files)
```

`os.ReadDir` already returns entries in lexicographic order — this line is dead code.

Also remove the `"sort"` import if it's no longer used anywhere else in the file.

**Step 2: Build and verify**

```bash
cd go && go build -o validator .
```
Expected: builds with no errors.

**Step 3: Verify correctness — batch mode still works**

```bash
./validator -b ../testdata -s ../schema/complex_schema.json -j
```
Expected: JSON output with `"valid": 80, "invalid": 20` (or similar counts matching the test data).

**Step 4: Commit**

```bash
git add go/validator.go
git commit -m "perf(go): remove redundant sort.Strings in runBatch (os.ReadDir already sorts)"
```

---

### Task 2: Pre-allocate the `files` slice

**Files:**
- Modify: `go/validator.go` — `runBatch` function, slice declaration

**Step 1: Replace the slice declaration**

Find:
```go
var files []string
```

Replace with:
```go
files := make([]string, 0, len(entries))
```

**Step 2: Build and verify**

```bash
cd go && go build -o validator .
```
Expected: builds with no errors.

**Step 3: Verify correctness**

```bash
./validator -b ../testdata -s ../schema/complex_schema.json -j
```
Expected: same output as before.

**Step 4: Commit**

```bash
git add go/validator.go
git commit -m "perf(go): pre-allocate files slice with capacity in runBatch"
```

---

### Task 3: Eliminate double syscall — replace `os.Stat` + `readJSON` with single `os.ReadFile`

**Files:**
- Modify: `go/validator.go` — batch loop body in `runBatch`

**Step 1: Replace the loop body**

Find this block inside the `for _, path := range files` loop:
```go
info, _ := os.Stat(path)
if info != nil {
    totalBytes += info.Size()
}
instance, err := readJSON(path)
if err != nil {
    invalidCount++
    continue
}
if err := sch.Validate(instance); err != nil {
    invalidCount++
} else {
    validCount++
}
```

Replace with:
```go
data, err := os.ReadFile(path)
if err != nil {
    invalidCount++
    continue
}
totalBytes += int64(len(data))
var instance interface{}
if err := json.Unmarshal(data, &instance); err != nil {
    invalidCount++
    continue
}
if err := sch.Validate(instance); err != nil {
    invalidCount++
} else {
    validCount++
}
```

**Step 2: Check imports**

Make sure `"encoding/json"` is imported (it should already be). If `readJSON` is now only called from `runSingle`, no import changes needed there.

**Step 3: Build**

```bash
cd go && go build -o validator .
```
Expected: builds with no errors.

**Step 4: Verify correctness — single-file mode still works**

```bash
./validator -f ../testdata/valid_001.json -s ../schema/complex_schema.json -j
```
Expected: `"valid": true`

```bash
./validator -f ../testdata/invalid_001.json -s ../schema/complex_schema.json -j
```
Expected: `"valid": false` with errors.

**Step 5: Verify batch mode**

```bash
./validator -b ../testdata -s ../schema/complex_schema.json -j
```
Expected: same valid/invalid counts as before.

**Step 6: Commit**

```bash
git add go/validator.go
git commit -m "perf(go): replace os.Stat+ReadFile with single ReadFile in batch loop"
```

---

### Task 4: Verify final build and benchmark sanity check

**Step 1: Final build**

```bash
cd go && go build -o validator .
```

**Step 2: Quick benchmark run**

```bash
./validator -b ../testdata -s ../schema/complex_schema.json -j
```

Note the `throughput_fps` and `throughput_mbps` values — they should be equal to or better than the baseline (Go: ~20.9 MB/s).

**Step 3: Final commit if needed**

If everything looks clean, no additional commit needed. The three previous commits cover all changes.
