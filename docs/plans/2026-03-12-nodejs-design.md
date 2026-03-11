# Design — Node.js (TypeScript) JSON Validator

**Date:** 2026-03-12
**Status:** Approved

## Context

The benchmark currently compares Rust, Go, C++ and Python implementations. The goal is to add a Node.js/TypeScript implementation representative of what developers use daily in industrial CI/CD pipelines.

## Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Language | TypeScript (strict) | Representative of industry usage, typed for quality |
| Execution | Compiled JS (`tsc` + `esbuild` bundle) | Pipeline-friendly, no `node_modules` at runtime |
| Schema validator | Ajv v8 + `ajv-formats` | De facto standard, compiles schema to V8 bytecode |
| Colors | `chalk` v5 | Standard Node.js equivalent of `colorama` |
| Deployment | Single `dist/validator.js` file | Published to Artifactory; run with `node validator.js` |

## Structure

```
nodejs/
  src/
    validator.ts         Main implementation
  dist/
    validator.js         esbuild bundle (deployable artifact)
  package.json           Dependencies + build scripts
  tsconfig.json          Strict mode, target Node20, ESNext modules
  .nvmrc                 Node.js version pin (20)
```

## Dependencies

```json
{
  "dependencies": {
    "ajv": "^8.17.1",
    "ajv-formats": "^3.0.1",
    "chalk": "^5.4.1"
  },
  "devDependencies": {
    "typescript": "^5.7.0",
    "esbuild": "^0.24.0",
    "@types/node": "^20.0.0"
  }
}
```

## Build

```bash
cd nodejs
npm ci                  # install devDeps only, reproducible
npm run build           # tsc (type-check) + esbuild (bundle)
# → dist/validator.js
```

The `build` script runs two steps:
1. `tsc --noEmit` — type-checking only (no output), catches errors early
2. `esbuild src/validator.ts --bundle --platform=node --outfile=dist/validator.js` — produces the deployable bundle

## CLI Interface

Identical to all other implementations:

```
node dist/validator.js -s SCHEMA (-f FILE | -b DIR) [-v] [-j]
```

Exit codes: `0` = valid, `1` = invalid or error.

## Benchmark Integration

`benchmark.sh` changes:
- Add `NODE_BIN="node $ROOT/nodejs/dist/validator.js"` variable
- Add a build step: `cd nodejs && npm ci && npm run build`
- Add `check_deps` entry for `node`
- Add `SKIP_NODE=1` guard (skipped if `node` not found or bundle missing)
- Add per-file and batch benchmark blocks mirroring the existing C++/Python pattern
- Update title banner to include Node.js
- Update startup overhead comment with estimated Node.js overhead (~30-50 ms)

## README Changes

- Add `Node.js` badge
- Update project description (5 implementations)
- Add `nodejs/` entry in project structure
- Add Node.js prerequisites row (`node >= 20`, `npm`)
- Add Node.js build section
- Add Node.js usage example
- Results tables will be updated after first benchmark run

## Artifactory Deployment

The artifact `dist/validator.js` is a self-contained bundle (~500 KB). It can be:
- Published as an npm package to an internal Artifactory npm registry
- Downloaded directly and run with `node validator.js`

No `node_modules` directory is needed at runtime.
