# Go Fast Variant (goccy/go-json) Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Create `go/fast/validator_fast` — a second Go binary using `goccy/go-json` instead of `encoding/json`, and integrate it into the benchmark.

**Architecture:** New `go/fast/` subdirectory with its own `go.mod` (Go requires separate directories for separate `main` packages). `validator_fast.go` is a copy of the optimized `go/validator.go` with a single import aliased to `goccy/go-json`. The benchmark script gets a new build step and two new entries (per-file + batch).

**Tech Stack:** Go 1.21, `goccy/go-json`, `santhosh-tekuri/jsonschema/v5`, `fatih/color`

---

### Task 1: Create `go/fast/go.mod`

**Files:**
- Create: `go/fast/go.mod`

**Step 1: Create the directory and go.mod**

```bash
mkdir -p /home/lajase/developement/json-validator/go/fast
```

Create `go/fast/go.mod` with this exact content:

```
module json-validator-go-fast

go 1.21

require (
	github.com/fatih/color v1.16.0
	github.com/goccy/go-json v0.10.3
	github.com/santhosh-tekuri/jsonschema/v5 v5.3.1
)
```

Note: indirect deps (`mattn/go-colorable`, etc.) will be added automatically by `go mod tidy` in the next task.

**Step 2: Commit**

```bash
git add go/fast/go.mod
git commit -m "feat(go/fast): add go.mod for goccy/go-json variant"
```

---

### Task 2: Create `go/fast/validator_fast.go`

**Files:**
- Create: `go/fast/validator_fast.go`

**Step 1: Create the file**

`validator_fast.go` is identical to `go/validator.go` with two changes:
1. `"encoding/json"` replaced by `json "github.com/goccy/go-json"` (aliased so all `json.X` calls work unchanged)
2. Package is still `main`

Create `go/fast/validator_fast.go` with this exact content:

