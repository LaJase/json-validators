#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <getopt.h>
#include <unistd.h>
#include <vector>

// RapidJSON
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

// valijson
#include <valijson/adapters/rapidjson_adapter.hpp>
#include <valijson/schema.hpp>
#include <valijson/schema_parser.hpp>
#include <valijson/validation_results.hpp>
#include <valijson/validator.hpp>

namespace fs = std::filesystem;
using clock_type = std::chrono::steady_clock;
using duration   = std::chrono::duration<double>;

// ── ANSI colors ───────────────────────────────────────────────────────────────

static bool g_color = true;

static std::string col(const std::string& s, const char* code) {
    return g_color ? std::string(code) + s + "\033[0m" : s;
}
static std::string green    (const std::string& s) { return col(s, "\033[1;32m"); }
static std::string red      (const std::string& s) { return col(s, "\033[0;31m"); }
static std::string bold_red (const std::string& s) { return col(s, "\033[1;31m"); }
static std::string cyan     (const std::string& s) { return col(s, "\033[0;36m"); }
static std::string yellow   (const std::string& s) { return col(s, "\033[1;33m"); }
static std::string dim      (const std::string& s) { return col(s, "\033[2m");    }
static std::string header_fmt(const std::string& s){ return col(s, "\033[1;37;44m"); }
static std::string separator() { return dim(std::string(55, '-')); }

// ── Timing ────────────────────────────────────────────────────────────────────

static std::string format_duration(double secs) {
    double micros = secs * 1e6;
    if (micros < 1'000.0)
        return std::to_string(static_cast<long long>(micros)) + "µs";
    if (micros < 1'000'000.0) {
        std::ostringstream ss; ss.precision(2);
        ss << std::fixed << secs * 1e3 << "ms";
        return ss.str();
    }
    std::ostringstream ss; ss.precision(3);
    ss << std::fixed << secs << "s";
    return ss.str();
}

static std::string fmt_f(double v, int prec) {
    std::ostringstream ss; ss.precision(prec);
    ss << std::fixed << v;
    return ss.str();
}

// ── JSON string escaping (via RapidJSON writer) ───────────────────────────────

static std::string json_str(const std::string& s) {
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> w(sb);
    w.String(s.c_str(), static_cast<rapidjson::SizeType>(s.size()));
    return sb.GetString(); // returns "\"escaped\""
}

// ── File I/O ──────────────────────────────────────────────────────────────────

static bool load_file(const std::string& path, std::vector<char>& buf, std::string& err) {
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) { err = "Cannot read '" + path + "'"; return false; }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    buf.resize(static_cast<size_t>(sz) + 1);
    size_t n = fread(buf.data(), 1, static_cast<size_t>(sz), fp);
    buf[n] = '\0';
    fclose(fp);
    return true;
}

static bool parse_insitu(std::vector<char>& buf, rapidjson::Document& doc,
                         const std::string& path, std::string& err) {
    doc.ParseInsitu(buf.data());
    if (doc.HasParseError()) {
        err = "Invalid JSON in '" + path + "': "
            + rapidjson::GetParseError_En(doc.GetParseError());
        return false;
    }
    return true;
}

// ── Error exit ────────────────────────────────────────────────────────────────

[[noreturn]] static void die(const std::string& msg, bool json_output) {
    if (json_output)
        std::cout << "{\"valid\":false,\"error\":" << json_str(msg) << "}\n";
    else
        std::cerr << "  \033[0;31m✗\033[0m " << msg << "\n";
    std::exit(1);
}

// ── Schema compilation ────────────────────────────────────────────────────────
// valijson::Schema is self-contained after populateSchema — source Document
// and its buffer can be destroyed once populateSchema returns.

static void compile_schema(const std::string& path, valijson::Schema& out,
                            bool json_output) {
    std::vector<char> buf;
    std::string err;
    if (!load_file(path, buf, err)) die(err, json_output);

    rapidjson::Document doc;
    if (!parse_insitu(buf, doc, path, err)) die(err, json_output);

    valijson::SchemaParser parser;
    valijson::adapters::RapidJsonAdapter adapter(doc);
    try {
        parser.populateSchema(adapter, out);
    } catch (const std::exception& e) {
        die(std::string("Schema compilation failed: ") + e.what(), json_output);
    }
}

// ── Validation ────────────────────────────────────────────────────────────────

struct ErrorEntry { std::string path, message; };

