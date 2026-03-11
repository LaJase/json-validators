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