```go
package main

import (
	"flag"
	"fmt"
	json "github.com/goccy/go-json"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/fatih/color"
	"github.com/santhosh-tekuri/jsonschema/v5"
)

const separator = "───────────────────────────────────────────────────────"

var (
	boldGreen  = color.New(color.FgGreen, color.Bold).SprintFunc()
	boldRed    = color.New(color.FgRed, color.Bold).SprintFunc()
	boldYellow = color.New(color.FgYellow, color.Bold).SprintFunc()
	cyan       = color.New(color.FgCyan).SprintFunc()
	dim        = color.New(color.Faint).SprintFunc()
	headerFmt  = color.New(color.FgWhite, color.Bold, color.BgBlue).SprintFunc()
)

type errorEntry struct {
	Path       string `json:"path"`
	Message    string `json:"message"`
	SchemaPath string `json:"schema_path"`
}

func formatDuration(d time.Duration) string {
	micros := d.Microseconds()
	switch {
	case micros < 1_000:
		return fmt.Sprintf("%dµs", micros)
	case micros < 1_000_000:
		return fmt.Sprintf("%.2fms", float64(d.Nanoseconds())/1e6)
	default:
		return fmt.Sprintf("%.3fs", d.Seconds())
	}
}

func readJSON(path string) (interface{}, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("cannot read '%s': %w", path, err)
	}
	var v interface{}
	if err := json.Unmarshal(data, &v); err != nil {
		return nil, fmt.Errorf("invalid JSON in '%s': %w", path, err)
	}
	return v, nil
}

func collectErrors(ve *jsonschema.ValidationError) []errorEntry {
	if len(ve.Causes) == 0 {
		return []errorEntry{{
			Path:       ve.InstanceLocation,
			Message:    ve.Message,
			SchemaPath: ve.KeywordLocation,
		}}
	}
	var entries []errorEntry
	for _, cause := range ve.Causes {
		entries = append(entries, collectErrors(cause)...)
	}
	return entries
}

func fatal(msg string, jsonOutput bool) {
	if jsonOutput {
		out, _ := json.MarshalIndent(map[string]interface{}{"valid": false, "error": msg}, "", "  ")
		fmt.Println(string(out))
	} else {
		fmt.Printf("  %s %s\n", boldRed("✗"), boldRed(msg))
		fmt.Println(dim(separator))
		fmt.Println()
	}
	os.Exit(1)
}

func compileSchema(schemaPath string, jsonOutput bool) *jsonschema.Schema {
	compiler := jsonschema.NewCompiler()
	compiler.Draft = jsonschema.Draft7

	schemaURL := schemaPath
	if !strings.HasPrefix(schemaPath, "/") {
		cwd, _ := os.Getwd()
		schemaURL = cwd + "/" + schemaPath
	}
	schemaURL = "file://" + schemaURL

	sch, err := compiler.Compile(schemaURL)
	if err != nil {
		fatal(fmt.Sprintf("schema compilation failed: %v", err), jsonOutput)
	}
	return sch
}

// ── Batch mode ────────────────────────────────────────────────────────────────

func runBatch(batchDir, schemaPath string, jsonOutput bool) {
	start := time.Now()

	entries, err := os.ReadDir(batchDir)
	if err != nil {
		fatal(fmt.Sprintf("cannot read directory '%s': %v", batchDir, err), jsonOutput)
	}
	files := make([]string, 0, len(entries))
	for _, e := range entries {
		if !e.IsDir() && strings.HasSuffix(e.Name(), ".json") {
			files = append(files, filepath.Join(batchDir, e.Name()))
		}
	}

	if len(files) == 0 {
		fatal(fmt.Sprintf("no .json files found in '%s'", batchDir), jsonOutput)
	}

	if !jsonOutput {
		fmt.Println()
		fmt.Println(headerFmt(" JSON Validator — Batch Mode "))
		fmt.Println(dim(separator))
		fmt.Printf("  %s  %s\n", dim("Schema :"), cyan(schemaPath))
		fmt.Printf("  %s  %s\n", dim("Dir    :"), cyan(batchDir))
		fmt.Printf("  %s  %s\n", dim("Files  :"), cyan(fmt.Sprintf("%d", len(files))))
		fmt.Println(dim(separator))
	}

	sch := compileSchema(schemaPath, jsonOutput)

	var validCount, invalidCount int
	var totalBytes int64

	for _, path := range files {
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
	}

	elapsed := time.Since(start)
	total := len(files)
	secs := elapsed.Seconds()
	avgMs := secs * 1000 / float64(total)
	fps := float64(total) / secs
	mbps := (float64(totalBytes) / 1_000_000) / secs

	if jsonOutput {
		out, _ := json.MarshalIndent(map[string]interface{}{
			"mode":            "batch",
			"total":           total,
			"valid":           validCount,
			"invalid":         invalidCount,
			"total_bytes":     totalBytes,
			"elapsed":         formatDuration(elapsed),
			"avg_ms_per_file": fmt.Sprintf("%.2f", avgMs),
			"throughput_fps":  fmt.Sprintf("%.1f", fps),
			"throughput_mbps": fmt.Sprintf("%.1f", mbps),
		}, "", "  ")
		fmt.Println(string(out))
	} else {
		fmt.Printf("  %s %s   %s %s\n",
			boldGreen("✓"), boldGreen(fmt.Sprintf("%d valid", validCount)),
			boldRed("✗"), boldRed(fmt.Sprintf("%d invalid", invalidCount)),
		)
		fmt.Printf("  %s  %s\n", dim("Time   :"), cyan(formatDuration(elapsed)))
		fmt.Printf("  %s  %s\n", dim("Avg    :"), cyan(fmt.Sprintf("%.2f ms/file", avgMs)))
		fmt.Printf("  %s  %s  |  %s\n",
			dim("Speed  :"),
			cyan(fmt.Sprintf("%.1f files/s", fps)),
			cyan(fmt.Sprintf("%.1f MB/s", mbps)),
		)
		fmt.Println(dim(separator))
		fmt.Println()
	}
}

// ── Single-file mode ──────────────────────────────────────────────────────────

func runSingle(filePath, schemaPath string, verbose, jsonOutput bool) {
	start := time.Now()

	if !jsonOutput {
		fmt.Println()
		fmt.Println(headerFmt(" JSON Validator "))
		fmt.Println(dim(separator))
		fmt.Printf("  %s  %s\n", dim("File  :"), cyan(filePath))
		fmt.Printf("  %s  %s\n", dim("Schema:"), cyan(schemaPath))
		fmt.Println(dim(separator))
	}

	instance, err := readJSON(filePath)
	if err != nil {
		fatal(err.Error(), jsonOutput)
	}

	sch := compileSchema(schemaPath, jsonOutput)

	validationErr := sch.Validate(instance)
	elapsed := time.Since(start)

	if validationErr == nil {
		if jsonOutput {
			out, _ := json.MarshalIndent(map[string]interface{}{
				"valid": true, "errors": []interface{}{}, "elapsed": formatDuration(elapsed),
			}, "", "  ")
			fmt.Println(string(out))
		} else {
			fmt.Printf("  %s %s\n", boldGreen("✓"), boldGreen("JSON is valid! Everything looks good."))
			fmt.Printf("  %s  %s\n", dim("Time  :"), cyan(formatDuration(elapsed)))
			fmt.Println(dim(separator))
			fmt.Println()
		}
		return
	}

	ve, ok := validationErr.(*jsonschema.ValidationError)
	var entries []errorEntry
	if ok {
		entries = collectErrors(ve)
	} else {
		entries = []errorEntry{{Message: validationErr.Error()}}
	}

	if jsonOutput {
		out, _ := json.MarshalIndent(map[string]interface{}{
			"valid":       false,
			"error_count": len(entries),
			"errors":      entries,
			"elapsed":     formatDuration(elapsed),
		}, "", "  ")
		fmt.Println(string(out))
	} else {
		fmt.Printf("  %s %s error(s) found:\n\n", boldRed("✗"), boldRed(fmt.Sprintf("%d", len(entries))))
		for i, e := range entries {
			fmt.Printf("  %s %s\n", boldYellow(fmt.Sprintf("[%d]", i+1)), boldRed(e.Message))
			if e.Path != "" {
				fmt.Printf("      %s %s\n", dim("at path  :"), cyan(e.Path))
			}
			if verbose && e.SchemaPath != "" {
				fmt.Printf("      %s %s\n", dim("schema   :"), dim(e.SchemaPath))
			}
			fmt.Println()
		}
		fmt.Printf("  %s  %s\n", dim("Time  :"), cyan(formatDuration(elapsed)))
		fmt.Println(dim(separator))
		fmt.Println()
	}
	os.Exit(1)
}

func main() {
	var (
		filePath   string
		schemaPath string
		batchDir   string
		verbose    bool
		jsonOutput bool
	)

	flag.StringVar(&filePath, "f", "", "Path to the JSON file to validate (single-file mode)")
	flag.StringVar(&filePath, "file", "", "Path to the JSON file to validate (single-file mode)")
	flag.StringVar(&schemaPath, "s", "", "Path to the JSON Schema file")
	flag.StringVar(&schemaPath, "schema", "", "Path to the JSON Schema file")
	flag.StringVar(&batchDir, "b", "", "Validate all *.json in DIR (batch mode, schema compiled once)")
	flag.StringVar(&batchDir, "batch", "", "Validate all *.json in DIR (batch mode, schema compiled once)")
	flag.BoolVar(&verbose, "v", false, "Show detailed errors")
	flag.BoolVar(&verbose, "verbose", false, "Show detailed errors")
	flag.BoolVar(&jsonOutput, "j", false, "Output in JSON format")
	flag.BoolVar(&jsonOutput, "json-output", false, "Output in JSON format")

	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage: validator -s SCHEMA (-f FILE | -b DIR) [-v] [-j]\n\n")
		flag.PrintDefaults()
	}
	flag.Parse()

	if schemaPath == "" {
		flag.Usage()
		os.Exit(1)
	}

	switch {
	case batchDir != "":
		runBatch(batchDir, schemaPath, jsonOutput)
	case filePath != "":
		runSingle(filePath, schemaPath, verbose, jsonOutput)
	default:
		flag.Usage()
		os.Exit(1)
	}
}
```

