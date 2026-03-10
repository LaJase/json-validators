# JSON Validator Benchmark

Compare JSON Schema validation across **Rust**, **Go**, **C++** and **Python** on 100 files of ~1.5 MB each.

Each validator exposes the same CLI interface:

```
validator -s SCHEMA (-f FILE | -b DIR) [-v] [-j]
```

| Flag | Description |
|------|-------------|
| `-f FILE` | Validate a single JSON file |
| `-s SCHEMA` | JSON Schema file |
| `-b DIR` | Batch mode — validate all `*.json` in DIR (schema compiled **once**) |
| `-v` | Verbose — show schema path and failing value |
| `-j` | JSON output — machine-readable |

---

## Structure

```
json-validator/
├── rust/                       # Rust  (serde_json + jsonschema crate)
│   ├── src/main.rs
│   └── Cargo.toml
├── go/                         # Go    (encoding/json + santhosh-tekuri/jsonschema)
│   ├── validator.go
│   └── go.mod
├── cpp/                        # C++17 (nlohmann/json + json-schema-validator)
│   ├── validator.cpp
│   └── CMakeLists.txt
├── python/                     # Python (json stdlib + jsonschema)
│   └── validator.py
├── schema/                     # JSON Schema and example files
│   ├── complex_schema.json         # E-commerce schema used for benchmark (Draft 7)
│   ├── simple_schema.json
│   ├── valid_example.json
│   └── invalid_example.json
└── benchmark/                  # Benchmark tooling
    ├── generate_testdata.py        # Generates 100 test files (~1.5 MB each)
    └── benchmark.sh                # Runs per-file + batch comparison
```

---

## Prerequisites

| Language | Required |
|----------|----------|
| Rust | `rustup` / `cargo` ≥ 1.70 |
| Go | Go ≥ 1.21 |
| C++ | `cmake` ≥ 3.14, `make`, GCC/Clang with C++17 support |
| Python | Python ≥ 3.8 + `pip install jsonschema colorama` |

---

## Build

### Rust

```bash
cd rust
cargo build --release
# Binary: rust/target/release/json-validator
```

### Go

```bash
cd go
go mod tidy
go build -o validator .
# Binary: go/validator
```

### C++

```bash
cd cpp
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
# Binary: cpp/build/validator
```

> **clangd / IDE support** — after running cmake, create the symlink for `compile_commands.json`:
> ```bash
> ln -sf build/compile_commands.json cpp/compile_commands.json
> ```

### Python

```bash
pip install jsonschema colorama
# No build step needed
```

---

## Usage

### Single file

```bash
# Rust
./rust/target/release/json-validator -f schema/valid_example.json -s schema/simple_schema.json

# Go
./go/validator -f schema/valid_example.json -s schema/simple_schema.json

# C++
./cpp/build/validator -f schema/valid_example.json -s schema/simple_schema.json

# Python
python3 python/validator.py -f schema/valid_example.json -s schema/simple_schema.json
```

### With verbose error details

```bash
./rust/target/release/json-validator \
  -f schema/invalid_example.json \
  -s schema/simple_schema.json -v
```

### JSON output (for scripting / CI)

```bash
./go/validator -f schema/valid_example.json -s schema/simple_schema.json -j
# → { "valid": true, "errors": [], "elapsed": "4.21ms" }
```

### Batch mode

Schema compiled **once**, then all files validated in a single process:

```bash
./rust/target/release/json-validator -b testdata/ -s schema/complex_schema.json -j
```

---

## Benchmark

### 1. Generate test data

Generates 100 JSON files (~1.45 MB each) in `testdata/`: 80 valid, 20 with deliberate schema violations.

```bash
python3 benchmark/generate_testdata.py
```

| Option | Default | Description |
|--------|---------|-------------|
| `--count N` | 100 | Total number of files |
| `--users N` | 500 | Users per file |
| `--products N` | 200 | Products per file |
| `--orders N` | 1500 | Orders per file |
| `--out DIR` | `testdata` | Output directory |

### 2. Run the benchmark

```bash
bash benchmark/benchmark.sh
```

The script automatically builds all validators, then runs two comparison modes:

| Mode | What it measures |
|------|-----------------|
| **Per-file** | One process per file — real-world CLI usage, includes startup overhead |
| **Batch** | One process for all 100 files — pure parsing + validation speed |

#### Example output

```
  PART 1 — Per-file  (one process per file — includes startup overhead)
  ──────────────────────────────────────────────────────────────────────
  Language    Total (s)   Avg/file   Files/sec     OK  INVALID
  ──────────────────────────────────────────────────────────────────────
  Rust          2.002s     20.0ms      49.9/s      80       20   ★ fastest
  C++           3.150s     31.5ms      31.7/s      80       20   ×1.6 slower
  Go            7.187s     71.8ms      13.9/s      80       20   ×3.6 slower

  PART 2 — Batch  (one process, schema compiled once — pure validation speed)
  ──────────────────────────────────────────────────────────────────────
  Language    Wall (s)   Avg/file   Files/sec   Throughput    OK  INVALID
  ──────────────────────────────────────────────────────────────────────
  Rust          1.375s    13.75ms     72.7/s    105.7 MB/s    80       20   ★ fastest
  C++           2.493s    24.93ms     40.1/s     58.3 MB/s    80       20   ×1.8 slower
  Go            7.929s    79.29ms     12.6/s     18.3 MB/s    80       20   ×5.8 slower
```

### Why these differences?

**Per-file mode** — process startup overhead dominates on short workloads:

| Language | Startup cost |
|----------|-------------|
| Rust | ~1 ms |
| C++ | ~5–10 ms |
| Go | ~10–15 ms (GC runtime init) |
| Python | ~30–50 ms (interpreter) |

**Batch mode** — pure parsing + validation throughput:
- **Rust** wins: `serde_json` is among the fastest JSON parsers available
- **C++** is second: `nlohmann/json` is ergonomic but not maximally fast; replacing it with `rapidjson` would likely surpass Rust
- **Go** is slow here: `encoding/json` is reflection-based, much slower than native parsers

---

## Schema

The benchmark uses a complex e-commerce schema (`schema/complex_schema.json`, JSON Schema Draft 7):

- Nested `$ref` definitions: `User`, `Product`, `Order`, `Address`, `OrderItem`
- Pattern constraints: `^usr_[a-z0-9]{8}$`, email regex, SKU format (`^[A-Z]{2,4}-[0-9]{4,6}-[A-Z0-9]{2,4}$`)
- Enum validation, `minimum`/`maximum`, `uniqueItems`, `minItems`
- `additionalProperties: false` on every object

Each generated file contains ~500 users, ~200 products, ~1500 orders (~1.45 MB compact JSON).

---

## Exit codes

| Code | Meaning |
|------|---------|
| `0` | JSON is valid |
| `1` | JSON is invalid, or an error occurred (bad path, invalid schema…) |

Useful in shell scripts: `./validator -f file.json -s schema.json && echo OK || echo INVALID`
