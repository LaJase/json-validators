package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"github.com/fatih/color"
	"github.com/santhosh-tekuri/jsonschema/v5"
)

const separator = "───────────────────────────────────────────────────────"

var (
	boldGreen  = color.New(color.FgGreen, color.Bold).SprintFunc()
	boldRed    = color.New(color.FgRed, color.Bold).SprintFunc()
	boldYellow = color.New(color.FgYellow, color.Bold).SprintFunc()
	cyan       = color.New(color.FgCyan).SprintFunc()
	dim        = color.New(color.Faint).SprintFunc()
	headerFmt  = color.New(color.FgWhite, color.Bold, color.BgBlue).SprintFunc()
)

type errorEntry struct {
	Path       string `json:"path"`
	Message    string `json:"message"`
	SchemaPath string `json:"schema_path"`
}

func formatDuration(d time.Duration) string {
	micros := d.Microseconds()
	switch {
	case micros < 1_000:
		return fmt.Sprintf("%dµs", micros)
	case micros < 1_000_000:
		return fmt.Sprintf("%.2fms", float64(d.Nanoseconds())/1e6)
	default:
		return fmt.Sprintf("%.3fs", d.Seconds())
	}
}

func readJSON(path string) (interface{}, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("cannot read '%s': %w", path, err)
	}
	var v interface{}
	if err := json.Unmarshal(data, &v); err != nil {
		return nil, fmt.Errorf("invalid JSON in '%s': %w", path, err)
	}
	return v, nil
}

func collectErrors(ve *jsonschema.ValidationError) []errorEntry {
	if len(ve.Causes) == 0 {
		return []errorEntry{{
			Path:       ve.InstanceLocation,
			Message:    ve.Message,
			SchemaPath: ve.KeywordLocation,
		}}
	}
	var entries []errorEntry
	for _, cause := range ve.Causes {
		entries = append(entries, collectErrors(cause)...)
	}
	return entries
}

func fatal(msg string, jsonOutput bool) {
	if jsonOutput {
		out, _ := json.MarshalIndent(map[string]interface{}{"valid": false, "error": msg}, "", "  ")
		fmt.Println(string(out))
	} else {
		fmt.Printf("  %s %s\n", boldRed("✗"), boldRed(msg))
		fmt.Println(dim(separator))
		fmt.Println()
	}
	os.Exit(1)
}

func compileSchema(schemaPath string, jsonOutput bool) *jsonschema.Schema {
	compiler := jsonschema.NewCompiler()
	compiler.Draft = jsonschema.Draft7

	schemaURL := schemaPath
	if !strings.HasPrefix(schemaPath, "/") {
		cwd, _ := os.Getwd()
		schemaURL = cwd + "/" + schemaPath
	}
	schemaURL = "file://" + schemaURL

	sch, err := compiler.Compile(schemaURL)
	if err != nil {
		fatal(fmt.Sprintf("schema compilation failed: %v", err), jsonOutput)
	}
	return sch
}

// ── Batch mode ────────────────────────────────────────────────────────────────

func runBatch(batchDir, schemaPath string, jsonOutput bool) {
	start := time.Now()

	// Collect .json files
	entries, err := os.ReadDir(batchDir)
	if err != nil {
		fatal(fmt.Sprintf("cannot read directory '%s': %v", batchDir, err), jsonOutput)
	}
	var files []string
	for _, e := range entries {
		if !e.IsDir() && strings.HasSuffix(e.Name(), ".json") {
			files = append(files, filepath.Join(batchDir, e.Name()))
		}
	}
	sort.Strings(files)

	if len(files) == 0 {
		fatal(fmt.Sprintf("no .json files found in '%s'", batchDir), jsonOutput)
	}

	if !jsonOutput {
		fmt.Println()
		fmt.Println(headerFmt(" JSON Validator — Batch Mode "))
		fmt.Println(dim(separator))
		fmt.Printf("  %s  %s\n", dim("Schema :"), cyan(schemaPath))
		fmt.Printf("  %s  %s\n", dim("Dir    :"), cyan(batchDir))
		fmt.Printf("  %s  %s\n", dim("Files  :"), cyan(fmt.Sprintf("%d", len(files))))
		fmt.Println(dim(separator))
	}

	sch := compileSchema(schemaPath, jsonOutput)

	var validCount, invalidCount int
	var totalBytes int64

	for _, path := range files {
		info, _ := os.Stat(path)
		if info != nil {
			totalBytes += info.Size()
		}
		instance, err := readJSON(path)
		if err != nil {
			invalidCount++
			continue
		}
		if err := sch.Validate(instance); err != nil {
			invalidCount++
		} else {
			validCount++
		}
	}

	elapsed := time.Since(start)
	total := len(files)
	secs := elapsed.Seconds()
	avgMs := secs * 1000 / float64(total)
	fps := float64(total) / secs
	mbps := (float64(totalBytes) / 1_000_000) / secs

	if jsonOutput {
		out, _ := json.MarshalIndent(map[string]interface{}{
			"mode":            "batch",
			"total":           total,
			"valid":           validCount,
			"invalid":         invalidCount,
			"total_bytes":     totalBytes,
			"elapsed":         formatDuration(elapsed),
			"avg_ms_per_file": fmt.Sprintf("%.2f", avgMs),
			"throughput_fps":  fmt.Sprintf("%.1f", fps),
			"throughput_mbps": fmt.Sprintf("%.1f", mbps),
		}, "", "  ")
		fmt.Println(string(out))
	} else {
		fmt.Printf("  %s %s   %s %s\n",
			boldGreen("✓"), boldGreen(fmt.Sprintf("%d valid", validCount)),
			boldRed("✗"), boldRed(fmt.Sprintf("%d invalid", invalidCount)),
		)
		fmt.Printf("  %s  %s\n", dim("Time   :"), cyan(formatDuration(elapsed)))
		fmt.Printf("  %s  %s\n", dim("Avg    :"), cyan(fmt.Sprintf("%.2f ms/file", avgMs)))
		fmt.Printf("  %s  %s  |  %s\n",
			dim("Speed  :"),
			cyan(fmt.Sprintf("%.1f files/s", fps)),
			cyan(fmt.Sprintf("%.1f MB/s", mbps)),
		)
		fmt.Println(dim(separator))
		fmt.Println()
	}
}

