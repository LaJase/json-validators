# JSON Schema Validator — Benchmark

> **Rust · Go · C++ · Python** — four implementations of the same JSON Schema validator,
> benchmarked on **100 files × 1.45 MB** with a complex e-commerce schema (Draft 7).

![Rust](https://img.shields.io/badge/Rust-1.70+-orange?logo=rust)
![Go](https://img.shields.io/badge/Go-1.21+-00ADD8?logo=go)
![C++](https://img.shields.io/badge/C++-17-00599C?logo=cplusplus)
![Python](https://img.shields.io/badge/Python-3.8+-3776AB?logo=python)

---

## Results

> Machine: Linux x86-64 — 100 files · 1.45 MB each · 145 MB total

### Part 1 — Per-file &nbsp;`(one process per file)`

Each invocation starts a new process, reads the schema, compiles it, validates one file, exits.
This is the real-world CLI experience.

```
  files/sec  ┊
             ┊
  44.6 ──── ┤ Rust   ████████████████████████████████████████  44.6/s  22ms   2.2s  ★
  38.0 ──── ┤ C++    ██████████████████████████████████        38.0/s  26ms   2.6s
  12.5 ──── ┤ Go     ███████████                               12.5/s  80ms   8.0s
   2.6 ──── ┤ Python ██                                         2.6/s 374ms  37.4s
             ┊
```

| Language | Total    | Avg/file | Files/sec | vs Rust       |
|----------|----------|----------|-----------|---------------|
| **Rust** | 2.239 s  | 22.3 ms  | 44.6 /s   | ★ baseline    |
| **C++**  | 2.631 s  | 26.3 ms  | 38.0 /s   | ×1.17 slower  |
| **Go**   | 7.954 s  | 79.5 ms  | 12.5 /s   | ×3.55 slower  |
| **Python**| 37.432 s | 374.3 ms | 2.6 /s   | ×16.71 slower |

---

### Part 2 — Batch &nbsp;`(one process, schema compiled once)`

The schema is compiled a single time, then all 100 files are validated in the same process.
This isolates pure parsing + validation throughput.

```
  MB/s       ┊
             ┊
  112.6 ─── ┤ Rust   ████████████████████████████████████████  112.6 MB/s  77.5/s  ★
   60.8 ─── ┤ C++    █████████████████████                      60.8 MB/s  41.8/s
   20.9 ─── ┤ Go     ███████                                    20.9 MB/s  14.4/s
    4.8 ─── ┤ Python ██                                          4.8 MB/s   3.3/s
             ┊
```

| Language | Wall time | Avg/file  | Files/sec | Throughput  | vs Rust       |
|----------|-----------|-----------|-----------|-------------|---------------|
| **Rust** | 1.298 s   | 12.91 ms  | 77.5 /s   | 112.6 MB/s  | ★ baseline    |
| **C++**  | 2.399 s   | 23.92 ms  | 41.8 /s   |  60.8 MB/s  | ×1.84 slower  |
| **Go**   | 6.955 s   | 69.47 ms  | 14.4 /s   |  20.9 MB/s  | ×5.35 slower  |
| **Python**| 30.421 s | 303.42 ms |  3.3 /s   |   4.8 MB/s  | ×23.43 slower |

---

### Startup overhead

The difference between per-file and batch time, divided across 100 processes:

| Language | Overhead/process | What's included |
|----------|-----------------|-----------------|
| **C++**  | ~2 ms           | OS loader, minimal runtime |
| **Rust** | ~9 ms           | Runtime + schema read & compile |
| **Go**   | ~9 ms           | GC runtime init + schema read & compile |
| **Python**| ~70 ms         | Interpreter boot + import chain + schema compile |

> **C++ starts faster than Rust and Go** because `nlohmann-json-schema-validator`
> compiles the schema in ~2 ms, while both `jsonschema` (Rust) and `santhosh-tekuri/jsonschema` (Go)
> take ~8–9 ms for schema compilation — dominating their per-file overhead.

---

### Why is Rust faster in batch mode?

| Factor | Rust | C++ | Go | Python |
|--------|------|-----|----|--------|
| JSON parser | `serde_json` (zero-copy) | `nlohmann/json` (DOM, copies) | `encoding/json` (reflection) | C extension |
| Schema compile | once | once | once | once |
| Memory model | no GC | no GC | GC pauses | GC |
| Regex | compiled at schema load | compiled at schema load | compiled at schema load | Python re |

Replacing `nlohmann/json` with `rapidjson` in the C++ implementation
would likely push C++ ahead of Rust for raw throughput.

---

## Project structure

```
json-validator/
├── rust/                       Rust  — serde_json + jsonschema crate + clap
│   ├── src/main.rs
│   └── Cargo.toml
├── go/                         Go    — encoding/json + santhosh-tekuri/jsonschema/v5
│   ├── validator.go
│   └── go.mod
├── cpp/                        C++17 — nlohmann/json + nlohmann/json-schema-validator
│   ├── validator.cpp
│   └── CMakeLists.txt          (FetchContent, no manual dependency install needed)
├── python/                     Python — json stdlib + jsonschema
│   └── validator.py
├── schema/
│   ├── complex_schema.json     E-commerce Draft 7 schema (used for benchmark)
│   ├── simple_schema.json
│   ├── valid_example.json
│   └── invalid_example.json
└── benchmark/
    ├── generate_testdata.py    Generates 100 test files
    └── benchmark.sh            Runs the full comparison
```

---

## Prerequisites

| Language | Install |
|----------|---------|
| Rust | [rustup.rs](https://rustup.rs) |
| Go | [go.dev/dl](https://go.dev/dl) ≥ 1.21 |
| C++ | `apt install cmake build-essential` |
| Python | `pip install jsonschema colorama` |

---

## Build

### Rust
```bash
cd rust && cargo build --release
# → rust/target/release/json-validator
```

### Go
```bash
cd go && go mod tidy && go build -o validator .
# → go/validator
```

### C++
```bash
cd cpp && mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)
# → cpp/build/validator
```

> **clangd support** — after cmake, symlink `compile_commands.json`:
> ```bash
> ln -sf build/compile_commands.json cpp/compile_commands.json
> ```

### Python

Using a virtual environment is recommended to avoid polluting your system packages.

```bash
# Create and activate the venv
python3 -m venv .venv
source .venv/bin/activate      # Linux / macOS
# .venv\Scripts\activate       # Windows

# Install dependencies
pip install jsonschema colorama

# Deactivate when done
deactivate
```

> The `.venv/` directory is already in `.gitignore`.
>
> To use the validator without activating the venv each time:
> ```bash
> .venv/bin/python3 python/validator.py -f schema/valid_example.json -s schema/simple_schema.json
> ```

---

## Usage

All four validators share the same interface:

```
validator -s SCHEMA (-f FILE | -b DIR) [-v] [-j]

  -f FILE      Single-file mode
  -s SCHEMA    JSON Schema file
  -b DIR       Batch mode (schema compiled once for all *.json in DIR)
  -v           Verbose errors (schema path + failing value)
  -j           JSON output (for scripting / CI pipelines)
```

### Single file

```bash
./rust/target/release/json-validator \
  -f schema/valid_example.json \
  -s schema/simple_schema.json
```

```
 JSON Validator
───────────────────────────────────────────────────────
  File  :  schema/valid_example.json
  Schema:  schema/simple_schema.json
───────────────────────────────────────────────────────
  ✓ JSON is valid! Everything looks good.
  Time  :  3.21ms
───────────────────────────────────────────────────────
```

### Invalid file with `-v`

```bash
./go/validator \
  -f schema/invalid_example.json \
  -s schema/simple_schema.json -v
```

```
 JSON Validator
───────────────────────────────────────────────────────
  ✗ 2 error(s) found:

  [1] 12 is less than minimum of 18
        at path  : /age
        schema   : /properties/age/minimum

  [2] "superuser" is not valid enum value
        at path  : /roles/0
───────────────────────────────────────────────────────
```

### JSON output (CI / scripting)

```bash
./cpp/build/validator \
  -f schema/valid_example.json \
  -s schema/simple_schema.json -j
```

```json
{
  "valid": true,
  "errors": [],
  "elapsed": "3.12ms"
}
```

### Batch mode

```bash
./rust/target/release/json-validator \
  -b testdata/ \
  -s schema/complex_schema.json -j
```

```json
{
  "mode": "batch",
  "total": 100,
  "valid": 80,
  "invalid": 20,
  "elapsed": "1.298s",
  "avg_ms_per_file": "12.91",
  "throughput_fps": "77.5",
  "throughput_mbps": "112.6"
}
```

---

## Running the benchmark

### 1. Generate test data

```bash
python3 benchmark/generate_testdata.py
```

Produces 100 files in `testdata/` — 80 valid, 20 with deliberate schema violations
(wrong enum values, missing required fields, out-of-range numbers, etc.).

| Option | Default | Description |
|--------|---------|-------------|
| `--count N` | 100 | Number of files |
| `--users N` | 500 | Users per file |
| `--products N` | 200 | Products per file |
| `--orders N` | 1500 | Orders per file |
| `--out DIR` | `testdata` | Output directory |

### 2. Run

```bash
bash benchmark/benchmark.sh
```

The script builds all validators automatically, then runs both **per-file** and **batch** modes
and prints the comparison table with speedup ratios.

---

## Schema

`schema/complex_schema.json` — JSON Schema Draft 7, e-commerce dataset:

```
EcommerceDataset
├── metadata      { version, generated_at, environment, counts… }
├── users[]       { id (^usr_[a-z0-9]{8}$), email, age ≥ 18, address, roles (enum),
│                   preferences, status (enum), created_at }
│   └── address   { street, city, state, country (^[A-Z]{2}$), postal_code }
├── products[]    { id (^prod_[a-z0-9]{8}$), sku (^[A-Z]{2,4}-[0-9]{4,6}-…$),
│                   price ≥ 0.01, category (enum), tags (unique), stock ≥ 0, status }
└── orders[]      { id (^ord_[a-z0-9]{12}$), user_id, items[] (minItems: 1),
                    subtotal, tax, total ≥ 0.01, status (enum), payment_method (enum) }
```

All objects use `additionalProperties: false`.
Each generated file: ~500 users · ~200 products · ~1500 orders · **~1.45 MB**.

---

## Exit codes

| Code | Meaning |
|------|---------|
| `0` | JSON is valid |
| `1` | JSON invalid, or error (bad path, invalid schema…) |

```bash
./rust/target/release/json-validator -f file.json -s schema.json && echo OK || echo INVALID
```
