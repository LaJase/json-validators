# Node.js TypeScript Validator Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a TypeScript/Node.js JSON Schema validator that matches the CLI interface of Rust/Go/C++/Python implementations and produces a single deployable `dist/validator.js` bundle.

**Architecture:** TypeScript source compiled via `tsc` (type-check) + `esbuild` (bundle) into a single self-contained `dist/validator.js`. Ajv v8 compiles the JSON Schema to V8 bytecode for fast validation. The bundle is the deployable artifact — no `node_modules` needed at runtime.

**Tech Stack:** Node.js 20, TypeScript 5 (strict), Ajv v8, chalk v5, esbuild

**Design doc:** `docs/plans/2026-03-12-nodejs-design.md`

---

### Task 1: Project scaffold (`nodejs/` directory)

**Files:**
- Create: `nodejs/package.json`
- Create: `nodejs/tsconfig.json`
- Create: `nodejs/.nvmrc`
- Create: `nodejs/src/` (empty directory placeholder via `.gitkeep`)

**Step 1: Create `.nvmrc`**

```
20
```

File: `nodejs/.nvmrc`

**Step 2: Create `package.json`**

```json
{
  "name": "json-validator",
  "version": "1.0.0",
  "description": "JSON Schema validator — Node.js/TypeScript implementation",
  "main": "dist/validator.js",
  "scripts": {
    "build": "tsc --noEmit && esbuild src/validator.ts --bundle --platform=node --target=node20 --outfile=dist/validator.js",
    "typecheck": "tsc --noEmit"
  },
  "dependencies": {
    "ajv": "^8.17.1",
    "ajv-formats": "^3.0.1",
    "chalk": "^5.4.1"
  },
  "devDependencies": {
    "@types/node": "^20.17.0",
    "esbuild": "^0.24.0",
    "typescript": "^5.7.0"
  }
}
```

File: `nodejs/package.json`

**Step 3: Create `tsconfig.json`**

```json
{
  "compilerOptions": {
    "target": "ES2022",
    "module": "ESNext",
    "moduleResolution": "bundler",
    "strict": true,
    "esModuleInterop": true,
    "skipLibCheck": true,
    "outDir": "dist",
    "rootDir": "src"
  },
  "include": ["src/**/*"]
}
```

File: `nodejs/tsconfig.json`

**Step 4: Install dependencies**

```bash
cd nodejs && npm install
```

Expected: `node_modules/` created, `package-lock.json` generated.

**Step 5: Add `node_modules` and `dist` to `.gitignore`**

Open the root `.gitignore` and verify or add:
```
nodejs/node_modules/
nodejs/dist/
```

**Step 6: Commit**

```bash
git add nodejs/package.json nodejs/tsconfig.json nodejs/.nvmrc nodejs/package-lock.json .gitignore
git commit -m "chore: scaffold Node.js TypeScript project"
```

---

### Task 2: Implement `src/validator.ts` — CLI skeleton + argument parsing

**Files:**
- Create: `nodejs/src/validator.ts`

The validator must implement this interface (identical to all other languages):
```
node dist/validator.js -s SCHEMA (-f FILE | -b DIR) [-v] [-j]
```

Exit 0 = valid, exit 1 = invalid or error.

**Step 1: Create `nodejs/src/validator.ts` with argument parsing**