**Step 2: Run go mod tidy to resolve indirect deps**

```bash
cd /home/lajase/developement/json-validator/go/fast && go mod tidy
```

Expected: downloads `goccy/go-json` and adds indirect deps to `go.mod` + creates `go.sum`.

**Step 3: Build**

```bash
go build -o validator_fast .
```

Expected: binary `go/fast/validator_fast` created, no errors.

**Step 4: Verify single-file mode**

```bash
./validator_fast -f ../../testdata/valid_001.json -s ../../schema/complex_schema.json -j
```
Expected: `"valid": true`

```bash
./validator_fast -f ../../testdata/invalid_001.json -s ../../schema/complex_schema.json -j
```
Expected: `"valid": false`

**Step 5: Verify batch mode**

```bash
./validator_fast -b ../../testdata -s ../../schema/complex_schema.json -j
```
Expected: `"valid": 80, "invalid": 20, "total": 100`

**Step 6: Commit**

```bash
git add go/fast/
git commit -m "feat(go/fast): add goccy/go-json variant validator"
```

---

### Task 3: Integrate into benchmark.sh

**Files:**
- Modify: `benchmark/benchmark.sh`

**Step 1: Add the binary variable** after `GO_BIN` (line ~15):

```bash
GO_FAST_BIN="$ROOT/go/fast/validator_fast"
```

