#!/usr/bin/env python3
"""JSON Schema Validator — validates a JSON file against a JSON Schema."""

import argparse
import json
import os
import sys
import time
from pathlib import Path

try:
    import jsonschema
    from jsonschema import Draft7Validator
except ImportError:
    print("Missing dependency: pip install jsonschema", file=sys.stderr)
    sys.exit(1)

try:
    from colorama import Fore, Back, Style, init as colorama_init
    colorama_init()
    HAS_COLOR = True
except ImportError:
    HAS_COLOR = False

# ── Color helpers ────────────────────────────────────────────────────────────

def _c(text, *codes):
    if not HAS_COLOR:
        return str(text)
    return "".join(codes) + str(text) + Style.RESET_ALL

def red(t):      return _c(t, Fore.RED)
def green(t):    return _c(t, Fore.GREEN, Style.BRIGHT)
def cyan(t):     return _c(t, Fore.CYAN)
def yellow(t):   return _c(t, Fore.YELLOW, Style.BRIGHT)
def dimmed(t):   return _c(t, Style.DIM)
def bold(t):     return _c(t, Style.BRIGHT)
def on_blue(t):  return _c(t, Back.BLUE, Fore.WHITE, Style.BRIGHT)

SEPARATOR = dimmed("─" * 55)

# ── Utilities ────────────────────────────────────────────────────────────────

def format_duration(seconds: float) -> str:
    micros = seconds * 1_000_000
    if micros < 1_000:
        return f"{micros:.0f}µs"
    elif micros < 1_000_000:
        return f"{seconds * 1_000:.2f}ms"
    else:
        return f"{seconds:.3f}s"


def read_json(path: Path):
    try:
        content = path.read_bytes()
    except OSError as e:
        return None, f"Cannot read '{path}': {e}"
    try:
        return json.loads(content), None
    except json.JSONDecodeError as e:
        return None, f"Invalid JSON in '{path}': {e}"


def fatal(msg: str, json_output: bool) -> None:
    if json_output:
        print(json.dumps({"valid": False, "error": msg}, indent=2))
    else:
        print(f"  {red('✗')} {red(msg)}")
        print(SEPARATOR)
        print()
    sys.exit(1)


def load_validator(schema_path: Path, json_output: bool) -> Draft7Validator:
    schema, err = read_json(schema_path)
    if err:
        fatal(err, json_output)
    try:
        validator = Draft7Validator(schema)
        validator.check_schema(schema)
        return validator
    except jsonschema.exceptions.SchemaError as e:
        fatal(f"Schema compilation failed: {e.message}", json_output)

# ── Batch mode ────────────────────────────────────────────────────────────────

def run_batch(batch_dir: Path, schema_path: Path, json_output: bool) -> None:
    start = time.perf_counter()

    files = sorted(batch_dir.glob("*.json"))
    if not files:
        fatal(f"No .json files found in '{batch_dir}'", json_output)

    if not json_output:
        print()
        print(on_blue(" JSON Validator — Batch Mode "))
        print(SEPARATOR)
        print(f"  {dimmed('Schema :')}  {cyan(schema_path)}")
        print(f"  {dimmed('Dir    :')}  {cyan(batch_dir)}")
        print(f"  {dimmed('Files  :')}  {cyan(len(files))}")
        print(SEPARATOR)

    validator = load_validator(schema_path, json_output)

    valid_count   = 0
    invalid_count = 0
    total_bytes   = 0

    for path in files:
        total_bytes += path.stat().st_size
        instance, err = read_json(path)
        if err:
            invalid_count += 1
            continue
        errors = list(validator.iter_errors(instance))
        if errors:
            invalid_count += 1
        else:
            valid_count += 1

    elapsed = time.perf_counter() - start
    total   = len(files)
    avg_ms  = elapsed * 1000 / total
    fps     = total / elapsed
    mbps    = (total_bytes / 1_000_000) / elapsed

    if json_output:
        print(json.dumps({
            "mode":            "batch",
            "total":           total,
            "valid":           valid_count,
            "invalid":         invalid_count,
            "total_bytes":     total_bytes,
            "elapsed":         format_duration(elapsed),
            "avg_ms_per_file": f"{avg_ms:.2f}",
            "throughput_fps":  f"{fps:.1f}",
            "throughput_mbps": f"{mbps:.1f}",
        }, indent=2))
    else:
        print(
            f"  {green('✓')} {green(f'{valid_count} valid')}   "
            f"{red('✗')} {red(f'{invalid_count} invalid')}"
        )
        print(f"  {dimmed('Time   :')}  {cyan(format_duration(elapsed))}")
        print(f"  {dimmed('Avg    :')}  {cyan(f'{avg_ms:.2f} ms/file')}")
        print(f"  {dimmed('Speed  :')}  {cyan(f'{fps:.1f} files/s')}  |  {cyan(f'{mbps:.1f} MB/s')}")
        print(SEPARATOR)
        print()