// ── Single-file mode ──────────────────────────────────────────────────────────

func runSingle(filePath, schemaPath string, verbose, jsonOutput bool) {
	start := time.Now()

	if !jsonOutput {
		fmt.Println()
		fmt.Println(headerFmt(" JSON Validator "))
		fmt.Println(dim(separator))
		fmt.Printf("  %s  %s\n", dim("File  :"), cyan(filePath))
		fmt.Printf("  %s  %s\n", dim("Schema:"), cyan(schemaPath))
		fmt.Println(dim(separator))
	}

	instance, err := readJSON(filePath)
	if err != nil {
		fatal(err.Error(), jsonOutput)
	}

	sch := compileSchema(schemaPath, jsonOutput)

	validationErr := sch.Validate(instance)
	elapsed := time.Since(start)

	if validationErr == nil {
		if jsonOutput {
			out, _ := json.MarshalIndent(map[string]interface{}{
				"valid": true, "errors": []interface{}{}, "elapsed": formatDuration(elapsed),
			}, "", "  ")
			fmt.Println(string(out))
		} else {
			fmt.Printf("  %s %s\n", boldGreen("✓"), boldGreen("JSON is valid! Everything looks good."))
			fmt.Printf("  %s  %s\n", dim("Time  :"), cyan(formatDuration(elapsed)))
			fmt.Println(dim(separator))
			fmt.Println()
		}
		return
	}

	ve, ok := validationErr.(*jsonschema.ValidationError)
	var entries []errorEntry
	if ok {
		entries = collectErrors(ve)
	} else {
		entries = []errorEntry{{Message: validationErr.Error()}}
	}

	if jsonOutput {
		out, _ := json.MarshalIndent(map[string]interface{}{
			"valid":       false,
			"error_count": len(entries),
			"errors":      entries,
			"elapsed":     formatDuration(elapsed),
		}, "", "  ")
		fmt.Println(string(out))
	} else {
		fmt.Printf("  %s %s error(s) found:\n\n", boldRed("✗"), boldRed(fmt.Sprintf("%d", len(entries))))
		for i, e := range entries {
			fmt.Printf("  %s %s\n", boldYellow(fmt.Sprintf("[%d]", i+1)), boldRed(e.Message))
			if e.Path != "" {
				fmt.Printf("      %s %s\n", dim("at path  :"), cyan(e.Path))
			}
			if verbose && e.SchemaPath != "" {
				fmt.Printf("      %s %s\n", dim("schema   :"), dim(e.SchemaPath))
			}
			fmt.Println()
		}
		fmt.Printf("  %s  %s\n", dim("Time  :"), cyan(formatDuration(elapsed)))
		fmt.Println(dim(separator))
		fmt.Println()
	}
	os.Exit(1)
}

func main() {
	var (
		filePath   string
		schemaPath string
		batchDir   string
		verbose    bool
		jsonOutput bool
	)

	flag.StringVar(&filePath, "f", "", "Path to the JSON file to validate (single-file mode)")
	flag.StringVar(&filePath, "file", "", "Path to the JSON file to validate (single-file mode)")
	flag.StringVar(&schemaPath, "s", "", "Path to the JSON Schema file")
	flag.StringVar(&schemaPath, "schema", "", "Path to the JSON Schema file")
	flag.StringVar(&batchDir, "b", "", "Validate all *.json in DIR (batch mode, schema compiled once)")
	flag.StringVar(&batchDir, "batch", "", "Validate all *.json in DIR (batch mode, schema compiled once)")
	flag.BoolVar(&verbose, "v", false, "Show detailed errors")
	flag.BoolVar(&verbose, "verbose", false, "Show detailed errors")
	flag.BoolVar(&jsonOutput, "j", false, "Output in JSON format")
	flag.BoolVar(&jsonOutput, "json-output", false, "Output in JSON format")

	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage: validator -s SCHEMA (-f FILE | -b DIR) [-v] [-j]\n\n")
		flag.PrintDefaults()
	}
	flag.Parse()

	if schemaPath == "" {
		flag.Usage()
		os.Exit(1)
	}

	switch {
	case batchDir != "":
		runBatch(batchDir, schemaPath, jsonOutput)
	case filePath != "":
		runSingle(filePath, schemaPath, verbose, jsonOutput)
	default:
		flag.Usage()
		os.Exit(1)
	}
}