```typescript
#!/usr/bin/env node
import { readFileSync, readdirSync, statSync } from "fs";
import { resolve, join } from "path";
import { hrtime } from "process";
import Ajv from "ajv";
import addFormats from "ajv-formats";
import chalk from "chalk";

// ── CLI argument parsing ──────────────────────────────────────────────────────

interface Args {
  schema: string;
  file?: string;
  batch?: string;
  verbose: boolean;
  jsonOutput: boolean;
}

function parseArgs(argv: string[]): Args {
  const args: Args = { schema: "", verbose: false, jsonOutput: false };
  const a = argv.slice(2);
  for (let i = 0; i < a.length; i++) {
    switch (a[i]) {
      case "-s": case "--schema":  args.schema = a[++i]; break;
      case "-f": case "--file":    args.file = a[++i]; break;
      case "-b": case "--batch":   args.batch = a[++i]; break;
      case "-v": case "--verbose": args.verbose = true; break;
      case "-j": case "--json-output": args.jsonOutput = true; break;
      default:
        process.stderr.write(`Unknown argument: ${a[i]}\n`);
        process.exit(1);
    }
  }
  if (!args.schema) {
    process.stderr.write("Error: -s/--schema is required\n");
    process.exit(1);
  }
  if (!args.file && !args.batch) {
    process.stderr.write("Error: provide --file FILE or --batch DIR\n");
    process.exit(1);
  }
  return args;
}

// ── Utilities ─────────────────────────────────────────────────────────────────

function formatDuration(ns: bigint): string {
  const micros = Number(ns) / 1000;
  if (micros < 1000) return `${micros.toFixed(0)}µs`;
  if (micros < 1_000_000) return `${(micros / 1000).toFixed(2)}ms`;
  return `${(micros / 1_000_000).toFixed(3)}s`;
}

const SEP = chalk.dim("─".repeat(55));

function fatal(msg: string, jsonOutput: boolean): never {
  if (jsonOutput) {
    process.stdout.write(JSON.stringify({ valid: false, error: msg }, null, 2) + "\n");
  } else {
    process.stdout.write(`  ${chalk.red("✗")} ${chalk.red(msg)}\n${SEP}\n\n`);
  }
  process.exit(1);
}

function readJson(path: string, jsonOutput: boolean): unknown {
  let content: string;
  try {
    content = readFileSync(resolve(path), "utf8");
  } catch (e) {
    fatal(`Cannot read '${path}': ${(e as Error).message}`, jsonOutput);
  }
  try {
    return JSON.parse(content!);
  } catch (e) {
    fatal(`Invalid JSON in '${path}': ${(e as Error).message}`, jsonOutput);
  }
}

// ── Schema compilation ────────────────────────────────────────────────────────

type ValidateFn = ReturnType<Ajv["compile"]>;

function compileSchema(schemaPath: string, jsonOutput: boolean): ValidateFn {
  const schema = readJson(schemaPath, jsonOutput);
  const ajv = new Ajv({ allErrors: true, strict: false });
  addFormats(ajv);
  try {
    return ajv.compile(schema as object);
  } catch (e) {
    fatal(`Schema compilation failed: ${(e as Error).message}`, jsonOutput);
  }
}

// ── Single-file mode ──────────────────────────────────────────────────────────

function runSingle(args: Args): void {
  const start = hrtime.bigint();

  if (!args.jsonOutput) {
    process.stdout.write(
      `\n${chalk.bgBlue.white.bold(" JSON Validator ")}\n${SEP}\n` +
      `  ${chalk.dim("File  :")}  ${chalk.cyan(args.file!)}\n` +
      `  ${chalk.dim("Schema:")}  ${chalk.cyan(args.schema)}\n${SEP}\n`
    );
  }

  const instance = readJson(args.file!, args.jsonOutput);
  const validate = compileSchema(args.schema, args.jsonOutput);
  const valid = validate(instance);
  const elapsed = hrtime.bigint() - start;

  if (valid) {
    if (args.jsonOutput) {
      process.stdout.write(JSON.stringify({ valid: true, errors: [], elapsed: formatDuration(elapsed) }, null, 2) + "\n");
    } else {
      process.stdout.write(
        `  ${chalk.green("✓")} ${chalk.green.bold("JSON is valid! Everything looks good.")}\n` +
        `  ${chalk.dim("Time  :")}  ${chalk.cyan(formatDuration(elapsed))}\n${SEP}\n\n`
      );
    }
    process.exit(0);
  }

  const errors = (validate.errors ?? []).map((e) => ({
    path: e.instancePath || "",
    message: e.message ?? "",
    schema_path: e.schemaPath ?? "",
    instance: e.data,
  }));

  if (args.jsonOutput) {
    process.stdout.write(JSON.stringify({ valid: false, error_count: errors.length, errors, elapsed: formatDuration(elapsed) }, null, 2) + "\n");
  } else {
    process.stdout.write(`  ${chalk.red("✗")} ${chalk.bold.red(String(errors.length))} error(s) found:\n\n`);
    errors.forEach((e, i) => {
      process.stdout.write(`  ${chalk.yellow(`[${i + 1}]`)} ${chalk.red(e.message)}\n`);
      if (e.path) process.stdout.write(`      ${chalk.dim("at path  :")} ${chalk.cyan(e.path)}\n`);
      if (args.verbose) {
        if (e.schema_path) process.stdout.write(`      ${chalk.dim("schema   :")} ${chalk.dim(e.schema_path)}\n`);
        if (e.instance !== undefined) process.stdout.write(`      ${chalk.dim("value    :")} ${chalk.yellow(JSON.stringify(e.instance))}\n`);
      }
      process.stdout.write("\n");
    });
    process.stdout.write(`  ${chalk.dim("Time  :")}  ${chalk.cyan(formatDuration(elapsed))}\n${SEP}\n\n`);
  }
  process.exit(1);
}

// ── Batch mode ────────────────────────────────────────────────────────────────

function runBatch(args: Args): void {
  const start = hrtime.bigint();

  const files = readdirSync(resolve(args.batch!))
    .filter((f) => f.endsWith(".json"))
    .sort()
    .map((f) => join(resolve(args.batch!), f));

  if (files.length === 0) fatal(`No .json files found in '${args.batch}'`, args.jsonOutput);

  if (!args.jsonOutput) {
    process.stdout.write(
      `\n${chalk.bgBlue.white.bold(" JSON Validator — Batch Mode ")}\n${SEP}\n` +
      `  ${chalk.dim("Schema :")}  ${chalk.cyan(args.schema)}\n` +
      `  ${chalk.dim("Dir    :")}  ${chalk.cyan(args.batch!)}\n` +
      `  ${chalk.dim("Files  :")}  ${chalk.cyan(String(files.length))}\n${SEP}\n`
    );
  }

  const validate = compileSchema(args.schema, args.jsonOutput);

  let validCount = 0;
  let invalidCount = 0;
  let totalBytes = 0;

  for (const f of files) {
    totalBytes += statSync(f).size;
    let instance: unknown;
    try {
      instance = JSON.parse(readFileSync(f, "utf8"));
    } catch {
      invalidCount++;
      continue;
    }
    if (validate(instance)) validCount++;
    else invalidCount++;
  }

  const elapsed = hrtime.bigint() - start;
  const elapsedSec = Number(elapsed) / 1e9;
  const total = files.length;
  const avgMs = (elapsedSec * 1000 / total).toFixed(2);
  const fps = (total / elapsedSec).toFixed(1);
  const mbps = (totalBytes / 1_000_000 / elapsedSec).toFixed(1);

  if (args.jsonOutput) {
    process.stdout.write(JSON.stringify({
      mode: "batch",
      total,
      valid: validCount,
      invalid: invalidCount,
      total_bytes: totalBytes,
      elapsed: formatDuration(elapsed),
      avg_ms_per_file: avgMs,
      throughput_fps: fps,
      throughput_mbps: mbps,
    }, null, 2) + "\n");
  } else {
    process.stdout.write(
      `  ${chalk.green("✓")} ${chalk.green(`${validCount} valid`)}   ` +
      `${chalk.red("✗")} ${chalk.red(`${invalidCount} invalid`)}\n` +
      `  ${chalk.dim("Time   :")}  ${chalk.cyan(formatDuration(elapsed))}\n` +
      `  ${chalk.dim("Avg    :")}  ${chalk.cyan(`${avgMs} ms/file`)}\n` +
      `  ${chalk.dim("Speed  :")}  ${chalk.cyan(`${fps} files/s`)}  |  ${chalk.cyan(`${mbps} MB/s`)}\n${SEP}\n\n`
    );
  }
}

// ── Entry point ───────────────────────────────────────────────────────────────

const args = parseArgs(process.argv);
if (args.batch) runBatch(args);
else runSingle(args);
```

