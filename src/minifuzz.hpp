#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace minifuzz {

// ---------------------------------------------------------------- target

// A redzone violation reported by the bundled parser. Production fuzzers get
// this from a sanitizer; here the target checks its own redzones and reports,
// so the loop can keep running, minimise the input and save it.
struct Crash {
    std::string bug;         // stable id, e.g. "svcf-value-off-by-one"
    std::size_t offset = 0;  // index of the first byte past the buffer
    std::string detail;
};

// Parses an SVCF buffer. Returns true when no redzone was touched; otherwise
// fills ``out`` with the violation.
bool run_target(const std::uint8_t* data, std::size_t size, Crash& out);

// The parser itself (no redzone accounting), used by unit tests.
struct Entry {
    std::string key;
    std::uint8_t flags = 0;
    std::vector<std::uint8_t> value;
};
struct ParseResult {
    bool ok = false;
    std::string error;
    std::vector<Entry> entries;
};
ParseResult parse_svfc(const std::uint8_t* data, std::size_t size);

// Seed inputs written by --init-corpus and used as the starting corpus.
std::vector<std::vector<std::uint8_t>> default_seeds();

// ---------------------------------------------------------------- rng

// xorshift64* - deterministic across platforms and standard libraries, which
// is what makes a fixed --seed reproduce the same search everywhere.
class Rng {
public:
    explicit Rng(std::uint64_t seed);
    std::uint64_t next();
    std::size_t below(std::size_t bound);  // unbiased-ish, stable across libcs

private:
    std::uint64_t state_;
};

// ---------------------------------------------------------------- mutation

struct MutationOptions {
    std::size_t max_input = 4096;
    std::vector<std::vector<std::uint8_t>> dictionary;
};

// Applies 1-8 mutations in place: bit flip, byte set, arithmetic, insert,
// delete, chunk duplicate, chunk swap, dictionary overwrite, splicing.
void mutate(std::vector<std::uint8_t>& data, Rng& rng, const MutationOptions& options);

// ---------------------------------------------------------------- corpus io

std::vector<std::vector<std::uint8_t>> load_corpus(const std::vector<std::string>& paths);
std::vector<std::uint8_t> read_file(const std::string& path);
bool write_file(const std::string& path, const std::vector<std::uint8_t>& bytes);

// ---------------------------------------------------------------- fuzzing

struct RunOptions {
    std::vector<std::vector<std::uint8_t>> seeds;
    std::vector<std::string> seed_files;
    std::size_t iterations = 100000;
    std::uint64_t seed = 0;
    std::string crashes_dir = "crashes";
    std::size_t max_input = 4096;
    bool keep_going = false;
    MutationOptions mutation;
};

struct RunStats {
    std::size_t executed = 0;
    std::size_t crashes = 0;
    std::size_t unique_bugs = 0;
    std::vector<std::string> crash_files;
    std::vector<std::string> bug_ids;
    double seconds = 0.0;
    bool stopped_early = false;
};

RunStats fuzz(const RunOptions& options);

// Delta-debugging style minimiser: repeatedly drop chunks while the input
// still reproduces the crash.
std::vector<std::uint8_t> minimize(const std::vector<std::uint8_t>& input,
                                   const std::function<bool(const std::vector<std::uint8_t>&)>&
                                       still_crashes);

// Stable file name for a crash: bug id + content hash.
std::string crash_name(const std::string& bug, const std::vector<std::uint8_t>& bytes);

// Built-in token dictionary used when the caller supplies none.
std::vector<std::vector<std::uint8_t>> default_dictionary();

}  // namespace minifuzz