**Step 2: Add the build step** after the Go build block (after line ~150):

```bash
sep
echo -e "${CYAN}► Building Go/fast (goccy/go-json)...${RESET}"
SKIP_GO_FAST=1
if (cd "$ROOT/go/fast" && go mod tidy -e 2>/dev/null && go build -o validator_fast . 2>/dev/null); then
    echo -e "  ${GREEN}✓ ${RESET}$(file "$GO_FAST_BIN" | grep -oP 'ELF.*' | head -c60)"
    SKIP_GO_FAST=0
else
    echo -e "  ${YELLOW}⚠  Go/fast build failed — skipping${RESET}"
fi
```

**Step 3: Add per-file entry** after the `[Go]` block in PART 1:

```bash
if [[ "$SKIP_GO_FAST" -eq 0 ]]; then
    echo -ne "  ${CYAN}[Go/fast]${RESET} validating... "
    run_per_file "$GO_FAST_BIN -f FILE -s $SCHEMA -j"
    echo -e "${GREEN}done${RESET}"
    PF_LABELS+=("Go/fast"); PF_TIMES+=("$ELAPSED"); PF_OK+=("$OK"); PF_FAIL+=("$FAIL")
fi
```

**Step 4: Add batch entry** after the `[Go]` block in PART 2:

```bash
if [[ "$SKIP_GO_FAST" -eq 0 ]]; then
    echo -ne "  ${CYAN}[Go/fast]${RESET} batch... "
    run_batch "$GO_FAST_BIN -b $TESTDATA -s $SCHEMA -j"
    echo -e "${GREEN}done${RESET}"
    BT_LABELS+=("Go/fast"); BT_TIMES+=("$ELAPSED")
    BT_FPS+=("$BATCH_FPS"); BT_MBPS+=("$BATCH_MBPS"); BT_AVG+=("$BATCH_AVG")
    BT_OK+=("$BATCH_VALID"); BT_FAIL+=("$BATCH_INVALID")
fi
```

**Step 5: Verify the script is syntactically valid**

```bash
bash -n /home/lajase/developement/json-validator/benchmark/benchmark.sh
```
Expected: no output (no syntax errors).

**Step 6: Quick smoke test of the benchmark build section only**

```bash
cd /home/lajase/developement/json-validator && bash benchmark/benchmark.sh 2>&1 | head -40
```
Expected: build steps complete without errors, `[Go/fast]` appears.

**Step 7: Commit**

```bash
git add benchmark/benchmark.sh
git commit -m "feat: add Go/fast (goccy/go-json) to benchmark"
```
