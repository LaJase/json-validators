use clap::Parser;
use colored::*;
use jsonschema::JSONSchema;
use serde_json::Value;
use std::fs;
use std::path::{Path, PathBuf};
use std::process;
use std::time::Instant;

/// JSON Schema Validator — validates a JSON file against a JSON Schema
#[derive(Parser, Debug)]
#[command(
    name = "json-validator",
    version = "1.0.0",
    about = "Validates a JSON file against a JSON Schema",
    long_about = "A CLI tool to check whether a JSON file is valid\naccording to a given JSON Schema (Draft 4, 6, 7 or 2019-09)."
)]
struct Args {
    /// Path to the JSON file to validate (single-file mode)
    #[arg(short = 'f', long = "file", value_name = "FILE")]
    file: Option<PathBuf>,

    /// Path to the JSON Schema file
    #[arg(short = 's', long = "schema", value_name = "SCHEMA", required = true)]
    schema: PathBuf,

    /// Validate all *.json files in DIR (batch mode, schema compiled once)
    #[arg(short = 'b', long = "batch", value_name = "DIR")]
    batch: Option<PathBuf>,

    /// Show detailed errors (schema path, failing value)
    #[arg(short = 'v', long = "verbose")]
    verbose: bool,

    /// Output in JSON format (for scripting)
    #[arg(short = 'j', long = "json-output")]
    json_output: bool,
}

fn read_json(path: &Path) -> Result<Value, String> {
    let content = fs::read_to_string(path)
        .map_err(|e| format!("Cannot read '{}': {}", path.display(), e))?;
    serde_json::from_str(&content)
        .map_err(|e| format!("Invalid JSON in '{}': {}", path.display(), e))
}

fn print_separator() {
    println!("{}", "─".repeat(55).dimmed());
}

fn fatal(msg: &str, json_output: bool) -> ! {
    if json_output {
        let out = serde_json::json!({ "valid": false, "error": msg });
        println!("{}", serde_json::to_string_pretty(&out).unwrap());
    } else {
        println!("  {} {}", "✗".red().bold(), msg.red());
        print_separator();
        println!();
    }
    process::exit(1);
}

fn format_duration(elapsed: std::time::Duration) -> String {
    let micros = elapsed.as_micros();
    if micros < 1_000 {
        format!("{}µs", micros)
    } else if micros < 1_000_000 {
        format!("{:.2}ms", elapsed.as_secs_f64() * 1_000.0)
    } else {
        format!("{:.3}s", elapsed.as_secs_f64())
    }
}

// ── Batch mode ────────────────────────────────────────────────────────────────

fn run_batch(dir: &Path, schema_path: &Path, json_output: bool) {
    let start = Instant::now();

    // Collect .json files
    let mut files: Vec<PathBuf> = fs::read_dir(dir)
        .unwrap_or_else(|e| {
            fatal(&format!("Cannot read directory '{}': {}", dir.display(), e), json_output)
        })
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .filter(|p| p.extension().and_then(|s| s.to_str()) == Some("json"))
        .collect();
    files.sort();

    if files.is_empty() {
        fatal(&format!("No .json files found in '{}'", dir.display()), json_output);
    }

    if !json_output {
        println!();
        println!("{}", " JSON Validator — Batch Mode ".on_blue().white().bold());
        print_separator();
        println!("  {}  {}", "Schema :".dimmed(), schema_path.display().to_string().cyan());
        println!("  {}  {}", "Dir    :".dimmed(), dir.display().to_string().cyan());
        println!("  {}  {}", "Files  :".dimmed(), files.len().to_string().cyan());
        print_separator();
    }

    // Compile schema once
    let schema_value = read_json(schema_path)
        .unwrap_or_else(|e| fatal(&e, json_output));
    let compiled = JSONSchema::compile(&schema_value)
        .unwrap_or_else(|e| fatal(&format!("Schema compilation failed: {}", e), json_output));

    // Validate all files
    let mut valid_count: usize = 0;
    let mut invalid_count: usize = 0;
    let mut total_bytes: u64 = 0;

    for path in &files {
        total_bytes += path.metadata().map(|m| m.len()).unwrap_or(0);
        match read_json(path) {
            Err(_) => invalid_count += 1,
            Ok(instance) => {
                if compiled.validate(&instance).is_ok() {
                    valid_count += 1;
                } else {
                    invalid_count += 1;
                }
            }
        }
    }

    let elapsed = start.elapsed();
    let total = files.len();
    let secs = elapsed.as_secs_f64();
    let avg_ms = secs * 1_000.0 / total as f64;
    let fps = total as f64 / secs;
    let mbps = (total_bytes as f64 / 1_000_000.0) / secs;

    if json_output {
        let out = serde_json::json!({
            "mode":            "batch",
            "total":           total,
            "valid":           valid_count,
            "invalid":         invalid_count,
            "total_bytes":     total_bytes,
            "elapsed":         format_duration(elapsed),
            "avg_ms_per_file": format!("{:.2}", avg_ms),
            "throughput_fps":  format!("{:.1}", fps),
            "throughput_mbps": format!("{:.1}", mbps),
        });
        println!("{}", serde_json::to_string_pretty(&out).unwrap());
    } else {
        println!(
            "  {} {}   {} {}",
            "✓".green().bold(), format!("{} valid", valid_count).green().bold(),
            "✗".red().bold(),   format!("{} invalid", invalid_count).red().bold(),
        );
        println!("  {}  {}", "Time   :".dimmed(), format_duration(elapsed).cyan());
        println!("  {}  {}", "Avg    :".dimmed(), format!("{:.2} ms/file", avg_ms).cyan());
        println!(
            "  {}  {}  |  {}",
            "Speed  :".dimmed(),
            format!("{:.1} files/s", fps).cyan(),
            format!("{:.1} MB/s", mbps).cyan(),
        );
        print_separator();
        println!();
    }
}

