# Benchmark File Count Design

**Date:** 2026-03-14
**Scope:** `benchmark/benchmark.sh` only

## Goal

Allow passing `--files N` to the benchmark script to control how many test files are used. Default stays 100. If N differs from the current testdata count, files are regenerated. If N would exceed ~1 GB on disk (>690 files), a confirmation prompt is shown.

## Usage

```bash
bash benchmark.sh              # uses 100 files (default)
bash benchmark.sh --files 50   # uses 50 files
bash benchmark.sh --files 500  # prompts confirmation (~725 MB)
bash benchmark.sh --files 700  # prompts confirmation (~1.015 GB)
```

## Changes (benchmark.sh only)

### 1. Argument parsing — top of script, before check_deps

```bash
N_REQUESTED=100
while [[ $# -gt 0 ]]; do
    case "$1" in
        --files) N_REQUESTED="$2"; shift 2 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done
```

### 2. Confirmation prompt — after arg parsing, before builds

Threshold: 690 files ≈ 1 GB (690 × 1.45 MB).

```bash
MAX_FILES_NO_CONFIRM=690
if [[ $N_REQUESTED -gt $MAX_FILES_NO_CONFIRM ]]; then
    size_gb=$(echo "scale=2; $N_REQUESTED * 145 / 100000" | bc)
    echo -e "${YELLOW}⚠  $N_REQUESTED files ≈ ${size_gb} GB on disk.${RESET}"
    read -r -p "Continue? [y/N] " confirm
    [[ "$confirm" =~ ^[yY]$ ]] || exit 0
fi
```

### 3. Testdata generation — replace the existing block

```bash
CURRENT_COUNT=$(ls "$TESTDATA"/*.json 2>/dev/null | wc -l)
if [[ "$CURRENT_COUNT" -ne "$N_REQUESTED" ]]; then
    echo -e "${CYAN}► Generating $N_REQUESTED test files...${RESET}"
    python3 "$ROOT/benchmark/generate_testdata.py" --count "$N_REQUESTED"
    echo
fi
```

### 4. N_FILES — unchanged

`N_FILES=$(ls "$TESTDATA"/*.json | wc -l)` is already set after the testdata block and used throughout — no change needed.

## Out of Scope

- Changing any validator source code
- Passing subsets of files without regenerating
- Any other benchmark.sh flags