**Step 2: Build**

```bash
cd nodejs && npm run build
```

Expected: `dist/validator.js` created, no TypeScript errors.

**Step 3: Smoke test — valid file**

```bash
node nodejs/dist/validator.js \
  -f schema/valid_example.json \
  -s schema/simple_schema.json
```

Expected: exit 0, output shows `✓ JSON is valid!`

**Step 4: Smoke test — invalid file**

```bash
node nodejs/dist/validator.js \
  -f schema/invalid_example.json \
  -s schema/simple_schema.json
```

Expected: exit 1, output shows `✗ N error(s) found`

**Step 5: Smoke test — JSON output mode**

```bash
node nodejs/dist/validator.js \
  -f schema/valid_example.json \
  -s schema/simple_schema.json -j
```

Expected: valid JSON with `"valid": true`, `"errors": []`, `"elapsed"` field.

**Step 6: Smoke test — batch mode**

```bash
# Generate test data first if not present
python3 benchmark/generate_testdata.py

node nodejs/dist/validator.js \
  -b testdata/ \
  -s schema/complex_schema.json -j
```

Expected: valid JSON with `"mode": "batch"`, `"total": 100`, `"valid": 80`, `"invalid": 20`, throughput fields.

**Step 7: Commit**

```bash
git add nodejs/src/validator.ts nodejs/dist/validator.js
git commit -m "feat: add Node.js TypeScript JSON Schema validator"
```

