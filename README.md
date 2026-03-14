# JSON Schema Validator — Benchmark

> **Rust · Go · C++ · Python · Node.js** — five implementations of the same JSON Schema validator,
> benchmarked on **100 files × 1.45 MB** with a complex e-commerce schema (Draft 7).

![Rust](https://img.shields.io/badge/Rust-1.70+-orange?logo=rust)
![Go](https://img.shields.io/badge/Go-1.21+-00ADD8?logo=go)
![C++](https://img.shields.io/badge/C++-17-00599C?logo=cplusplus)
![Python](https://img.shields.io/badge/Python-3.8+-3776AB?logo=python)
![Node.js](https://img.shields.io/badge/Node.js-20+-339933?logo=node.js)

---

## Results

> Machine: Linux x86-64 — 100 files · 1.45 MB each · 145 MB total

### Part 1 — Per-file &nbsp;`(one process per file)`

Each invocation starts a new process, reads the schema, compiles it, validates one file, exits.
This is the real-world CLI experience.

```
  files/sec  ┊
             ┊
  56.7 ──── ┤ C++/RJ ████████████████████████████████████████  56.7/s  18ms   1.8s  ★
  46.4 ──── ┤ Rust   █████████████████████████████████         46.4/s  22ms   2.2s
  36.9 ──── ┤ C++    ██████████████████████████                36.9/s  27ms   2.7s
  12.3 ──── ┤ Go     █████████                                 12.3/s  81ms   8.1s
   8.0 ──── ┤ Node   ██████                                     8.0/s 124ms  12.4s
   2.9 ──── ┤ Python ██                                         2.9/s 344ms  34.4s
             ┊
```

| Language            | Total    | Avg/file | Files/sec | vs C++/RJ     |
| ------------------- | -------- | -------- | --------- | ------------- |
| **C++ (RapidJSON)** | 1.762 s  | 17.6 ms  | 56.7 /s   | ★ baseline    |
| **Rust**            | 2.154 s  | 21.5 ms  | 46.4 /s   | ×1.22 slower  |
| **C++**             | 2.709 s  | 27.0 ms  | 36.9 /s   | ×1.53 slower  |
| **Go**              | 8.110 s  | 81.1 ms  | 12.3 /s   | ×4.60 slower  |
| **Node.js**         | 12.407 s | 124.0 ms | 8.0 /s    | ×7.04 slower  |
| **Python**          | 34.389 s | 343.8 ms | 2.9 /s    | ×19.51 slower |

---

### Part 2 — Batch &nbsp;`(one process, schema compiled once)`

The schema is compiled a single time, then all 100 files are validated in the same process.
This isolates pure parsing + validation throughput.

```
  MB/s      ┊
            ┊
  224.8 ─── ┤ Node     ████████████████████████████████████████  224.8 MB/s  154.7/s  ★
  126.3 ─── ┤ Rust     ██████████████████████                    126.3 MB/s   86.9/s
  110.7 ─── ┤ C++ (RJ) ████████████████████                    110.7 MB/s   76.2/s
   69.9 ─── ┤ C++      ████████████                               69.9 MB/s   48.1/s
   22.2 ─── ┤ Go       ████                                       22.2 MB/s   15.3/s
    5.2 ─── ┤ Python   █                                           5.2 MB/s    3.6/s
            ┊
```

| Language            | Wall time | Avg/file  | Files/sec | Throughput | vs Node.js    |
| ------------------- | --------- | --------- | --------- | ---------- | ------------- |
| **Node.js**         | 0.726 s   | 6.47 ms   | 154.7 /s  | 224.8 MB/s | ★ baseline    |
| **Rust**            | 1.156 s   | 11.51 ms  | 86.9 /s   | 126.3 MB/s | ×1.59 slower  |
| **C++ (RapidJSON)** | 1.317 s   | 13.12 ms  | 76.2 /s   | 110.7 MB/s | ×1.81 slower  |
| **C++**             | 2.088 s   | 20.80 ms  | 48.1 /s   | 69.9 MB/s  | ×2.87 slower  |
| **Go**              | 6.563 s   | 65.55 ms  | 15.3 /s   | 22.2 MB/s  | ×9.03 slower  |
| **Python**          | 27.805 s  | 277.37 ms | 3.6 /s    | 5.2 MB/s   | ×38.29 slower |

---

### Startup overhead

The difference between per-file and batch time, divided across 100 processes:

| Language            | Overhead/process | What's included                                  |
| ------------------- | ---------------- | ------------------------------------------------ |
| **C++ (RapidJSON)** | ~4 ms            | OS loader, minimal runtime                       |
| **C++**             | ~6 ms            | OS loader, minimal runtime                       |
| **Rust**            | ~9 ms            | Runtime + schema read & compile                  |
| **Go**              | ~15 ms           | GC runtime init + schema read & compile          |
| **Python**          | ~65 ms           | Interpreter boot + import chain + schema compile |
| **Node.js**         | ~116 ms          | V8 JIT warm-up + module loading                  |

> **C++ starts faster than Rust and Go** because both nlohmann and RapidJSON/valijson
> compile the schema in a few ms, while both `jsonschema` (Rust) and `santhosh-tekuri/jsonschema` (Go)
> take ~8–15 ms for schema compilation — dominating their per-file overhead.

> **Node.js is fastest in batch mode** because Ajv v8 compiles the JSON Schema to V8 bytecode optimized for the V8 JIT, and Node.js's `JSON.parse` is implemented in highly optimized C++. The startup overhead (~103 ms) is the trade-off.

---

### Why is Node.js fastest in batch mode?

| Factor         | Node.js                     | Rust                     | C++ (RapidJSON)         | C++                           | Go                           | Python      |
| -------------- | --------------------------- | ------------------------ | ----------------------- | ----------------------------- | ---------------------------- | ----------- |
| JSON parser    | V8 `JSON.parse` (C++)       | `serde_json` (zero-copy) | RapidJSON in-situ (C++) | `nlohmann/json` (DOM, copies) | `encoding/json` (reflection) | C extension |
| Schema compile | once                        | once                     | once                    | once                          | once                         | once        |
| Memory model   | JIT (no GC pauses in batch) | no GC                    | no GC                   | no GC                         | GC pauses                    | GC          |
| Regex          | compiled at schema load     | compiled at schema load  | compiled at schema load | compiled at schema load       | compiled at schema load      | Python re   |

The RapidJSON variant (`validator_rj`) uses RapidJSON in-situ parsing with valijson
and reaches 109.2 MB/s — within 3% of Rust's 112.6 MB/s — confirming that the JSON
parser is the primary bottleneck in the nlohmann C++ implementation.

---

## Project structure

```
json-validator/
├── rust/                       Rust  — serde_json + jsonschema crate + clap
│   ├── src/main.rs
│   └── Cargo.toml
├── go/                         Go    — encoding/json + santhosh-tekuri/jsonschema/v5
│   ├── validator.go
│   ├── go.mod
│   └── fast/                   Go (fast) — goccy/go-json drop-in replacement
│       ├── validator_fast.go
│       └── go.mod
├── cpp/                        C++17 — two implementations built from the same CMakeLists.txt
│   ├── validator.cpp           nlohmann/json + nlohmann/json-schema-validator (validator)
│   ├── validator_rapidjson.cpp RapidJSON + valijson (validator_rj — 1.80× faster in batch)
│   └── CMakeLists.txt          (FetchContent, no manual dependency install needed)
├── python/                     Python — json stdlib + jsonschema
│   └── validator.py
├── nodejs/                     Node.js — TypeScript + Ajv v8 + esbuild bundle
│   ├── src/validator.ts
│   ├── dist/validator.js       (compiled bundle, deployable artifact)
│   └── package.json
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

| Language | Install                                                            |
| -------- | ------------------------------------------------------------------ |
| Rust     | [rustup.rs](https://rustup.rs)                                     |
| Go       | [go.dev/dl](https://go.dev/dl) ≥ 1.21                              |
| C++      | `apt install cmake build-essential`                                |
| Python   | `pip install jsonschema colorama`                                  |
| Node.js  | [nodejs.org](https://nodejs.org) ≥ 20, or `nvm install 20` + `npm` |

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

### Go (fast — goccy/go-json)

```bash
cd go/fast && go mod tidy && go build -o validator_fast .
# → go/fast/validator_fast
```

### C++

```bash
cd cpp && mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)
# → cpp/build/validator
```

### C++ (RapidJSON variant)

```bash
cd cpp/build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc) validator_rj
# → cpp/build/validator_rj
```

> **clangd support** — after cmake, symlink `compile_commands.json`:
>
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
>
> ```bash
> .venv/bin/python3 python/validator.py -f schema/valid_example.json -s schema/simple_schema.json
> ```

### Node.js

```bash
cd nodejs && npm install && npm run build
# → nodejs/dist/validator.js  (self-contained bundle, no node_modules at runtime)
```

> The bundle is the deployable artifact. Publish it to an internal Artifactory npm registry
> and run with just `node validator.js` — no `npm install` needed on the target machine.

---

## Usage

All five validators share the same interface:

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

```bash
node nodejs/dist/validator.js \
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

| Option         | Default    | Description       |
| -------------- | ---------- | ----------------- |
| `--count N`    | 100        | Number of files   |
| `--users N`    | 500        | Users per file    |
| `--products N` | 200        | Products per file |
| `--orders N`   | 1500       | Orders per file   |
| `--out DIR`    | `testdata` | Output directory  |

### 2. Run

```bash
bash benchmark/benchmark.sh              # default: 100 files
bash benchmark/benchmark.sh --files 50  # custom file count
```

The script builds all validators automatically, then runs both **per-file** and **batch** modes
and prints the comparison table with speedup ratios.

If `--files N` differs from the current `testdata/` count, files are regenerated automatically.
Above 690 files (~1 GB on disk), a confirmation prompt is shown.

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

| Code | Meaning                                            |
| ---- | -------------------------------------------------- |
| `0`  | JSON is valid                                      |
| `1`  | JSON invalid, or error (bad path, invalid schema…) |

```bash
./rust/target/release/json-validator -f file.json -s schema.json && echo OK || echo INVALID
```