static std::vector<ErrorEntry> validate_doc(const valijson::Schema& schema,
                                             rapidjson::Document& doc) {
    valijson::Validator validator;
    valijson::ValidationResults results;
    valijson::adapters::RapidJsonAdapter adapter(doc);
    validator.validate(schema, adapter, &results);

    std::vector<ErrorEntry> errors;
    valijson::ValidationResults::Error e;
    while (results.popError(e)) {
        std::string ctx;
        for (const auto& c : e.context) ctx += c;
        errors.push_back({ctx, e.description});
    }
    return errors;
}

// ── Batch mode ────────────────────────────────────────────────────────────────

static void run_batch(const std::string& dir_path, const std::string& schema_path,
                      bool json_output) {
    auto t_start = clock_type::now();

    std::vector<std::string> files;
    for (auto& entry : fs::directory_iterator(dir_path)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        files.push_back(entry.path().string());
    }
    std::sort(files.begin(), files.end());

    if (files.empty()) {
        std::cerr << "No .json files found in '" << dir_path << "'\n";
        std::exit(1);
    }

    if (!json_output) {
        std::cout << "\n" << header_fmt(" JSON Validator RJ — Batch Mode ") << "\n"
                  << separator() << "\n"
                  << "  " << dim("Schema :") << "  " << cyan(schema_path) << "\n"
                  << "  " << dim("Dir    :") << "  " << cyan(dir_path)    << "\n"
                  << "  " << dim("Files  :") << "  "
                  << cyan(std::to_string(files.size())) << "\n"
                  << separator() << "\n";
    }

    valijson::Schema schema;
    compile_schema(schema_path, schema, json_output);

    long valid_count = 0, invalid_count = 0;
    long long total_bytes = 0;

    for (const auto& path : files) {
        total_bytes += static_cast<long long>(fs::file_size(path));

        std::vector<char> buf;
        std::string err;
        if (!load_file(path, buf, err)) { ++invalid_count; continue; }

        rapidjson::Document doc;
        if (!parse_insitu(buf, doc, path, err)) { ++invalid_count; continue; }

        auto errors = validate_doc(schema, doc);
        if (errors.empty()) ++valid_count; else ++invalid_count;
    }

    double elapsed  = duration(clock_type::now() - t_start).count();
    long   total    = static_cast<long>(files.size());
    double fps      = total / elapsed;
    double mbps     = (total_bytes / 1e6) / elapsed;
    double avg_ms   = elapsed * 1000.0 / total;

    if (json_output) {
        std::cout << "{\n"
            << "  \"mode\": \"batch\",\n"
            << "  \"total\": "         << total         << ",\n"
            << "  \"valid\": "         << valid_count   << ",\n"
            << "  \"invalid\": "       << invalid_count << ",\n"
            << "  \"total_bytes\": "   << total_bytes   << ",\n"
            << "  \"elapsed\": \""     << format_duration(elapsed) << "\",\n"
            << "  \"avg_ms_per_file\": \"" << fmt_f(avg_ms, 2)  << "\",\n"
            << "  \"throughput_fps\": \""  << fmt_f(fps,    1)  << "\",\n"
            << "  \"throughput_mbps\": \"" << fmt_f(mbps,   1)  << "\"\n"
            << "}\n";
    } else {
        std::cout
            << "  " << green("✓") << " "
            << green(std::to_string(valid_count) + " valid") << "   "
            << red("✗") << " "
            << bold_red(std::to_string(invalid_count) + " invalid") << "\n"
            << "  " << dim("Time   :") << "  " << cyan(format_duration(elapsed)) << "\n"
            << "  " << dim("Avg    :") << "  " << cyan(fmt_f(avg_ms, 2) + " ms/file") << "\n"
            << "  " << dim("Speed  :") << "  " << cyan(fmt_f(fps, 1) + " files/s")
            << "  |  " << cyan(fmt_f(mbps, 1) + " MB/s") << "\n"
            << separator() << "\n\n";
    }
}

// ── Single-file mode ──────────────────────────────────────────────────────────