---

### Task 3: Update `benchmark/benchmark.sh`

**Files:**
- Modify: `benchmark/benchmark.sh`

**Step 1: Add Node.js binary variable after CPP_BIN (line ~16)**

```bash
NODE_BIN="node $ROOT/nodejs/dist/validator.js"
```

**Step 2: Add `node` to `check_deps`**

In the `check_deps()` function, add:
```bash
command -v node &>/dev/null || missing+=("node (nodejs)")
```

**Step 3: Add Node.js build step after the Python check block (~line 175)**

```bash
sep
echo -e "${CYAN}► Building Node.js (TypeScript → esbuild bundle)...${RESET}"
SKIP_NODE=1
NODE_DIST="$ROOT/nodejs/dist/validator.js"
if command -v node &>/dev/null && command -v npm &>/dev/null; then
    if (cd "$ROOT/nodejs" && npm ci --silent 2>/dev/null && npm run build --silent 2>/dev/null); then
        echo -e "  ${GREEN}✓ bundle ready: nodejs/dist/validator.js${RESET}"
        SKIP_NODE=0
    else
        echo -e "  ${YELLOW}⚠  Node.js build failed — skipping${RESET}"
    fi
else
    echo -e "  ${YELLOW}⚠  node/npm not found — skipping Node.js (install from nodejs.org or nvm)${RESET}"
fi
```

**Step 4: Add Node.js to PART 1 per-file section (after the Python block)**

```bash
if [[ "$SKIP_NODE" -eq 0 ]]; then
    echo -ne "  ${CYAN}[Node]  ${RESET} validating... "
    run_per_file "node $ROOT/nodejs/dist/validator.js -f FILE -s $SCHEMA -j"
    echo -e "${GREEN}done${RESET}"
    PF_LABELS+=("Node  "); PF_TIMES+=("$ELAPSED"); PF_OK+=("$OK"); PF_FAIL+=("$FAIL")
fi
```

**Step 5: Add Node.js to PART 2 batch section (after the Python block)**

```bash
if [[ "$SKIP_NODE" -eq 0 ]]; then
    echo -ne "  ${CYAN}[Node]  ${RESET} batch... "
    run_batch "node $ROOT/nodejs/dist/validator.js -b $TESTDATA -s $SCHEMA -j"
    echo -e "${GREEN}done${RESET}"
    BT_LABELS+=("Node  "); BT_TIMES+=("$ELAPSED")
    BT_FPS+=("$BATCH_FPS"); BT_MBPS+=("$BATCH_MBPS"); BT_AVG+=("$BATCH_AVG")
    BT_OK+=("$BATCH_VALID"); BT_FAIL+=("$BATCH_INVALID")
fi
```

**Step 6: Update the title banner**