// ── Single-file mode ──────────────────────────────────────────────────────────

fn run_single(file: &Path, schema_path: &Path, verbose: bool, json_output: bool) {
    let start = Instant::now();

    if !json_output {
        println!();
        println!("{}", " JSON Validator ".on_blue().white().bold());
        print_separator();
        println!("  {}  {}", "File  :".dimmed(), file.display().to_string().cyan());
        println!("  {}  {}", "Schema:".dimmed(), schema_path.display().to_string().cyan());
        print_separator();
    }

    let instance = read_json(file)
        .unwrap_or_else(|e| fatal(&e, json_output));

    let schema_value = read_json(schema_path)
        .unwrap_or_else(|e| fatal(&e, json_output));

    let compiled = JSONSchema::compile(&schema_value)
        .unwrap_or_else(|e| fatal(&format!("Schema compilation failed: {}", e), json_output));

    let result = compiled.validate(&instance);
    let elapsed = start.elapsed();

    match result {
        Ok(_) => {
            if json_output {
                let out = serde_json::json!({
                    "valid": true, "errors": [], "elapsed": format_duration(elapsed),
                });
                println!("{}", serde_json::to_string_pretty(&out).unwrap());
            } else {
                println!(
                    "  {} {}",
                    "✓".green().bold(),
                    "JSON is valid! Everything looks good.".green().bold()
                );
                println!("  {}  {}", "Time  :".dimmed(), format_duration(elapsed).cyan());
                print_separator();
                println!();
            }
        }
        Err(errors) => {
            let error_list: Vec<serde_json::Value> = errors
                .map(|e| {
                    serde_json::json!({
                        "path":        e.instance_path.to_string(),
                        "message":     e.to_string(),
                        "schema_path": e.schema_path.to_string(),
                        "instance":    e.instance.as_ref(),
                    })
                })
                .collect();

            if json_output {
                let out = serde_json::json!({
                    "valid": false,
                    "error_count": error_list.len(),
                    "errors": error_list,
                    "elapsed": format_duration(elapsed),
                });
                println!("{}", serde_json::to_string_pretty(&out).unwrap());
            } else {
                println!(
                    "  {} {} error(s) found:\n",
                    "✗".red().bold(),
                    error_list.len().to_string().red().bold()
                );
                for (i, err) in error_list.iter().enumerate() {
                    let path        = err["path"].as_str().unwrap_or("/");
                    let msg         = err["message"].as_str().unwrap_or("unknown error");
                    let schema_path = err["schema_path"].as_str().unwrap_or("");

                    println!("  {} {}", format!("[{}]", i + 1).yellow().bold(), msg.red());
                    if !path.is_empty() && path != "/" {
                        println!("      {} {}", "at path  :".dimmed(), path.cyan());
                    }
                    if verbose {
                        if !schema_path.is_empty() {
                            println!("      {} {}", "schema   :".dimmed(), schema_path.dimmed());
                        }
                        if let Some(val) = err.get("instance") {
                            if !val.is_null() {
                                println!("      {} {}", "value    :".dimmed(), val.to_string().yellow());
                            }
                        }
                    }
                    println!();
                }
                println!("  {}  {}", "Time  :".dimmed(), format_duration(elapsed).cyan());
                print_separator();
                println!();
            }
            process::exit(1);
        }
    }
}

fn main() {
    let args = Args::parse();

    match (&args.batch, &args.file) {
        (Some(dir), _) => run_batch(dir, &args.schema, args.json_output),
        (None, Some(file)) => run_single(file, &args.schema, args.verbose, args.json_output),
        (None, None) => fatal("Provide --file FILE or --batch DIR", args.json_output),
    }
}
