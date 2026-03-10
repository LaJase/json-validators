#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using json_validator = nlohmann::json_schema::json_validator;
namespace fs = std::filesystem;
using clock_type = std::chrono::steady_clock;
using duration = std::chrono::duration<double>;

// ── ANSI colors
// ───────────────────────────────────────────────────────────────

static bool g_color = true;

static std::string col(const std::string &s, const char *code) {
  if (!g_color)
    return s;
  return std::string(code) + s + "\033[0m";
}

static std::string green(const std::string &s) { return col(s, "\033[1;32m"); }
static std::string red(const std::string &s) { return col(s, "\033[0;31m"); }
static std::string bold_red(const std::string &s) {
  return col(s, "\033[1;31m");
}
static std::string cyan(const std::string &s) { return col(s, "\033[0;36m"); }
static std::string yellow(const std::string &s) { return col(s, "\033[1;33m"); }
static std::string dim(const std::string &s) { return col(s, "\033[2m"); }
static std::string header_fmt(const std::string &s) {
  return col(s, "\033[1;37;44m");
}

static std::string separator() { return dim(std::string(55, '-')); }

// ── Timing
// ────────────────────────────────────────────────────────────────────

static std::string format_duration(double secs) {
  double micros = secs * 1e6;
  if (micros < 1'000.0)
    return std::to_string(static_cast<long long>(micros)) + "µs";
  if (micros < 1'000'000.0) {
    std::ostringstream ss;
    ss.precision(2);
    ss << std::fixed << secs * 1e3 << "ms";
    return ss.str();
  }
  std::ostringstream ss;
  ss.precision(3);
  ss << std::fixed << secs << "s";
  return ss.str();
}

// ── JSON loading
// ──────────────────────────────────────────────────────────────

static bool load_json(const std::string &path, json &out, std::string &err) {
  std::ifstream f(path);
  if (!f.is_open()) {
    err = "Cannot read '" + path + "'";
    return false;
  }
  try {
    out = json::parse(f);
    return true;
  } catch (const json::parse_error &e) {
    err = "Invalid JSON in '" + path + "': " + e.what();
    return false;
  }
}

// ── Error collector
// ───────────────────────────────────────────────────────────

struct ErrorEntry {
  std::string path, message;
};

class ErrorCollector : public nlohmann::json_schema::error_handler {
public:
  std::vector<ErrorEntry> errors;

  void error(const json::json_pointer &ptr, const json & /*instance*/,
             const std::string &msg) override {
    errors.push_back({ptr.to_string(), msg});
  }
};

// ── Schema compilation
// ────────────────────────────────────────────────────────

static json_validator compile_schema(const std::string &path,
                                     bool json_output) {
  json schema_json;
  std::string err;
  if (!load_json(path, schema_json, err)) {
    if (json_output)
      std::cout << json{{"valid", false}, {"error", err}}.dump(2) << "\n";
    else
      std::cerr << "  " << red("✗") << " " << red(err) << "\n";
    std::exit(1);
  }
  json_validator v;
  try {
    v.set_root_schema(schema_json);
  } catch (const std::exception &e) {
    std::string msg = std::string("Schema compilation failed: ") + e.what();
    if (json_output)
      std::cout << json{{"valid", false}, {"error", msg}}.dump(2) << "\n";
    else
      std::cerr << "  " << red("✗") << " " << red(msg) << "\n";
    std::exit(1);
  }
  return v;
}

// ── Batch mode
// ────────────────────────────────────────────────────────────────

static void run_batch(const std::string &dir_path,
                      const std::string &schema_path, bool json_output) {
  auto t_start = clock_type::now();

  // Collect .json files
  std::vector<std::string> files;
  for (auto &entry : fs::directory_iterator(dir_path)) {
    if (!entry.is_regular_file())
      continue;
    if (entry.path().extension() != ".json")
      continue;
    files.push_back(entry.path().string());
  }
  std::sort(files.begin(), files.end());

  if (files.empty()) {
    std::cerr << "No .json files found in '" << dir_path << "'\n";
    std::exit(1);
  }

  if (!json_output) {
    std::cout << "\n"
              << header_fmt(" JSON Validator — Batch Mode ") << "\n"
              << separator() << "\n"
              << "  " << dim("Schema :") << "  " << cyan(schema_path) << "\n"
              << "  " << dim("Dir    :") << "  " << cyan(dir_path) << "\n"
              << "  " << dim("Files  :") << "  "
              << cyan(std::to_string(files.size())) << "\n"
              << separator() << "\n";
  }

  json_validator v = compile_schema(schema_path, json_output);

  long valid_count = 0;
  long invalid_count = 0;
  long long total_bytes = 0;

  for (const auto &path : files) {
    total_bytes += static_cast<long long>(fs::file_size(path));

    json instance;
    std::string err;
    if (!load_json(path, instance, err)) {
      ++invalid_count;
      continue;
    }

    ErrorCollector collector;
    v.validate(instance, collector);
    if (collector.errors.empty())
      ++valid_count;
    else
      ++invalid_count;
  }

  double elapsed = duration(clock_type::now() - t_start).count();
  long total = static_cast<long>(files.size());
  double avg_ms = elapsed * 1000.0 / total;
  double fps = total / elapsed;
  double mbps = (total_bytes / 1e6) / elapsed;

  auto fmt2 = [](double v) {
    std::ostringstream ss;
    ss.precision(2);
    ss << std::fixed << v;
    return ss.str();
  };
  auto fmt1 = [](double v) {
    std::ostringstream ss;
    ss.precision(1);
    ss << std::fixed << v;
    return ss.str();
  };

  if (json_output) {
    json out = {
        {"mode", "batch"},
        {"total", total},
        {"valid", valid_count},
        {"invalid", invalid_count},
        {"total_bytes", total_bytes},
        {"elapsed", format_duration(elapsed)},
        {"avg_ms_per_file", fmt2(avg_ms)},
        {"throughput_fps", fmt1(fps)},
        {"throughput_mbps", fmt1(mbps)},
    };
    std::cout << out.dump(2) << "\n";
  } else {
    std::cout << "  " << green("✓") << " "
              << green(std::to_string(valid_count) + " valid") << "   "
              << red("✗") << " "
              << bold_red(std::to_string(invalid_count) + " invalid") << "\n"
              << "  " << dim("Time   :") << "  "
              << cyan(format_duration(elapsed)) << "\n"
              << "  " << dim("Avg    :") << "  "
              << cyan(fmt2(avg_ms) + " ms/file") << "\n"
              << "  " << dim("Speed  :") << "  " << cyan(fmt1(fps) + " files/s")
              << "  |  " << cyan(fmt1(mbps) + " MB/s") << "\n"
              << separator() << "\n\n";
  }
}

// ── Single-file mode
// ──────────────────────────────────────────────────────────

static void run_single(const std::string &file_path,
                       const std::string &schema_path, bool verbose,
                       bool json_output) {
  auto t_start = clock_type::now();

  if (!json_output) {
    std::cout << "\n"
              << header_fmt(" JSON Validator ") << "\n"
              << separator() << "\n"
              << "  " << dim("File  :") << "  " << cyan(file_path) << "\n"
              << "  " << dim("Schema:") << "  " << cyan(schema_path) << "\n"
              << separator() << "\n";
  }

  json instance;
  std::string load_err;
  if (!load_json(file_path, instance, load_err)) {
    if (json_output)
      std::cout << json{{"valid", false}, {"error", load_err}}.dump(2) << "\n";
    else
      std::cerr << "  " << red("✗") << " " << red(load_err) << "\n";
    std::exit(1);
  }

  json_validator v = compile_schema(schema_path, json_output);

  ErrorCollector collector;
  v.validate(instance, collector);

  double elapsed = duration(clock_type::now() - t_start).count();

  if (collector.errors.empty()) {
    if (json_output) {
      std::cout << json{{"valid", true},
                        {"errors", json::array()},
                        {"elapsed", format_duration(elapsed)}}
                       .dump(2)
                << "\n";
    } else {
      std::cout << "  " << green("✓") << " "
                << green("JSON is valid! Everything looks good.") << "\n"
                << "  " << dim("Time  :") << "  "
                << cyan(format_duration(elapsed)) << "\n"
                << separator() << "\n\n";
    }
    return;
  }

  // Validation errors
  if (json_output) {
    json errs = json::array();
    for (auto &e : collector.errors)
      errs.push_back({{"path", e.path}, {"message", e.message}});

    std::cout << json{{"valid", false},
                      {"error_count", collector.errors.size()},
                      {"errors", errs},
                      {"elapsed", format_duration(elapsed)}}
                     .dump(2)
              << "\n";
  } else {
    std::cout << "  " << red("✗") << " "
              << bold_red(std::to_string(collector.errors.size()))
              << " error(s) found:\n\n";

    for (size_t i = 0; i < collector.errors.size(); ++i) {
      auto &e = collector.errors[i];
      std::cout << "  " << yellow("[" + std::to_string(i + 1) + "]") << " "
                << red(e.message) << "\n";
      if (!e.path.empty())
        std::cout << "      " << dim("at path  :") << " " << cyan(e.path)
                  << "\n";
      if (verbose && !e.path.empty())
        std::cout << "      " << dim("schema   :") << " " << dim(e.path)
                  << "\n";
      std::cout << "\n";
    }

    std::cout << "  " << dim("Time  :") << "  "
              << cyan(format_duration(elapsed)) << "\n"
              << separator() << "\n\n";
  }
  std::exit(1);
}

// ── main
// ──────────────────────────────────────────────────────────────────────

static void usage(const char *prog) {
  std::cerr << "Usage: " << prog
            << " -s SCHEMA (-f FILE | -b DIR) [-v] [-j]\n\n"
            << "  -f, --file FILE      Validate a single JSON file\n"
            << "  -s, --schema SCHEMA  JSON Schema file\n"
            << "  -b, --batch DIR      Validate all *.json in DIR (schema "
               "compiled once)\n"
            << "  -v, --verbose        Show detailed error info\n"
            << "  -j, --json-output    Machine-readable JSON output\n";
}

int main(int argc, char *argv[]) {
  std::string file_path, schema_path, batch_dir;
  bool verbose = false;
  bool json_output = false;

  // Disable colors if stdout is not a terminal
  if (!isatty(STDOUT_FILENO))
    g_color = false;

  static struct option long_opts[] = {
      {"file", required_argument, nullptr, 'f'},
      {"schema", required_argument, nullptr, 's'},
      {"batch", required_argument, nullptr, 'b'},
      {"verbose", no_argument, nullptr, 'v'},
      {"json-output", no_argument, nullptr, 'j'},
      {nullptr, 0, nullptr, 0}};

  int opt;
  while ((opt = getopt_long(argc, argv, "f:s:b:vj", long_opts, nullptr)) !=
         -1) {
    switch (opt) {
    case 'f':
      file_path = optarg;
      break;
    case 's':
      schema_path = optarg;
      break;
    case 'b':
      batch_dir = optarg;
      break;
    case 'v':
      verbose = true;
      break;
    case 'j':
      json_output = true;
      break;
    default:
      usage(argv[0]);
      return 1;
    }
  }

  if (schema_path.empty()) {
    usage(argv[0]);
    return 1;
  }

  if (!batch_dir.empty())
    run_batch(batch_dir, schema_path, json_output);
  else if (!file_path.empty())
    run_single(file_path, schema_path, verbose, json_output);
  else {
    usage(argv[0]);
    return 1;
  }
}
