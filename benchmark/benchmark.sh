#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════
#  benchmark.sh — Compare Rust / Go / Python JSON validators
#  Two modes:
#    • per-file : one process per file (measures startup + validation)
#    • batch    : one process for all files (measures pure validation)
# ══════════════════════════════════════════════════════════════════════════════
set -euo pipefail

# ── CLI args ──────────────────────────────────────────────────────────────────

N_REQUESTED=100
while [[ $# -gt 0 ]]; do
    case "$1" in
        --files) N_REQUESTED="$2"; shift 2 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCHEMA="$ROOT/schema/complex_schema.json"
TESTDATA="$ROOT/testdata"
RUST_BIN="$ROOT/rust/target/release/json-validator"
GO_BIN="$ROOT/go/validator"
GO_FAST_BIN="$ROOT/go/fast/validator_fast"
CPP_BIN="$ROOT/cpp/build/validator"
CPP_RJ_BIN="$ROOT/cpp/build/validator_rj"
NODE_BIN="node $ROOT/nodejs/dist/validator.js"

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
YELLOW='\033[1;33m'
BOLD='\033[1m'
DIM='\033[2m'
RESET='\033[0m'

sep() { echo -e "${DIM}──────────────────────────────────────────────────────────────${RESET}"; }
sep_thick() { echo -e "${BOLD}══════════════════════════════════════════════════════════════${RESET}"; }

# ── Helpers ───────────────────────────────────────────────────────────────────

check_deps() {
  local missing=()
  command -v python3 &>/dev/null || missing+=("python3")
  command -v go &>/dev/null || missing+=("go")
  command -v cargo &>/dev/null || missing+=("cargo (Rust)")
  command -v bc &>/dev/null || missing+=("bc")
  if [[ ${#missing[@]} -gt 0 ]]; then
    echo -e "${RED}Missing dependencies: ${missing[*]}${RESET}" >&2
    exit 1
  fi
}

# Time a command that loops over all files (per-file mode).
# Sets: ELAPSED, OK, FAIL
run_per_file() {
  local cmd="$1" # must contain FILE placeholder
  local files=("$TESTDATA"/*.json)
  local ok=0 fail=0

  local t_start t_end
  t_start=$(date +%s%N)
  for f in "${files[@]}"; do
    if eval "${cmd/FILE/$f}" >/dev/null 2>&1; then
      ok=$((ok + 1))
    else
      fail=$((fail + 1))
    fi
  done
  t_end=$(date +%s%N)

  ELAPSED=$(echo "scale=3; ($t_end - $t_start) / 1000000000" | bc)
  OK=$ok
  FAIL=$fail
}

# Time a single batch command.
# Sets: ELAPSED, BATCH_VALID, BATCH_INVALID, BATCH_FPS, BATCH_MBPS, BATCH_AVG
run_batch() {
  local cmd="$1"
  local t_start t_end

  t_start=$(date +%s%N)
  local output
  output=$(eval "$cmd" 2>/dev/null) || true
  t_end=$(date +%s%N)

  ELAPSED=$(echo "scale=3; ($t_end - $t_start) / 1000000000" | bc)

  # Parse JSON fields from tool output
  BATCH_VALID=$(echo "$output" | grep '"valid"' | head -1 | grep -oP '\d+' | tail -1)
  BATCH_INVALID=$(echo "$output" | grep '"invalid"' | head -1 | grep -oP '\d+' | tail -1)
  BATCH_FPS=$(echo "$output" | grep '"throughput_fps"' | head -1 | grep -oP '[0-9.]+')
  BATCH_MBPS=$(echo "$output" | grep '"throughput_mbps"' | head -1 | grep -oP '[0-9.]+')
  BATCH_AVG=$(echo "$output" | grep '"avg_ms_per_file"' | head -1 | grep -oP '[0-9.]+')
}

print_row() {
  local lbl="$1" t="$2" n="$3" ok="$4" fail="$5" fastest="$6"
  local avg_ms fps ratio badge

  avg_ms=$(echo "scale=1; $t * 1000 / $n" | bc)
  fps=$(echo "scale=1; $n / $t" | bc)
  ratio=$(echo "scale=2; $t / $fastest" | bc)

  if (($(echo "$t == $fastest" | bc -l))); then
    badge="${GREEN}★ fastest${RESET}"
  else
    badge="${DIM}×${ratio} slower${RESET}"
  fi

  printf "  ${CYAN}%-10s${RESET} %10ss  %9sms  %9s/s  %6d  %6d   %b\n" \
    "$lbl" "$t" "$avg_ms" "$fps" "$ok" "$fail" "$badge"
}

print_batch_row() {
  local lbl="$1" wall="$2" self_fps="$3" self_mbps="$4" self_avg="$5" \
    ok="$6" fail="$7" fastest="$8"
  local ratio badge

  ratio=$(echo "scale=2; $wall / $fastest" | bc)
  if (($(echo "$wall == $fastest" | bc -l))); then
    badge="${GREEN}★ fastest${RESET}"
  else
    badge="${DIM}×${ratio} slower${RESET}"
  fi

  printf "  ${CYAN}%-10s${RESET} %10ss  %9sms  %9s/s  %8s MB/s  %6d  %6d   %b\n" \
    "$lbl" "$wall" "$self_avg" "$self_fps" "$self_mbps" "$ok" "$fail" "$badge"
}

# ── Setup ─────────────────────────────────────────────────────────────────────

echo
echo -e "${BOLD}╔══════════════════════════════════════════════════════════════╗${RESET}"
echo -e "${BOLD}║ JSON Validator — Benchmark (Rust/Go/C++/Python/Node.js)      ║${RESET}"
echo -e "${BOLD}╚══════════════════════════════════════════════════════════════╝${RESET}"
echo

check_deps

# ── Size guard ────────────────────────────────────────────────────────────────

MAX_FILES_NO_CONFIRM=690   # 690 × 1.45 MB ≈ 1 GB
if [[ $N_REQUESTED -gt $MAX_FILES_NO_CONFIRM ]]; then
    size_gb=$(echo "scale=2; $N_REQUESTED * 145 / 100000" | bc)
    echo -e "${YELLOW}⚠  ${N_REQUESTED} files ≈ ${size_gb} GB on disk.${RESET}"
    read -r -p "Continue? [y/N] " confirm
    [[ "$confirm" =~ ^[yY]$ ]] || exit 0
fi

# ── Generate test data if needed ──────────────────────────────────────────────

CURRENT_COUNT=$(ls "$TESTDATA"/*.json 2>/dev/null | wc -l)
if [[ "$CURRENT_COUNT" -ne "$N_REQUESTED" ]]; then
    echo -e "${CYAN}► Generating ${N_REQUESTED} test files...${RESET}"
    rm -f "$TESTDATA"/*.json
    python3 "$ROOT/benchmark/generate_testdata.py" --count "$N_REQUESTED"
    echo
fi

N_FILES=$(ls "$TESTDATA"/*.json | wc -l)
TOTAL_MB=$(du -sm "$TESTDATA" | cut -f1)
echo -e "  ${DIM}Test files : ${RESET}${CYAN}${N_FILES}${RESET}"
echo -e "  ${DIM}Total size : ${RESET}${CYAN}${TOTAL_MB} MB${RESET}"
echo -e "  ${DIM}Schema     : ${RESET}${CYAN}$SCHEMA${RESET}"
echo

# ── Build ─────────────────────────────────────────────────────────────────────

sep
echo -e "${CYAN}► Building Rust (release)...${RESET}"
(cd "$ROOT/rust" && cargo build --release -q)
echo -e "  ${GREEN}✓ serde_json \t\t${RESET}$(file "$RUST_BIN" | grep -oP 'ELF.*' | head -c60)"

sep
echo -e "${CYAN}► Building Go...${RESET}"
(
  cd "$ROOT/go" && go mod tidy -e 2>/dev/null
  go build -o validator .
)
echo -e "  ${GREEN}✓ encoding/json \t${RESET}$(file "$GO_BIN" | grep -oP 'ELF.*' | head -c60)"

SKIP_GO_FAST=1
if (cd "$ROOT/go/fast" && go mod tidy -e 2>/dev/null && go build -o validator_fast . 2>/dev/null); then
  echo -e "  ${GREEN}✓ goccy/go-json \t${RESET}$(file "$GO_FAST_BIN" | grep -oP 'ELF.*' | head -c60)"
  SKIP_GO_FAST=0
else
  echo -e "  ${YELLOW}⚠  Go/fast build failed — skipping${RESET}"
fi

sep
echo -e "${CYAN}► Building C++ (Release, -O3 -march=native)...${RESET}"
CPP_BUILD_DIR="$ROOT/cpp/build"
SKIP_CPP=1
SKIP_CPP_RJ=1
if command -v cmake &>/dev/null && command -v make &>/dev/null; then
  mkdir -p "$CPP_BUILD_DIR"
  if (cd "$CPP_BUILD_DIR" && cmake .. -DCMAKE_BUILD_TYPE=Release -Wno-dev -DCMAKE_VERBOSE_MAKEFILE=OFF >/dev/null 2>&1 &&
    make -j"$(nproc)" >/dev/null 2>&1); then
    echo -e "  ${GREEN}✓ nlohmann \t\t${RESET}$(file "$CPP_BIN" | grep -oP 'ELF.*' | head -c60)"
    echo -e "  ${GREEN}✓ rapidjson \t\t${RESET}$(file "$CPP_RJ_BIN" | grep -oP 'ELF.*' | head -c60)"
    SKIP_CPP=0
    SKIP_CPP_RJ=0
  else
    echo -e "  ${YELLOW}⚠  C++ build failed — skipping${RESET}"
  fi
else
  echo -e "  ${YELLOW}⚠  cmake/make not found — skipping C++ (apt install cmake build-essential)${RESET}"
fi

sep
echo -e "${CYAN}► Checking Python dependencies...${RESET}"
SKIP_PYTHON=1
if python3 -c "import jsonschema" 2>/dev/null; then
  echo -e "  ${GREEN}✓ jsonschema available${RESET}"
  SKIP_PYTHON=0
else
  echo -e "  ${YELLOW}⚠  jsonschema not found — skipping Python (pip install jsonschema colorama)${RESET}"
fi

sep
echo -e "${CYAN}► Building Node.js (TypeScript → esbuild bundle)...${RESET}"
SKIP_NODE=1
if command -v node &>/dev/null && command -v npm &>/dev/null; then
  if (cd "$ROOT/nodejs" && npm ci --silent 2>/dev/null && npm run build --silent 2>/dev/null); then
    echo -e "  ${GREEN}✓ bundle ready: nodejs/dist/validator.js${RESET}"
    SKIP_NODE=0
  else
    echo -e "  ${YELLOW}⚠  Node.js build failed — skipping${RESET}"
  fi
else
  echo -e "  ${YELLOW}⚠  node/npm not found — skipping Node.js (install from nodejs.org or nvm install 20)${RESET}"
fi

echo
sep_thick

# ══════════════════════════════════════════════════════════════════════════════
#  PART 1 — Per-file (one process per file)
# ══════════════════════════════════════════════════════════════════════════════

echo
echo -e "${BOLD}  PART 1 — Per-file  ${DIM}(one process per file — includes startup overhead)${RESET}"
sep
printf "  ${BOLD}%-10s %12s %12s %12s %8s %8s${RESET}\n" \
  "Language" "Total (s)" "Avg/file" "Files/sec" "OK" "INVALID"
sep

declare -a PF_LABELS PF_TIMES PF_OK PF_FAIL

echo -ne "  ${CYAN}[Rust]${RESET}\t validating... "
run_per_file "$RUST_BIN -f FILE -s $SCHEMA -j"
echo -e "${GREEN}done${RESET}"
PF_LABELS+=("Rust  ")
PF_TIMES+=("$ELAPSED")
PF_OK+=("$OK")
PF_FAIL+=("$FAIL")

echo -ne "  ${CYAN}[Go]${RESET}\t\t validating... "
run_per_file "$GO_BIN -f FILE -s $SCHEMA -j"
echo -e "${GREEN}done${RESET}"
PF_LABELS+=("Go    ")
PF_TIMES+=("$ELAPSED")
PF_OK+=("$OK")
PF_FAIL+=("$FAIL")

if [[ "$SKIP_GO_FAST" -eq 0 ]]; then
  echo -ne "  ${CYAN}[Go/fast]${RESET}\t validating... "
  run_per_file "$GO_FAST_BIN -f FILE -s $SCHEMA -j"
  echo -e "${GREEN}done${RESET}"
  PF_LABELS+=("Go/fast")
  PF_TIMES+=("$ELAPSED")
  PF_OK+=("$OK")
  PF_FAIL+=("$FAIL")
fi

if [[ "$SKIP_CPP" -eq 0 ]]; then
  echo -ne "  ${CYAN}[C++]${RESET}\t\t validating... "
  run_per_file "$CPP_BIN -f FILE -s $SCHEMA -j"
  echo -e "${GREEN}done${RESET}"
  PF_LABELS+=("C++   ")
  PF_TIMES+=("$ELAPSED")
  PF_OK+=("$OK")
  PF_FAIL+=("$FAIL")
fi

if [[ "$SKIP_CPP_RJ" -eq 0 ]]; then
  echo -ne "  ${CYAN}[C++/RJ]${RESET}\t validating... "
  run_per_file "$CPP_RJ_BIN -f FILE -s $SCHEMA -j"
  echo -e "${GREEN}done${RESET}"
  PF_LABELS+=("C++/RJ")
  PF_TIMES+=("$ELAPSED")
  PF_OK+=("$OK")
  PF_FAIL+=("$FAIL")
fi

if [[ "$SKIP_PYTHON" -eq 0 ]]; then
  echo -ne "  ${CYAN}[Python]${RESET}\t validating... "
  run_per_file "python3 $ROOT/python/validator.py -f FILE -s $SCHEMA -j"
  echo -e "${GREEN}done${RESET}"
  PF_LABELS+=("Python")
  PF_TIMES+=("$ELAPSED")
  PF_OK+=("$OK")
  PF_FAIL+=("$FAIL")
fi

if [[ "$SKIP_NODE" -eq 0 ]]; then
  echo -ne "  ${CYAN}[Node]${RESET}\t validating... "
  run_per_file "$NODE_BIN -f FILE -s $SCHEMA -j"
  echo -e "${GREEN}done${RESET}"
  PF_LABELS+=("Node  ")
  PF_TIMES+=("$ELAPSED")
  PF_OK+=("$OK")
  PF_FAIL+=("$FAIL")
fi

echo
PF_FASTEST="${PF_TIMES[0]}"
for t in "${PF_TIMES[@]}"; do
  (($(echo "$t < $PF_FASTEST" | bc -l))) && PF_FASTEST="$t"
done
for i in "${!PF_LABELS[@]}"; do
  print_row "${PF_LABELS[$i]}" "${PF_TIMES[$i]}" "$N_FILES" "${PF_OK[$i]}" "${PF_FAIL[$i]}" "$PF_FASTEST"
done

sep
echo -e "  ${DIM}Startup overhead included (~1 ms Rust, ~10-15 ms Go, ~30-50 ms Python/Node per process)${RESET}"

echo
sep_thick

# ══════════════════════════════════════════════════════════════════════════════
#  PART 2 — Batch (one process for all files, schema compiled once)
# ══════════════════════════════════════════════════════════════════════════════

echo
echo -e "${BOLD}  PART 2 — Batch  ${DIM}(one process, schema compiled once — pure validation speed)${RESET}"
sep
printf "  ${BOLD}%-10s %12s %12s %12s %14s %6s %8s${RESET}\n" \
  "Language" "Wall (s)" "Avg/file" "Files/sec" "Throughput" "OK" "INVALID"
sep

declare -a BT_LABELS BT_TIMES BT_FPS BT_MBPS BT_AVG BT_OK BT_FAIL

echo -ne "  ${CYAN}[Rust]  ${RESET} batch... "
run_batch "$RUST_BIN -b $TESTDATA -s $SCHEMA -j"
echo -e "${GREEN}done${RESET}"
BT_LABELS+=("Rust  ")
BT_TIMES+=("$ELAPSED")
BT_FPS+=("$BATCH_FPS")
BT_MBPS+=("$BATCH_MBPS")
BT_AVG+=("$BATCH_AVG")
BT_OK+=("$BATCH_VALID")
BT_FAIL+=("$BATCH_INVALID")

echo -ne "  ${CYAN}[Go]    ${RESET} batch... "
run_batch "$GO_BIN -b $TESTDATA -s $SCHEMA -j"
echo -e "${GREEN}done${RESET}"
BT_LABELS+=("Go    ")
BT_TIMES+=("$ELAPSED")
BT_FPS+=("$BATCH_FPS")
BT_MBPS+=("$BATCH_MBPS")
BT_AVG+=("$BATCH_AVG")
BT_OK+=("$BATCH_VALID")
BT_FAIL+=("$BATCH_INVALID")

if [[ "$SKIP_GO_FAST" -eq 0 ]]; then
  echo -ne "  ${CYAN}[Go/fast]${RESET} batch... "
  run_batch "$GO_FAST_BIN -b $TESTDATA -s $SCHEMA -j"
  echo -e "${GREEN}done${RESET}"
  BT_LABELS+=("Go/fast")
  BT_TIMES+=("$ELAPSED")
  BT_FPS+=("$BATCH_FPS")
  BT_MBPS+=("$BATCH_MBPS")
  BT_AVG+=("$BATCH_AVG")
  BT_OK+=("$BATCH_VALID")
  BT_FAIL+=("$BATCH_INVALID")
fi

if [[ "$SKIP_CPP" -eq 0 ]]; then
  echo -ne "  ${CYAN}[C++]   ${RESET} batch... "
  run_batch "$CPP_BIN -b $TESTDATA -s $SCHEMA -j"
  echo -e "${GREEN}done${RESET}"
  BT_LABELS+=("C++   ")
  BT_TIMES+=("$ELAPSED")
  BT_FPS+=("$BATCH_FPS")
  BT_MBPS+=("$BATCH_MBPS")
  BT_AVG+=("$BATCH_AVG")
  BT_OK+=("$BATCH_VALID")
  BT_FAIL+=("$BATCH_INVALID")
fi

if [[ "$SKIP_CPP_RJ" -eq 0 ]]; then
  echo -ne "  ${CYAN}[C++/RJ]${RESET} batch... "
  run_batch "$CPP_RJ_BIN -b $TESTDATA -s $SCHEMA -j"
  echo -e "${GREEN}done${RESET}"
  BT_LABELS+=("C++/RJ")
  BT_TIMES+=("$ELAPSED")
  BT_FPS+=("$BATCH_FPS")
  BT_MBPS+=("$BATCH_MBPS")
  BT_AVG+=("$BATCH_AVG")
  BT_OK+=("$BATCH_VALID")
  BT_FAIL+=("$BATCH_INVALID")
fi

if [[ "$SKIP_PYTHON" -eq 0 ]]; then
  echo -ne "  ${CYAN}[Python]${RESET} batch... "
  run_batch "python3 $ROOT/python/validator.py -b $TESTDATA -s $SCHEMA -j"
  echo -e "${GREEN}done${RESET}"
  BT_LABELS+=("Python")
  BT_TIMES+=("$ELAPSED")
  BT_FPS+=("$BATCH_FPS")
  BT_MBPS+=("$BATCH_MBPS")
  BT_AVG+=("$BATCH_AVG")
  BT_OK+=("$BATCH_VALID")
  BT_FAIL+=("$BATCH_INVALID")
fi

if [[ "$SKIP_NODE" -eq 0 ]]; then
  echo -ne "  ${CYAN}[Node]  ${RESET} batch... "
  run_batch "$NODE_BIN -b $TESTDATA -s $SCHEMA -j"
  echo -e "${GREEN}done${RESET}"
  BT_LABELS+=("Node  ")
  BT_TIMES+=("$ELAPSED")
  BT_FPS+=("$BATCH_FPS")
  BT_MBPS+=("$BATCH_MBPS")
  BT_AVG+=("$BATCH_AVG")
  BT_OK+=("$BATCH_VALID")
  BT_FAIL+=("$BATCH_INVALID")
fi

echo
BT_FASTEST="${BT_TIMES[0]}"
for t in "${BT_TIMES[@]}"; do
  (($(echo "$t < $BT_FASTEST" | bc -l))) && BT_FASTEST="$t"
done
for i in "${!BT_LABELS[@]}"; do
  print_batch_row \
    "${BT_LABELS[$i]}" "${BT_TIMES[$i]}" "${BT_FPS[$i]}" \
    "${BT_MBPS[$i]}" "${BT_AVG[$i]}" "${BT_OK[$i]}" \
    "${BT_FAIL[$i]}" "$BT_FASTEST"
done

sep
echo -e "  ${DIM}Wall time measured externally; avg/fps/MB/s reported by the tool itself.${RESET}"

# ══════════════════════════════════════════════════════════════════════════════
#  Summary
# ══════════════════════════════════════════════════════════════════════════════

echo
sep_thick
echo
echo -e "${BOLD}  Summary${RESET}"
sep
printf "  ${BOLD}%-10s  %18s  %18s  %20s${RESET}\n" \
  "Language" "Per-file total" "Batch total" "Startup overhead est."
sep

for i in "${!PF_LABELS[@]}"; do
  lbl="${PF_LABELS[$i]}"
  pf="${PF_TIMES[$i]}"
  bt="${BT_TIMES[$i]}"
  overhead=$(echo "scale=0; ($pf - $bt) * 1000 / $N_FILES" | bc)
  printf "  ${CYAN}%-10s${RESET}  %16ss  %16ss  %18s ms/process\n" \
    "$lbl" "$pf" "$bt" "$overhead"
done

sep
echo
