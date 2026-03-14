# Go Fast Variant Design

**Date:** 2026-03-14
**Scope:** New `go/fast/` directory + benchmark integration
**Library:** `goccy/go-json` — drop-in replacement for `encoding/json`, ~2-3× faster, portable (no amd64 constraint)

## Goal

Create a second Go executable using `goccy/go-json` instead of `encoding/json`, mirroring the C++/RapidJSON pattern. Add it to the benchmark to measure the impact of the JSON parser on Go performance.

## Structure

```
go/
├── validator.go          existing — encoding/json
├── go.mod
└── fast/
    ├── validator_fast.go  new — goccy/go-json (copy of validator.go with one import changed)
    └── go.mod             new — module json-validator-go-fast, depends on goccy/go-json
```

Go does not allow two `main` packages in the same directory, hence the `fast/` subdirectory.

## Code Change

`validator_fast.go` is a copy of `validator.go` with a single import change:

```go
// encoding/json → goccy/go-json (drop-in, same API)
import json "github.com/goccy/go-json"
```

All optimizations from the stdlib pass are included:
- No redundant `sort.Strings`
- `make([]string, 0, len(entries))` pre-allocation
- Single `os.ReadFile` in batch loop (no `os.Stat`)

## go.mod

```
module json-validator-go-fast

go 1.21

require github.com/goccy/go-json vX.X.X
require github.com/fatih/color vX.X.X
require github.com/santhosh-tekuri/jsonschema/v5 vX.X.X
```

## Benchmark Integration

In `benchmark/benchmark.sh`:

1. Add binary variable:
```bash
GO_FAST_BIN="$ROOT/go/fast/validator_fast"
```

2. Add build step after Go build:
```bash
echo -e "${CYAN}► Building Go/fast (goccy/go-json)...${RESET}"
SKIP_GO_FAST=1
if (cd "$ROOT/go/fast" && go mod tidy -e 2>/dev/null && go build -o validator_fast .); then
    echo -e "  ${GREEN}✓ ${RESET}$(file "$GO_FAST_BIN" | grep -oP 'ELF.*' | head -c60)"
    SKIP_GO_FAST=0
else
    echo -e "  ${YELLOW}⚠  Go/fast build failed — skipping${RESET}"
fi
```

3. Add `[Go/fast]` entries in Part 1 (per-file) and Part 2 (batch), after the Go entry.

## Out of Scope

- Changing the schema validator library (`santhosh-tekuri/jsonschema/v5` stays)
- Goroutines / parallelism
- Any change to the existing `go/validator.go`