# ── Single-file mode ──────────────────────────────────────────────────────────

def run_single(file: Path, schema_path: Path, verbose: bool, json_output: bool) -> None:
    start = time.perf_counter()

    if not json_output:
        print()
        print(on_blue(" JSON Validator "))
        print(SEPARATOR)
        print(f"  {dimmed('File  :')}  {cyan(file)}")
        print(f"  {dimmed('Schema:')}  {cyan(schema_path)}")
        print(SEPARATOR)

    instance, err = read_json(file)
    if err:
        fatal(err, json_output)

    validator = load_validator(schema_path, json_output)
    errors    = sorted(validator.iter_errors(instance), key=lambda e: list(e.path))
    elapsed   = time.perf_counter() - start

    if not errors:
        if json_output:
            print(json.dumps({"valid": True, "errors": [], "elapsed": format_duration(elapsed)}, indent=2))
        else:
            print(f"  {green('✓')} {green('JSON is valid! Everything looks good.')}")
            print(f"  {dimmed('Time  :')}  {cyan(format_duration(elapsed))}")
            print(SEPARATOR)
            print()
        return

    error_list = []
    for e in errors:
        path        = "/" + "/".join(str(p) for p in e.absolute_path)   if e.absolute_path        else ""
        schema_path_str = "/" + "/".join(str(p) for p in e.absolute_schema_path) if e.absolute_schema_path else ""
        error_list.append({
            "path": path, "message": e.message,
            "schema_path": schema_path_str, "instance": e.instance,
        })

    if json_output:
        print(json.dumps({
            "valid": False, "error_count": len(error_list),
            "errors": error_list, "elapsed": format_duration(elapsed),
        }, indent=2))
    else:
        print(f"  {red('✗')} {bold(red(str(len(error_list))))} error(s) found:\n")
        for i, err in enumerate(error_list):
            print(f"  {yellow(f'[{i+1}]')} {red(err['message'])}")
            if err["path"]:
                print(f"      {dimmed('at path  :')} {cyan(err['path'])}")
            if verbose:
                if err["schema_path"]:
                    print(f"      {dimmed('schema   :')} {dimmed(err['schema_path'])}")
                if err["instance"] is not None:
                    print(f"      {dimmed('value    :')} {yellow(json.dumps(err['instance']))}")
            print()
        print(f"  {dimmed('Time  :')}  {cyan(format_duration(elapsed))}")
        print(SEPARATOR)
        print()

    sys.exit(1)

# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        prog="validator.py",
        description="Validates a JSON file against a JSON Schema",
        epilog="Supports JSON Schema Draft 4, 6, 7 and 2019-09.",
    )
    parser.add_argument("-f", "--file",       metavar="FILE",   type=Path,
                        help="Path to the JSON file to validate (single-file mode)")
    parser.add_argument("-s", "--schema",     metavar="SCHEMA", type=Path, required=True,
                        help="Path to the JSON Schema file")
    parser.add_argument("-b", "--batch",      metavar="DIR",    type=Path,
                        help="Validate all *.json in DIR (batch mode, schema compiled once)")
    parser.add_argument("-v", "--verbose",     action="store_true",
                        help="Show detailed errors (schema path, failing value)")
    parser.add_argument("-j", "--json-output", action="store_true",
                        help="Output in JSON format (for scripting)")
    args = parser.parse_args()

    if args.batch:
        run_batch(args.batch, args.schema, args.json_output)
    elif args.file:
        run_single(args.file, args.schema, args.verbose, args.json_output)
    else:
        parser.error("Provide --file FILE or --batch DIR")


if __name__ == "__main__":
    main()