static void run_single(const std::string& file_path, const std::string& schema_path,
                       bool verbose, bool json_output) {
    auto t_start = clock_type::now();

    if (!json_output) {
        std::cout << "\n" << header_fmt(" JSON Validator RJ ") << "\n"
                  << separator() << "\n"
                  << "  " << dim("File  :") << "  " << cyan(file_path)   << "\n"
                  << "  " << dim("Schema:") << "  " << cyan(schema_path) << "\n"
                  << separator() << "\n";
    }

    std::vector<char> buf;
    std::string load_err;
    if (!load_file(file_path, buf, load_err)) die(load_err, json_output);

    rapidjson::Document doc;
    if (!parse_insitu(buf, doc, file_path, load_err)) die(load_err, json_output);

    valijson::Schema schema;
    compile_schema(schema_path, schema, json_output);

    auto errors = validate_doc(schema, doc);
    double elapsed = duration(clock_type::now() - t_start).count();

    if (errors.empty()) {
        if (json_output) {
            std::cout << "{\n"
                      << "  \"valid\": true,\n"
                      << "  \"errors\": [],\n"
                      << "  \"elapsed\": \"" << format_duration(elapsed) << "\"\n"
                      << "}\n";
        } else {
            std::cout << "  " << green("✓") << " "
                      << green("JSON is valid! Everything looks good.") << "\n"
                      << "  " << dim("Time  :") << "  "
                      << cyan(format_duration(elapsed)) << "\n"
                      << separator() << "\n\n";
        }
        return;
    }

    if (json_output) {
        std::cout << "{\n"
                  << "  \"valid\": false,\n"
                  << "  \"error_count\": " << errors.size() << ",\n"
                  << "  \"errors\": [\n";
        for (size_t i = 0; i < errors.size(); ++i) {
            std::cout << "    {\"path\":" << json_str(errors[i].path)
                      << ",\"message\":" << json_str(errors[i].message) << "}";
            if (i + 1 < errors.size()) std::cout << ",";
            std::cout << "\n";
        }
        std::cout << "  ],\n"
                  << "  \"elapsed\": \"" << format_duration(elapsed) << "\"\n"
                  << "}\n";
    } else {
        std::cout << "  " << red("✗") << " "
                  << bold_red(std::to_string(errors.size())) << " error(s) found:\n\n";
        for (size_t i = 0; i < errors.size(); ++i) {
            auto& e = errors[i];
            std::cout << "  " << yellow("[" + std::to_string(i + 1) + "]") << " "
                      << red(e.message) << "\n";
            if (!e.path.empty())
                std::cout << "      " << dim("at path  :") << " " << cyan(e.path) << "\n";
            if (verbose && !e.path.empty())
                std::cout << "      " << dim("schema   :") << " " << dim(e.path) << "\n";
            std::cout << "\n";
        }
        std::cout << "  " << dim("Time  :") << "  "
                  << cyan(format_duration(elapsed)) << "\n"
                  << separator() << "\n\n";
    }
    std::exit(1);
}

// ── main ──────────────────────────────────────────────────────────────────────

static void usage(const char* prog) {
    std::cerr << "Usage: " << prog
              << " -s SCHEMA (-f FILE | -b DIR) [-v] [-j]\n\n"
              << "  -f, --file FILE      Validate a single JSON file\n"
              << "  -s, --schema SCHEMA  JSON Schema file\n"
              << "  -b, --batch DIR      Validate all *.json in DIR\n"
              << "  -v, --verbose        Show detailed error info\n"
              << "  -j, --json-output    Machine-readable JSON output\n";
}

int main(int argc, char* argv[]) {
    std::string file_path, schema_path, batch_dir;
    bool verbose = false, json_output = false;

    if (!isatty(STDOUT_FILENO)) g_color = false;

    static struct option long_opts[] = {
        {"file",        required_argument, nullptr, 'f'},
        {"schema",      required_argument, nullptr, 's'},
        {"batch",       required_argument, nullptr, 'b'},
        {"verbose",     no_argument,       nullptr, 'v'},
        {"json-output", no_argument,       nullptr, 'j'},
        {nullptr, 0, nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "f:s:b:vj", long_opts, nullptr)) != -1) {
        switch (opt) {
        case 'f': file_path   = optarg; break;
        case 's': schema_path = optarg; break;
        case 'b': batch_dir   = optarg; break;
        case 'v': verbose     = true;   break;
        case 'j': json_output = true;   break;
        default:  usage(argv[0]); return 1;
        }
    }

    if (schema_path.empty()) { usage(argv[0]); return 1; }

    if      (!batch_dir.empty()) run_batch(batch_dir,  schema_path, json_output);
    else if (!file_path.empty()) run_single(file_path, schema_path, verbose, json_output);
    else                         { usage(argv[0]); return 1; }
}
