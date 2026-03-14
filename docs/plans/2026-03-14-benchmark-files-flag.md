# Benchmark --files Flag Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add `--files N` option to `benchmark.sh` so the user can control how many test files are used, with automatic regeneration if needed and a confirmation prompt above ~1 GB.

**Architecture:** Three changes to `benchmark/benchmark.sh` only: (1) argument parsing at the top, (2) a 1 GB confirmation guard, (3) replace the hardcoded testdata generation block with a smart regenerate-if-needed block. No validator source code is touched.

**Tech Stack:** bash, `generate_testdata.py --count N` (already supports this flag)

---

### Task 1: Add argument parsing

**Files:**
- Modify: `benchmark/benchmark.sh` — after line 8 (`set -euo pipefail`)

**Step 1: Add the argument parsing block**

Insert this block after line 8 (`set -euo pipefail`), before the `ROOT=` line:

```bash
# ── CLI args ──────────────────────────────────────────────────────────────────

N_REQUESTED=100
while [[ $# -gt 0 ]]; do
    case "$1" in
        --files) N_REQUESTED="$2"; shift 2 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done
```

**Step 2: Verify syntax**

```bash
bash -n /home/lajase/developement/json-validator/benchmark/benchmark.sh
```
Expected: no output.

**Step 3: Smoke test — default**

```bash
bash /home/lajase/developement/json-validator/benchmark/benchmark.sh --files 100 2>&1 | head -5
```
Expected: script starts without "Unknown option" error.

**Step 4: Smoke test — unknown flag**

```bash
bash /home/lajase/developement/json-validator/benchmark/benchmark.sh --foo 2>&1 | head -3
```
Expected: `Unknown option: --foo` on stderr, exit 1.

**Step 5: Commit**

```bash
git add benchmark/benchmark.sh
git commit -m "feat(benchmark): add --files N argument parsing"
```

---

### Task 2: Add 1 GB confirmation guard

**Files:**
- Modify: `benchmark/benchmark.sh` — after `check_deps` (line ~131)

**Step 1: Add the guard block**

Insert this block right after the `check_deps` call and before the `# ── Generate test data` comment:

```bash
# ── Size guard ────────────────────────────────────────────────────────────────

MAX_FILES_NO_CONFIRM=690   # 690 × 1.45 MB ≈ 1 GB
if [[ $N_REQUESTED -gt $MAX_FILES_NO_CONFIRM ]]; then
    size_gb=$(echo "scale=2; $N_REQUESTED * 145 / 100000" | bc)
    echo -e "${YELLOW}⚠  ${N_REQUESTED} files ≈ ${size_gb} GB on disk.${RESET}"
    read -r -p "Continue? [y/N] " confirm
    [[ "$confirm" =~ ^[yY]$ ]] || exit 0
fi
```

**Step 2: Verify syntax**

```bash
bash -n /home/lajase/developement/json-validator/benchmark/benchmark.sh
```
Expected: no output.

**Step 3: Commit**

```bash
git add benchmark/benchmark.sh
git commit -m "feat(benchmark): add 1 GB confirmation guard for --files"
```

---

### Task 3: Replace hardcoded testdata block with smart regeneration

**Files:**
- Modify: `benchmark/benchmark.sh` — lines 133-139 (the testdata generation block)

**Step 1: Replace the existing block**

Find and replace this exact block (lines 133-139):

```bash
# ── Generate test data if missing ─────────────────────────────────────────────

if [[ ! -d "$TESTDATA" ]] || [[ $(ls "$TESTDATA"/*.json 2>/dev/null | wc -l) -lt 100 ]]; then
  echo -e "${CYAN}► Generating test data...${RESET}"
  python3 "$ROOT/benchmark/generate_testdata.py"
  echo
fi
```

Replace with:

```bash
# ── Generate test data if needed ──────────────────────────────────────────────

CURRENT_COUNT=$(ls "$TESTDATA"/*.json 2>/dev/null | wc -l)
if [[ "$CURRENT_COUNT" -ne "$N_REQUESTED" ]]; then
    echo -e "${CYAN}► Generating ${N_REQUESTED} test files...${RESET}"
    python3 "$ROOT/benchmark/generate_testdata.py" --count "$N_REQUESTED"
    echo
fi
```

**Step 2: Verify syntax**

```bash
bash -n /home/lajase/developement/json-validator/benchmark/benchmark.sh
```
Expected: no output.

**Step 3: Verify default behaviour (100 files already present)**

```bash
cd /home/lajase/developement/json-validator
bash benchmark/benchmark.sh 2>&1 | grep -E "Test files|Generating"
```
Expected: `Test files :  100` with no "Generating" line (files already exist).

**Step 4: Verify --files with a different count triggers regeneration**

```bash
bash benchmark/benchmark.sh --files 10 2>&1 | grep -E "Generating|Test files"
```
Expected: `► Generating 10 test files...` then `Test files :  10`.

After this test, restore 100 files:
```bash
bash benchmark/benchmark.sh --files 100 2>&1 | grep -E "Generating|Test files"
```
Expected: `► Generating 100 test files...` then `Test files :  100`.

**Step 5: Commit**

```bash
git add benchmark/benchmark.sh
git commit -m "feat(benchmark): regenerate testdata when --files N differs from current count"
```