Change:
```bash
echo -e "${BOLD}║    JSON Validator — Benchmark  (Rust / Go / C++ / Python)   ║${RESET}"
```
To:
```bash
echo -e "${BOLD}║  JSON Validator — Benchmark (Rust/Go/C++/Python/Node.js)    ║${RESET}"
```

**Step 7: Update startup overhead comment**

Change the startup overhead comment to include Node.js:
```bash
echo -e "  ${DIM}Startup overhead included (~1 ms Rust, ~10-15 ms Go, ~30-50 ms Python/Node per process)${RESET}"
```

**Step 8: Run the full benchmark to verify**

```bash
bash benchmark/benchmark.sh
```

Expected: Node.js appears in both Part 1 and Part 2 tables with valid throughput numbers.

**Step 9: Commit**

```bash
git add benchmark/benchmark.sh
git commit -m "feat: add Node.js to benchmark script"
```

---

### Task 4: Update `README.md`

**Files:**
- Modify: `README.md`

**Step 1: Add Node.js badge (after Python badge, line 9)**

```markdown
![Node.js](https://img.shields.io/badge/Node.js-20+-339933?logo=node.js)
```

**Step 2: Update description line 4**

Change:
```markdown
> **Rust · Go · C++ · Python** — four implementations of the same JSON Schema validator,
```
To:
```markdown
> **Rust · Go · C++ · Python · Node.js** — five implementations of the same JSON Schema validator,
```

**Step 3: Update project structure section**

Add after the `python/` entry:
```markdown
├── nodejs/                     Node.js — TypeScript + Ajv v8 + esbuild bundle
│   ├── src/validator.ts
│   ├── dist/validator.js       (compiled bundle, deployable artifact)
│   └── package.json
```

**Step 4: Add Node.js to the prerequisites table**

```markdown
| Node.js | [nodejs.org](https://nodejs.org) ≥ 20, or `nvm install 20` |
```

**Step 5: Add Node.js build section (after Python section)**

```markdown
### Node.js

```bash
cd nodejs && npm install && npm run build
# → nodejs/dist/validator.js  (self-contained bundle, no node_modules at runtime)
```

> The bundle is the deployable artifact. It can be published to an Artifactory npm registry
> and run with just `node validator.js` — no `npm install` needed on the target machine.
```

**Step 6: Add Node.js usage example in the Usage section**

In the "Single file" subsection, add:
```bash
node nodejs/dist/validator.js \
  -f schema/valid_example.json \
  -s schema/simple_schema.json
```

**Step 7: Update the dependency table in "Why is Rust faster" section**

Add a Node.js row:
```markdown
| Node.js | V8 `JSON.parse` | Ajv v8 (bytecode) | no GC pauses (event loop) | Node.js re |
```

**Step 8: Update results tables with actual benchmark numbers**

After running `bash benchmark/benchmark.sh`, fill in the Node.js row in both Part 1 and Part 2 tables with the measured values.

**Step 9: Commit**

```bash
git add README.md
git commit -m "docs: add Node.js to README (structure, build, usage, results)"
```

---

### Task 5: Update `.gitignore`

**Files:**
- Modify: `.gitignore`

**Step 1: Verify `nodejs/node_modules/` and `nodejs/dist/` are ignored**

```bash
cat .gitignore
```

If not present, add:
```
nodejs/node_modules/
nodejs/dist/
```

**Step 2: Commit if changed**

```bash
git add .gitignore
git commit -m "chore: ignore nodejs/node_modules and nodejs/dist"
```

---

## Final Verification

```bash
# Clean build from scratch
rm -rf nodejs/node_modules nodejs/dist
cd nodejs && npm ci && npm run build && cd ..

# Single file
node nodejs/dist/validator.js -f schema/valid_example.json -s schema/simple_schema.json
node nodejs/dist/validator.js -f schema/invalid_example.json -s schema/simple_schema.json -v
node nodejs/dist/validator.js -f schema/valid_example.json -s schema/simple_schema.json -j

# Batch
node nodejs/dist/validator.js -b testdata/ -s schema/complex_schema.json -j

# Full benchmark
bash benchmark/benchmark.sh
```
